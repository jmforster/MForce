"""Measure the SlewLimiterSource click ladder (backlog 3g / REVIEW 14).

Reports, over the attack window where the clicks live:
  - sha256, so "it changed" is proven rather than assumed
  - spectral centroid and 8 kHz+ energy fraction (click brightness)
  - the same figures for the whole file, as a sanity control

Usage: python tools/measure_slew_clicks.py
"""
import hashlib
import struct
import wave
from pathlib import Path

import numpy as np

RENDERS = Path(__file__).resolve().parent.parent / "renders" / "slew_clicks"
SR = 48000
ATTACK_S = 0.25


def read_mono(path):
    with wave.open(str(path), "rb") as w:
        n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(n)
    vals = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch == 2:
        vals = vals[0::2]
    return vals


def spectrum_stats(x):
    if len(x) < 256:
        return 0.0, 0.0
    win = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * win))
    freqs = np.fft.rfftfreq(len(x), 1.0 / SR)
    tot = mag.sum()
    if tot <= 0:
        return 0.0, 0.0
    centroid = float((freqs * mag).sum() / tot)
    hi = float(mag[freqs >= 8000.0].sum() / tot)
    return centroid, hi


def main():
    files = sorted(RENDERS.glob("*.wav"))
    ctrl_name = "slewclick_ctrl_none.wav"
    rows = []
    for p in files:
        x = read_mono(p)
        sha = hashlib.sha256(p.read_bytes()).hexdigest()[:12]
        atk = x[: int(ATTACK_S * SR)]
        c_a, h_a = spectrum_stats(atk)
        c_f, h_f = spectrum_stats(x)
        rows.append((p.name, sha, c_a, h_a, c_f, h_f))

    ctrl = next(r for r in rows if r[0] == ctrl_name)
    print(f"{'render':28} {'sha256':>13} {'atk cent':>9} {'atk>8k':>8} "
          f"{'full cent':>10} {'vs ctrl':>8}")
    for name, sha, c_a, h_a, c_f, h_f in rows:
        rel = c_a / ctrl[2] if ctrl[2] else 0.0
        tag = "  <- control" if name == ctrl_name else ""
        print(f"{name:28} {sha:>13} {c_a:9.1f} {h_a*100:7.2f}% "
              f"{c_f:10.1f} {rel:7.3f}x{tag}")

    distinct = len({r[1] for r in rows})
    print(f"\ndistinct sha256: {distinct} of {len(rows)} "
          f"({'all ladder rungs differ' if distinct == len(rows) else 'COLLISION'})")


if __name__ == "__main__":
    main()
