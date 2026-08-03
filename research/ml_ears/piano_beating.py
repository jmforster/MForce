"""Piano unison-string beating — measure the rates the piano encoder can't guess.

Closes the top open item of out/piano_analysis_report.md: "Unison-string beating
rates unmeasured (needed to lock shimmer dims)." Without these numbers the piano
encoder has to leave shimmerDepth/shimmerHz/shimmerCoherence fully searchable,
spending CMA-ES evals on structure that is directly measurable — the same
mistake the clarinet run made before Matt's bed was locked to measurement.

WHY THERE IS ANYTHING TO MEASURE
Mid and treble piano keys are strung with 2-3 strings tuned very slightly apart.
Each partial is therefore a sum of near-equal frequencies, which reads as one
line whose AMPLITUDE beats at the difference frequency. That is exactly what the
engine's shimmer layer does (a per-partial gain wobble), so the mapping is
direct rather than analogical:

    beat rate   (Hz)          -> shimmerHz
    beat depth  (0-1)         -> shimmerDepth1/2
    rate/phase agreement
      across partials         -> shimmerCoherence

METHOD (and why each step is needed)
1. Heterodyne the partial to baseband and take |z| — the partial's own amplitude
   envelope, isolated from its neighbours (reused from piano_analysis).
2. Fit and REMOVE the exponential decay in dB. Beating rides on top of a decay of
   2-40 dB/s; without detrending, the envelope spectrum is dominated by the decay
   ramp and every partial would "beat" at ~0 Hz. A two-segment fit is used
   because the report established double decay is universal — a single slope
   leaves a kink that shows up as spurious sub-Hz energy.
3. Spectrum of the detrended envelope over 0.3-30 Hz. The peak is the beat rate;
   its prominence over the local median is the accept/reject test, so partials
   that simply decay (single string, or unisons tuned too close to beat within
   the window) are reported as "no beat" rather than given a noise-peak rate.
4. Depth = the peak-to-trough swing of the detrended envelope, expressed as the
   fraction the gain dips below its local mean — directly comparable to
   shimmerDepth, which the engine applies as a multiplicative gain wobble.
5. Coherence = agreement of the per-partial beat RATES within a note
   (1 - spread/median, clipped). If every partial of a note beats at the same
   rate, one coherent shimmer oscillator is the right model; if rates scatter,
   the engine's decorrelated per-partial walk is, and shimmerCoherence should be
   low. This is the one number that decides which way that knob goes, and it is
   why the pass is worth running before the encoder is written.

Bass notes (single-strung, B0-F1) are the built-in control: they should show
LITTLE coherent beating. If they beat as strongly as the trichord register, the
measurement is picking up something other than unisons and the numbers should
not be trusted.

Writes out/piano_beating.json + a summary table; figure out/piano_beating.png.
Run:  python research/ml_ears/piano_beating.py
"""
import json
import os
import sys

import numpy as np
from scipy.signal import butter, sosfiltfilt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from piano_analysis import (  # noqa: E402
    OUT as OUT_DIR, load_mono, find_onset, measure_partials, heterodyne_env,
    note_to_midi, midi_to_freq,
)

# Iowa strings: 1 string in the low bass, 2 through the tenor break, 3 above.
# The register label is carried through to the output so the control reads
# clearly in the table.
# F1 was labelled "single" on the first pass and the data disagreed: B0/C1 come
# in at depth 0.016-0.029 while F1 sits at 0.150, right in the multi-strung band.
# Real pianos go single-strung only for the lowest handful of keys, so F1 is
# almost certainly a bichord and is labelled accordingly — the control is B0/C1.
NOTES = [
    ("B0", "single"), ("C1", "single"), ("F1", "double"),
    ("C2", "double"), ("G2", "double"), ("C3", "double"),
    ("G3", "triple"), ("C4", "triple"), ("G4", "triple"),
    ("C5", "triple"), ("C6", "triple"), ("C7", "triple"),
]

N_PARTIALS = 10        # beating is clearest on strong low partials
ENV_SR_TARGET = 200.0  # decimated envelope rate (Nyquist 100 Hz >> 30 Hz max)
BEAT_HI = 30.0
PROMINENCE = 4.0       # peak must beat the off-peak median of its own band

