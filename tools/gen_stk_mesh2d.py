"""STK Mesh2D port — patch generator + reference comparison.

Rectilinear 2D waveguide mesh (Van Duyne & Smith 1993). The engine node
Mesh2D (engine/include/mforce/source/mesh2d_source.h) is a verbatim port of
STK's tick0/tick1, so the patch is trivial: an excitation source feeding the
mesh, nothing else. All the work is making the two sides see the SAME
excitation.

Excitation: STK's own noteOn/strike is a corner impulse the graph cannot
express, so the reference driver injects through inputTick() instead and both
sides use a 1 ms raised cosine of amplitude 0.5. On this side that is an
Envelope in seconds mode with two Sine stages, 0.0005 s each: the Sine ramp
is value = start + range * (cos((1+t)*PI) + 1)/2, so 0->1 followed by 1->0
gives exactly 0.5*(1 - cos(2*pi*m/48)) over m = 0..47. Not an approximation —
tools/stk_ref/mesh2d_ref.cpp evaluates the same float expression, so the
drive samples are bit-identical and nothing has to be fitted.

Sample rate 48000 (Chafe's rate). Mesh2D bakes no fixed-Hz coefficients —
geometry is in samples (N = SR*len/c) and the edge one-pole constants are
per-hop — so the native-rate discipline from the bowed thread has nothing to
correct here.

Gates per case: top-15 spectral peak set within +-0.5%, broadband envelope
correlation >= 0.98, plus a reported (not gated) sample-domain correlation
and max sample difference.
Level safety: no 0.5 s window above rms 0.5; audibility floor rms >= 1e-4.

Usage: python tools/gen_stk_mesh2d.py [--only case]
Patches -> patches/sweep/stk_mesh2d_port/
Renders -> renders/dsp/sweep/stk_mesh2d_port/
Reference WAVs from renders/scratch/stk_ref/mesh2d48k/ (build + run
tools/stk_ref/stk_mesh2d_ref.exe first).
"""
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import read_mono  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_mesh2d_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_mesh2d_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "mesh2d48k")

SR = 48000
SLOT = 6.0
BURST_HALF = 0.0005      # seconds per Sine stage; two stages = 1 ms
BURST_AMP = 0.5
DECAY = 0.99             # STK Mesh2D default (OnePole gain)

# name: (cols NX, rows NY, inX, inY, outX, outY) — mirrors kCases in
# tools/stk_ref/mesh2d_ref.cpp.
CASES = {
    "25x6_a": (25, 6, 0.0, 0.0, 1.0, 1.0),
    "25x6_b": (25, 6, 0.5, 0.5, 1.0, 1.0),
    "25x6_c": (25, 6, 0.3, 0.7, 0.6, 0.4),
    "12x3_a": (12, 3, 0.0, 0.0, 1.0, 1.0),
    "12x3_b": (12, 3, 0.5, 0.5, 1.0, 1.0),
    "12x3_c": (12, 3, 0.3, 0.7, 0.6, 0.4),
}

PEAK_TOL = 0.005         # +-0.5% of the reference peak frequency
N_PEAKS = 15
MIN_SEP = 10.0           # Hz between distinct peaks (see peaks())
F_LO = 200.0             # analysis floor, below every mesh mode (see peaks())
ENV_CORR_MIN = 0.98


def sine_stage(v0, v1, sec):
    return {"type": "Sine", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(nx, ny, ix, iy, ox, oy):
    nodes = [
        {"id": "Burst", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": BURST_AMP,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": [sine_stage(0.0, 1.0, BURST_HALF),
                       sine_stage(1.0, 0.0, BURST_HALF),
                       {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
                        "percent": 0.0, "power": 0.0,
                        "minSec": 0.0, "maxSec": 0.0}]}},
        {"id": "Mesh", "type": "Mesh2D", "params": {
            "source": {"ref": "Burst"},
            "cols": nx, "rows": ny,
            "inX": ix, "inY": iy, "outX": ox, "outY": oy,
            "decay": DECAY}},
    ]
    return {
        "sampleRate": SR, "seconds": SLOT,
        "instrument": {"polyphony": 1, "volume": 1.0},
        "score": [{"note": 60, "time": 0.0, "duration": SLOT,
                   "velocity": 1.0}],
        "graph": {"output": "Mesh", "nodes": nodes},
    }


def peaks(x, sr):
    """All in-band spectral peaks: (frequency, magnitude), parabolically
    refined, normalized so the strongest in-band peak is 1.

    Band: F_LO to 15 kHz. Below ~100 Hz there is no mesh mode — the lowest
    is 660 Hz on the 25x6 plate, 1323 Hz on the 12x3 bar — only the smooth
    low-frequency shoulder of the excitation burst and of the decay. Its
    "local maxima" are 40-60 dB-down ripple with no peak structure, and one
    16-bit LSB of difference between two otherwise identical renders moves
    that ripple a couple of bins, which at 20-70 Hz is a 2-9% frequency
    error. Measuring mode frequencies there measures the dither, so the
    band starts above it."""
    seg = x[:int(4.0 * sr)]
    w = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * w))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    loc = np.where((mag[1:-1] > mag[:-2]) & (mag[1:-1] >= mag[2:]))[0] + 1
    loc = loc[(freqs[loc] >= F_LO) & (freqs[loc] <= 15000.0)]
    if len(loc) == 0:
        return np.array([]), np.array([])
    a = np.log(mag[loc - 1] + 1e-30)
    b = np.log(mag[loc] + 1e-30)
    c = np.log(mag[loc + 1] + 1e-30)
    den = a - 2 * b + c
    d = np.where(den != 0, 0.5 * (a - c) / np.where(den != 0, den, 1.0), 0.0)
    return freqs[loc] + d * (freqs[1] - freqs[0]), mag[loc] / mag[loc].max()


