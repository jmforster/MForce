"""Shared reference-metric extractors for the ml_ears scorer (CMA-ES stage a).

Four timbre-distance dimensions, each computed IDENTICALLY on a real Iowa
sample and on a rendered candidate so the two can be differenced:
  1. harmonic_env    — log-amp per harmonic (spectral / body-formant shape)
  2. motion_stats    — resid cents, rates, coherences, amp fluct (derive_motion)
  3. broadband_ratios— inter-harmonic (bow-noise) energy per band, the ml_ears gap
  4. attack_stats    — per-band rise time + inter-band onset spread

Keeping the extractors in one module guarantees reference (iowa_reference.py)
and candidate (score_candidate.py) measure the same way.
"""
import numpy as np
from scipy.signal import butter, sosfiltfilt

import derive_motion as dm  # heterodyne / refine_f0 / rate_centroid / N_HARM

# lo / mid / hi analysis bands (Hz) for broadband + attack terms.
BANDS = [(160.0, 700.0), (700.0, 2500.0), (2500.0, 8000.0)]

NOTE_SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_to_midi(name):
    letter, acc, octv = name[0], name[1:-1], int(name[-1])
    semi = NOTE_SEMI[letter] + (1 if acc == "#" else -1 if acc == "b" else 0)
    return 12 * (octv + 1) + semi


