"""Render the expand-sweep-2 patches and rank them by the novelty metric
(dsp BACKLOG item 5). Renders to renders/expand_sweep2/; builds a reference
library from the front-2 viola/vowel renders plus the un-expanded control so
"novel" means "timbrally far from both conventional sounds and the plain base".
Prints a novelty-ranked table; survivors go to REVIEW for a listen."""
import glob
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PATCHES = os.path.join(REPO, "patches", "expand_sweep2")
OUT = os.path.join(REPO, "renders", "expand_sweep2")
LIBDIR = os.path.join(REPO, "renders", "novelty", "library")   # front-2 renders
os.makedirs(OUT, exist_ok=True)

sys.path.insert(0, os.path.join(REPO, "research", "novelty"))
import novelty as nov  # noqa: E402


def render(patch, dest):
    name = os.path.splitext(os.path.basename(patch))[0]
    wav = os.path.join(dest, name + ".wav")
    r = subprocess.run([CLI, patch, wav], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    line = out[-1] if out else ""
    ok = r.returncode == 0 and os.path.exists(wav) and "peak=0 " not in line
    print(f"  {'OK ' if ok else 'ERR'} {name}: {line}")
    return wav if ok else None


def main():
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.json")))
    print(f"rendering {len(patches)} expand-sweep-2 patches...")
    rendered = [w for w in (render(p, OUT) for p in patches) if w]

    # Reference library: conventional timbres (front 2) + the plain control.
    lib_wavs = sorted(glob.glob(os.path.join(LIBDIR, "*.wav")))
    control = os.path.join(OUT, "control_noexpand.wav")
    if os.path.exists(control):
        lib_wavs.append(control)
    print(f"\nbuilding reference library ({len(lib_wavs)} sounds)...")
    M, mean, std, names = nov.build_library(lib_wavs)

    rows = []
    for w in rendered:
        v = nov.emb.embed_file(w)
        near, cen, idx = nov.novelty(v, M, mean, std)
        rows.append((os.path.basename(w), near, cen, names[idx]))
    rows.sort(key=lambda r: r[1], reverse=True)

    print(f"\n{'novelty':>9} {'centroid':>9}  regime  (nearest reference)")
    for name, near, cen, nearest in rows:
        print(f"{near:9.3f} {cen:9.3f}  {name}  ({nearest})")
    print(f"\nrenders -> {OUT}")


if __name__ == "__main__":
    main()