# ATTEMPT 1 FAILED ITS OWN CONTROL, and the fix is here. With a 3 s window the
# FFT bin spacing is 0.34 Hz, and essentially every partial of every note --
# single-strung bass included -- "beat" at 0.35 or 0.69 Hz, i.e. bins 1 and 2.
# That is leftover decay curvature the two-segment detrend did not flatten, not
# beating. Three changes make the number mean what it says:
#   - a much longer window, so slow beats are actually resolved;
#   - MIN_CYCLES full cycles required inside the window, which puts the floor
#     several bins above DC where trend residue lives;
#   - the residual high-passed at that floor before the spectrum is taken.
WIN_S = 8.0
MIN_CYCLES = 4.0                       # -> BEAT_LO = 0.5 Hz at WIN_S = 8
BEAT_LO = MIN_CYCLES / WIN_S


def detrend_db(t, db):
    """Remove the decay so only modulation is left. Two-segment continuous fit
    (double decay is universal per the report); falls back to a single line when
    the window is too short for a stable break."""
    if len(t) < 40:
        c = np.polyfit(t, db, 1)
        return db - np.polyval(c, t)
    best, bestsse = None, np.inf
    for tb in np.linspace(t[0] + 0.2 * (t[-1] - t[0]),
                          t[0] + 0.8 * (t[-1] - t[0]), 25):
        A = np.vstack([np.ones_like(t), t, np.maximum(t - tb, 0.0)]).T
        c, *_ = np.linalg.lstsq(A, db, rcond=None)
        sse = float(((db - A @ c) ** 2).sum())
        if sse < bestsse:
            bestsse, best = sse, (A, c)
    A, c = best
    return db - A @ c


def beat_of_partial(x, sr, onset, fmeas, half_bw):
    """Beat rate/depth of one partial, or None when it does not beat."""
    n0 = onset + int(0.10 * sr)          # skip the hammer knock (report §3)
    seg = x[n0: n0 + int(WIN_S * sr)]
    if len(seg) < int(0.5 * sr):
        return None
    decim = max(1, int(sr / ENV_SR_TARGET))
    env, esr = heterodyne_env(seg, sr, fmeas, half_bw, decim)
    if len(env) < 64:
        return None
    env = np.asarray(env, dtype=float)
    if not np.all(np.isfinite(env)) or env.max() <= 0:
        return None

    # Trim the filter's edge transients before fitting anything.
    k = max(2, int(0.05 * esr))
    env = env[k:-k] if len(env) > 4 * k else env
    t = np.arange(len(env)) / esr
    db = 20.0 * np.log10(np.maximum(env, env.max() * 1e-6))
    if db.ptp() > 60.0:                  # ran into the noise floor mid-window
        keep = db > db.max() - 45.0
        if keep.sum() < 64:
            return None
        t, db, env = t[keep], db[keep], env[keep]

    span = t[-1] - t[0]
    if span < MIN_CYCLES / BEAT_LO * 0.9:     # too short to resolve a real beat
        return None

    resid = detrend_db(t, db)
    resid = resid - resid.mean()
    # Belt and braces: whatever curvature the piecewise fit left behind lives
    # below BEAT_LO, so remove it outright rather than hoping the fit caught it.
    sos = butter(2, BEAT_LO / (esr / 2), btype="high", output="sos")
    resid = sosfiltfilt(sos, resid)

    w = np.hanning(len(resid))
    sp = np.abs(np.fft.rfft(resid * w))
    fr = np.fft.rfftfreq(len(resid), 1.0 / esr)
    band = (fr >= BEAT_LO) & (fr <= BEAT_HI)
    if band.sum() < 8:
        return None
    fb, sb = fr[band], sp[band]
    i = int(np.argmax(sb))
    rate = float(fb[i])
    peak = float(sb[i])
    # Median EXCLUDING the peak's own neighbourhood, else a strong narrow peak
    # inflates the median it is being compared against.
    off = np.abs(fb - rate) > max(3 * (fb[1] - fb[0]), 0.15 * rate)
    med = float(np.median(sb[off])) if off.sum() > 4 else float(np.median(sb))
    prom = peak / (med + 1e-12)
    if prom < PROMINENCE:
        return None

    # Depth from the NARROWBAND component at the beat rate (not the total
    # residual std, which also counts broadband envelope noise). Hann coherent
    # gain 0.5 -> amplitude = 2*|X|/(N*0.5); x sqrt(2)... no: |X| already gives
    # half-amplitude, so amp = 2*peak/(N*0.5) is the peak dB swing.
    amp_db = float(2.0 * peak / (len(resid) * 0.5))
    depth = float(1.0 - 10.0 ** (-amp_db / 20.0))
    return dict(rate=rate, depth=min(max(depth, 0.0), 1.0), prominence=prom,
                amp_db=amp_db)


