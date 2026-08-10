"""Derive motion-layer parameters (motionDepth/Hz/coherence, shimmer*) from
real Iowa viola samples, per dsp/BACKLOG item 2 (GOALS G1a).

Method, per sample and per harmonic k (sustain window only):
  1. Heterodyne: multiply by exp(-j*2*pi*k*f0*t), low-pass -> complex
     envelope A_k(t) * exp(j*phi_k(t)).
  2. Instantaneous frequency deviation = d(phi)/dt / (2*pi) Hz -> cents
     relative to k*f0.
  3. Decompose cents traces into a COMMON component (mean across harmonics —
     f0 movement: vibrato/pitch drift, belongs to the Vibrato node) and a
     per-harmonic RESIDUAL (line broadening — belongs to the motion layer).
  4. Amplitude: fractional fluctuation of |A_k| -> shimmer depth/rate and
     cross-harmonic correlation.

Mapping to engine configs (walk value RMS ~= 0.55 of its +-depth bound):
  motionDepth  ~= residual cents RMS / 0.55
  motionHz     ~= spectral centroid of the residual deviation PSD (0.5-20 Hz)
  motionCoherence ~= mean cross-harmonic correlation of residual cents
  shimmerDepth ~= fractional amp RMS / 0.55, shimmerHz / shimmerCoherence same
"""
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import firwin, fftconvolve, welch

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")

SUS_START, SUS_LEN = 0.6, 2.0
N_HARM = 20          # harmonics to analyze (SNR falls off above)
LP_HZ = 40.0         # heterodyne low-pass half-width (captures <=40 Hz motion)
BAND_HZ = (0.5, 20.0)  # analysis band for rates (below vibrato..above)

NOTE_FREQ = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5,
             "Gb": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}

# Spread across the four strings, mid-register (same spirit as build_*.py).
SAMPLES = ["sulC.C3", "sulC.G3", "sulG.G3", "sulG.D4",
           "sulD.D4", "sulD.A4", "sulA.A4", "sulA.E5"]


def note_to_freq(name):
    pc, octv = name[:-1], int(name[-1])
    midi = 12 * (octv + 1) + NOTE_FREQ[pc]
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def find_file(tag):
    string, note = tag.split(".")
    for f in os.listdir(SAMPLE_DIR):
        if f".{string}." in f and f".{note}." in f and f.endswith(".aif"):
            return os.path.join(SAMPLE_DIR, f)
    raise FileNotFoundError(tag)


def refine_f0(x, sr, f0_nom):
    """Peak-pick the true fundamental near nominal (players aren't A440-exact)."""
    n = len(x)
    spec = np.abs(np.fft.rfft(x * np.hanning(n)))
    freqs = np.fft.rfftfreq(n, 1 / sr)
    band = (freqs > f0_nom * 0.94) & (freqs < f0_nom * 1.06)
    return freqs[band][np.argmax(spec[band])]


def heterodyne(x, sr, fc, lp_hz=None):
    """Mix the partial at fc down to DC and low-pass to isolate it.

    lp_hz: passband half-width, default LP_HZ. Isolating ONE partial requires
    the neighbours -- a spacing of ~f0 away -- to sit outside the passband,
    i.e. lp_hz < f0/2. At LP_HZ = 40 Hz that fails below f0 = 80 Hz, and the
    "residual" then measures beating between adjacent partials rather than
    line broadening. Callers pass a capped value for low notes; see
    refmetrics.motion_stats and diag_lowf0_heterodyne.py.

    Tap count scales with the cutoff. The stock firwin(1025) has a transition
    band of roughly sr/1025 ~ 43 Hz at 44.1 kHz, so a narrowed cutoff with a
    fixed tap count would be meaningless. 1025 is retained exactly when the
    cutoff is the default, so nothing above the cap changes by a bit.
    """
    lp = LP_HZ if lp_hz is None else float(lp_hz)
    t = np.arange(len(x)) / sr
    z = x * np.exp(-2j * np.pi * fc * t)
    if lp >= LP_HZ:
        ntaps = 1025
    else:
        ntaps = min(max(int(4 * sr / lp) | 1, 1025), len(x) | 1)
    taps = firwin(ntaps, lp / (sr / 2))
    return fftconvolve(z, taps, mode="same")


def rate_centroid(trace, sr_env):
    """Spectral centroid of the trace's PSD within BAND_HZ."""
    f, p = welch(trace, fs=sr_env, nperseg=min(len(trace), 4096))
    band = (f >= BAND_HZ[0]) & (f <= BAND_HZ[1])
    if not band.any() or p[band].sum() <= 0:
        return float("nan")
    return float((f[band] * p[band]).sum() / p[band].sum())


