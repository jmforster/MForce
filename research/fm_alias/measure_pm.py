#!/usr/bin/env python3
"""Measure what the FMSource `phase` fix actually did to the spectrum.

For each (control, treated) WAV pair: spectral centroid, RMS bandwidth, and the
count of significant partials (peaks within 40 dB of the strongest). A working
phase-modulation path should WIDEN the spectrum -- more sidebands, higher
centroid -- not merely produce a different file.

Usage: python research/fm_alias/measure_pm.py
"""
import os
import sys
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REND = os.path.join(ROOT, "renders", "fm_phase")

PAIRS = [
    ("t1_06  phase LFO 1.7Hz +-1cyc", "t1_06_ctrl.wav", "t1_06_after.wav"),
    ("t1_07  phase 220Hz +-0.5cyc PM", "t1_07_ctrl.wav", "t1_07_after.wav"),
]


def read_mono(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw != 2:
        raise SystemExit("expected 16-bit: %s" % path)
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def analyse(path):
    x, sr = read_mono(path)
    # Analyse the first 2 s, where the amplitude envelope still has level.
    seg = x[: 2 * sr]
    win = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * win))
    freq = np.fft.rfftfreq(len(seg), 1.0 / sr)
    p = mag ** 2
    tot = p.sum()
    if tot <= 0:
        return dict(centroid=0.0, bw=0.0, npeak=0)
    centroid = float((freq * p).sum() / tot)
    bw = float(np.sqrt(((freq - centroid) ** 2 * p).sum() / tot))
    # Significant partials: local maxima within 40 dB of the peak bin.
    thresh = mag.max() * (10 ** (-40 / 20.0))
    loc = (mag[1:-1] > mag[:-2]) & (mag[1:-1] > mag[2:]) & (mag[1:-1] > thresh)
    return dict(centroid=centroid, bw=bw, npeak=int(loc.sum()))


def main():
    print("%-32s %10s %10s %8s" % ("", "centroid", "rms bw", "peaks"))
    fail = 0
    for label, ctrl, treat in PAIRS:
        c = analyse(os.path.join(REND, ctrl))
        t = analyse(os.path.join(REND, treat))
        print(label)
        print("  %-30s %10.1f %10.1f %8d" % ("control (phase unwired)", c["centroid"], c["bw"], c["npeak"]))
        print("  %-30s %10.1f %10.1f %8d" % ("treated (phase wired)", t["centroid"], t["bw"], t["npeak"]))
        print("  %-30s %9.2fx %9.2fx %+8d" % (
            "ratio / delta",
            t["centroid"] / c["centroid"] if c["centroid"] else 0.0,
            t["bw"] / c["bw"] if c["bw"] else 0.0,
            t["npeak"] - c["npeak"]))
        if t["npeak"] <= c["npeak"] and t["bw"] <= c["bw"]:
            print("  -> NO WIDENING: phase path is not producing sidebands")
            fail += 1
        print()
    print("FAIL" if fail else "PASS: every phase-wired render is spectrally wider than its control")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
