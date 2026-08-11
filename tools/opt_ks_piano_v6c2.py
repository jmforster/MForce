#!/usr/bin/env python3
"""v6c2 — rung-2 revision after Matt's "drumstick, turned up too high" verdict.

Two structural changes vs opt_ks_piano_v6c.py:
  1. Click path gets its OWN envelope (his dual hit-length structure): shape
     searched — tick (2 ms) vs bloom (15/30 ms attack). Measurement of his
     string-input hits says attack-window (15 ms) click fraction is ~0.002,
     i.e. his HF is a post-attack sheen, NOT a transient tick. The grid
     includes both; the attack metric decides.
  2. Click gain is pitch-dependent (paramMap curve): searched at C2 and C6
     endpoints. Fixes the constant-click-over-shrinking-body ratio that made
     the low registers read as drumstick.

Scoring adds the attack-window term the first optimizer couldn't see:
  120 ms bands as before (body>=0.88, mid<=0.03, click~0.06)
  + strong penalty on attack-window click fraction above 0.01 (his ~0.002).
Body path and burst (40 ms) fixed at the rung-2 winner.
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
WORK = os.path.join(ROOT, "patches", "sweep", "ks_piano_v6c2_opt")

NOTES = [(36, 65.41, 0.0), (60, 261.63, 4.0), (84, 1046.5, 8.0)]
SCORE = [{"note": n, "velocity": 0.85, "time": t, "duration": 3.5}
         for (n, f, t) in NOTES]
LP_MULT = 2.5   # rung-2 winner, held fixed


def excitation_patch(click_attack, click_decay, g_lo, g_hi):
    lp_curve = [[f, LP_MULT * f] for (_, f, _) in NOTES]
    gain_curve = [[65.0, g_lo], [1046.5, g_hi]]
    return {
        "sampleRate": 48000,
        "graph": {
            "nodes": [
                {"id": "env", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": 0.002, "decay": 0.040,
                    "sustainLevel": 0.0, "release": 0.0}},
                {"id": "noise", "type": "WhiteNoiseSource", "params": {
                    "amplitude": {"ref": "env"}}},
                {"id": "env_click", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": click_attack, "decay": click_decay,
                    "sustainLevel": 0.0, "release": 0.0}},
                {"id": "noise_click", "type": "WhiteNoiseSource", "params": {
                    "amplitude": {"ref": "env_click"}}},
                {"id": "hammer", "type": "HammerBank", "params": {
                    "source": {"ref": "noise"}, "frequency": 220.0,
                    "numBands": 4,
                    "harm1": 1.0, "harm2": 2.0, "harm3": 3.0, "harm4": 4.0,
                    "resStart": 25.0, "resEnd": 1.2, "resDecay": 0.012,
                    "bandTilt": -0.25, "direct": 0.0, "gain": 2.0}},
                {"id": "exc_lp", "type": "BWLowpassFilter", "params": {
                    "source": {"ref": "hammer"}, "sections": 2,
                    "cutoffFreq": 800.0}},
                {"id": "exc_click", "type": "BWBandpassFilter", "params": {
                    "source": {"ref": "noise_click"}, "sections": 2,
                    "lowCutoff": 4000.0, "highCutoff": 9500.0}},
                {"id": "exc_clickgain", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_click"}, "source2": 0.15,
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
            "volume": 0.05,
            "paramMap": {
                "frequency": [
                    "hammer.frequency",
                    {"target": "exc_lp.cutoffFreq", "curve": copy.deepcopy(lp_curve)},
                    {"target": "exc_clickgain.source2", "curve": copy.deepcopy(gain_curve)},
                ],
            },
        },
        "score": SCORE,
    }


def measure(wav_path):
    w = wave.open(wav_path)
    sr, ch, nf = w.getframerate(), w.getnchannels(), w.getnframes()
    s = struct.unpack("<%dh" % (nf * ch), w.readframes(nf))
    w.close()
    mono = np.array(s[::ch], dtype=float)
    out = []
    for (_, f0, t) in NOTES:
        o = int(t * sr)
        seg = mono[o:o + int(0.12 * sr)]
        mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        freqs = np.fft.rfftfreq(len(seg), 1 / sr)
        tot = mag.sum() + 1e-12
        body = mag[freqs < 4.5 * f0].sum() / tot
        mid = mag[(freqs >= 4.5 * f0) & (freqs < 4000)].sum() / tot
        click = mag[(freqs >= 4000) & (freqs < 10000)].sum() / tot
        aseg = mono[o:o + int(0.015 * sr)]
        amag = np.abs(np.fft.rfft(aseg * np.hanning(len(aseg)))) ** 2
        afreqs = np.fft.rfftfreq(len(aseg), 1 / sr)
        aclick = amag[(afreqs >= 4000) & (afreqs < 10000)].sum() / (amag.sum() + 1e-12)
        out.append((body, mid, click, aclick))
    return out


def distance(profile):
    err = 0.0
    for (body, mid, click, aclick) in profile:
        err += (0.88 - body) ** 2
        err += 4.0 * max(0.0, mid - 0.03) ** 2
        err += 10.0 * (click - 0.06) ** 2
        err += 50.0 * max(0.0, aclick - 0.01) ** 2   # his hits: ~0.002
    return err


def main():
    os.makedirs(WORK, exist_ok=True)
    grid = list(itertools.product(
        [0.002, 0.015, 0.030],   # click attack (tick vs bloom)
        [0.050, 0.120],          # click decay
        [0.03, 0.08],            # gain at C2
        [0.15, 0.30],            # gain at C6
    ))
    results = []
    for (a, d, glo, ghi) in grid:
        tag = "a%.3f_d%.3f_lo%.2f_hi%.2f" % (a, d, glo, ghi)
        ppath = os.path.join(WORK, tag + ".json")
        wpath = os.path.join(WORK, tag + ".wav")
        json.dump(excitation_patch(a, d, glo, ghi), open(ppath, "w"))
        subprocess.run([CLI, ppath, wpath], capture_output=True)
        prof = measure(wpath)
        results.append((distance(prof), a, d, glo, ghi, prof))
    results.sort()
    print("rank  err     atk    dec   gLo   gHi   per-note body/mid/click/atkClick")
    for i, (err, a, d, glo, ghi, prof) in enumerate(results[:10]):
        pstr = "  ".join("%.2f/%.2f/%.2f/%.3f" % p for p in prof)
        print("%2d  %7.4f  %.3f  %.3f  %.2f  %.2f  %s"
              % (i + 1, err, a, d, glo, ghi, pstr))
    b = results[0]
    print("\nWINNER: attack=%.3f decay=%.3f gLo=%.2f gHi=%.2f (err %.4f)"
          % (b[1], b[2], b[3], b[4], b[0]))


if __name__ == "__main__":
    main()