def analyze(path, f0_nom, decim=32, offset=0.0, start=None, length=None):
    """offset: seconds to skip before the window (onset alignment for samples
    with variable lead-in). start/length: override SUS_START/SUS_LEN."""
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n0 = int((offset + (SUS_START if start is None else start)) * sr)
    x = x[n0: n0 + int((SUS_LEN if length is None else length) * sr)].astype(np.float64)
    f0 = refine_f0(x, sr, f0_nom)

    sr_env = sr / decim
    cents, amps = [], []
    for k in range(1, N_HARM + 1):
        env = heterodyne(x, sr, k * f0)[::decim]
        env = env[int(0.1 * sr_env): -int(0.1 * sr_env) or None]  # edge trim
        a = np.abs(env)
        if a.mean() < 1e-7:      # harmonic below noise floor — stop here
            break
        dphi = np.angle(env[1:] / env[:-1])
        dev_hz = dphi * sr_env / (2 * np.pi)
        # clip the ratio > 0 so a rare ~pi phase jump (weak harmonic, e.g.
        # clarinet evens) can't make log2 NaN — same guard as
        # refmetrics.motion_stats; medians dominate such samples out.
        ratio = np.maximum(1.0 + dev_hz / (k * f0), 1e-3)
        c = 1200.0 * np.log2(ratio)
        cents.append(c - c.mean())
        amps.append(a[1:] / a.mean())
    cents = np.array(cents)
    amps = np.array(amps)
    if len(cents) < 4:
        raise RuntimeError(f"{path}: only {len(cents)} usable harmonics")

    # Common (f0-track) vs residual decomposition.
    common = cents.mean(axis=0)
    resid = cents - common
    # Cross-harmonic correlation of residuals (mean pairwise, upper triangle).
    cc = np.corrcoef(resid)
    iu = np.triu_indices(len(resid), k=1)
    amp_fluct = amps - amps.mean(axis=1, keepdims=True)
    ca = np.corrcoef(amp_fluct)

    return {
        "f0": f0,
        "n_harm": len(cents),
        "common_cents_rms": float(common.std()),
        "common_rate_hz": rate_centroid(common, sr_env),
        "resid_cents_rms": float(np.median(resid.std(axis=1))),
        "resid_rate_hz": float(np.nanmedian(
            [rate_centroid(r, sr_env) for r in resid])),
        "resid_coherence": float(cc[iu].mean()),
        "amp_frac_rms": float(np.median(amp_fluct.std(axis=1))),
        "amp_rate_hz": float(np.nanmedian(
            [rate_centroid(a, sr_env) for a in amp_fluct])),
        "amp_coherence": float(ca[iu].mean()),
    }


def main():
    rows = []
    for tag in SAMPLES:
        f0_nom = note_to_freq(tag.split(".")[1])
        try:
            r = analyze(find_file(tag), f0_nom)
        except Exception as e:  # noqa: BLE001 — report and continue
            print(f"{tag}: FAILED ({e})")
            continue
        r["tag"] = tag
        rows.append(r)
        print(f"{tag:10s} f0={r['f0']:7.2f}  harm={r['n_harm']:2d}  "
              f"common {r['common_cents_rms']:5.1f}c @{r['common_rate_hz']:4.1f}Hz | "
              f"resid {r['resid_cents_rms']:5.2f}c @{r['resid_rate_hz']:4.1f}Hz "
              f"coh={r['resid_coherence']:5.2f} | "
              f"amp {100*r['amp_frac_rms']:5.1f}% @{r['amp_rate_hz']:4.1f}Hz "
              f"coh={r['amp_coherence']:5.2f}")

    if not rows:
        sys.exit(1)
    med = {k: float(np.median([r[k] for r in rows]))
           for k in rows[0] if k != "tag"}
    print("\n=== medians across samples ===")
    for k, v in med.items():
        print(f"  {k}: {v:.3f}")

    print("\n=== derived engine configs (walk RMS ~ 0.55 x depth) ===")
    print(f"  motionDepth    = {med['resid_cents_rms'] / 0.55:.1f}  (cents)")
    print(f"  motionHz       = {med['resid_rate_hz']:.1f}")
    print(f"  motionCoherence= {max(0.0, med['resid_coherence']):.2f}")
    print(f"  shimmerDepth   = {med['amp_frac_rms'] / 0.55:.2f}")
    print(f"  shimmerHz      = {med['amp_rate_hz']:.1f}")
    print(f"  shimmerCoherence={max(0.0, med['amp_coherence']):.2f}")
    print(f"  [info] f0-track (vibrato node's domain): "
          f"{med['common_cents_rms']:.1f} cents RMS @ {med['common_rate_hz']:.1f} Hz")


if __name__ == "__main__":
    main()
