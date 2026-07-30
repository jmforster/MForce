#!/usr/bin/env python3
"""Additive-synthesis performance profile (item 8 stage 1, G4).

Controlled sweep: minimal AdditiveSource + FullPartials patches identical in
every respect except partial count (maxPartials). Isolates the per-partial
per-sample cost — the hot loop SIMD / iFFT-overlap-add work will target.

f0=110 Hz so partial N sits at 110N Hz; at N=200 that's 22 kHz < Nyquist(24k),
so nothing is culled and scaling stays clean-linear. Renders each patch REPS
times, reports MIN render time (least-contended => truest compute cost).

numpy-free; shells out to mforce_cli and parses the `render=…ms` stderr line.
"""
import json, os, re, subprocess, sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PDIR = os.path.join(ROOT, "patches", "prof_additive")
WAV = os.path.join(ROOT, "renders", "fm_oversample", "_prof.wav")
os.makedirs(PDIR, exist_ok=True)

COUNTS = [8, 16, 32, 48, 64, 96, 128, 160, 200]
SECONDS = 2.0
SR = 48000
REPS = 5


def patch(n):
    return {
        "sampleRate": SR, "seconds": SECONDS,
        "graph": {"nodes": [
            {"id": "ampEnv", "type": "Envelope",
             "params": {"preset": "adsr", "attack": 0.05, "decay": 0.1,
                        "sustainLevel": 1.0, "release": 0.2}},
            {"id": "parts", "type": "FullPartials",
             "params": {"maxPartials": n}},
            {"id": "add", "type": "AdditiveSource",
             "params": {"seed": 1, "frequency": 110.0,
                        "amplitude": {"ref": "ampEnv"},
                        "partials": {"ref": "parts"}, "formantWeight": 0.0}},
            {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "add"},
             "params": {"volume": 0.5, "pan": 0.0}},
            {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
             "params": {"gainL": 1.0, "gainR": 1.0}},
        ], "output": "mix"},
    }


RE = re.compile(r"render=([\d.]+)ms for (\d+) frames")


def run(pfile):
    r = subprocess.run([CLI, pfile, WAV], capture_output=True, text=True)
    m = RE.search(r.stderr)
    if not m:
        sys.exit(f"no timing in stderr for {pfile}:\n{r.stderr}\n{r.stdout}")
    return float(m.group(1)), int(m.group(2))


def main():
    print(f"{'N':>4} {'min ms':>8} {'Msamp/s':>9} {'ns/(smp*part)':>14} {'xRT':>7}")
    print("-" * 48)
    frames = None
    rows = []
    for n in COUNTS:
        pf = os.path.join(PDIR, f"add_{n:03d}p.json")
        json.dump(patch(n), open(pf, "w"), indent=1)
        times = [run(pf)[0] for _ in range(REPS)]
        frames = run(pf)[1]
        tmin = min(times)
        msps = frames / (tmin / 1000.0) / 1e6
        ns_per = (tmin / 1000.0) / frames / n * 1e9
        xrt = (1000.0 * frames / SR) / tmin
        rows.append((n, tmin, msps, ns_per, xrt))
        print(f"{n:>4} {tmin:>8.2f} {msps:>9.2f} {ns_per:>14.3f} {xrt:>7.1f}")

    # linear-fit slope (ns per sample per partial) from the two ends
    n0, t0 = rows[0][0], rows[0][1]
    n1, t1 = rows[-1][0], rows[-1][1]
    slope_ns = ((t1 - t0) / 1000.0) / frames / (n1 - n0) * 1e9
    print("-" * 48)
    print(f"marginal cost (fit {n0}->{n1}p): {slope_ns:.3f} ns per sample per partial")
    print(f"=> at 48kHz stereo, one voice saturates a core at ~"
          f"{int(1e9/(slope_ns*SR))} partials real-time (single-thread).")


if __name__ == "__main__":
    main()
