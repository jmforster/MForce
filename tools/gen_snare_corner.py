"""gen_snare_corner.py — the snare recipe, from Matt's round-2 jagged verdict.

"Jagged paid off! Snare drum city!" — the three jagged_in cells. Recipe
(verbatim, 2026-08-23): "pairing that with a hit (pulse) at the onset,
varying the pulse width from sharp to fat, varying the jagged tail from
white to coarse, and varying the speed of the tailoff should produce an
infinite variety of snares."

Grid: pulse {sharp 1.5 ms, mid 8 ms tri, fat 25 ms tri}
    x tail noise {white, coarse 1800 Hz, coarse 700 Hz}
    x tailoff  {fast 0.10 s, mid 0.22 s, slow 0.45 s}          = 27 cells.
Tail = the jagged out-leg (body ramp fading under emerging noise) x an
overall (1-x)^1.2 fade so it genuinely tails off. No filters anywhere.

Output: patches/audition/snare_corner/*.json,
        renders/dsp/pending/snare_corner/*.wav + manifest.json + README.md
Usage: python tools/gen_snare_corner.py [--no-render]
"""
import argparse, json, subprocess
from pathlib import Path
import numpy as np

import gen_segment_sweep as g1
from gen_segment_sweep import Shape, atom, place, n_of, SR

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH_DIR = ROOT / "patches/audition/snare_corner"
RENDER_DIR = ROOT / "renders/dsp/pending/snare_corner"

PULSES = [("sharp",  lambda: atom(0.0015, 1.0, "sharp", 2.0)),
          ("mid",    lambda: atom(0.008, 1.0, "tri")),
          ("fat",    lambda: atom(0.025, 1.0, "tri"))]
NOISES = [("white", "white", 0), ("coarse18", "coarse", 1800), ("coarse7", "coarse", 700)]
TAILS = [("fast", 0.10), ("mid", 0.22), ("slow", 0.45)]

def tail(dur_s, noise_kind, noise_hz, seed, body=0.5, depth=1.0):
    r = np.random.default_rng(seed)
    n = n_of(dur_s)
    x = np.linspace(0.0, 1.0, n, endpoint=False)
    ramp = (1.0 - x) ** 1.5 * body                     # tonal body fading
    j = x ** 0.6                                       # noise emerging
    if noise_kind == "white":
        nz = r.uniform(-1, 1, n)
    else:
        m = max(2, int(n / (SR / noise_hz)))
        nz = np.interp(np.arange(n), np.linspace(0, n, m + 1), r.uniform(-1, 1, m + 1))
    v = (ramp + depth * j * nz) * (1.0 - x) ** 1.2     # overall tail-off
    return v.astype(np.float32)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-render", action="store_true")
    args = ap.parse_args()
    shapes = []
    for pi, (pn, pf) in enumerate(PULSES):
        for ni, (nn, nk, nhz) in enumerate(NOISES):
            for ti, (tn, td) in enumerate(TAILS):
                p = pf()
                t = tail(td, nk, nhz, seed=700 + pi * 9 + ni * 3 + ti)
                buf = place([(0.0, p), (len(p) / SR * 0.6, t)])   # tail overlaps the pulse decay
                shapes.append(Shape("snare", f"{pn}_{nn}_{tn}", "samples", buf,
                                    pulse=pn, noise=nn, tail_s=td))
    PATCH_DIR.mkdir(parents=True, exist_ok=True)
    RENDER_DIR.mkdir(parents=True, exist_ok=True)
    manifest, fails = [], 0
    for i, sh in enumerate(shapes):
        pj = g1.patch_json(sh, 3000 + i)
        ppath = PATCH_DIR / f"{sh.name}.json"
        ppath.write_text(json.dumps(pj, indent=1), encoding="utf-8")
        wpath = RENDER_DIR / f"{sh.name}.wav"
        e = {"name": sh.name, "duration_s": round(sh.duration(), 3), "meta": sh.meta}
        if not args.no_render:
            r = subprocess.run([str(CLI), str(ppath), str(wpath)], capture_output=True, text=True)
            e["render_ok"] = r.returncode == 0
            if r.returncode != 0: fails += 1
        manifest.append(e)
    (RENDER_DIR / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    (RENDER_DIR / "README.md").write_text(
        "# snare_corner — Matt's jagged snare recipe, 27 cells\n\n"
        "Names: <pulse>_<noise>_<tailoff> — pulse sharp(1.5ms)/mid(8ms tri)/fat(25ms tri);\n"
        "noise white/coarse18(1800Hz)/coarse7(700Hz); tailoff fast(0.10s)/mid(0.22s)/slow(0.45s).\n"
        "Tail = jagged out-leg (fading body under emerging noise) x overall fade. No filters.\n",
        encoding="utf-8")
    print(f"{len(shapes)} snares; render fails {fails}")

if __name__ == "__main__":
    main()
