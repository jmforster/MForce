"""Render the FM matrix sweep and rank it by novelty (dsp item 7 stage 2, G3).

Renders patches/fm_matrix/ to renders/fm_matrix/, then scores every result
against a reference library of CONVENTIONAL sounds — the front-2 viola/vowel
renders plus this batch's own textbook-FM control — so "novel" means "far from
both an ordinary instrument tone and ordinary FM", not merely "loud" or "odd".

Also reports peak/RMS per render so silent or blown-out topologies are caught
mechanically instead of being discovered by ear. Which of the survivors is
MUSICAL is a taste question: it goes to REVIEW, it is not decided here.
"""
import glob
import os
import re
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PATCHES = os.path.join(REPO, "patches", "fm_matrix")
OUT = os.path.join(REPO, "renders", "fm_matrix")
LIBDIR = os.path.join(REPO, "renders", "novelty", "library")
os.makedirs(OUT, exist_ok=True)

sys.path.insert(0, os.path.join(REPO, "research", "novelty"))
import novelty as nov  # noqa: E402

RE_PEAK = re.compile(r"peak=([\d.eE+-]+) rms=([\d.eE+-]+)")


def render(patch, dest):
    name = os.path.splitext(os.path.basename(patch))[0]
    wav = os.path.join(dest, name + ".wav")
    r = subprocess.run([CLI, patch, wav], capture_output=True, text=True)
    m = RE_PEAK.search(r.stderr)
    peak = float(m.group(1)) if m else 0.0
    rms = float(m.group(2)) if m else 0.0
    ok = r.returncode == 0 and os.path.exists(wav) and peak > 0.0
    flag = "OK " if ok else "ERR"
    if ok and peak >= 0.999:
        flag = "CLIP"
    print(f"  {flag:4s} {name:32s} peak={peak:.3f} rms={rms:.4f}")
    return (wav, peak, rms) if ok else None


def main():
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.json")))
    print(f"rendering {len(patches)} fm-matrix patches...")
    rendered = [x for x in (render(p, OUT) for p in patches) if x]

    lib_wavs = sorted(glob.glob(os.path.join(LIBDIR, "*.wav")))
    control = os.path.join(OUT, "t1_00_control_classic.wav")
    if os.path.exists(control):
        lib_wavs.append(control)
    print(f"\nreference library: {len(lib_wavs)} conventional sounds "
          f"(incl. the textbook-FM control)")
    M, mean, std, names = nov.build_library(lib_wavs)

    rows = []
    for wav, peak, rms in rendered:
        v = nov.emb.embed_file(wav)
        near, cen, idx = nov.novelty(v, M, mean, std)
        rows.append((os.path.basename(wav)[:-4], near, cen, names[idx], peak, rms))
    rows.sort(key=lambda r: r[1], reverse=True)

    print(f"\n{'novelty':>8} {'centroid':>9} {'peak':>6}  case"
          f"                              (nearest reference)")
    for name, near, cen, nearest, peak, rms in rows:
        print(f"{near:8.3f} {cen:9.3f} {peak:6.3f}  {name:33s} ({nearest})")
    print(f"\nrenders -> {OUT}")


if __name__ == "__main__":
    main()
