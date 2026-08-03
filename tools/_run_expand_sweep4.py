"""Render the expand-sweep-4 patches and rank them by the novelty metric.

Same shape as _run_expand_sweep3.py, plus the thing round 4 actually needs: a
DEPTH-CONTROL comparison. The batch's whole premise is that recursion depth
buys something count and width do not, so the harness prints the pairwise
embedding distance between micro_r3 and its two matched controls. If those
distances sit at or below the batch's own noise floor, depth is not a separate
timbral axis and item 5 closes on that number rather than on an opinion.

Renders to renders/expand_sweep4/; reference library = front-2 viola/vowel
renders (renders/novelty/library) plus the round-2 un-expanded control, so
"novel" means "timbrally far from conventional + plain base".
"""
import glob
import os
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PATCHES = os.path.join(REPO, "patches", "expand_sweep4")
OUT = os.path.join(REPO, "renders", "expand_sweep4")
LIBDIR = os.path.join(REPO, "renders", "novelty", "library")
os.makedirs(OUT, exist_ok=True)

sys.path.insert(0, os.path.join(REPO, "research", "novelty"))
import novelty as nov  # noqa: E402

# The depth question, stated as explicit comparisons (see module docstring).
DEPTH_TRIAD = [
    ("micro_r3", "ctrl_r1_match", "same partial count + same spread, half the depth"),
    ("micro_r3", "ctrl_r0_wide", "same spread, one level, 9x fewer partials"),
    ("micro_r3", "micro_r2", "one level shallower, same spacing"),
    ("micro_r4", "micro_r3", "one level deeper, same spacing"),
    ("flat_deep", "peaked_deep", "same geometry, opposite loPct floor"),
    ("taper_flat_c2", "taper_steep_c2", "ONLY power differs, at count=2 (must be > 0)"),
]


def render(patch, dest):
    name = os.path.splitext(os.path.basename(patch))[0]
    wav = os.path.join(dest, name + ".wav")
    t0 = time.time()
    r = subprocess.run([CLI, patch, wav], capture_output=True, text=True)
    dt = time.time() - t0
    out = (r.stdout + r.stderr).strip().splitlines()
    line = out[-1] if out else ""
    ok = r.returncode == 0 and os.path.exists(wav) and "peak=0 " not in line
    print("  %s %-18s %6.1fs  %s" % ("OK " if ok else "ERR", name, dt, line), flush=True)
    return wav if ok else None


def main():
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.json")))
    print("rendering %d expand-sweep-4 patches..." % len(patches), flush=True)
    t0 = time.time()
    rendered = [w for w in (render(p, OUT) for p in patches) if w]
    print("rendered %d/%d in %.1fs" % (len(rendered), len(patches), time.time() - t0))

    lib_wavs = sorted(glob.glob(os.path.join(LIBDIR, "*.wav")))
    control = os.path.join(REPO, "renders", "expand_sweep2", "control_noexpand.wav")
    if os.path.exists(control):
        lib_wavs.append(control)
    print("\nbuilding reference library (%d sounds)..." % len(lib_wavs))
    M, mean, std, names = nov.build_library(lib_wavs)

    vecs, rows = {}, []
    for w in rendered:
        base = os.path.splitext(os.path.basename(w))[0]
        v = nov.emb.embed_file(w)
        vecs[base] = (v - mean) / std
        near, cen, idx = nov.novelty(v, M, mean, std)
        rows.append((base, near, cen, names[idx]))
    rows.sort(key=lambda r: r[1], reverse=True)

    print("\n%9s %9s  regime  (nearest reference)" % ("novelty", "centroid"))
    for name, near, cen, nearest in rows:
        print("%9.3f %9.3f  %s  (%s)" % (near, cen, name, nearest))

    # Noise floor: how far apart are the two cells that SHOULD be near-identical
    # in kind (the two inflate variants differ only in transition speed)? Any
    # depth distance below the smallest such pair is not a real difference.
    import numpy as np

    def dist(a, b):
        return float(np.linalg.norm(vecs[a] - vecs[b])) if a in vecs and b in vecs else float("nan")

    print("\n=== does DEPTH do anything count/width cannot? ===")
    print("%-32s %9s   %s" % ("pair", "distance", "what differs"))
    for a, b, why in DEPTH_TRIAD:
        print("%-32s %9.3f   %s" % ("%s vs %s" % (a, b), dist(a, b), why))

    allnames = sorted(vecs)
    pairs = [(dist(a, b), a, b) for i, a in enumerate(allnames) for b in allnames[i + 1:]]
    pairs.sort()
    print("\nbatch scale, for reading those numbers:")
    print("  closest pair overall : %.3f  (%s / %s)" % pairs[0])
    print("  median pair          : %.3f" % pairs[len(pairs) // 2][0])
    print("  widest pair          : %.3f  (%s / %s)" % pairs[-1])

    print("\nrenders -> %s" % OUT)


if __name__ == "__main__":
    main()
