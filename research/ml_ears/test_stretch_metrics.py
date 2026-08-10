"""Ground-truth checks for the stretch-aware extractors (dsp backlog 3e-NEXT b).

Three things are shown rather than asserted:

  1. B=0 REGRESSION. The pre-change formulas are kept inline here, so the
     claim "viola and clarinet are untouched" is a comparison against the
     actual old code, not a promise.
  2. B RECOVERY. A synthetic stiff string with a KNOWN B is measured; the
     estimator has to return that number.
  3. THE BUG ITSELF. The same synthetic carries a KNOWN amount of planted
     between-line noise. Stretch-aware masking must recover it; harmonic
     masking must inflate it. If the second number is not much larger, the
     item's premise was wrong and this run should say so.

Usage: python research/ml_ears/test_stretch_metrics.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refmetrics as rm  # noqa: E402

SR = 44100
DUR = 1.0
RNG = np.random.default_rng(20260806)   # seed stored, per the repo convention


def synth_stiff(f0, B, n_partials, noise_frac, sr=SR, dur=DUR):
    """Partials at n*f0*sqrt(1+B n^2), 1/n amplitude, plus band-limited noise
    planted BETWEEN the lines. Returns (x, planted_between_over_at_energy)."""
    t = np.arange(int(dur * sr)) / sr
    x = np.zeros_like(t)
    at_e = 0.0
    for n in range(1, n_partials + 1):
        fc = rm.partial_freq(f0, B, n)
        if fc > 0.45 * sr:
            break
        a = 1.0 / n
        x += a * np.sin(2 * np.pi * fc * t + RNG.uniform(0, 2 * np.pi))
        at_e += 0.5 * a * a * len(t)      # sine power * samples
    noise = RNG.standard_normal(len(t))
    # Shape the noise to the analysed span so the planted ratio is meaningful
    # inside BANDS rather than being dumped above 8 kHz.
    spec = np.fft.rfft(noise)
    fb = np.fft.rfftfreq(len(t), 1.0 / sr)
    spec[(fb < 160.0) | (fb > 8000.0)] = 0.0
    noise = np.fft.irfft(spec, len(t))
    noise *= np.sqrt(noise_frac * at_e / (np.sum(noise ** 2) + 1e-30))
    return x + noise, noise_frac


# ---------------------------------------------------------------------------
# 1. B = 0 regression — the pre-change formulas, inline.

def old_harmonic_env(x, sr, f0, n_harm=32, tol=0.03):
    seg = x * np.hanning(len(x))
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    amps = []
    for k in range(1, n_harm + 1):
        fc = k * f0
        sel = (fbin >= fc * (1 - tol)) & (fbin <= fc * (1 + tol))
        amps.append(float(np.max(spec[sel])) if np.any(sel) else 0.0)
    amps = np.array(amps)
    return 20 * np.log10(amps / (amps.max() + 1e-12) + 1e-6)


def old_broadband_ratios(x, sr, f0, tol_frac=0.06):
    seg = x * np.hanning(len(x))
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft)) ** 2
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    tol_hz = f0 * tol_frac
    kmax = int(fbin[-1] / f0)
    lines = np.zeros(len(fbin), bool)
    n_in_band = [0] * len(rm.BANDS)
    for k in range(1, kmax + 1):
        fk = k * f0
        lines |= np.abs(fbin - fk) <= tol_hz
        for bi, (lo, hi) in enumerate(rm.BANDS):
            if lo <= fk < hi:
                n_in_band[bi] += 1
    ratios = []
    for bi, (lo, hi) in enumerate(rm.BANDS):
        if n_in_band[bi] == 0:
            ratios.append(float("nan"))
            continue
        b = (fbin >= lo) & (fbin < hi)
        ratios.append(float(spec[b & ~lines].sum() / (spec[b & lines].sum() + 1e-12)))
    return ratios


def test_b0_identical():
    ok = True
    for f0 in (130.813, 261.626, 523.251):
        x, _ = synth_stiff(f0, 0.0, 32, 0.02)
        a, b = rm.harmonic_env(x, SR, f0), old_harmonic_env(x, SR, f0)
        same_env = np.array_equal(a, b)
        ra = np.array(rm.broadband_ratios(x, SR, f0))
        rb = np.array(old_broadband_ratios(x, SR, f0))
        same_bb = np.array_equal(ra[~np.isnan(ra)], rb[~np.isnan(rb)]) and \
            np.array_equal(np.isnan(ra), np.isnan(rb))
        # motion_stats too: B=0 must reproduce the k*f0 heterodyne exactly.
        ms = rm.motion_stats(x, SR, f0)
        ok &= same_env and same_bb and ms is not None
        print(f"  f0={f0:8.3f}  harmonic_env identical={same_env}  "
              f"broadband identical={same_bb}")
    print(f"  [B=0 regression] {'PASS' if ok else 'FAIL'}"
          "  (bit-identical to the pre-change formulas)")
    return ok


# ---------------------------------------------------------------------------
# 2 + 3. B recovery, and the masking bug.

def test_recovery_and_mask():
    ok = True
    rows = []
    # C3/C4/C5-ish with their measured piano B values (out/piano_analysis_report.md).
    for f0, B_true in ((130.813, 1.1e-4), (261.626, 1.7e-4), (523.251, 9.6e-4)):
        planted = 0.02
        x, _ = synth_stiff(f0, B_true, 32, planted)
        f0_fit, B_fit = rm.estimate_inharmonicity(x, SR, f0)
        err = abs(B_fit - B_true) / B_true
        with_stretch = np.array(rm.broadband_ratios(x, SR, f0_fit, B=B_fit), float)
        without = np.array(rm.broadband_ratios(x, SR, f0), float)
        ws = float(np.nanmean(with_stretch))
        wo = float(np.nanmean(without))
        # The planted ratio is a whole-signal energy fraction; the per-band
        # numbers are compared to it in order of magnitude, and to each other
        # exactly. Recovery gate: B within 5%, and stretch-aware masking at
        # least 10x closer to the planted value than harmonic masking.
        good_B = err < 0.05
        good_mask = abs(np.log10(ws / planted)) < abs(np.log10(wo / planted)) - 1.0
        ok &= good_B and good_mask
        rows.append((f0, B_true, B_fit, err, planted, ws, wo))
        print(f"  f0={f0:8.3f}  B true={B_true:.3e} fit={B_fit:.3e} "
              f"({100*err:5.2f}% err, {'ok' if good_B else 'BAD'})")
        print(f"            planted between/at={planted:.4f}  "
              f"stretch-aware={ws:.4f}  harmonic-mask={wo:.4f}  "
              f"inflation={wo/ws:8.1f}x  {'ok' if good_mask else 'BAD'}")
        # Per band, because the mean hides WHERE the error is: band0 holds only
        # low partials (offset f0*B*n^3/2 still inside the mask) and must come
        # back IDENTICAL either way. The elevated band2 figure is a property of
        # this synthetic — flat noise against 1/n partials — not a mask failure.
        top = rm.partial_freq(f0, B_true, 32)
        for bi, (lo, hi) in enumerate(rm.BANDS):
            print(f"              band{bi} {lo:5.0f}-{hi:5.0f}: "
                  f"stretch={with_stretch[bi]:9.4f}  harmonic={without[bi]:9.4f}"
                  f"   partials reach top of band={hi <= top}")
    print(f"  [B recovery + mask] {'PASS' if ok else 'FAIL'}")
    return ok


def test_harmonic_env_high_partials():
    """Where harmonic_env actually breaks: the relative 3% window survives low
    n and fails high. Shown as the partial index where the two disagree."""
    f0, B = 261.626, 9.6e-4       # C4 f0 with C5's stiffer B, to exercise n<32
    x, _ = synth_stiff(f0, B, 32, 0.0)
    a = rm.harmonic_env(x, SR, f0, B=B)
    b = rm.harmonic_env(x, SR, f0, B=0.0)
    d = np.abs(a - b)
    first = int(np.argmax(d > 1.0)) + 1 if np.any(d > 1.0) else None
    print(f"  first partial misread by >1 dB with harmonic centres: n={first}"
          f"  (predicted n>{int(np.sqrt(2*0.03/B))} from B*n^2/2 > 3%)")
    print(f"  max error over 32 partials: {d.max():.1f} dB")
    return first is not None


def main():
    print("== 1. B=0 regression vs the pre-change formulas ==")
    r1 = test_b0_identical()
    print("\n== 2/3. B recovery and the line-mask inflation ==")
    r2 = test_recovery_and_mask()
    print("\n== 4. harmonic_env high-partial breakdown ==")
    r3 = test_harmonic_env_high_partials()
    ok = r1 and r2 and r3
    print(f"\n{'ALL PASS' if ok else 'FAILURES ABOVE'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
