"""Why the stretch-aware line mask makes the BASS notes worse, not better.

Wiring reference_build.inharmonic=true fixed the mid/treble band-2 ratios
exactly as predicted (C3 120.2 -> 0.027, G4 2056 -> 0.0034, C5 259.5 -> 0.020)
but made the three lowest notes WORSE (C2 3.995 -> 29.34, and C2 is an eval
note). Two candidate explanations:

  (a) the fitted B is wrong at low f0, or
  (b) B is right but the FIXED mask half-width tol_frac*f0 is far too tight at
      the high partial indices a low note reaches, because model position
      error grows as f0*n^3*dB/2 while the tolerance does not grow at all.

Distinguishable by measurement: track the real peaks and compare them to the
model's predicted positions as a function of n, in units of the mask tolerance.
If (a), the error is large even at low n. If (b), the error is small at low n
and blows past the tolerance somewhere up the series.

Usage:  python research/ml_ears/diag_stretch_bass.py
"""
import pathlib
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import refmetrics as rm  # noqa: E402
import iowa_reference as ir  # noqa: E402
import score_candidate as sc  # noqa: E402

CFG = "piano_mf"
NOTES = ["C2", "C3", "C4", "C5"]
TOL_FRAC = 0.06


def main():
    cfg = sc.load_config(CFG)
    rb = cfg["reference_build"]
    ext = cfg.get("sample_glob", "*.aif").lstrip("*")
    by_note = {sn["note"]: sn for sn in cfg["score_notes"]}

    print(f"{'note':5s} {'f0':>7s} {'B':>10s} {'tol_Hz':>7s} "
          f"{'n_tracked':>9s} {'max n':>6s}   error vs model, in tolerances")
    for note in NOTES:
        sn = by_note[note]
        x, sr = ir.load_mono(ir.find_file(cfg["sample_dir"], sn["string"],
                                          note, ext))
        x = x[ir.onset_index(x, sr):]
        n0 = int(rb["sus_start"] * sr)
        sus = x[n0: n0 + int(rb["sus_len"] * sr)]
        f0 = rm.midi_to_freq(sn["midi"])

        # track as far up the series as the SNR allows, not just n_max=40
        f0_fit, B, plist = rm.track_partials(sus, sr, f0, n_max=90)
        if not plist:
            print(f"  {note}: no partials tracked")
            continue
        tol = TOL_FRAC * f0
        rows = []
        for n, f_meas, _amp_db in plist:
            f_pred = rm.partial_freq(f0, B, n)
            rows.append((n, f_meas, f_pred, (f_meas - f_pred) / tol))
        ns = [r[0] for r in rows]
        errs = np.array([abs(r[3]) for r in rows])
        # first index where the model leaves the mask
        first_out = next((n for n, e in zip(ns, errs) if e > 1.0), None)
        print(f"{note:5s} {f0:7.1f} {B:10.3e} {tol:7.2f} "
              f"{len(rows):9d} {max(ns):6d}   "
              f"first outside mask at n={first_out}")
        buckets = [(1, 8), (9, 16), (17, 32), (33, 64), (65, 90)]
        for lo, hi in buckets:
            sel = errs[[i for i, n in enumerate(ns) if lo <= n <= hi]]
            if len(sel):
                print(f"        n {lo:2d}-{hi:2d}: median "
                      f"{np.median(sel):8.2f} tol   max {sel.max():8.2f} tol")

        # how wide would the mask have to be to hold everything?
        need = errs.max() * tol
        print(f"        mask needed to keep every tracked partial: "
              f"{need:.1f} Hz  ({need/f0:.3f} * f0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
