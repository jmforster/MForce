"""Verify renders/vowel_grid/*.wav against patches/vowel_grid/_manifest.json.

Two-level check per render:

1. Envelope match (engine correctness): the patch predicts the harmonic
   envelope EXACTLY — flat buzz, formantWeight 1.0, formantFloor 0.0, so
   amplitude of harmonic k = max over formants of the engine's power-2 taper
   at k*f0 (formant.h get_gain, FormantSpectrum = max). Measured harmonic
   amplitudes (windowed DFT over the steady middle) must correlate with the
   prediction (r >= 0.985).

2. Formant-peak check (vowel identity): for each intended F1/F2/F3, if the
   PREDICTED envelope has a distinct local maximum within max(0.75*f0, 80 Hz)
   of the formant center, the MEASURED envelope must have a peak on the same
   harmonic (+/- one). Where the prediction itself has no distinct peak the
   entry is flagged physics-limited, not failed:
     F<f0    formant center below the fundamental (the soprano problem)
     merged  skirt overlap absorbs the formant into a neighbour's slope
"""
import json
import os
import sys
import wave

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PATCHES = os.path.join(REPO, "patches", "vowel_grid")
RENDERS = os.path.join(REPO, "renders", "vowel_grid")


def read_wav(path):
    with wave.open(path, "rb") as w:
        sr, n, ch, sw = (w.getframerate(), w.getnframes(),
                         w.getnchannels(), w.getsampwidth())
        raw = w.readframes(n)
    dtype = {2: np.int16, 4: np.int32}[sw]
    x = np.frombuffer(raw, dtype=dtype).astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return sr, x / np.iinfo(dtype).max


def taper(f, F, gain, w):
    """Engine formant taper (formant.h get_gain), w = engine width (full)."""
    lo, hi = F - w / 2.0, F + w / 2.0
    if f <= lo or f >= hi:
        return 0.0
    if f < F:
        return ((f - lo) / (F - lo)) ** 2 * gain
    return ((hi - f) / (hi - F)) ** 2 * gain


def predicted_env(f0, formants, fmax):
    ks = np.arange(1, int(fmax / f0) + 1)
    p = np.array([max(taper(k * f0, fm["freq"], fm["gain"], fm["engine_width"])
                      for fm in formants) for k in ks])
    return ks * f0, p


def measured_env(x, sr, f0, fmax):
    a, b = int(0.5 * sr), int(2.5 * sr)
    seg = x[a:b] * np.hanning(b - a)
    t = np.arange(b - a) / sr
    ks = np.arange(1, int(fmax / f0) + 1)
    return np.array([abs(np.sum(seg * np.exp(-2j * np.pi * k * f0 * t)))
                     for k in ks])


def local_maxima(freqs, amps, floor_frac=0.005):
    thresh = amps.max() * floor_frac
    out = []
    for i in range(len(amps)):
        lo = amps[i - 1] if i > 0 else -1.0
        hi = amps[i + 1] if i < len(amps) - 1 else -1.0
        if amps[i] >= lo and amps[i] > hi and amps[i] > thresh:
            out.append(freqs[i])
    return out


def check(m):
    name = m["file"][:-5]
    sr, x = read_wav(os.path.join(RENDERS, name + ".wav"))
    rms = float(np.sqrt(np.mean(x ** 2)))
    if rms < 1e-5:
        return name, "FAIL", ["silent"], rms, 0.0

    f0 = m["f0_rendered"]
    fmax = 6500.0
    tol = max(0.75 * f0, 80.0)
    freqs, P = predicted_env(f0, m["formants"], fmax)
    M = measured_env(x, sr, f0, fmax)
    P, M = P / (P.max() + 1e-30), M / (M.max() + 1e-30)
    r = float(np.corrcoef(P, M)[0, 1])

    ppeaks = local_maxima(freqs, P)
    mpeaks = local_maxima(freqs, M)
    notes, ok = [], r >= 0.985
    if r < 0.985:
        notes.append(f"envelope corr {r:.3f} < 0.985")

    prev_pd = None
    for lab, fm in zip(("F1", "F2", "F3"), m["formants"][:3]):
        F = fm["freq"]
        pd = min(ppeaks, key=lambda p: abs(p - F)) if ppeaks else None
        if pd is None or abs(pd - F) > tol:
            if F < f0:
                notes.append(f"{lab} {F:.0f} < f0 {f0:.0f} (unexcitable)")
            else:
                notes.append(f"{lab} {F:.0f} no distinct peak (merged)")
            continue
        md = min(mpeaks, key=lambda p: abs(p - pd)) if mpeaks else None
        if md is not None and abs(md - pd) <= 1.01 * f0:
            if F < f0:
                notes.append(f"{lab} {F:.0f} < f0 {f0:.0f} "
                             f"(soprano problem: peak sits at fundamental)")
            elif prev_pd is not None and pd == prev_pd:
                notes.append(f"{lab} merged with previous (shared peak "
                             f"{pd:.0f})")
            else:
                notes.append(f"{lab} ok ({md - F:+.0f})")
        else:
            ok = False
            notes.append(f"{lab} MISS: predicted peak {pd:.0f}, measured "
                         + (f"{md:.0f}" if md is not None else "none"))
        prev_pd = pd
    return name, ("PASS" if ok else "FAIL"), notes, rms, r


def main():
    manifest = json.load(open(os.path.join(PATCHES, "_manifest.json")))
    npass, limited = 0, []
    print(f"{'patch':<22} {'f0':>6} {'rms':>7} {'corr':>6}  result  notes")
    for m in manifest:
        name, res, notes, rms, r = check(m)
        if res == "PASS":
            npass += 1
        if any("unexcitable" in n or "merged" in n or "soprano" in n
               for n in notes):
            limited.append(name)
        print(f"{name:<22} {m['f0_rendered']:>6.1f} {rms:>7.4f} {r:>6.3f}  "
              f"{res:<6}  " + "; ".join(notes))
    print(f"\n{npass}/{len(manifest)} PASS")
    print(f"physics-limited entries ({len(limited)}): " + ", ".join(limited))
    sys.exit(0 if npass == len(manifest) else 1)


if __name__ == "__main__":
    main()
