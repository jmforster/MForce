"""Verify the v5 grounded render reproduces the measured Iowa motion stats.

Runs derive_motion's per-harmonic analysis on the C4 note of a fable1_v5
render (note 4 in the ladder, t=8.0..9.6) and prints the same metrics for
direct comparison against the Iowa medians:
  target: resid ~7.3 cents RMS @ ~8.3 Hz, coh ~0.0; amp ~50% @ ~1.4 Hz coh ~0.5
"""
import sys

import numpy as np
import soundfile as sf

import derive_motion as dm

NOTE_T0, NOTE_LEN = 8.0, 1.6   # C4 in the v3+ ladder
SUS0, SUS1 = 0.25, 1.55        # sustain window inside the note


def analyze_segment(x, sr, f0_nom):
    f0 = dm.refine_f0(x, sr, f0_nom)
    sr_env = sr / 32
    cents, amps = [], []
    for k in range(1, dm.N_HARM + 1):
        env = dm.heterodyne(x, sr, k * f0)[::32]
        env = env[int(0.1 * sr_env): -int(0.1 * sr_env) or None]
        a = np.abs(env)
        if a.mean() < 1e-7:
            break
        dphi = np.angle(env[1:] / env[:-1])
        c = 1200.0 * np.log2(1.0 + (dphi * sr_env / (2 * np.pi)) / (k * f0))
        cents.append(c - c.mean())
        amps.append(a[1:] / a.mean())
    cents, amps = np.array(cents), np.array(amps)
    common = cents.mean(axis=0)
    resid = cents - common
    iu = np.triu_indices(len(resid), k=1)
    amp_fluct = amps - amps.mean(axis=1, keepdims=True)
    print(f"  harmonics={len(cents)}  f0={f0:.1f}")
    print(f"  common {common.std():5.1f}c @ {dm.rate_centroid(common, sr_env):4.1f}Hz")
    print(f"  resid  {np.median(resid.std(axis=1)):5.2f}c @ "
          f"{np.nanmedian([dm.rate_centroid(r, sr_env) for r in resid]):4.1f}Hz "
          f"coh={np.corrcoef(resid)[iu].mean():5.2f}")
    print(f"  amp    {100*np.median(amp_fluct.std(axis=1)):5.1f}% @ "
          f"{np.nanmedian([dm.rate_centroid(a, sr_env) for a in amp_fluct]):4.1f}Hz "
          f"coh={np.corrcoef(amp_fluct)[iu].mean():5.2f}")


def main():
    path = sys.argv[1]
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n0 = int((NOTE_T0 + SUS0) * sr)
    n1 = int((NOTE_T0 + SUS1) * sr)
    print(path)
    analyze_segment(x[n0:n1].astype(np.float64), sr, 261.63)


if __name__ == "__main__":
    main()