def analyse(note, register):
    x, sr = load_mono(note)
    onset, _env = find_onset(x, sr)      # find_onset returns (index, envelope)
    f0_nom = midi_to_freq(note_to_midi(note))
    seg_len = 2.0 if f0_nom < 70 else (0.4 if f0_nom < 1200 else 0.25)
    f0, B, plist = measure_partials(x, sr, onset, f0_nom, seg_len, N_PARTIALS * 2)
    if not plist:
        return None

    rows = []
    for n, fmeas, amp_db in plist[:N_PARTIALS]:
        # Band must be narrower than the gap to the neighbouring partial, or the
        # "beat" is just the neighbour leaking in — the single biggest way this
        # measurement could fool itself.
        half_bw = min(0.35 * f0, 0.4 * fmeas / max(n, 1))
        half_bw = max(half_bw, 2.0)
        r = beat_of_partial(x, sr, onset, fmeas, half_bw)
        if r:
            r.update(n=int(n), f=float(fmeas), amp_db=float(amp_db))
            rows.append(r)

    if not rows:
        return dict(note=note, register=register, f0=f0, n_beating=0,
                    n_tested=len(plist[:N_PARTIALS]), rates=[], depths=[],
                    median_rate=None, median_depth=None, coherence=None)

    rates = np.array([r["rate"] for r in rows])
    depths = np.array([r["depth"] for r in rows])
    med = float(np.median(rates))
    # Robust spread (IQR/1.349 ~ sd) so one outlier partial cannot dictate the
    # coherence verdict.
    q75, q25 = np.percentile(rates, [75, 25])
    spread = float((q75 - q25) / 1.349)
    coherence = float(np.clip(1.0 - spread / max(med, 1e-6), 0.0, 1.0))

    return dict(note=note, register=register, f0=float(f0),
                n_beating=len(rows), n_tested=len(plist[:N_PARTIALS]),
                rates=[float(r) for r in rates],
                depths=[float(d) for d in depths],
                partials=rows, median_rate=med,
                median_depth=float(np.median(depths)), coherence=coherence)


