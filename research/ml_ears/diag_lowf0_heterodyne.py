"""Is the low-note resid_cents real line broadening, or neighbour leakage?

Regenerating the piano reference (run 25, front B) left C1/F1/C2 reporting
resid_cents_rms of 181-469 cents against 5-15 c for every note from G2 up.
469 cents is a tritone of "line broadening"; that is not a measurement, it is
an artifact.

The suspect is the heterodyne's fixed low-pass. derive_motion.heterodyne
mixes a partial to DC and low-passes at LP_HZ = 40 Hz, which isolates that
partial ONLY if the neighbours -- one partial spacing away, i.e. ~f0 -- fall
outside the passband. That needs LP_HZ < f0/2:

    C1  f0/2 = 16.4 Hz  <  40   violated by 2.4x
    F1  f0/2 = 21.8 Hz  <  40   violated by 1.8x
    C2  f0/2 = 32.7 Hz  <  40   violated
    G2  f0/2 = 49.0 Hz  >  40   ok
    C3  f0/2 = 65.4 Hz  >  40   ok

The break is between C2 and G2, exactly where the reported numbers break.
There is a second, independent problem: the filter is firwin(1025) at the full
sample rate, so its transition band is roughly sr/1025 ~ 43 Hz -- wider than
the 40 Hz cutoff itself. Even at high notes the passband edge is soft.

The test is a sensitivity sweep, the same shape as run 18's beat-rate control:
a real measurement is STABLE as the cutoff moves; an artifact tracks it.

Usage:  python research/ml_ears/diag_lowf0_heterodyne.py
"""
import pathlib
import sys

import numpy as np
from scipy.signal import fftconvolve, firwin

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import derive_motion as dm  # noqa: E402
import iowa_reference as ir  # noqa: E402
import refmetrics as rm  # noqa: E402
import score_candidate as sc  # noqa: E402

CFG = "piano_mf"
CUTOFFS = [40.0, 30.0, 20.0]        # fixed cutoffs, the current design
FRACTIONS = [0.40, 0.30, 0.20]      # spacing-relative, the proposed one


def heterodyne_at(x, sr, fc, lp_hz, ntaps=None):
    """Same as dm.heterodyne but with an explicit cutoff and scaled tap count.

    Tap count is scaled so the transition band stays a fixed fraction of the
    cutoff; a 1025-tap filter at 44.1 kHz has a ~43 Hz transition, which makes
    a 20 Hz cutoff meaningless.
    """
    t = np.arange(len(x)) / sr
    z = x * np.exp(-2j * np.pi * fc * t)
    if ntaps is None:
        ntaps = int(4 * sr / lp_hz) | 1        # transition ~ lp_hz/2
        ntaps = min(max(ntaps, 1025), len(x) | 1)
    taps = firwin(ntaps, lp_hz / (sr / 2))
    return fftconvolve(z, taps, mode="same")


def resid_cents(x, sr, f0, B, lp_hz, n_harm=None):
    """motion_stats' resid_cents_rms with the low-pass under our control."""
    decim = 32
    sr_env = sr / decim
    cents = []
    for k in range(1, (n_harm or dm.N_HARM) + 1):
        fc = rm.partial_freq(f0, B, k)
        env = heterodyne_at(x, sr, fc, lp_hz)[::decim]
        env = env[int(0.1 * sr_env): -int(0.1 * sr_env) or None]
        a = np.abs(env)
        if a.mean() < 1e-7:
            break
        dphi = np.angle(env[1:] / env[:-1])
        ratio = np.maximum(1.0 + (dphi * sr_env / (2 * np.pi)) / fc, 1e-3)
        c = 1200.0 * np.log2(ratio)
        cents.append(c - c.mean())
    if len(cents) < 4:
        return float("nan"), len(cents)
    cents = np.array(cents)
    resid = cents - cents.mean(axis=0)
    return float(np.median(resid.std(axis=1))), len(cents)


def main():
    cfg = sc.load_config(CFG)
    rb = cfg["reference_build"]
    ext = cfg.get("sample_glob", "*.aif").lstrip("*")

    print("resid_cents_rms as the heterodyne low-pass moves.")
    print("A real measurement holds still; an artifact tracks the cutoff.\n")
    hdr = (f"{'note':5s} {'f0':>7s} {'f0/2':>6s} "
           + " ".join(f"{'fix ' + str(int(c)):>9s}" for c in CUTOFFS)
           + "  |" + " ".join(f"{'x' + str(f):>9s}" for f in FRACTIONS))
    print(hdr)
    print("-" * len(hdr))

    rows = {}
    for sn in cfg["score_notes"]:
        note = sn["note"]
        x, sr = ir.load_mono(ir.find_file(cfg["sample_dir"], sn["string"],
                                          note, ext))
        x = x[ir.onset_index(x, sr):]
        n0 = int(rb["sus_start"] * sr)
        sus = x[n0: n0 + int(rb["sus_len"] * sr)]
        f0 = rm.midi_to_freq(sn["midi"])
        _lines, B, _n = rm.measured_lines(sus, sr, f0)

        fixed = [resid_cents(sus, sr, f0, B, c)[0] for c in CUTOFFS]
        frac = [resid_cents(sus, sr, f0, B, fr * f0)[0] for fr in FRACTIONS]
        rows[note] = {"f0": f0, "fixed": fixed, "frac": frac}
        print(f"{note:5s} {f0:7.1f} {f0/2:6.1f} "
              + " ".join(f"{v:9.1f}" for v in fixed)
              + "  |" + " ".join(f"{v:9.1f}" for v in frac))

    def spread(vals):
        vals = [v for v in vals if np.isfinite(v)]
        if len(vals) < 2 or min(vals) <= 0:
            return float("nan")
        return max(vals) / min(vals)

    print("\nsensitivity = max/min across the three settings "
          "(1.0 = perfectly stable)")
    print(f"  {'note':5s} {'fixed cutoff':>14s} {'spacing-relative':>18s}")
    for note, r in rows.items():
        print(f"  {note:5s} {spread(r['fixed']):14.2f} "
              f"{spread(r['frac']):18.2f}")

    low = [n for n in rows if rows[n]["f0"] < 80]
    hi = [n for n in rows if rows[n]["f0"] >= 80]
    for label, sel in (("f0 < 80 Hz (violates LP<f0/2)", low),
                       ("f0 >= 80 Hz", hi)):
        fs = np.nanmean([spread(rows[n]["fixed"]) for n in sel])
        rs = np.nanmean([spread(rows[n]["frac"]) for n in sel])
        print(f"\n  {label}: mean sensitivity "
              f"fixed {fs:.2f}  spacing-relative {rs:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
