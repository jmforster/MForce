"""Sanity metrics for the Fable1 renders (no ground-truth evaluation).

Per render, on the first scale note (t=0..1.6 s):
  attack_flux  — mean spectral flux over 0.0-0.45 s (chaos-settle patches
                 should be high)
  sustain_flux — mean spectral flux over 0.6-1.5 s (must be > 0: proves the
                 sustain is not steady-state)
  hi_lag_ms    — arrival lag of the >2.5 kHz band vs the <1 kHz band
                 (onset-dispersion patches should show positive lag)
All patches must also be non-silent (peak from the CLI already confirmed).
"""
import os
import sys
import wave

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REND = os.path.join(REPO, "renders", sys.argv[1] if len(sys.argv) > 1 else "fable1")

NFFT, HOP = 2048, 512


def read_wav_mono(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        raw = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
        raw = raw.reshape(-1, w.getnchannels()).mean(axis=1) / 32768.0
    return raw, sr


def stft_mag(x, sr):
    frames = []
    win = np.hanning(NFFT)
    for i in range(0, len(x) - NFFT, HOP):
        frames.append(np.abs(np.fft.rfft(x[i:i + NFFT] * win)))
    return np.array(frames), np.fft.rfftfreq(NFFT, 1.0 / sr)


def flux(mags):
    if len(mags) < 2:
        return 0.0
    d = np.diff(mags, axis=0)
    denom = mags[:-1].sum(axis=1) + 1e-9
    return float(np.mean(np.sqrt((d * d).sum(axis=1)) / denom))


def band_onset_lag(x, sr):
    """Time (ms) for the hi band (>2.5k) to reach half its sustain level,
    minus the same for the lo band (<1k)."""
    mags, freqs = stft_mag(x[: int(0.9 * sr)], sr)
    t = np.arange(len(mags)) * HOP / sr
    lo = mags[:, freqs < 1000].sum(axis=1)
    hi = mags[:, (freqs > 2500) & (freqs < 8000)].sum(axis=1)
    out = []
    for band in (lo, hi):
        sus = np.median(band[int(0.5 * sr / HOP):])
        idx = np.argmax(band >= 0.5 * sus)
        out.append(t[idx] * 1000.0)
    return out[1] - out[0]


def main():
    print(f"{'render':26s} {'attack_flux':>11s} {'sustain_flux':>12s} "
          f"{'ratio':>6s} {'hi_lag_ms':>9s}")
    failures = []
    for f in sorted(os.listdir(REND)):
        if not f.endswith(".wav"):
            continue
        x, sr = read_wav_mono(os.path.join(REND, f))
        note = x[: int(1.6 * sr)]
        a_mags, _ = stft_mag(note[: int(0.45 * sr)], sr)
        s_mags, _ = stft_mag(note[int(0.6 * sr): int(1.5 * sr)], sr)
        af, sf = flux(a_mags), flux(s_mags)
        lag = band_onset_lag(x, sr)
        print(f"{f:26s} {af:11.4f} {sf:12.4f} {af / (sf + 1e-9):6.2f} "
              f"{lag:9.1f}")
        if np.max(np.abs(x)) < 1e-4:
            failures.append(f + ": silent")
        if sf <= 1e-5:
            failures.append(f + ": steady-state sustain")
    if failures:
        print("FAILURES:")
        for m in failures:
            print(" ", m)
        sys.exit(1)
    print("all sane")


if __name__ == "__main__":
    main()
