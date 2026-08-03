"""Iowa MIS piano mf — groundwork analysis for the piano CMA-ES instrument.

Measures the three phenomena that make piano structurally different from the
sustained-tone instruments analysed so far (viola, clarinet):

  1. INHARMONICITY  — partial stretch f_n = n*f0*sqrt(1 + B*n^2); B fitted per
     note from long-window FFT peak frequencies of the early "sustain"
     (iterative predict->peak->refit so high partials are found at their
     stretched, not harmonic, positions).
  2. DECAY STRUCTURE — per-partial heterodyne envelopes; single-slope decay
     rate (dB/s) over the top of the decay for the frequency-scaling law, and
     a two-segment piecewise-linear fit (grid-searched break) on h1 and the
     total envelope for the prompt-sound / aftersound double decay.
  3. ATTACK — broadband 10->90 rise from acoustic onset, band-energy tilt of
     the first 50 ms vs the quasi-sustain, and a between-line/at-line energy
     ratio (stretched line positions) in attack vs sustain windows to show the
     hammer knock is a genuine noise transient distinct from the partials.

Also emits, per analysed note, the stretched ExplicitPartials mult table the
engine would need, and flags where the descriptor caps (mult <= 200,
maxPartials <= 200) bind.

Writes out/piano_analysis_data.json + plots out/piano_*.png. Report prose
lives in out/piano_analysis_report.md (hand-written from this data).

Usage: python research/ml_ears/piano_analysis.py   (from repo root)
"""
import json
import os

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SAMPLE_DIR = os.path.join(REPO, "research", "inst_samples", "piano_mf")
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

# ~13 notes spanning B0-C8, weighted low/mid (9 of 13 at or below G4).
NOTES = ["B0", "C1", "F1", "C2", "G2", "C3", "G3", "C4",
         "G4", "C5", "C6", "C7", "C8"]

NOTE_SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

ENGINE_MULT_CAP = 200.0     # ExplicitPartials mult1/mult2 ArrayDescriptor max
ENGINE_MAXPARTIALS = 200    # ExplicitPartials maxPartials config max
ENGINE_SR = 48000.0


def note_to_midi(name):
    letter, acc, octv = name[0], name[1:-1], int(name[-1])
    return 12 * (octv + 1) + NOTE_SEMI[letter] + (1 if acc == "#" else -1 if acc == "b" else 0)


def midi_to_freq(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


def load_mono(note):
    path = os.path.join(SAMPLE_DIR, f"Piano.mf.{note}.aiff")
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def find_onset(x, sr):
    """Acoustic onset: first crossing of 2% of peak on a 5 ms RMS envelope."""
    win = max(1, int(0.005 * sr))
    env = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, "same"))
    i = int(np.argmax(env >= 0.02 * env.max()))
    return i, env


# ---------------------------------------------------------------------------
# 1. Inharmonicity
# ---------------------------------------------------------------------------