def floor_sensitivity():
    """Is the measured rate real, or is it just sitting on the analysis floor?

    Attempt 2's rates piled up at 0.51-0.65 Hz with the floor at 0.50 -- which is
    exactly what a boundary artifact looks like. Re-running with the floor raised
    settles it: if the rates track the floor upward, they were never real and
    shimmerHz must NOT be locked from this pass. If notes that already beat above
    the new floor keep their rate, the number is a measurement.
    """
    global MIN_CYCLES, BEAT_LO
    saved = MIN_CYCLES
    table = {}
    for mc in (4.0, 6.0, 9.0):
        MIN_CYCLES = mc
        BEAT_LO = mc / WIN_S
        col = {}
        for note, reg in NOTES:
            try:
                r = analyse(note, reg)
            except Exception:
                continue
            if r and r["median_rate"]:
                col[note] = r["median_rate"]
        table[BEAT_LO] = col
    MIN_CYCLES = saved
    BEAT_LO = saved / WIN_S

    floors = sorted(table)
    print("\n=== floor sensitivity: does the rate track the analysis floor? ===")
    print("%-6s %s" % ("note", "  ".join("floor=%.2f" % f for f in floors)))
    moved, held = 0, 0
    for note, _reg in NOTES:
        vals = [table[f].get(note) for f in floors]
        if vals[0] is None:
            continue
        cells = "  ".join("%9s" % ("%.2f" % v if v else "-") for v in vals)
        print("%-6s %s" % (note, cells))
        # A rate that was already comfortably above the highest floor and stayed
        # put is real; one that climbs with each floor was an artifact.
        if vals[0] and vals[-1]:
            if vals[0] > floors[-1] * 1.15 and abs(vals[-1] - vals[0]) < 0.25 * vals[0]:
                held += 1
            elif vals[-1] > vals[0] * 1.3:
                moved += 1
    print("\nrates that HELD above the raised floor : %d" % held)
    print("rates that CLIMBED with the floor      : %d  <- artifact if this dominates"
          % moved)
    return held, moved


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    results = []
    print("%-5s %-7s %8s %8s %10s %9s %10s" %
          ("note", "strung", "f0", "beat/n", "rate(Hz)", "depth", "coherence"))
    for note, reg in NOTES:
        try:
            r = analyse(note, reg)
        except Exception as e:                      # a missing sample is data
            print("%-5s %-7s  FAILED: %s" % (note, reg, e))
            continue
        if r is None:
            print("%-5s %-7s  no partials tracked" % (note, reg))
            continue
        results.append(r)
        if r["n_beating"] == 0:
            print("%-5s %-7s %8.1f %8s %10s %9s %10s" %
                  (note, reg, r["f0"], "0/%d" % r["n_tested"], "-", "-", "-"))
        else:
            print("%-5s %-7s %8.1f %8s %10.2f %9.3f %10.2f" %
                  (note, reg, r["f0"], "%d/%d" % (r["n_beating"], r["n_tested"]),
                   r["median_rate"], r["median_depth"], r["coherence"]))

    with open(os.path.join(OUT_DIR, "piano_beating.json"), "w") as f:
        json.dump(results, f, indent=1)

    # Control check: single-strung bass must beat less than the trichords.
    def pool(regs, key):
        v = [r[key] for r in results
             if r["register"] in regs and r.get(key) is not None]
        return float(np.median(v)) if v else float("nan")

    def frac(regs):
        rs = [r for r in results if r["register"] in regs]
        tot = sum(r["n_tested"] for r in rs)
        return (sum(r["n_beating"] for r in rs) / tot) if tot else float("nan")

    print("\n=== control: unisons should beat, single strings should not ===")
    print("%-14s %12s %12s %12s" % ("register", "partials beating", "rate", "depth"))
    for label, regs in (("single (bass)", ("single",)),
                        ("double", ("double",)),
                        ("triple", ("triple",))):
        print("%-14s %11.0f%% %12.2f %12.3f" %
              (label, 100 * frac(regs), pool(regs, "median_rate"), pool(regs, "depth"
               if False else "median_depth")))

    held, moved = floor_sensitivity()

    coh = [r["coherence"] for r in results
           if r["register"] != "single" and r["coherence"] is not None]
    print("\n=== shimmer dims for the piano encoder ===")
    if moved > held:
        print("shimmerHz        NOT LOCKABLE from this pass — the measured rates")
        print("                 track the analysis floor (%d climbed vs %d held),"
              % (moved, held))
        print("                 so they are a window artifact, not beat rates.")
        print("                 Leave shimmerHz searchable in the encoder.")
    if coh:
        mc = float(np.median(coh))
        print("shimmerHz        ~ %.2f   (median unison beat rate%s)" %
              (pool(("double", "triple"), "median_rate"),
               "" if moved <= held else " — SUSPECT, see above"))
        print("shimmerDepth     ~ %.3f  (median measured dip)" %
              pool(("double", "triple"), "median_depth"))
        print("shimmerCoherence ~ %.2f   (rate agreement across partials of a note)" % mc)
        print("  -> %s" % ("HIGH: one shared oscillator per note is the right model"
                           if mc > 0.6 else
                           "LOW: partials beat independently; keep the engine's "
                           "decorrelated per-partial walk"))
    else:
        print("no coherent beating found in the unison registers — do NOT lock "
              "shimmer dims from this pass; leave them searchable.")

    # Figure: rate vs f0, and rate vs partial index for the clearest note.
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    for reg, mk in (("single", "x"), ("double", "o"), ("triple", "^")):
        pts = [(r["f0"], r["median_rate"]) for r in results
               if r["register"] == reg and r["median_rate"]]
        if pts:
            ax[0].semilogx(*zip(*pts), mk, label=reg)
    ax[0].set_xlabel("f0 (Hz)"); ax[0].set_ylabel("median beat rate (Hz)")
    ax[0].set_title("Unison beat rate vs register"); ax[0].legend(); ax[0].grid(alpha=.3)

    best = max((r for r in results if r["n_beating"] >= 3),
               key=lambda r: r["n_beating"], default=None)
    if best:
        ns = [p["n"] for p in best["partials"]]
        rs = [p["rate"] for p in best["partials"]]
        ax[1].plot(ns, rs, "o-")
        ax[1].set_xlabel("partial n"); ax[1].set_ylabel("beat rate (Hz)")
        ax[1].set_title("Per-partial beat rate — %s (coherence %.2f)"
                        % (best["note"], best["coherence"]))
        ax[1].grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "piano_beating.png"), dpi=110)
    print("\nwrote %s" % os.path.join(OUT_DIR, "piano_beating.json"))


if __name__ == "__main__":
    main()
