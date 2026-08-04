#!/usr/bin/env python3
"""Render a list of patches to a named arm and hash them, for engine A/B tests.

Renders every patch named in a set file (one repo-relative path per line) and
writes <out>/<arm>/hashes.txt. `compare` then diffs two arms and prints which
patches moved. Use when an engine change is claimed bit-exact for some subset:
build arm A, change the engine, build arm B, compare.

Usage:
  python tools/ab_render_set.py render <set.txt> <outdir> <armname>
  python tools/ab_render_set.py compare <outdir> <armA> <armB>
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")


def render(setfile, outdir, arm):
    paths = [l.strip() for l in open(setfile, encoding="utf-8") if l.strip()]
    armdir = os.path.join(outdir, arm)
    os.makedirs(armdir, exist_ok=True)
    hashes, failed = {}, []
    for i, rel in enumerate(paths):
        src = os.path.join(ROOT, rel)
        wav = os.path.join(armdir, "_tmp.wav")
        r = subprocess.run([CLI, src, wav], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(wav):
            failed.append(rel)
            continue
        with open(wav, "rb") as f:
            hashes[rel] = hashlib.sha256(f.read()).hexdigest()
        os.remove(wav)
        if (i + 1) % 50 == 0:
            print("  ...%d/%d" % (i + 1, len(paths)))
    with open(os.path.join(armdir, "hashes.txt"), "w", encoding="utf-8") as f:
        for k in sorted(hashes):
            f.write("%s  %s\n" % (hashes[k], k))
    print("arm %-6s rendered %d/%d (%d failed to render)"
          % (arm, len(hashes), len(paths), len(failed)))
    for p in failed[:10]:
        print("    FAILED %s" % p)
    return 0


def load(outdir, arm):
    d = {}
    with open(os.path.join(outdir, arm, "hashes.txt"), encoding="utf-8") as f:
        for line in f:
            h, _, p = line.strip().partition("  ")
            if p:
                d[p] = h
    return d


def compare(outdir, a, b):
    A, B = load(outdir, a), load(outdir, b)
    common = sorted(set(A) & set(B))
    moved = [p for p in common if A[p] != B[p]]
    print("compared %d patches present in both arms" % len(common))
    print("  identical : %d" % (len(common) - len(moved)))
    print("  DIFFER    : %d" % len(moved))
    for p in moved:
        print("      %s" % p)
    only = (set(A) ^ set(B))
    if only:
        print("  present in only one arm (%d): %s" % (len(only), sorted(only)[:5]))
    return 0


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "render":
        return render(sys.argv[2], sys.argv[3], sys.argv[4])
    if sys.argv[1] == "compare":
        return compare(sys.argv[2], sys.argv[3], sys.argv[4])
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
