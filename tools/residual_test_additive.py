#!/usr/bin/env python3
"""Quantify the render difference between renders/nulltest/base and .../new.

The byte-hash null test answers "identical or not". When an optimization is
deliberately not bit-exact (a sin approximation), the question becomes "how
far off, in dB" — this reports peak and RMS of the difference signal relative
to full scale, plus how many samples moved and by how many 16-bit LSBs.

Usage: python tools/residual_test_additive.py
"""
import os
import struct
import sys
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "renders", "nulltest")


def read(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        raw = w.readframes(n)
    return np.frombuffer(raw, dtype="<i2").astype(np.float64)


def db(x):
    return -np.inf if x <= 0 else 20.0 * np.log10(x)


def main():
    base, new = os.path.join(OUT, "base"), os.path.join(OUT, "new")
    tags = sorted(f[:-4] for f in os.listdir(base) if f.endswith(".wav"))

    print(f"{'patch':16s} {'peak dBFS':>10} {'resid pk':>10} {'resid rms':>10} "
          f"{'diff smp':>9} {'max LSB':>8}")
    print("-" * 70)
    worst = -np.inf
    for t in tags:
        pa, pb = os.path.join(base, t + ".wav"), os.path.join(new, t + ".wav")
        if not os.path.exists(pb):
            print(f"{t:16s}  (missing in new)")
            continue
        a, b = read(pa), read(pb)
        if a.shape != b.shape:
            print(f"{t:16s}  LENGTH MISMATCH {a.shape} vs {b.shape}")
            continue
        d = b - a
        fs = 32768.0
        sig_pk = db(np.abs(a).max() / fs)
        res_pk = db(np.abs(d).max() / fs)
        res_rms = db(np.sqrt(np.mean(d * d)) / fs)
        nz = int(np.count_nonzero(d))
        print(f"{t:16s} {sig_pk:>10.1f} {res_pk:>10.1f} {res_rms:>10.1f} "
              f"{100.0*nz/len(d):>8.1f}% {int(np.abs(d).max()):>8d}")
        worst = max(worst, res_pk)

    print("-" * 70)
    print(f"worst-case residual peak across the set: {worst:.1f} dBFS")
    print("(16-bit LSB = -90.3 dBFS; 1 LSB of difference is requantization, "
          "not a signal change)")


if __name__ == "__main__":
    main()
