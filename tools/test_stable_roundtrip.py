"""Roundtrip regression: node ids survive mforce_ui --roundtrip verbatim,
and the roundtripped patch renders byte-identically to the original.
Usage: python tools/test_stable_roundtrip.py <patch-or-dir> [...] [--jobs N]
Exit 1 on any id change or render mismatch; skips patches that fail to
render in BOTH forms (pre-existing CLI limitation, reported not failed).
Parallel since 2026-09-05: each patch is an independent (headless UI
roundtrip + two CLI renders) pipeline with per-patch temp files."""
import concurrent.futures as cf
import hashlib, json, os, subprocess, sys, tempfile
from pathlib import Path

UI = Path("build/tools/mforce_ui/Release/mforce_ui.exe")
CLI = Path("build/tools/mforce_cli/Release/mforce_cli.exe")

# Pre-existing roundtrip render infidelity (backlog 3n): param species the
# save path re-serializes lossily (Envelope preset semantics, SegmentSource
# values, RangeSource.normalized, FMSource phase/oversample, MultiSource
# source, score injection). Proven to predate stable ids (2026-08-14:
# params in the roundtripped file are produced by save code the id change
# never touched, and ids compare equal). Fixing that class shrinks this
# list; a NEW entry here is a regression and fails the run.
# Every roundtrip-fidelity failure of the save path was root-caused and fixed
# (score absence, formant child ids, RangeSource.normalized default,
# unmodeled-param carry, adsr-shape sustain rewrite + declamp). What remains
# is the backlog-79 six: patches whose render depends on tap/advance order or
# a dynamic-pin setting that the editor's codec cannot yet carry (Part 4's).
# They are listed so the run is green and a SEVENTH diff fails it; a new
# entry here is a regression — fix the save path, don't grow the list.
KNOWN_DIFFS = {
    "patches/baselines/double_advance_bug.json",
    "patches/baselines/double_advance_ok.json",
    "patches/baselines/feedback/loop_tap_counter.json",
    "patches/baselines/feedback/loop_tap_starved.json",
    "patches/baselines/perform/wiring_setting.json",
    "patches/baselines/perform/wiring_smoke.json",
}

def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))

def ids_of(path):
    """Node ids, excluding Formant children consumed into FormantSpectrum
    rows — the UI re-synthesizes those as hidden `<spec>__fN` nodes by
    design, so their names are not part of the stable-identity contract."""
    j = load(path)
    nodes = j.get("graph", {}).get("nodes", [])
    types = {n["id"]: n.get("type") for n in nodes}
    formant_children = set()
    for n in nodes:
        for f in n.get("params", {}).get("formants", []) or []:
            if isinstance(f, dict) and types.get(f.get("ref")) == "Formant":
                formant_children.add(f["ref"])
    return sorted(n["id"] for n in nodes
                  if n["id"] not in formant_children
                  and "__f" not in n["id"])

def render(patch, wav):
    r = subprocess.run([str(CLI), str(patch), str(wav)],
                       capture_output=True, text=True)
    return r.returncode == 0

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def jobs_arg():
    if "--jobs" in sys.argv:
        return max(1, int(sys.argv[sys.argv.index("--jobs") + 1]))
    return max(4, (os.cpu_count() or 8) - 2)

def check_one(args):
    """One patch's verdict: ('id', why) | ('render',) | ('skip',) |
    ('known',) | ('id_only',) | ('ok',)."""
    i, pt, td = args
    rt = Path(td) / f"{i}_{pt.stem}_rt.json"
    r = subprocess.run([str(UI), "--roundtrip", str(pt), str(rt)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return pt, ("id", "roundtrip failed: " + r.stderr.strip())
    # The property is that no id in the source file was renamed or lost,
    # not that the id sets are equal: any source id missing from the
    # roundtripped file fails, and so does any added id outside the
    # reserved "__" namespace. An added "__" id passes: the editor's save
    # (save_patch_graph) writes one PerformNode per wired Note-face pin,
    # under the id that pin was loaded from, or under a fresh
    # __perf_<field> when the pin has none or that id is already taken,
    # and sanitize_unique_id keeps every other node out of the "__"
    # namespace unless its id arrived from the file already in it.
    before, after = set(ids_of(pt)), set(ids_of(rt))
    lost = before - after
    added = {i2 for i2 in (after - before) if not i2.startswith("__")}
    if lost or added:
        why = []
        if lost:  why.append(f"lost {sorted(lost)}")
        if added: why.append(f"unexpected new {sorted(added)}")
        return pt, ("id", "; ".join(why))
    # Render byte-identity is asserted only for instrument-style
    # patches: --roundtrip force-saves through save_patch_graph,
    # which is documented-lossy for node-graph-style files (score
    # injection, seconds reset, mixer output dropped) — that class
    # predates stable ids (proven old-exe A/B 2026-08-14).
    if "instrument" not in load(pt):
        return pt, ("id_only",)
    wa, wb = Path(td) / f"{i}_a.wav", Path(td) / f"{i}_b.wav"
    ra, rb = render(pt, wa), render(rt, wb)
    if not ra and not rb:
        return pt, ("skip",)
    if ra != rb or sha(wa) != sha(wb):
        if str(pt).replace("\\", "/") in KNOWN_DIFFS:
            return pt, ("known",)
        return pt, ("render",)
    return pt, ("ok",)

def main():
    patches = []
    for arg in sys.argv[1:]:
        if arg == "--jobs":
            continue
        if arg.isdigit() and "--jobs" in sys.argv \
           and sys.argv.index(arg) == sys.argv.index("--jobs") + 1:
            continue
        p = Path(arg)
        patches += sorted(p.rglob("*.json")) if p.is_dir() else [p]
    bad_ids, bad_render, skipped, known_hits = [], [], [], []
    id_only = 0
    with tempfile.TemporaryDirectory() as td:
        with cf.ThreadPoolExecutor(max_workers=jobs_arg()) as ex:
            results = list(ex.map(
                check_one, [(i, pt, td) for i, pt in enumerate(patches)]))
    for pt, verdict in results:
        kind = verdict[0]
        if   kind == "id":      bad_ids.append((pt, verdict[1]))
        elif kind == "render":  bad_render.append(pt)
        elif kind == "skip":    skipped.append(pt)
        elif kind == "known":   known_hits.append(pt)
        elif kind == "id_only": id_only += 1
    for pt, why in bad_ids: print(f"ID CHANGE {pt}: {why}")
    for pt in bad_render:   print(f"RENDER DIFF {pt}")
    for pt in skipped:      print(f"skip (unrenderable both forms) {pt}")
    for pt in known_hits: print(f"known diff (backlog 79) {pt}")
    print(f"{len(patches)} patches: {len(bad_ids)} id changes, "
          f"{len(bad_render)} NEW render diffs, {len(known_hits)} known "
          f"diffs, {id_only} id-only (non-instrument), {len(skipped)} skipped")
    return 1 if (bad_ids or bad_render) else 0

if __name__ == "__main__":
    sys.exit(main())
