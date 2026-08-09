#!/usr/bin/env python3
"""Byte-exact null test for FMSource changes (dsp backlog 3c2).

Renders EVERY patch in patches/ that instantiates an FMSource and compares
WAV hashes against a stored baseline. Unlike the additive null test this one
expects a partition, not a clean sweep:

  - patches that do NOT wire the `phase` param must be byte-identical;
  - patches that DO wire it must DIFFER (that is the proof the fix took).

Usage:
  python tools/null_test_fm.py baseline [exe]   # -> renders/nulltest_fm/base/
  python tools/null_test_fm.py check    [exe]   # -> .../new/ and compare
"""
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "nulltest_fm")


def fm_patches():
    """Every patch containing an FMSource node, with a flag for wired phase."""
    found = []
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "patches")):
        for fn in files:
            if not fn.endswith(".json"):
                continue
            p = os.path.join(dirpath, fn)
            try:
                raw = open(p, "r", encoding="utf-8").read()
            except OSError:
                continue
            if '"FMSource"' not in raw:
                continue
            try:
                doc = json.loads(raw)
            except json.JSONDecodeError:
                continue
            wired = False
            for node in _walk_nodes(doc):
                if node.get("type") != "FMSource":
                    continue
                ph = (node.get("params") or {}).get("phase")
                # A bare 0.0 constant is the default and stays a no-op; only a
                # ref or a non-zero constant counts as "wired".
                if isinstance(ph, dict) or (isinstance(ph, (int, float)) and ph != 0.0):
                    wired = True
            found.append((os.path.relpath(p, ROOT).replace("\\", "/"), wired))
    return sorted(found)


def _walk_nodes(doc):
    graph = doc.get("graph") or doc
    for n in graph.get("nodes", []) or []:
        yield n
    # instrument-style patches carry their graph under "instrument"
    inst = doc.get("instrument")
    if isinstance(inst, dict):
        for n in (inst.get("graph") or {}).get("nodes", []) or []:
            yield n


def tag_for(rel):
    return rel[len("patches/"):-len(".json")].replace("/", "__")


def render_all(exe, outdir):
    os.makedirs(outdir, exist_ok=True)
    rows = []
    for rel, wired in fm_patches():
        tag = tag_for(rel)
        wav = os.path.join(outdir, tag + ".wav")
        r = subprocess.run([exe, os.path.join(ROOT, rel), wav],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(wav):
            rows.append((tag, wired, None, (r.stderr or r.stdout).strip()[:120]))
            continue
        h = hashlib.sha256(open(wav, "rb").read()).hexdigest()
        rows.append((tag, wired, h, ""))
    return rows


def write_manifest(rows, path):
    with open(path, "w", encoding="utf-8") as f:
        json.dump([{"tag": t, "phase_wired": w, "sha": h, "err": e}
                   for t, w, h, e in rows], f, indent=1)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    exe = sys.argv[2] if len(sys.argv) > 2 else CLI
    sub = "base" if mode == "baseline" else "new"
    outdir = os.path.join(OUT, sub)
    rows = render_all(exe, outdir)
    write_manifest(rows, os.path.join(OUT, sub + ".json"))
    ok = [r for r in rows if r[2]]
    print(f"{mode}: rendered {len(ok)}/{len(rows)} FM patches -> {outdir}")
    for t, _w, h, e in rows:
        if not h:
            print(f"  RENDER FAIL {t}: {e}")

    if mode == "baseline":
        return 0

    basep = os.path.join(OUT, "base.json")
    if not os.path.exists(basep):
        print("no baseline; run `baseline` with the pre-change exe first")
        return 2
    base = {d["tag"]: d for d in json.load(open(basep, encoding="utf-8"))}

    same_unwired, diff_unwired, same_wired, diff_wired, missing = [], [], [], [], []
    for t, w, h, _e in rows:
        b = base.get(t)
        if not b or not b["sha"] or not h:
            missing.append(t)
            continue
        if h == b["sha"]:
            (same_wired if w else same_unwired).append(t)
        else:
            (diff_wired if w else diff_unwired).append(t)

    print(f"\nunwired phase : {len(same_unwired)} identical, "
          f"{len(diff_unwired)} DIFFER (expect 0)")
    for t in diff_unwired:
        print(f"    REGRESSION {t}")
    print(f"wired phase   : {len(diff_wired)} differ (expect all), "
          f"{len(same_wired)} identical (expect 0)")
    for t in same_wired:
        print(f"    STILL DEAD {t}")
    if missing:
        print(f"skipped (no pair): {len(missing)} -> {missing}")

    passed = not diff_unwired and not same_wired and diff_wired
    print("\nPASS" if passed else "\nFAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
