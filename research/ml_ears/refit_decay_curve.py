"""dsp run 23 — rebuild the piano decayRate register curve (REVIEW 16 fix 3).

The template's 4-anchor h1 curve (C1 1.4 / C3 14 / C7 19 / C8 38.3) was
assembled from piano_analysis_data.json single-slope fits whose windows
varied per note (fit "until 25 dB below peak"), so the anchors mixed prompt
and aftersound segments differently per register — the run-22 diagnosis
measured the result as C2 ~3.4x too fast and C4/C5 ~3x too slow.

Consistent re-fit, per note:
  For each measured partial n <= 6 (measured stretched frequency, same
  heterodyne machinery as piano_analysis.decay_analysis):
    rate_n = (db(peak) - db(peak + 1.0 s)) / 1.0     "1-second drop rate"
      - peak searched in [0.10, 0.60] s after acoustic onset (0.10 s skips
        the broadband hammer leak into every heterodyne band),
      - both endpoints median-smoothed over +/-50 ms so unison-string
        beating nulls don't pick the reading,
      - floor guard: if the envelope reaches floor + 6 dB before 1.0 s the
        rate uses the crossing time instead (a lower bound, note fully
        decayed by then anyway).
    h1_equiv_n = rate_n / mult_n^decayExp   (engine law: rate scales with
        (n*sqrt(1+B n^2))^0.6, so every partial votes for the same anchor)
  anchor = ENERGY-WEIGHTED median over usable partials (weights = linear
        energy from the measured partial amplitudes). Real notes violate
        the fixed-exponent law per register (e.g. G2's fundamental decays
        3 dB/s while its dominant n=2 decays 21 dB/s; C4's fundamental is
        both dominant AND fast at 34 dB/s) — with one rate knob per note
        the audible compromise is to decay the partials that carry the
        energy at their measured rate.

Why the 1-second drop and not an LSQ slope: the engine has ONE exponential
per note (double decay = deferred option 4). An LSQ line over a fixed
window is dragged flat by the post-break aftersound exactly where the
audition said decay was too slow (C5: LSQ 12.5 dB/s vs prompt 47.7); the
1-second drop is the total dB lost across the first second — the single
exponential that lands the envelope where the real note is at t=1 s, the
midpoint of the scorer's sus window (0.25-1.25 s).

Prints the per-note table + a DECAY_CURVE literal for build_piano_template.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import piano_analysis as pa  # noqa: E402

NOTES = ["C1", "C2", "G2", "C3", "G3", "C4", "G4", "C5", "C6", "C7"]
DROP_WIN_S = 1.0
DECAY_EXP = 0.6
N_MAX = 6
DATA = json.load(open(os.path.join(HERE, "out", "piano_analysis_data.json")))


def smooth_db(db, i, sre, half_s=0.05):
    h = max(1, int(half_s * sre))
    lo, hi = max(0, i - h), min(len(db), i + h + 1)
    return float(np.median(db[lo:hi]))


def partial_drop_rate(x, sr, onset, f0, f_meas):
    bw = min(0.4 * 0.5 * f0, 30.0)
    decim = max(1, int(sr / 200.0))
    env, sre = pa.heterodyne_env(x[onset:], sr, f_meas, bw, decim)
    db = 20 * np.log10(env + 1e-12)
    floor_db = float(np.median(db[-max(4, len(db) // 20):]))
    j0 = int(0.10 * sre)
    pk_i = j0 + int(np.argmax(env[j0: j0 + int(0.5 * sre)]))
    pk_db = smooth_db(db, pk_i, sre)
    if pk_db - floor_db < 20.0:
        return None                       # too close to the noise floor
    j1 = pk_i + int(DROP_WIN_S * sre)
    guard = floor_db + 6.0
    seg = db[pk_i: min(j1 + 1, len(db))]
    below = np.nonzero(seg <= guard)[0]
    if len(below) and below[0] > int(0.05 * sre):
        t = below[0] / sre                # floor crossed early: use crossing
        return (pk_db - guard) / t
    if j1 >= len(db):
        return None
    end_db = smooth_db(db, j1, sre)
    return (pk_db - end_db) / DROP_WIN_S


def note_anchor(note):
    x, sr = pa.load_mono(note)
    onset, _ = pa.find_onset(x, sr)
    d = DATA[note]
    f0, B = d["f0_fit"], d["B"]
    votes = []
    for n, f_meas, adb in d["partials"]:
        if n > N_MAX or adb < -40:
            continue
        r = partial_drop_rate(x, sr, onset, f0, f_meas)
        if r is None or r <= 0:
            continue
        mult = n * np.sqrt(1.0 + B * n * n)
        votes.append((n, adb, r, r / mult ** DECAY_EXP))
    return f0, votes


def weighted_median(vals, wts):
    order = np.argsort(vals)
    v, w = np.asarray(vals)[order], np.asarray(wts)[order]
    c = np.cumsum(w) / w.sum()
    return float(v[int(np.searchsorted(c, 0.5))])


def main():
    print(f"h1-equivalent 1s-drop rates (dB/s), engine law n^{DECAY_EXP}, "
          f"energy-weighted:")
    print(" note    f0      votes (n[ampdB]:rate->h1eq)          anchor")
    curve = []
    for note in NOTES:
        f0, votes = note_anchor(note)
        if not votes:
            print(f"  {note:3s} {f0:8.2f}   NO USABLE PARTIALS")
            continue
        anchor = weighted_median([v[3] for v in votes],
                                 [10 ** (v[1] / 10.0) for v in votes])
        vs = " ".join(f"{n}[{a:.0f}]:{r:.1f}->{h:.1f}"
                      for n, a, r, h in votes)
        print(f"  {note:3s} {f0:8.2f}   {vs:60s} {anchor:6.2f}")
        curve.append([round(f0, 1), round(anchor, 2)])
    print("\nDECAY_CURVE = " + json.dumps(curve))


if __name__ == "__main__":
    main()
