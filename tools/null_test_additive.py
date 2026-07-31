#!/usr/bin/env python3
"""Byte-exact null test for additive render-path refactors (item 8 stage 2a).

Renders a patch set that between them exercise every branch of the additive
hot loop, and compares the WAVs against a stored baseline directory byte for
byte. Any optimization claiming bit-exactness must pass this with zero
differing bytes — there is no tolerance and no listening involved.

Usage:
  python tools/null_test_additive.py baseline [exe]  # -> renders/nulltest/base/
  python tools/null_test_additive.py check    [exe]  # -> .../new/ and compare

`exe` defaults to the freshly built mforce_cli; pass a saved pre-change binary
to capture a baseline without rebuilding.
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "nulltest")

# (tag, patch) — chosen so that between them they hit: plain FullPartials,
# ExplicitPartials, motion/shimmer/trade/onset, bandwidth, formant + floor,
# gliding FormantSequence, the expand rule, paramMap curves, instrument+score
# pre-render, and the raw profiling ladder.
PATCHES = [
    ("viola_default",  "patches/viola_default.json"),
    ("v6_cmaes_best",  "patches/fable1_v6/v6_05_cmaes_best.json"),
    ("v6_bwfloor",     "patches/fable1_v6/v6_04_bwfloor_curve.json"),
    ("clarinet_bed",   "patches/clarinet_bed_test.json"),
    ("vowel_liar1320", "patches/vowel_baseline/liar_1320.json"),
    ("vowel_liar220",  "patches/vowel_baseline/liar_220.json"),
    ("fable_control",  "patches/fable1/00_control.json"),
    ("fable_mot_inc",  "patches/fable1/03_mot_incoherent.json"),
    ("fable_onset",    "patches/fable1/04_onset_bloom.json"),
    ("fable_trade",    "patches/fable1/06_trade_slosh.json"),
    ("fable_bw",       "patches/fable1_v4/v4_03_bw_floor.json"),
    ("expand_diverge", "patches/expand_sweep/diverge_spacing.json"),
    ("expand_control", "patches/expand_sweep/control_noexpand.json"),
    ("prof_096p",      "patches/prof_additive/add_096p.json"),
]


def render(patch, wav, exe):
    r = subprocess.run([exe, os.path.join(ROOT, patch), wav],
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(wav):
        return None, (r.stderr or r.stdout).strip()[:300]
    return hashlib.sha256(open(wav, "rb").read()).hexdigest(), None


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    if mode not in ("baseline", "check"):
        sys.exit(__doc__)
    exe = sys.argv[2] if len(sys.argv) > 2 else CLI
    if not os.path.isabs(exe):
        exe = os.path.join(ROOT, exe)
    d = os.path.join(OUT, "base" if mode == "baseline" else "new")
    os.makedirs(d, exist_ok=True)

    digests = {}
    for tag, patch in PATCHES:
        if not os.path.exists(os.path.join(ROOT, patch)):
            print(f"  {tag:16s} SKIP (missing {patch})")
            continue
        h, err = render(patch, os.path.join(d, tag + ".wav"), exe)
        if h is None:
            print(f"  {tag:16s} RENDER FAILED: {err}")
            continue
        digests[tag] = h
        print(f"  {tag:16s} {h[:16]}")

    with open(os.path.join(d, "_digests.txt"), "w") as f:
        for k, v in sorted(digests.items()):
            f.write(f"{v}  {k}\n")

    if mode == "baseline":
        print(f"\nbaseline written: {len(digests)} patches -> {d}")
        return

    base = os.path.join(OUT, "base", "_digests.txt")
    if not os.path.exists(base):
        sys.exit("no baseline — run `baseline` against the pre-change build first")
    ref = {line.split()[1]: line.split()[0] for line in open(base) if line.strip()}

    same = [k for k in digests if ref.get(k) == digests[k]]
    diff = [k for k in digests if k in ref and ref[k] != digests[k]]
    missing = [k for k in ref if k not in digests]

    print(f"\nidentical: {len(same)}/{len(ref)}")
    for k in diff:
        print(f"  DIFFERS: {k}\n    base {ref[k]}\n    new  {digests[k]}")
    for k in missing:
        print(f"  MISSING FROM NEW RUN: {k}")
    if diff or missing:
        sys.exit(1)
    print("NULL TEST PASS — every render byte-identical")


if __name__ == "__main__":
    main()
