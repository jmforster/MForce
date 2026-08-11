#!/usr/bin/env python3
"""Rung 2 (v6c) excitation-shaping optimizer — objective match to the band
targets measured from the 2021 AF video (docs/research/afpiano_2021/ANALYSIS.md).

His final string input: dominant body (low harmonics), deep mid scoop, small
separated HF click at 4-10 kHz. We shape our (noise burst -> hammer bank)
excitation with a pitch-tracked Butterworth LP (body) in parallel with a fixed
4-9.5 kHz bandpass click path, and grid-search:

  LP_MULT    - LP cutoff as multiple of f0 (pitch-tracked via paramMap curve)
  CLICK_GAIN - click path level into the sum
  DECAY      - noise burst decay (re-brackets Matt's rung-1 'short' pick
               objectively; attack fixed at his winner's 2 ms)

Renders excitation-only patches (output = exc_mix, no string) on the C2/C4/C6
score, FFTs a 120 ms window at each onset, and scores per-note band fractions:

  body  = 0 .. 4.5*f0          target-ish 0.88 (dominant)
  mid   = 4.5*f0 .. 4 kHz      target <= 0.03 (the scoop)
  click = 4 .. 10 kHz          target 0.06

Prints the ranked grid; winner is baked into gen_ks_piano_v6.py as v6c.
"""
import copy
import itertools
import json
import os
import struct
import subprocess
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
WORK = os.path.join(ROOT, "patches", "sweep", "ks_piano_v6c_opt")

NOTES = [(36, 65.41, 0.0), (60, 261.63, 4.0), (84, 1046.5, 8.0)]

SCORE = [{"note": n, "velocity": 0.85, "time": t, "duration": 3.5}
         for (n, f, t) in NOTES]


def excitation_patch(lp_mult, click_gain, decay):
    curve = [[f, lp_mult * f] for (_, f, _) in NOTES]
    return {
        "sampleRate": 48000,
        "graph": {
            "nodes": [
                {"id": "env", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": 0.002, "decay": decay,
                    "sustainLevel": 0.0, "release": 0.0}},
                {"id": "noise", "type": "WhiteNoiseSource", "params": {
                    "amplitude": {"ref": "env"}}},
                {"id": "hammer", "type": "HammerBank", "params": {
                    "source": {"ref": "noise"}, "frequency": 220.0,
                    "numBands": 4,
                    "harm1": 1.0, "harm2": 2.0, "harm3": 3.0, "harm4": 4.0,
                    "resStart": 25.0, "resEnd": 1.2, "resDecay": 0.012,
                    "bandTilt": -0.25, "direct": 0.0, "gain": 2.0}},
                {"id": "exc_lp", "type": "BWLowpassFilter", "params": {
                    "source": {"ref": "hammer"}, "sections": 2,
                    "cutoffFreq": 800.0}},
                # Click path taps the NOISE, not the bank: the bank re-filters
                # everything onto harmonics 1-4, so at C2/C4 its output has no
                # 4-10 kHz left to extract (first grid run proved it). His
                # patch runs the filter paths from the noise in parallel too.
                {"id": "exc_click", "type": "BWBandpassFilter", "params": {
                    "source": {"ref": "noise"}, "sections": 2,
                    "lowCutoff": 4000.0, "highCutoff": 9500.0}},
                {"id": "exc_clickgain", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_click"}, "source2": click_gain,
                    "operation": "multiply"}},
                {"id": "exc_mix", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_lp"},
                    "source2": {"ref": "exc_clickgain"},
                    "operation": "sum"}},
            ],
            "output": "exc_mix",
        },
        "instrument": {
            "polyphony": 1,
            "volume": 0.05,   # keep well under the clipper; we measure ratios
            "paramMap": {
                "frequency": [
                    "hammer.frequency",
                    {"target": "exc_lp.cutoffFreq", "curve": copy.deepcopy(curve)},
                ],
            },
        },
        "score": SCORE,
    }


def band_profile(wav_path):
    w = wave.open(wav_path)
    sr, ch, nf = w.getframerate(), w.getnchannels(), w.getnframes()
    s = struct.unpack("<%dh" % (nf * ch), w.readframes(nf))
    w.close()
    mono = np.array(s[::ch], dtype=float)
    out = []
    for (_, f0, t) in NOTES:
        seg = mono[int(t * sr):int(t * sr) + int(0.12 * sr)]
        mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        freqs = np.fft.rfftfreq(len(seg), 1 / sr)
        tot = mag.sum() + 1e-12
        body = mag[freqs < 4.5 * f0].sum() / tot
        mid = mag[(freqs >= 4.5 * f0) & (freqs < 4000)].sum() / tot
        click = mag[(freqs >= 4000) & (freqs < 10000)].sum() / tot
        out.append((body, mid, click))
    return out


def distance(profile):
    err = 0.0
    for (body, mid, click) in profile:
        err += (0.88 - body) ** 2
        err += 4.0 * max(0.0, mid - 0.03) ** 2
        err += 10.0 * (click - 0.06) ** 2
    return err


def main():
    os.makedirs(WORK, exist_ok=True)
    grid = list(itertools.product(
        [1.5, 2.5, 3.5, 5.0],          # LP_MULT
        [0.03, 0.08, 0.15, 0.30],      # CLICK_GAIN
        [0.015, 0.020, 0.040],         # DECAY
    ))
    results = []
    for (m, g, d) in grid:
        tag = "m%.1f_g%.2f_d%.3f" % (m, g, d)
        ppath = os.path.join(WORK, tag + ".json")
        wpath = os.path.join(WORK, tag + ".wav")
        json.dump(excitation_patch(m, g, d), open(ppath, "w"))
        subprocess.run([CLI, ppath, wpath], capture_output=True)
        prof = band_profile(wpath)
        err = distance(prof)
        results.append((err, m, g, d, prof))
    results.sort()
    print("rank  err     LPxF0 clickG decay  per-note (body/mid/click)")
    for i, (err, m, g, d, prof) in enumerate(results[:10]):
        pstr = "  ".join("%.2f/%.2f/%.2f" % p for p in prof)
        print("%2d  %7.4f  %4.1f  %5.2f  %5.3f  %s" % (i + 1, err, m, g, d, pstr))
    best = results[0]
    print("\nWINNER: LP_MULT=%.1f CLICK_GAIN=%.2f DECAY=%.3f (err %.4f)"
          % (best[1], best[2], best[3], best[0]))


if __name__ == "__main__":
    main()