def top_peaks(f, m, n=N_PEAKS):
    """The n strongest DISTINCT peaks, strongest first. A mesh mode is a
    lobe several bins wide, so plain local maxima sample one mode a dozen
    times; MIN_SEP keeps the selection on distinct modes."""
    out = []
    for i in np.argsort(m)[::-1]:
        if any(abs(f[i] - g) < MIN_SEP for g, _ in out):
            continue
        out.append((f[i], m[i]))
        if len(out) == n:
            break
    return out


def match_peaks(port, ref_top):
    """For each of the reference's top peaks, does the port put a peak of
    comparable strength at the same frequency?

    Frequency alone is not a gate here: the port spectrum has thousands of
    local maxima, so at +-0.5% of any frequency SOMETHING matches. A hit
    therefore also requires the port peak to be within 10 dB of the
    reference peak's relative magnitude, which no noise-floor ripple can
    fake. Returns the hit fraction and the worst frequency error over the
    hits' best-magnitude candidates."""
    fp, mp = port
    if len(fp) == 0 or not ref_top:
        return 0.0, float("nan")
    hits, worst = 0, 0.0
    for f, m in ref_top:
        near = np.abs(fp - f) / f <= PEAK_TOL
        ok = near & (mp >= m / 3.1623) & (mp <= m * 3.1623)
        if ok.any():
            hits += 1
            j = np.argmax(np.where(ok, mp, -1.0))
            worst = max(worst, abs(fp[j] - f) / f)
    return hits / len(ref_top), worst


def env_corr(xp, xr, sr):
    n = int(0.02 * sr)
    k = min(len(xp), len(xr)) // n
    rp = np.sqrt(np.mean(xp[:k * n].reshape(k, n) ** 2, axis=1) + 1e-20)
    rr = np.sqrt(np.mean(xr[:k * n].reshape(k, n) ** 2, axis=1) + 1e-20)
    return float(np.corrcoef(rp, rr)[0, 1])


def level_safe(x, sr):
    win = int(0.5 * sr)
    nw = len(x) // win
    wrms = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    return float(wrms.max())


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)

    print(f"{'case':8s} {'peaks':>10s} {'worstErr':>9s} {'envCorr':>8s} "
          f"{'sampCorr':>10s} {'maxDiff':>9s} {'rms':>8s} {'winRms':>7s}  "
          f"verdict")
    fails = []
    for name, (nx, ny, ix, iy, ox, oy) in CASES.items():
        if only and name != only:
            continue
        patch = make_patch(nx, ny, ix, iy, ox, oy)
        pj = os.path.join(PATCH_OUT, f"stk_mesh2d_{name}.json")
        pw = os.path.join(REND_OUT, f"stk_mesh2d_{name}.wav")
        with open(pj, "w") as f:
            json.dump(patch, f, indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=600)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:8s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[-300:]}")
            fails.append(name)
            continue

        xp, srp = read_mono(pw)
        xr, srr = read_mono(os.path.join(REF_DIR, f"stk_mesh2d_{name}.wav"))
        n = min(len(xp), len(xr))
        xp, xr = xp[:n], xr[:n]

        rms = float(np.sqrt((xp ** 2).mean()))
        wrms = level_safe(xp, srp)
        hit, worst = match_peaks(peaks(xp, srp), top_peaks(*peaks(xr, srr)))
        ec = env_corr(xp, xr, srp)
        md = float(np.abs(xp - xr).max())
        sc = float(np.corrcoef(xp, xr)[0, 1])

        bad = []
        if hit < 1.0:
            bad.append(f"peaks {hit:.0%}")
        if ec < ENV_CORR_MIN:
            bad.append(f"envCorr {ec:.3f}")
        if wrms > 0.5:
            bad.append(f"LEVEL {wrms:.2f}")
        if rms < 1e-4:
            bad.append("SILENT")
        verdict = "PASS" if not bad else "FAIL: " + ", ".join(bad)
        if bad:
            fails.append(name)
        print(f"{name:8s} {hit * 100:9.0f}% {worst * 100:8.3f}% {ec:8.4f} "
              f"{sc:10.7f} {md:9.2e} {rms:8.5f} {wrms:7.3f}  {verdict}",
              flush=True)

    print()
    print("ALL PASS" if not fails else "FAILED: " + ", ".join(fails))


if __name__ == "__main__":
    main()
