#!/usr/bin/env python3
"""v6d — fixed knock + full-chain level calibration.

Matt's fresh-ears verdict (2026-08-12): low registers "rattle, no thump",
harpsichord/dulcimer attack; volume falls dramatically with pitch. Two
mechanisms:

1. KNOCK: pitch-FIXED low band (the hammer/soundboard body real pianos share
   across the keyboard; his "clunk" stage measures 0-200:0.47 / 200-600:0.52).
   Parallel path: own envelope -> fixed BW bandpass -> gain -> into the sum.
   Grid over band top, envelope decay, gain; scored on C2 attack compactness
   (attack peak vs early RMS) while preserving the click fraction and scoop.

2. FULL-CHAIN LEVEL: v6c3 equalized excitation RMS, but the string loop
   builds amplitude ~ burst_span/period (42 periods at C6 vs 2.6 at C2):
   another ~9 dB C2 loss AFTER the excitation. The chain is linear, so the
   exc_level curve is recalibrated against full-chain attack peaks (C6 = ref)
   in one measured correction.
"""
import copy
import itertools
import json
import os
import struct
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_ks_piano_v6 import base_patch  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
WORK = os.path.join(ROOT, "patches", "sweep", "ks_piano_v6d_opt")

NOTES = [(36, 65.41, 0.0), (60, 261.63, 4.0), (84, 1046.5, 8.0)]


def load_mono(path):
    w = wave.open(path)
    sr, ch, nf = w.getframerate(), w.getnchannels(), w.getnframes()
    s = struct.unpack("<%dh" % (nf * ch), w.readframes(nf))
    w.close()
    return np.array(s[::ch], dtype=float) / 32768.0, sr


def render(patch, tag, out_node=None, volume=None):
    p = copy.deepcopy(patch)
    if out_node:
        p["graph"]["output"] = out_node
    if volume is not None:
        p["instrument"]["volume"] = volume
    pp = os.path.join(WORK, tag + ".json")
    wp = os.path.join(WORK, tag + ".wav")
    json.dump(p, open(pp, "w"))
    subprocess.run([CLI, pp, wp], capture_output=True)
    return wp


def knock_patch(band_hi, decay, gain):
    return base_patch(excite_noise=(0.002, 0.040), shaping=(2.5, 0.15),
                      click_env=(0.002, 0.050, 0.03, 0.15),
                      level_curve=[(65.0, 2.284), (261.63, 1.0), (1046.5, 0.458)],
                      knock=(0.001, decay, 80.0, band_hi, gain))


def exc_metrics(wav):
    mono, sr = load_mono(wav)
    out = []
    for (_, f0, t) in NOTES:
        i = int(t * sr)
        atk = np.abs(mono[i:i + int(0.05 * sr)]).max()
        early = np.sqrt((mono[i:i + int(0.5 * sr)] ** 2).mean())
        seg = mono[i:i + int(0.12 * sr)]
        mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        freqs = np.fft.rfftfreq(len(seg), 1 / sr)
        tot = mag.sum() + 1e-12
        click = mag[(freqs >= 4000) & (freqs < 10000)].sum() / tot
        out.append({"atk": atk, "early": early, "click": click,
                    "compact": atk / (early + 1e-9)})
    return out


def main():
    os.makedirs(WORK, exist_ok=True)
    print("bandHi decay gain | C2 atk/compact/click | C6 click")
    results = []
    for (bh, dc, g) in itertools.product([300.0, 600.0], [0.03, 0.08],
                                         [0.5, 1.0, 2.0]):
        tag = "k%d_d%d_g%s" % (int(bh), int(dc * 1000), str(g).replace('.', ''))
        wav = render(knock_patch(bh, dc, g), tag, out_node="exc_level",
                     volume=0.02)
        m = exc_metrics(wav)
        # want: C2 attack compactness UP vs baseline, click preserved 3-9%
        score = m[0]["compact"] - 8.0 * abs(m[0]["click"] - 0.05) \
                - 8.0 * abs(m[2]["click"] - 0.05)
        results.append((score, bh, dc, g, m))
        print("%5.0f %.3f %4.1f | %.3f / %5.2f / %.3f | %.3f"
              % (bh, dc, g, m[0]["atk"], m[0]["compact"], m[0]["click"],
                 m[2]["click"]))
    results.sort(reverse=True)
    top2 = results[:2]

    print("\nTop-2 knock configs; now full-chain level calibration (C6 ref):")
    for rank, (score, bh, dc, g, _) in enumerate(top2, 1):
        patch = knock_patch(bh, dc, g)
        wav = render(patch, "full_r%d" % rank, volume=0.05)
        mono, sr = load_mono(wav)
        peaks = []
        for (_, f0, t) in NOTES:
            i = int(t * sr)
            peaks.append(np.abs(mono[i:i + int(0.05 * sr)]).max())
        ref = peaks[2]
        base = [2.284, 1.0, 0.458]
        corr = [b * ref / max(pk, 1e-9) for b, pk in zip(base, peaks)]
        curve = [[65.0, round(corr[0], 3)], [261.63, round(corr[1], 3)],
                 [1046.5, round(corr[2], 3)]]
        # verify
        patch2 = base_patch(excite_noise=(0.002, 0.040), shaping=(2.5, 0.15),
                            click_env=(0.002, 0.050, 0.03, 0.15),
                            level_curve=[tuple(c) for c in curve],
                            knock=(0.001, dc, 80.0, bh, g))
        wav2 = render(patch2, "full_r%d_cal" % rank, volume=0.05)
        mono2, _ = load_mono(wav2)
        peaks2 = [np.abs(mono2[int(t * sr):int(t * sr) + int(0.05 * sr)]).max()
                  for (_, _, t) in NOTES]
        print("rank %d: bandHi=%.0f decay=%.3f gain=%.1f  curve=%s"
              % (rank, bh, dc, g, curve))
        print("        attack peaks before %s -> after %s"
              % (["%.3f" % p for p in peaks], ["%.3f" % p for p in peaks2]))


if __name__ == "__main__":
    main()
