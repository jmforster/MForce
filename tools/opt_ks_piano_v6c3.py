#!/usr/bin/env python3
"""v6c3 — level normalization + low-register tail control on the pitch-tracked
excitation (Matt: hold the fixed-knock idea; revise existing first).

Two mechanisms, both driven by measurement:
  1. resStart curve — the bank's starting Q per note. Q 25 at 65 Hz rings
     ~120 ms on its own (the "loose snare" tail); treble ring at Q 25 is ~2 ms.
     Search the C2 endpoint, keep C6 at 25.
  2. Gain compensation curve — constant-Q resonators pass noise energy
     proportional to absolute bandwidth, so C2 renders ~12 dB quieter than C6
     (Matt obs 1). Measured per note and equalized via an exc_out gain node.

Scoring per resStart_lo candidate: band-profile targets (unchanged from v6c2),
C2 tail t(-20dB) vs his measured 110-195 ms (target <= 150 ms), level
uniformity enforced by construction (comp curve computed from measurement).
"""
import copy
import json
import os
import struct
import subprocess
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
WORK = os.path.join(ROOT, "patches", "sweep", "ks_piano_v6c3_opt")

NOTES = [(36, 65.41, 0.0), (60, 261.63, 4.0), (84, 1046.5, 8.0)]
SCORE = [{"note": n, "velocity": 0.85, "time": t, "duration": 3.5}
         for (n, f, t) in NOTES]

# v6c2_shaped winners, held fixed
LP_MULT = 2.5
CLICK = (0.002, 0.050, 0.03, 0.15)   # click env attack/decay, gain lo/hi
BURST = (0.002, 0.040)


def excitation_patch(res_lo, gain_curve=None):
    res_curve = [[65.0, res_lo], [1046.5, 25.0]]
    if gain_curve is None:
        gain_curve = [[65.0, 1.0], [1046.5, 1.0]]
    ca, cd, g_lo, g_hi = CLICK
    return {
        "sampleRate": 48000,
        "graph": {
            "nodes": [
                {"id": "env", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": BURST[0], "decay": BURST[1],
                    "sustainLevel": 0.0, "release": 0.0}},
                {"id": "noise", "type": "WhiteNoiseSource", "params": {
                    "amplitude": {"ref": "env"}}},
                {"id": "env_click", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": ca, "decay": cd,
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
                {"id": "exc_sum", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_lp"},
                    "source2": {"ref": "exc_clickgain"},
                    "operation": "sum"}},
                {"id": "exc_mix", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_sum"}, "source2": 1.0,
                    "operation": "multiply"}},
            ],
            "output": "exc_mix",
        },
        "instrument": {
            "polyphony": 1,
            "volume": 0.02,
            "paramMap": {
                "frequency": [
                    "hammer.frequency",
                    {"target": "exc_lp.cutoffFreq",
                     "curve": [[f, LP_MULT * f] for (_, f, _) in NOTES]},
                    {"target": "exc_clickgain.source2",
                     "curve": [[65.0, g_lo], [1046.5, g_hi]]},
                    {"target": "hammer.resStart", "curve": copy.deepcopy(res_curve)},
                    {"target": "exc_mix.source2", "curve": copy.deepcopy(gain_curve)},
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
        rms = np.sqrt((seg ** 2).mean())
        mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        freqs = np.fft.rfftfreq(len(seg), 1 / sr)
        tot = mag.sum() + 1e-12
        body = mag[freqs < 4.5 * f0].sum() / tot
        mid = mag[(freqs >= 4.5 * f0) & (freqs < 4000)].sum() / tot
        click = mag[(freqs >= 4000) & (freqs < 10000)].sum() / tot
        # tail: 5 ms envelope, time from peak to -20 dB
        long_seg = mono[o:o + int(0.6 * sr)]
        hop = int(0.005 * sr)
        env = np.array([np.abs(long_seg[i:i + hop]).max()
                        for i in range(0, len(long_seg) - hop, hop)])
        pk = env[:8].max() + 1e-12
        t20 = 0.6
        for j, e in enumerate(env):
            if j > 0 and 20 * np.log10(e / pk + 1e-12) < -20:
                t20 = j * 0.005
                break
        out.append({"rms": rms, "body": body, "mid": mid, "click": click, "t20": t20})
    return out


def run(res_lo, gain_curve=None, tag=""):
    ppath = os.path.join(WORK, "c3_%s.json" % tag)
    wpath = os.path.join(WORK, "c3_%s.wav" % tag)
    json.dump(excitation_patch(res_lo, gain_curve), open(ppath, "w"))
    subprocess.run([CLI, ppath, wpath], capture_output=True)
    return measure(wpath)


def main():
    os.makedirs(WORK, exist_ok=True)
    print("resLo  per-note rms         t20 ms (C2/C4/C6)   C2 body/mid/click")
    results = []
    for res_lo in [8.0, 12.0, 18.0, 25.0]:
        m = run(res_lo, tag="r%d" % int(res_lo))
        rms = [x["rms"] for x in m]
        t20 = [x["t20"] * 1000 for x in m]
        print("%5.0f  %7.1f %7.1f %7.1f   %4.0f %4.0f %4.0f    %.2f/%.2f/%.2f"
              % (res_lo, *rms, *t20, m[0]["body"], m[0]["mid"], m[0]["click"]))
        results.append((res_lo, m))

    # pick: smallest resStart_lo whose C2 band profile stays on target
    # (body >= 0.85), preferring C2 t20 <= 150 ms
    good = [(r, m) for (r, m) in results
            if m[0]["body"] >= 0.85 and m[0]["t20"] * 1000 <= 160]
    pick = good[0] if good else results[0]
    res_lo, m = pick
    # gain compensation: equalize per-note rms to the C4 level
    ref = m[1]["rms"]
    gains = [ref / max(x["rms"], 1e-9) for x in m]
    gain_curve = [[65.0, round(gains[0], 3)], [261.63, round(gains[1], 3)],
                  [1046.5, round(gains[2], 3)]]
    m2 = run(res_lo, gain_curve, tag="r%d_comp" % int(res_lo))
    rms2 = [x["rms"] for x in m2]
    print("\nPICK resStart_lo=%.0f  gain_curve=%s" % (res_lo, gain_curve))
    print("compensated rms: %7.1f %7.1f %7.1f  (uniformity %.2f)"
          % (*rms2, max(rms2) / min(rms2)))


if __name__ == "__main__":
    main()