def midi_to_freq(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


def partial_freq(f0, B, n):
    """Centre frequency of partial n under the stiff-string stretch law.

    f_n = n*f0*sqrt(1 + B*n^2). At B=0 this returns exactly n*f0 — the sqrt is
    exactly 1.0 and IEEE multiplication by 1.0 is exact — so every B=0 caller
    is bit-identical to the harmonic-position code this replaced.
    """
    return n * f0 * np.sqrt(1.0 + B * n * n)


def _fit_stretch(meas):
    """Least squares (f/n)^2 = a + b*n^2, b >= 0, with one 3-sigma outlier pass.

    Returns (a, b); f0 = sqrt(a), B = b/a. Lifted from piano_analysis.py so
    the scorer and the analysis script share one implementation (the import
    goes analysis -> refmetrics: piano_analysis pulls matplotlib and the
    CMA-ES inner loop must not).
    """
    ns = np.array([m[0] for m in meas], float)
    fs = np.array([m[1] for m in meas], float)
    y = (fs / ns) ** 2
    A = np.vstack([np.ones_like(ns), ns ** 2]).T
    coef, *_ = np.linalg.lstsq(A, y, rcond=None)
    resid = y - A @ coef
    keep = np.abs(resid) < 3 * (resid.std() + 1e-12)
    if 4 <= keep.sum() < len(meas):
        coef, *_ = np.linalg.lstsq(A[keep], y[keep], rcond=None)
    return float(max(coef[0], 1e-12)), float(max(coef[1], 0.0))


def _peak_interp(spec, i):
    """Parabolic interpolation on log magnitude around bin i -> frac offset."""
    if i <= 0 or i >= len(spec) - 1:
        return 0.0
    a, b, c = (np.log(spec[i - 1] + 1e-30), np.log(spec[i] + 1e-30),
               np.log(spec[i + 1] + 1e-30))
    d = a - 2 * b + c
    return 0.0 if d == 0 else 0.5 * (a - c) / d


def track_partials(x, sr, f0_nom, n_max=40, snr_db=10.0, pad=4):
    """Sequentially track partial peaks up the spectrum, refitting the stretch
    model after each acceptance so the search window FOLLOWS the stretch
    instead of assuming harmonic positions.

    Returns (f0_fit, B, [(n, f_meas, amp_db_rel_strongest)]).
    """
    seg = x * np.hanning(len(x))
    nfft = pad * (1 << int(np.ceil(np.log2(len(seg)))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    df = fbin[1] - fbin[0]
    a, b = float(f0_nom) ** 2, 0.0
    meas = []
    for n in range(1, n_max + 1):
        fpred = n * np.sqrt(a + b * n * n)
        fnext = (n + 1) * np.sqrt(a + b * (n + 1) ** 2)
        if fpred > 0.92 * fbin[-1]:
            break
        half = max(0.35 * (fnext - fpred), 4 * df)
        lo, hi = np.searchsorted(fbin, [fpred - half, fpred + half])
        if hi - lo < 3:
            continue
        i = lo + int(np.argmax(spec[lo:hi]))
        span = 3 * (fnext - fpred)
        nb_lo, nb_hi = np.searchsorted(fbin, [max(0.0, fpred - span), fpred + span])
        floor = np.median(spec[nb_lo:nb_hi])
        if spec[i] < floor * 10 ** (snr_db / 20.0) or i in (lo, hi - 1):
            continue    # too weak, or peak pinned to window edge (not a peak)
        fmeas = fbin[i] + _peak_interp(spec, i) * df
        meas.append((n, fmeas, spec[i]))
        if len(meas) >= 5:
            a, b = _fit_stretch(meas)
    if len(meas) >= 5:
        a, b = _fit_stretch(meas)
    amax = max(m[2] for m in meas) if meas else 1.0
    plist = [(int(n), float(f), float(20 * np.log10(am / amax + 1e-12)))
             for n, f, am in meas]
    return float(np.sqrt(a)), float(b / a), plist


def estimate_inharmonicity(x, sr, f0_nom, n_max=40):
    """Measure B (and the refined f0) from a signal. Returns (f0, B).

    Falls back to (refined-by-peak f0, 0.0) when too few partials clear the
    SNR gate to fit — a fit on <5 points is noise, and B=0 is the honest
    "no stretch measured" answer rather than a fabricated one.
    """
    f0, B, plist = track_partials(x, sr, f0_nom, n_max=n_max)
    if len(plist) < 5:
        return float(dm.refine_f0(x, sr, f0_nom)), 0.0
    return f0, B


def harmonic_env(x, sr, f0, n_harm=32, tol=0.03, B=0.0):
    """Log-amplitude (dB, peak normalised to 0) at each partial of f0.

    B is the inharmonicity coefficient (0 = harmonic positions k*f0). The
    window is +-tol RELATIVE to the partial's own centre, so with the centre
    at the stretched position it tracks a stiff string; without it, the offset
    in percent is B*n^2/2 and passes 3% at n>23 for C4, n>8 for C6.
    """
    seg = x * np.hanning(len(x))
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    amps = []
    for k in range(1, n_harm + 1):
        fc = partial_freq(f0, B, k)
        sel = (fbin >= fc * (1 - tol)) & (fbin <= fc * (1 + tol))
        amps.append(float(np.max(spec[sel])) if np.any(sel) else 0.0)
    amps = np.array(amps)
    return 20 * np.log10(amps / (amps.max() + 1e-12) + 1e-6)


def motion_stats(x, sr, f0_nom, B=0.0):
    """derive_motion's per-harmonic decomposition on a sustain segment.

    Returns the residual (line-broadening) and amplitude-fluctuation stats,
    or None if too few harmonics clear the noise floor.

    B stretches the heterodyne centres. Without it a stiff-string partial
    displaced by more than the 40 Hz LP half-width leaves the passband
    entirely and drops out at the noise-floor break, silently shortening the
    analysis rather than reporting anything wrong.
    """
    f0 = dm.refine_f0(x, sr, f0_nom)
    decim = 32
    sr_env = sr / decim
    cents, amps = [], []
    for k in range(1, dm.N_HARM + 1):
        fc = partial_freq(f0, B, k)
        env = dm.heterodyne(x, sr, fc)[::decim]
        env = env[int(0.1 * sr_env): -int(0.1 * sr_env) or None]
        a = np.abs(env)
        if a.mean() < 1e-7:
            break
        dphi = np.angle(env[1:] / env[:-1])
        # clip the ratio > 0 so a rare ~pi phase jump (noisy harmonic) can't
        # make log2 NaN; such samples are dominated out by the median anyway.
        ratio = np.maximum(1.0 + (dphi * sr_env / (2 * np.pi)) / fc, 1e-3)
        c = 1200.0 * np.log2(ratio)
        cents.append(c - c.mean())
        amps.append(a[1:] / a.mean())
    cents, amps = np.array(cents), np.array(amps)
    if len(cents) < 4:
        return None
    common = cents.mean(axis=0)
    resid = cents - common
    iu = np.triu_indices(len(resid), k=1)
    amp_fluct = amps - amps.mean(axis=1, keepdims=True)

    def med_rate(traces):
        return float(np.nanmedian([dm.rate_centroid(t, sr_env) for t in traces]))

    return {
        "resid_cents_rms": float(np.median(resid.std(axis=1))),
        "resid_rate_hz": med_rate(resid),
        "resid_coherence": float(np.corrcoef(resid)[iu].mean()),
        "amp_frac_rms": float(np.median(amp_fluct.std(axis=1))),
        "amp_rate_hz": med_rate(amp_fluct),
        "amp_coherence": float(np.corrcoef(amp_fluct)[iu].mean()),
    }


MOTION_KEYS = ["resid_cents_rms", "resid_rate_hz", "resid_coherence",
               "amp_frac_rms", "amp_rate_hz", "amp_coherence"]


def broadband_ratios(x, sr, f0, tol_frac=0.06, B=0.0):
    """Per band: energy BETWEEN partial lines / energy AT lines.

    Real bowed strings carry broadband bow noise between the harmonics; pure
    additive has (almost) none. This is the ml_ears-quantified gap.

    A band with NO harmonic line in it returns NaN, not a number. BANDS[0] is
    (160, 700), so every eval note above 700 Hz leaves it empty — and the
    "ratio" there was between_e / 1e-12, i.e. a noise-floor measurement
    inflated by twelve decades, which then dominated term3. Callers must
    treat these bands as missing (nanmean), not as zero and not as huge.

    B stretches the line positions. The mask half-width is a fixed tol_frac*f0
    (line width is set by the analysis window, not by partial index), while a
    stiff-string partial's offset grows as f0*B*n^3/2 — so at B=0 masking a
    real piano, every partial past n^3 > 2*tol_frac/B falls OFF the mask and
    is counted as inter-harmonic energy. That is n>10.6 at C4's B=1.1e-4, and
    it is why the piano reference carried band ratios of 120/260/2056.
    """
    seg = x * np.hanning(len(x))
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft)) ** 2
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    tol_hz = f0 * tol_frac
    kmax = int(fbin[-1] / f0)
    lines = np.zeros(len(fbin), bool)
    n_in_band = [0] * len(BANDS)
    for k in range(1, kmax + 1):
        fk = partial_freq(f0, B, k)
        if fk > fbin[-1]:
            break       # stretch outruns Nyquist before kmax when B > 0
        lines |= np.abs(fbin - fk) <= tol_hz
        for bi, (lo, hi) in enumerate(BANDS):
            if lo <= fk < hi:
                n_in_band[bi] += 1
    ratios = []
    for bi, (lo, hi) in enumerate(BANDS):
        if n_in_band[bi] == 0:
            ratios.append(float("nan"))
            continue
        b = (fbin >= lo) & (fbin < hi)
        line_e = spec[b & lines].sum()
        between_e = spec[b & ~lines].sum()
        ratios.append(float(between_e / (line_e + 1e-12)))
    return ratios


def attack_stats(x_note, sr):
    """Per band: (inter-band onset lag, 10->90% rise time), both in seconds.

    Onset lag is relative to the earliest band's 10%-crossing, so it is
    invariant to where in the file the note starts and to absolute register.
    x_note must include the note onset (not just the sustain).
    """
    i10s, out = [], []
    for lo, hi in BANDS:
        sos = butter(4, [lo / (sr / 2), min(0.999, hi / (sr / 2))],
                     btype="band", output="sos")
        y = sosfiltfilt(sos, x_note)
        win = max(1, int(0.005 * sr))
        env = np.sqrt(np.convolve(y ** 2, np.ones(win) / win, "same"))
        pk = env.max()
        if pk < 1e-9:
            i10s.append(np.nan)
            out.append([np.nan, np.nan])
            continue
        i10 = int(np.argmax(env >= 0.1 * pk))
        i90 = int(np.argmax(env >= 0.9 * pk))
        i10s.append(i10)
        out.append([i10 / sr, max(0.0, (i90 - i10) / sr)])
    base = np.nanmin(i10s) if np.any(np.isfinite(i10s)) else 0.0
    for j, i10 in enumerate(i10s):
        out[j][0] = (i10 - base) / sr if np.isfinite(i10) else np.nan
    return out  # [[rel_lag, rise] x 3 bands]
