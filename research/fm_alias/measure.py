#!/usr/bin/env python3
"""Measure FM alias suppression by oversampling (item 7, G3).

Reference-render method: render the same aliasing-heavy FM tone at
oversample = 1,2,4,8,16. Treat M=16 as the (near) alias-free ground truth.
For each M, the alias residual is the RMS of the in-band (0..18 kHz)
magnitude-spectrum difference vs the M=16 reference. Magnitude-only, so the
decimation filter's group delay is common-mode. Band capped at 18 kHz to stay
below the decimation filter transition so we measure folded aliases, not the
(common-mode) rolloff.

Suppression(M) dB = 20*log10(residual_1 / residual_M).

numpy only (wave stdlib for I/O). Steady segment 0.3..1.7 s.
"""
import os, wave, numpy as np

RDIR = os.path.join(os.path.dirname(__file__), "..", "..", "renders", "fm_oversample")
FACTORS = [1, 2, 4, 8, 16]
SR = 48000
T0, T1 = 0.3, 1.7          # steady window
BAND_HI = 18000.0          # in-band ceiling (below filter transition)


def read_mono(path):
    with wave.open(path, "rb") as w:
        n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(n)
    assert sw == 2, f"expected 16-bit, got {sw*8}-bit"
    a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a


def spectrum(sig):
    seg = sig[int(T0 * SR):int(T1 * SR)]
    win = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * win))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / SR)
    return freqs, mag


def main():
    specs = {}
    for m in FACTORS:
        p = os.path.join(RDIR, f"fm_alias_os{m:02d}.wav")
        freqs, mag = spectrum(read_mono(p))
        specs[m] = mag
    ref = specs[16]
    inband = freqs <= BAND_HI

    def residual(m):
        d = specs[m][inband] - ref[inband]
        return np.sqrt(np.mean(d * d))

    r1 = residual(1)
    print(f"{'M':>3}  {'in-band resid':>13}  {'suppression':>11}  {'RMS(full)':>10}")
    print("-" * 46)
    for m in FACTORS:
        r = residual(m)
        supp = 20 * np.log10(r1 / r) if r > 0 else float("inf")
        full_rms = np.sqrt(np.mean(read_mono(
            os.path.join(RDIR, f"fm_alias_os{m:02d}.wav")) ** 2))
        tag = "  <- reference" if m == 16 else ""
        print(f"{m:>3}  {r:>13.6f}  {supp:>9.1f}dB  {full_rms:>10.5f}{tag}")

    # Fraction of M=1 in-band alias energy removed by M=8 (the practical setting)
    r8 = residual(8)
    print(f"\nM=8 removes {100*(1 - r8/r1):.1f}% of M=1's in-band alias residual "
          f"(vs the M=16 reference).")


if __name__ == "__main__":
    main()
