"""Stability metrics for harness renders (Matt 2026-09-12): flutter =
large volume fluctuation in the sustain (we want its ABSENCE) and pitch
accuracy/steadiness, measured on the MIDDLE note of the 5-note C-octave
score (slot 2, t=4..6s; sustain window 4.5-5.8s).

Per file:
  flutter_pct   = envelope std/mean over the sustain, 10 ms RMS hops (%)
  dip_pct       = deepest envelope dip below the sustain mean (%)
  cents_med     = median pitch error vs the slot's nominal f0 (cents,
                  folded to nearest harmonic per 100 ms window)
  cents_iqr     = pitch wobble (IQR of the per-window cents track)
Lower is better on all four. Prints a table sorted by flutter and
writes stability.json beside the WAVs.

Usage: python tools/measure_stability.py <render_dir> <middle_f0_hz>
"""
import json
import math
import os
import struct
import sys
import wave

T0, T1 = 4.5, 5.8      # sustain window of slot 2 (notes at 0,2,4,6,8 s)


def load_mono(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    return [sum(raw[i * ch:(i + 1) * ch]) / ch / 32767.0
            for i in range(n)], sr


def analyze(path, f0):
    mono, sr = load_mono(path)
    seg = mono[int(T0 * sr):int(T1 * sr)]
    if len(seg) < sr // 4:
        return None
    # Envelope: 10 ms RMS hops.
    hop = int(0.010 * sr)
    env = []
    for i in range(0, len(seg) - hop, hop):
        s = seg[i:i + hop]
        env.append(math.sqrt(sum(x * x for x in s) / len(s)))
    mean = sum(env) / len(env)
    if mean < 1e-4:
        return {"silent": True}
    var = sum((e - mean) ** 2 for e in env) / len(env)
    flutter = math.sqrt(var) / mean * 100.0
    dip = (mean - min(env)) / mean * 100.0
    # Pitch: 100 ms windows, quadratic-interpolated FFT peak folded to the
    # nearest harmonic of f0 (plain DFT via Goertzel scan around
    # candidates is too slow; use zero-padded real FFT per window).
    import cmath
    cents = []
    win = int(0.100 * sr)
    for i in range(0, len(seg) - win, win):
        s = seg[i:i + win]
        m = sum(s) / len(s)
        s = [x - m for x in s]
        # Hann + rfft via builtin complex FFT (recursive) is heavy in pure
        # python; use numpy if present, else Goertzel at harmonic candidates.
        try:
            import numpy as np
            arr = np.array(s) * np.hanning(len(s))
            sp = np.abs(np.fft.rfft(arr, 4 * len(arr)))
            fr = np.fft.rfftfreq(4 * len(arr), 1.0 / sr)
            lo, hi = np.searchsorted(fr, f0 * 0.5), np.searchsorted(fr, f0 * 6)
            pk = fr[lo + int(np.argmax(sp[lo:hi]))]
        except ImportError:
            best, pk = -1.0, f0
            for h in range(1, 7):
                for c in (0.97, 0.985, 1.0, 1.015, 1.03):
                    f = f0 * h * c
                    wq = 2.0 * math.pi * f / sr
                    co = 2.0 * math.cos(wq)
                    s0 = s1 = 0.0
                    for x in s:
                        s0, s1 = x + co * s0 - s1, s0
                    p = s1 * s1 + s0 * s0 - co * s0 * s1
                    if p > best:
                        best, pk = p, f
        h = max(1, round(pk / f0))
        cents.append(1200.0 * math.log2(pk / (h * f0)))
    cents.sort()
    n = len(cents)
    med = cents[n // 2]
    iqr = cents[(3 * n) // 4] - cents[n // 4]
    return {"flutter_pct": round(flutter, 2), "dip_pct": round(dip, 1),
            "cents_med": round(med, 1), "cents_iqr": round(iqr, 1)}


def main():
    d = sys.argv[1]
    f0 = float(sys.argv[2])
    out = {}
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".wav"):
            continue
        r = analyze(os.path.join(d, fn), f0)
        out[fn[:-4]] = r
    ranked = sorted((k for k, v in out.items() if v and not v.get("silent")),
                    key=lambda k: out[k]["flutter_pct"])
    print(f"{'cell':26s} {'flutter%':>8s} {'dip%':>6s} {'cents':>7s} {'iqr':>6s}")
    for k in ranked:
        v = out[k]
        print(f"{k:26s} {v['flutter_pct']:8.2f} {v['dip_pct']:6.1f} "
              f"{v['cents_med']:7.1f} {v['cents_iqr']:6.1f}")
    for k, v in out.items():
        if not v or v.get("silent"):
            print(f"{k:26s}   silent/short")
    json.dump(out, open(os.path.join(d, "stability.json"), "w"), indent=1)


main()
