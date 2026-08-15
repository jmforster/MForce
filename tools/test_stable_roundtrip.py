"""Roundtrip regression: node ids survive mforce_ui --roundtrip verbatim,
and the roundtripped patch renders byte-identically to the original.
Usage: python tools/test_stable_roundtrip.py <patch-or-dir> [...]
Exit 1 on any id change or render mismatch; skips patches that fail to
render in BOTH forms (pre-existing CLI limitation, reported not failed)."""
import hashlib, json, subprocess, sys, tempfile
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
KNOWN_DIFFS = {
    "patches/baselines/bowed_test.json",
    "patches/baselines/FormantSequence1.json",
    "patches/baselines/inst_fm_bell_melody_test.json",
    "patches/baselines/reed_test.json",
    "patches/baselines/TriTest.json",
    "patches/library/_eval/bowed_cello.json",
    "patches/library/_eval/buzzy_bow.json",
    "patches/library/_eval/fm_gritty_bass.json",
    "patches/library/_eval/harsh_pluck.json",
    "patches/library/effects/spacy/inst_spacy.json",
    "patches/library/fm/inst_fm_rhodes_base.json",
    "patches/library/fm/inst_fm_rhodes_clack.json",
    "patches/library/fm/inst_fm_rhodes_dual.json",
    "patches/library/percussion/hi_hat.json",
    "patches/library/percussion/kick_drum.json",
    "patches/library/percussion/snare_drum.json",
    "patches/library/strings/v6_02_res_curve_mid.json",
    "patches/library/strings/v6_03_res_curve_hi.json",
    # Pruned as fixed (2026-08-14/15): clarinet_locked (paramMap repair),
    # 7 score-absence patches (Rhodes EPs, bells, slow_attack_horn),
    # 8 voice patches (formant child ids preserved).
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

def main():
    patches = []
    for arg in sys.argv[1:]:
        p = Path(arg)
        patches += sorted(p.rglob("*.json")) if p.is_dir() else [p]
    bad_ids, bad_render, skipped, known_hits = [], [], [], []
    id_only = 0
    with tempfile.TemporaryDirectory() as td:
        for pt in patches:
            rt = Path(td) / (pt.stem + "_rt.json")
            r = subprocess.run([str(UI), "--roundtrip", str(pt), str(rt)],
                               capture_output=True, text=True)
            if r.returncode != 0:
                bad_ids.append((pt, "roundtrip failed: " + r.stderr.strip()))
                continue
            if ids_of(pt) != ids_of(rt):
                bad_ids.append((pt, f"{ids_of(pt)} -> {ids_of(rt)}"))
                continue
            # Render byte-identity is asserted only for instrument-style
            # patches: --roundtrip force-saves through save_patch_graph,
            # which is documented-lossy for node-graph-style files (score
            # injection, seconds reset, mixer output dropped) — that class
            # predates stable ids (proven old-exe A/B 2026-08-14).
            if "instrument" not in load(pt):
                id_only += 1
                continue
            wa, wb = Path(td) / "a.wav", Path(td) / "b.wav"
            ra, rb = render(pt, wa), render(rt, wb)
            if not ra and not rb:
                skipped.append(pt); continue
            if ra != rb or sha(wa) != sha(wb):
                if str(pt).replace("\\", "/") in KNOWN_DIFFS:
                    known_hits.append(pt)
                else:
                    bad_render.append(pt)
    for pt, why in bad_ids: print(f"ID CHANGE {pt}: {why}")
    for pt in bad_render:   print(f"RENDER DIFF {pt}")
    for pt in skipped:      print(f"skip (unrenderable both forms) {pt}")
    for pt in known_hits: print(f"known diff (backlog 3n) {pt}")
    print(f"{len(patches)} patches: {len(bad_ids)} id changes, "
          f"{len(bad_render)} NEW render diffs, {len(known_hits)} known "
          f"diffs, {id_only} id-only (non-instrument), {len(skipped)} skipped")
    return 1 if (bad_ids or bad_render) else 0

if __name__ == "__main__":
    sys.exit(main())