def spectrum(seg, sr, pad=4):
    seg = seg * np.hanning(len(seg))
    nfft = pad * (1 << int(np.ceil(np.log2(len(seg)))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    return spec, fbin


def peak_interp(spec, i):
    """Parabolic interpolation on log magnitude around bin i -> frac offset."""
    if i <= 0 or i >= len(spec) - 1:
        return 0.0
    a, b, c = np.log(spec[i - 1] + 1e-30), np.log(spec[i] + 1e-30), np.log(spec[i + 1] + 1e-30)
    d = a - 2 * b + c
    return 0.0 if d == 0 else 0.5 * (a - c) / d


def _fit_ab(meas):
    """Least-squares (f/n)^2 = a + b*n^2 with b >= 0; one 3-sigma outlier pass."""
    ns = np.array([m[0] for m in meas], float)
    fs = np.array([m[1] for m in meas], float)
    y = (fs / ns) ** 2
    A = np.vstack([np.ones_like(ns), ns ** 2]).T
    coef, *_ = np.linalg.lstsq(A, y, rcond=None)
    resid = y - A @ coef
    keep = np.abs(resid) < 3 * (resid.std() + 1e-12)
    if 4 <= keep.sum() < len(meas):
        coef, *_ = np.linalg.lstsq(A[keep], y[keep], rcond=None)
    a = float(max(coef[0], 1e-12))
    b = float(max(coef[1], 0.0))
    return a, b


def measure_partials(x, sr, onset, f0_nom, seg_len, n_max, snr_db=10.0):
    """Sequentially track partial peaks up the spectrum, refitting the stretch
    model (f_n/n)^2 = a + b*n^2 after each acceptance so the search window
    follows the inharmonic stretch instead of assuming harmonic positions.

    Returns (f0_fit, B, list of (n, f_meas, amp_db)). amp_db rel strongest.
    """
    n0 = onset + int(0.03 * sr)
    seg = x[n0: n0 + int(seg_len * sr)]
    spec, fbin = spectrum(seg, sr)
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
        fmeas = fbin[i] + peak_interp(spec, i) * df
        meas.append((n, fmeas, spec[i]))
        if len(meas) >= 5:
            a, b = _fit_ab(meas)
    if len(meas) >= 5:
        a, b = _fit_ab(meas)
    f0 = float(np.sqrt(a))
    B = b / a
    amax = max(m[2] for m in meas) if meas else 1.0
    plist = [(int(n), float(f), float(20 * np.log10(am / amax + 1e-12)))
             for n, f, am in meas]
    return f0, B, plist


def pair_B(plist):
    """Top-octave fallback: B from (f1, f2) alone.

    f1 = f0 sqrt(1+B), f2 = 2 f0 sqrt(1+4B) => (f2/2f1)^2 = (1+4B)/(1+B),
    so B = (r-1)/(4-r) with r = (f2/2f1)^2. Needed above ~C6 where the
    full-track fit collapses (only 2-4 real partials; later "peaks" are
    soundboard resonances, not string partials).
    """
    d = {n: f for n, f, _ in plist}
    if 1 not in d or 2 not in d:
        return None
    r = (d[2] / (2.0 * d[1])) ** 2
    if r >= 3.9:
        return None
    B = max((r - 1.0) / (4.0 - r), 0.0)
    return float(d[1] / np.sqrt(1.0 + B)), float(B)


# ---------------------------------------------------------------------------
# 2. Decay
# ---------------------------------------------------------------------------

def heterodyne_env(x, sr, fc, bw_hz, decim):
    t = np.arange(len(x)) / sr
    z = x * np.exp(-2j * np.pi * fc * t)
    sos = butter(4, bw_hz / (sr / 2), btype="low", output="sos")
    z = sosfiltfilt(sos, z.real) + 1j * sosfiltfilt(sos, z.imag)
    return np.abs(z[::decim]), sr / decim


def fit_two_segment(t, db, n_grid=60):
    """Piecewise-linear (two-segment continuous) fit of db(t).

    Returns (t_break, slope1, slope2, sse2, slope_single, sse1)."""
    A = np.vstack([np.ones_like(t), t]).T
    c, res1, *_ = np.linalg.lstsq(A, db, rcond=None)
    sse1 = float(res1[0]) if len(res1) else float(((db - A @ c) ** 2).sum())
    s_single = float(c[1])
    best = None
    cands = np.linspace(t[0] + 0.08 * (t[-1] - t[0]),
                        t[0] + 0.75 * (t[-1] - t[0]), n_grid)
    for tb in cands:
        h = np.maximum(t - tb, 0.0)          # hinge
        A2 = np.vstack([np.ones_like(t), t, h]).T
        c2, res2, *_ = np.linalg.lstsq(A2, db, rcond=None)
        sse2 = float(res2[0]) if len(res2) else float(((db - A2 @ c2) ** 2).sum())
        if best is None or sse2 < best[3]:
            best = (float(tb), float(c2[1]), float(c2[1] + c2[2]), sse2)
    tb, s1, s2, sse2 = best
    return tb, s1, s2, sse2, s_single, sse1


def decay_analysis(x, sr, onset, f0, B, plist, note):
    """Per-partial single-slope decay + double-decay fit on h1 and total."""
    xa = x[onset:]
    dur = len(xa) / sr
    decim = max(1, int(sr / 200.0))
    spacing = 0.5 * f0
    bw = min(0.4 * spacing, 30.0)
    per_partial = []
    for n, fmeas, adb in plist[:16]:
        if adb < -50:            # too weak for a clean envelope
            continue
        env, sre = heterodyne_env(xa, sr, fmeas, bw, decim)
        te = np.arange(len(env)) / sre
        db = 20 * np.log10(env + 1e-12)
        floor_db = float(np.median(db[-max(4, len(db) // 20):]))
        # skip the first 100 ms: the hammer thump is broadband and leaks into
        # every heterodyne band (it made bass h1 look like 100+ dB/s decay)
        j0 = int(0.10 * sre)
        pk_i = j0 + int(np.argmax(env[j0: j0 + int(0.5 * sre)]))
        pk_db = db[pk_i]
        if pk_db - floor_db < 20.0:            # too close to the noise floor
            continue
        # fit until 25 dB below peak but stay 6 dB above the floor
        lim = max(pk_db - 25.0, floor_db + 6.0)
        after = db[pk_i:]
        below = np.nonzero(after <= lim)[0]
        j_end = pk_i + (int(below[0]) if len(below) else len(after) - 1)
        if j_end - pk_i < int(0.15 * sre):    # need >=150 ms of decay
            j_end = min(len(db) - 1, pk_i + int(0.15 * sre))
        tt = te[pk_i:j_end] - te[pk_i]
        dd = db[pk_i:j_end]
        if len(tt) < 8:
            continue
        A = np.vstack([np.ones_like(tt), tt]).T
        c, *_ = np.linalg.lstsq(A, dd, rcond=None)
        rate = float(-c[1])                    # dB/s, positive = decaying
        if rate <= 0:                          # not decaying = floor artifact
            continue
        per_partial.append({"n": n, "f": fmeas, "rate_db_s": rate,
                            "t60_s": 60.0 / rate if rate > 1e-3 else None})
    # double-decay: h1 (or strongest of first 3) + total RMS envelope
    dd_fits = {}
    for tag, env_pair in (("h1", None), ("total", None)):
        if tag == "h1" and plist:
            best = max(plist[:3], key=lambda m: m[2])
            env, sre = heterodyne_env(xa, sr, best[1], bw, decim)
        else:
            win = max(1, int(0.02 * sr))
            e = np.sqrt(np.convolve(xa ** 2, np.ones(win) / win, "same"))
            env, sre = e[::decim], sr / decim
        te = np.arange(len(env)) / sre
        db = 20 * np.log10(env + 1e-12)
        pk_i = int(np.argmax(db[: int(0.5 * sre)]))
        pk_db = db[pk_i]
        lim = pk_db - 45.0
        after = db[pk_i:]
        below = np.nonzero(after <= lim)[0]
        j_end = pk_i + (int(below[0]) if len(below) else len(after) - 1)
        j_end = min(j_end, pk_i + int(min(12.0, dur - 0.2) * sre))
        tt = te[pk_i:j_end] - te[pk_i]
        dd = db[pk_i:j_end] - pk_db
        if len(tt) < 20:
            continue
        tb, s1, s2, sse2, ss, sse1 = fit_two_segment(tt, dd)
        dd_fits[tag] = {"t_break_s": tb, "slope1_db_s": s1, "slope2_db_s": s2,
                        "slope_single_db_s": ss,
                        "sse_ratio": sse2 / (sse1 + 1e-12),
                        "fit_span_s": float(tt[-1])}
    return per_partial, dd_fits


# ---------------------------------------------------------------------------
# 3. Attack
# ---------------------------------------------------------------------------

BANDS = [(30.0, 160.0), (160.0, 700.0), (700.0, 2500.0), (2500.0, 8000.0)]


def band_energy_db(seg, sr):
    spec, fbin = spectrum(seg, sr, pad=1)
    p = spec ** 2
    tot = p.sum() + 1e-30
    out = []
    for lo, hi in BANDS:
        b = (fbin >= lo) & (fbin < hi)
        out.append(float(10 * np.log10(p[b].sum() / tot + 1e-12)))
    return out


def line_ratio(seg, sr, f0, B, tol_frac=0.08):
    """Between-line / at-line energy, lines at stretched positions."""
    spec, fbin = spectrum(seg, sr, pad=2)
    p = spec ** 2
    lines = np.zeros(len(fbin), bool)
    n = 1
    while True:
        fc = n * f0 * np.sqrt(1.0 + B * n * n)
        if fc > min(8000.0, fbin[-1]):
            break
        tol = max(tol_frac * f0, 2 * (fbin[1] - fbin[0]))
        lines |= np.abs(fbin - fc) <= tol
        n += 1
    band = (fbin >= 50.0) & (fbin < 8000.0)
    at = p[band & lines].sum()
    between = p[band & ~lines].sum()
    return float(between / (at + 1e-12))


def attack_analysis(x, sr, onset, f0, B):
    win = max(1, int(0.002 * sr))
    env = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, "same"))
    seg_env = env[onset: onset + int(0.5 * sr)]
    pk = seg_env.max()
    i10 = int(np.argmax(seg_env >= 0.1 * pk))
    i90 = int(np.argmax(seg_env >= 0.9 * pk))
    rise_ms = 1000.0 * max(0, i90 - i10) / sr
    peak_ms = 1000.0 * int(np.argmax(seg_env)) / sr

    first50 = x[onset: onset + int(0.050 * sr)]
    sus = x[onset + int(0.5 * sr): onset + int(1.5 * sr)]
    tilt_50 = band_energy_db(first50, sr)
    tilt_sus = band_energy_db(sus, sr)

    # noise transient: between/at-line ratio needs enough freq resolution to
    # separate lines; 120 ms window (res ~ 8 Hz Hann) vs bass spacing >= 31 Hz.
    atk_seg = x[onset: onset + int(0.120 * sr)]
    sus_seg = x[onset + int(0.8 * sr): onset + int(0.8 * sr) + int(0.120 * sr)]
    ntr_atk = line_ratio(atk_seg, sr, f0, B)
    ntr_sus = line_ratio(sus_seg, sr, f0, B)
    return {"rise_10_90_ms": rise_ms, "peak_ms": peak_ms,
            "band_db_first50": tilt_50, "band_db_sustain": tilt_sus,
            "band_tilt_delta": [round(a - b, 2) for a, b in zip(tilt_50, tilt_sus)],
            "line_ratio_attack": ntr_atk, "line_ratio_sustain": ntr_sus}


# ---------------------------------------------------------------------------
# Engine mapping check
# ---------------------------------------------------------------------------

def engine_mult_check(f0, B):
    """Stretched mult table up to 18 kHz at ENGINE_SR; cap statistics."""
    mults = []
    n = 1
    while True:
        m = n * np.sqrt(1.0 + B * n * n)
        if m * f0 > min(18000.0, 0.45 * ENGINE_SR):
            break
        mults.append(m)
        n += 1
        if n > 400:
            break
    n_total = len(mults)
    n_under_cap = sum(1 for m in mults if m <= ENGINE_MULT_CAP)
    return {"n_partials_to_18k": n_total,
            "n_under_mult_cap": n_under_cap,
            "max_mult": round(mults[-1], 2) if mults else None,
            "mult_cap_binds": n_under_cap < n_total,
            "maxPartials_binds": n_total > ENGINE_MAXPARTIALS,
            "mult_table_first12": [round(m, 4) for m in mults[:12]]}


# ---------------------------------------------------------------------------

def main():
    results = {}
    for note in NOTES:
        x, sr = load_mono(note)
        midi = note_to_midi(note)
        f0_nom = midi_to_freq(midi)
        onset, _ = find_onset(x, sr)
        # analysis segment length: long enough for bass line resolution,
        # short enough that fast treble decay doesn't bury the peaks
        seg_len = float(np.clip(6.0 / f0_nom * 12, 0.4, 2.0))
        if f0_nom < 100:
            seg_len = 2.0
        elif f0_nom > 1500:
            seg_len = 0.4
        n_max = int(min(80, 18000.0 / f0_nom))
        if f0_nom > 1500:
            seg_len = 0.25          # top partials die within ~100 ms
        f0, B, plist = measure_partials(x, sr, onset, f0_nom, seg_len, n_max)
        b_method = "track"
        if f0_nom > 1500:
            pb = pair_B(plist)
            if pb is not None:
                f0, B = pb
                b_method = "pair(f1,f2)"
                # drop tracked "partials" that don't sit on the stretch model
                plist = [(n, f, adb) for n, f, adb in plist
                         if abs(f - n * f0 * np.sqrt(1 + B * n * n)) < 0.05 * f]
        per_partial, dd = decay_analysis(x, sr, onset, f0, B, plist, note)
        atk = attack_analysis(x, sr, onset, f0, B)
        eng = engine_mult_check(f0, B)
        results[note] = {
            "midi": midi, "f0_nominal": round(f0_nom, 3),
            "f0_fit": round(f0, 3), "B": B, "B_method": b_method,
            "n_partials_measured": len(plist),
            "partials": [[n, round(f, 2), round(a, 1)] for n, f, a in plist],
            "decay_per_partial": per_partial,
            "double_decay": dd,
            "attack": atk,
            "engine": eng,
        }
        cents_off = 1200 * np.log2(f0 / f0_nom)
        print(f"{note:3s} f0={f0:8.2f} ({cents_off:+5.1f}c)  B={B:.3e}  "
              f"nP={len(plist):2d}  rise={atk['rise_10_90_ms']:5.1f}ms  "
              f"lineR atk/sus={atk['line_ratio_attack']:.3f}/{atk['line_ratio_sustain']:.3f}")
        if "total" in dd:
            t = dd["total"]
            print(f"     total dbl-decay: break={t['t_break_s']:.2f}s "
                  f"s1={t['slope1_db_s']:+.1f} s2={t['slope2_db_s']:+.1f} dB/s "
                  f"(sse2/sse1={t['sse_ratio']:.2f})")

    # ---- summary law fits ------------------------------------------------
    def powfit(pairs):
        """log-log linear fit y = c * x^p over (x, y) pairs."""
        x_ = np.log([p[0] for p in pairs])
        y_ = np.log([p[1] for p in pairs])
        A = np.vstack([np.ones_like(x_), x_]).T
        c, *_ = np.linalg.lstsq(A, y_, rcond=None)
        pred = A @ c
        ss = 1 - ((y_ - pred) ** 2).sum() / (((y_ - y_.mean()) ** 2).sum() + 1e-12)
        return float(np.exp(c[0])), float(c[1]), float(ss)

    bass = [(results[n]["f0_fit"], results[n]["B"]) for n in NOTES
            if n in results and results[n]["f0_fit"] <= 70 and results[n]["B"] > 0]
    treb = [(results[n]["f0_fit"], results[n]["B"]) for n in NOTES
            if n in results and results[n]["f0_fit"] >= 120 and results[n]["B"] > 0]
    pooled = [(p["f"], p["rate_db_s"]) for n in NOTES if n in results
              for p in results[n]["decay_per_partial"] if p["rate_db_s"] > 0.1]
    summary = {}
    if len(bass) >= 3:
        c, p, r2 = powfit(bass)
        summary["B_bass_powerlaw"] = {"c": c, "p": p, "r2": r2,
                                      "note_range": "f0<=70Hz"}
    if len(treb) >= 3:
        c, p, r2 = powfit(treb)
        summary["B_treble_powerlaw"] = {"c": c, "p": p, "r2": r2,
                                        "note_range": "f0>=120Hz"}
    if len(pooled) >= 10:
        c, p, r2 = powfit(pooled)
        summary["decay_rate_powerlaw"] = {"c": c, "p": p, "r2": r2,
                                          "pairs": len(pooled),
                                          "units": "dB/s vs partial Hz"}
    results["_summary"] = summary
    for k, v in summary.items():
        print(f"{k}: y = {v['c']:.4g} * x^{v['p']:.3f}  (R2={v['r2']:.2f})")

    json.dump(results, open(os.path.join(OUT, "piano_analysis_data.json"), "w"),
              indent=1)

    # ---- plots -----------------------------------------------------------
    notes = [n for n in NOTES if n in results]
    f0s = np.array([results[n]["f0_fit"] for n in notes])
    Bs = np.array([results[n]["B"] for n in notes])
    ok = Bs > 0
    fig, ax = plt.subplots(figsize=(7, 5))
    ax.loglog(f0s[ok], Bs[ok], "o")
    for n, fx, b in zip(notes, f0s, Bs):
        if b > 0:
            ax.annotate(n, (fx, b), fontsize=8, xytext=(3, 3),
                        textcoords="offset points")
    ax.set_xlabel("f0 (Hz)"), ax.set_ylabel("B")
    ax.set_title("Piano inharmonicity coefficient B vs f0 (Iowa mf)")
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "piano_B_vs_f0.png"), dpi=110)

    fig, ax = plt.subplots(figsize=(7, 5))
    for n in notes:
        pp = results[n]["decay_per_partial"]
        if len(pp) >= 3:
            fs = [p["f"] for p in pp]
            rs = [p["rate_db_s"] for p in pp]
            ax.loglog(fs, rs, "o-", label=n, alpha=0.7, ms=3)
    ax.set_xlabel("partial frequency (Hz)"), ax.set_ylabel("decay rate (dB/s)")
    ax.set_title("Per-partial decay rate vs partial frequency")
    ax.legend(fontsize=7, ncol=2), ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "piano_decay_rates.png"), dpi=110)

    fig, ax = plt.subplots(figsize=(7, 4))
    rises = [results[n]["attack"]["rise_10_90_ms"] for n in notes]
    ax.semilogx(f0s, rises, "o-")
    for n, fx, r in zip(notes, f0s, rises):
        ax.annotate(n, (fx, r), fontsize=8, xytext=(3, 3),
                    textcoords="offset points")
    ax.set_xlabel("f0 (Hz)"), ax.set_ylabel("10-90 rise (ms)")
    ax.set_title("Attack rise time vs register")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "piano_attack_rise.png"), dpi=110)

    # B(f0) power-law fits per side of the minimum
    if ok.sum() >= 4:
        i_min = int(np.argmin(Bs[ok]))
        print("\nB(f0): min at", np.array(notes)[ok][i_min])
    print("wrote", os.path.join(OUT, "piano_analysis_data.json"))


if __name__ == "__main__":
    main()
