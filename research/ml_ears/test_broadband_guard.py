"""Regression test for the broadband_ratios empty-band guard (dsp 3e-NEXT a).

BANDS[0] is (160, 700) Hz. Any eval note above 700 Hz puts no harmonic line
in it, and the old code returned between_e / 1e-12 there — a noise-floor
measurement inflated by twelve decades, which then dominated term3.

Run from research/ml_ears:  python test_broadband_guard.py
"""
import numpy as np

import refmetrics as rm

SR = 48000
DUR = 1.0


def tone(f0, n_harm=20, noise=1e-3, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(int(SR * DUR)) / SR
    x = np.zeros_like(t)
    for k in range(1, n_harm + 1):
        if k * f0 >= SR / 2:
            break
        x += (1.0 / k) * np.sin(2 * np.pi * k * f0 * t + rng.uniform(0, 6.28))
    return x + noise * rng.standard_normal(len(t))


def unguarded(x, sr, f0, tol_frac=0.06):
    """The pre-fix formula, kept here so the blowup is SHOWN, not asserted."""
    seg = x * np.hanning(len(x))
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft)) ** 2
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    tol_hz = f0 * tol_frac
    lines = np.zeros(len(fbin), bool)
    for k in range(1, int(fbin[-1] / f0) + 1):
        lines |= np.abs(fbin - k * f0) <= tol_hz
    out = []
    for lo, hi in rm.BANDS:
        b = (fbin >= lo) & (fbin < hi)
        out.append(float(spec[b & ~lines].sum() / (spec[b & lines].sum() + 1e-12)))
    return out


def main():
    fails = []

    print(f"BANDS = {rm.BANDS}\n")
    for f0, label in [(220.0, "below band0 top"), (880.0, "ABOVE band0 top"),
                      (1760.0, "above band0 and band1")]:
        x = tone(f0)
        new = rm.broadband_ratios(x, SR, f0)
        old = unguarded(x, SR, f0)
        print(f"f0 = {f0:7.1f} Hz  ({label})")
        print(f"  guarded  : " + "  ".join(
            "nan" if np.isnan(v) else f"{v:.4g}" for v in new))
        print(f"  unguarded: " + "  ".join(f"{v:.4g}" for v in old))

        n_lines = [sum(1 for k in range(1, 200) if lo <= k * f0 < hi)
                   for lo, hi in rm.BANDS]
        for bi, n in enumerate(n_lines):
            if n == 0:
                if not np.isnan(new[bi]):
                    print(f"  FAIL: band {bi} has no harmonic but returned "
                          f"{new[bi]:.4g}")
                    fails.append((f0, bi))
                else:
                    print(f"  band {bi}: 0 harmonics -> nan (was {old[bi]:.4g}, "
                          f"a {np.log10(max(old[bi], 1e-300)):.0f}-decade artifact)")
            else:
                if np.isnan(new[bi]):
                    print(f"  FAIL: band {bi} has {n} harmonics but returned nan")
                    fails.append((f0, bi))
        print()

    # A populated band must be UNCHANGED by the guard — the fix must not move
    # any number that was already meaningful.
    x = tone(220.0)
    new, old = rm.broadband_ratios(x, SR, 220.0), unguarded(x, SR, 220.0)
    for bi, (a, b) in enumerate(zip(new, old)):
        if not np.isnan(a) and abs(a - b) > 1e-12:
            print(f"FAIL: guard changed populated band {bi}: {a} vs {b}")
            fails.append(("unchanged", bi))
    print("populated bands identical to the unguarded formula: "
          f"{'PASS' if not any(f[0] == 'unchanged' for f in fails) else 'FAIL'}")

    print()
    print("FAIL" if fails else "ALL PASS")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
