"""dsp run 22 — piano smoke diagnosis (measurement only, no re-tune).

Compares renders/cmaes_piano/piano_smoke_best_C2C4C6.wav against the real
Iowa piano mf C2/C4/C6, autopsies the attack ("chuff"), the sustain
("string-y"), checks the FM accident (fm_matrix2 t1_04), and sanity-checks
the locked decay laws. Prints everything; writes nothing outside stdout.
"""
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, stft

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import piano_analysis as pa  # noqa: E402  (find_onset, heterodyne_env, ...)

BEST_WAV = os.path.join(REPO, "renders", "cmaes_piano", "piano_smoke_best_C2C4C6.wav")
BASE_WAV = os.path.join(REPO, "renders", "cmaes_piano", "piano_smoke_baseline_C2C4C6.wav")
FM_WAV = os.path.join(REPO, "renders", "fm_matrix2", "modulated",
                      "t1_04_cratio_audiorate_lo.wav")
DATA = json.load(open(os.path.join(HERE, "out", "piano_analysis_data.json")))

# note layout in the smoke render (see renders/cmaes_piano/_best_C2C4C6.json)
NOTES = [("C2", 36, 65.406, 0.0), ("C4", 60, 261.626, 4.0), ("C6", 84, 1046.502, 8.0)]
SEG_LEN = 3.9

# converged optimizer values (cmaes_runs/piano_smoke/best_meta.json, decoded)
DECAY_SCALE = 0.617
TEMPLATE_H1 = [(32.7, 1.4), (130.8, 14.0), (2093.0, 19.0), (4186.0, 38.3)]
DECAY_EXP = 0.6
B_CURVE = [(30.76, 2.41e-4), (43.52, 1.11e-4), (65.42, 1.23e-4), (130.98, 1.14e-4),
           (262.57, 2.89e-4), (523.38, 9.59e-4), (1040.96, 4.37e-3),
           (2100.79, 7.33e-3), (3988.69, 2.48e-2)]


def logf_interp(f, curve):
    xs = np.log(np.array([c[0] for c in curve]))
    ys = np.array([c[1] for c in curve])
    return float(np.interp(np.log(f), xs, ys))


def load_wav(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def rms_env(x, sr, win_s=0.002):
    w = max(1, int(win_s * sr))
    return np.sqrt(np.convolve(x ** 2, np.ones(w) / w, "same"))


def band_env(x, sr, lo, hi, win_s=0.002):
    sos = butter(4, [lo / (sr / 2), min(hi / (sr / 2), 0.999)], btype="band",
                 output="sos")
    return rms_env(sosfiltfilt(sos, x), sr, win_s)


def rise_times(env, sr, onset_i):
    """(t10, t50, t90) in ms after onset_i, on env's own max after onset."""
    e = env[onset_i:]
    pk = e[:int(0.5 * sr)].max()
    out = []
    for frac in (0.1, 0.5, 0.9):
        idx = np.nonzero(e >= frac * pk)[0]
        out.append(1000.0 * idx[0] / sr if len(idx) else np.nan)
    return out


def spectral_flux(x, sr, onset_i, dur_s=0.12, nper=512):
    seg = x[onset_i: onset_i + int((dur_s + 0.02) * sr)]
    f, t, Z = stft(seg, sr, nperseg=nper, noverlap=nper - nper // 2)
    M = np.abs(Z)
    flux = np.maximum(M[:, 1:] - M[:, :-1], 0.0).sum(axis=0)
    flux = flux / (flux.max() + 1e-12)
    tms = t[1:] * 1000.0
    keep = tms <= dur_s * 1000.0
    return tms[keep], flux[keep]


def centroid_at(x, sr, onset_i, t_s, win_s=0.2, fmax=8000.0):
    s0 = onset_i + int(t_s * sr)
    seg = x[s0: s0 + int(win_s * sr)]
    if len(seg) < 256:
        return np.nan
    spec, fbin = pa.spectrum(seg, sr, pad=1)
    m = fbin <= fmax
    p = spec[m] ** 2
    return float((fbin[m] * p).sum() / (p.sum() + 1e-30))


def attack_autopsy(tag, x, sr, onset_i, f0):
    """Rise/lag numbers for one note. tonal env = heterodyne of h1."""
    full = rms_env(x, sr, 0.002)
    t10, t50, t90 = rise_times(full, sr, onset_i)
    bw = min(0.4 * f0, 30.0)
    ton = pa.heterodyne_env(x, sr, f0, bw, 1)[0]
    kn = band_env(x, sr, 2500.0, 8000.0)
    tt = rise_times(ton, sr, onset_i)
    kk = rise_times(kn, sr, onset_i)
    # knock band energy envelope: time of its own peak; decay to 10% of peak
    ke = kn[onset_i:]
    kpk_i = int(np.argmax(ke[:int(0.5 * sr)]))
    kpk_t = 1000.0 * kpk_i / sr
    below = np.nonzero(ke[kpk_i:] <= 0.1 * ke[kpk_i])[0]
    kdur = 1000.0 * below[0] / sr if len(below) else np.nan
    tms, flux = spectral_flux(x, sr, onset_i)
    fpk_t = tms[int(np.argmax(flux))]
    late = flux[tms > 40.0]
    flux40 = float(late.max()) if len(late) else np.nan
    print(f"  {tag:14s} rise10-90 {t90 - t10:6.1f} ms  t90 {t90:6.1f} ms | "
          f"tonal t50 {tt[1]:6.1f}  knock t50 {kk[1]:6.1f}  "
          f"lag(kn-ton) {kk[1] - tt[1]:+6.1f} ms | knock pk@{kpk_t:5.1f} ms "
          f"->10% in {kdur:6.1f} ms | fluxpk@{fpk_t:5.1f} ms  flux>40ms {flux40:.2f}")
    return {"rise1090": t90 - t10, "t90": t90, "ton_t50": tt[1], "kn_t50": kk[1],
            "kn_pk_ms": kpk_t, "kn_dur_ms": kdur, "flux_pk_ms": fpk_t,
            "flux_after40": flux40}


def fit_partial_rates(x, sr, onset_i, f0, B, n_max=16):
    """Same method as piano_analysis.decay_analysis but at model freqs."""
    xa = x[onset_i:]
    decim = max(1, int(sr / 200.0))
    bw = min(0.4 * 0.5 * f0, 30.0)
    rows = []
    for n in range(1, n_max + 1):
        fc = n * f0 * np.sqrt(1.0 + B * n * n)
        if fc > 0.45 * sr:
            break
        env, sre = pa.heterodyne_env(xa, sr, fc, bw, decim)
        db = 20 * np.log10(env + 1e-12)
        floor_db = float(np.median(db[-max(4, len(db) // 20):]))
        j0 = int(0.10 * sre)
        pk_i = j0 + int(np.argmax(env[j0: j0 + int(0.5 * sre)]))
        pk_db = db[pk_i]
        if pk_db - floor_db < 20.0:
            continue
        lim = max(pk_db - 25.0, floor_db + 6.0)
        after = db[pk_i:]
        below = np.nonzero(after <= lim)[0]
        j_end = pk_i + (int(below[0]) if len(below) else len(after) - 1)
        if j_end - pk_i < int(0.15 * sre):
            j_end = min(len(db) - 1, pk_i + int(0.15 * sre))
        tt = (np.arange(pk_i, j_end) - pk_i) / sre
        dd = db[pk_i:j_end]
        if len(tt) < 8:
            continue
        c = np.polyfit(tt, dd, 1)
        if -c[0] > 0:
            rows.append((n, fc, float(-c[0])))
    return rows


def two_seg_total(x, sr, onset_i, span_s):
    xa = x[onset_i: onset_i + int(span_s * sr)]
    decim = max(1, int(sr / 200.0))
    env = rms_env(xa, sr, 0.02)[::decim]
    sre = sr / decim
    te = np.arange(len(env)) / sre
    db = 20 * np.log10(env + 1e-12)
    m = te >= 0.15
    return pa.fit_two_segment(te[m], db[m])


def powfit(ns, rs):
    ns, rs = np.asarray(ns, float), np.asarray(rs, float)
    keep = (rs > 0)
    if keep.sum() < 3:
        return np.nan, np.nan, np.nan
    X = np.log(ns[keep]); Y = np.log(rs[keep])
    b, a = np.polyfit(X, Y, 1)
    r2 = 1 - np.var(Y - (a + b * X)) / np.var(Y)
    return float(np.exp(a)), float(b), float(r2)


def main():
    best, sr_b = load_wav(BEST_WAV)
    print(f"candidate: {os.path.basename(BEST_WAV)}  sr={sr_b}")

    reals = {}
    for name, midi, f0, _ in NOTES:
        x, sr = pa.load_mono(name)
        onset, _ = pa.find_onset(x, sr)
        reals[name] = (x, sr, onset)

    # ------------------------------------------------------------------ A
    print("\n=== A. ATTACK AUTOPSY (times in ms after acoustic onset) ===")
    A = {}
    for name, midi, f0, t0 in NOTES:
        seg = best[int(t0 * sr_b): int((t0 + SEG_LEN) * sr_b)]
        onset_c, _ = pa.find_onset(seg, sr_b)
        x, sr, onset_r = reals[name]
        print(f" {name}  (f0 {f0:.1f} Hz)")
        A[(name, "real")] = attack_autopsy("real", x, sr, onset_r, f0)
        A[(name, "cand")] = attack_autopsy("candidate", seg, sr_b, onset_c, f0)

    # ------------------------------------------------------------------ B
    print("\n=== B. SUSTAIN — per-partial decay rates (dB/s) ===")
    for name, midi, f0, t0 in NOTES:
        Bv = logf_interp(f0, B_CURVE)
        h1_rate = DECAY_SCALE * logf_interp(f0, TEMPLATE_H1)
        seg = best[int(t0 * sr_b): int((t0 + SEG_LEN) * sr_b)]
        onset_c, _ = pa.find_onset(seg, sr_b)
        cand = dict((n, r) for n, _, r in
                    fit_partial_rates(seg, sr_b, onset_c, f0, Bv))
        pred = {n: h1_rate * (n * np.sqrt(1 + Bv * n * n)) ** DECAY_EXP
                for n in range(1, 17)}
        realr = {d["n"]: d["rate_db_s"] for d in DATA[name]["decay_per_partial"]}
        print(f" {name}: engine decayRate(f0)={h1_rate:.2f} dB/s "
              f"(template {logf_interp(f0, TEMPLATE_H1):.2f} x {DECAY_SCALE})")
        print("   n      pred    cand-meas    real")
        for n in (1, 2, 3, 4, 6, 8, 10, 12, 16):
            c = cand.get(n, np.nan)
            r = realr.get(n, np.nan)
            print(f"  {n:3d}  {pred[n]:8.2f}  {c:9.2f}  {r:8.2f}")
        # double decay on the candidate note
        tb, s1, s2, sse2, s_sing, sse1 = two_seg_total(seg, sr_b, onset_c,
                                                       SEG_LEN - 0.2)
        dd = DATA[name]["double_decay"].get("total") or \
            list(DATA[name]["double_decay"].values())[0]
        print(f"   two-seg cand: break {tb:.2f}s  {s1:.1f}->{s2:.1f} dB/s "
              f"sse2/sse1 {sse2 / (sse1 + 1e-12):.2f}   "
              f"real: break {dd['t_break_s']:.2f}s  {dd['slope1_db_s']:.1f}->"
              f"{dd['slope2_db_s']:.1f} dB/s sse2/sse1 {dd['sse_ratio']:.2f}")
        # brighter->darker
        cc = [centroid_at(seg, sr_b, onset_c, t) for t in (0.2, 1.0, 2.0, 3.0)]
        x, sr, onset_r = reals[name]
        rc = [centroid_at(x, sr, onset_r, t) for t in (0.2, 1.0, 2.0, 3.0)]
        print("   centroid Hz @0.2/1/2/3s  cand: " +
              " ".join(f"{v:6.0f}" for v in cc) + "   real: " +
              " ".join(f"{v:6.0f}" for v in rc))

    # ------------------------------------------------------------------ C
    print("\n=== C. FM ACCIDENT — t1_04_cratio_audiorate_lo ===")
    fx, fsr = load_wav(FM_WAV)
    onset_f, _ = pa.find_onset(fx, fsr)
    # long-window spectrum of the early decay; grid-fit spacing of peaks
    seg = fx[onset_f + int(0.03 * fsr): onset_f + int(1.0 * fsr)]
    spec, fbin = pa.spectrum(seg, fsr, pad=4)
    m = (fbin > 25) & (fbin < 6000)
    sp, fb = spec[m], fbin[m]
    thr = sp.max() * 10 ** (-50 / 20.0)
    peaks = []
    for i in range(2, len(sp) - 2):
        if sp[i] > thr and sp[i] >= sp[i - 1] and sp[i] > sp[i + 1] \
                and sp[i] > 1.5 * np.median(sp[max(0, i - 200): i + 200]):
            fpk = fb[i] + pa.peak_interp(sp, i) * (fb[1] - fb[0])
            peaks.append((fpk, 20 * np.log10(sp[i] / sp.max())))
    # thin near-duplicates
    thin = []
    for f, a in peaks:
        if thin and f - thin[-1][0] < 8.0:
            if a > thin[-1][1]:
                thin[-1] = (f, a)
        else:
            thin.append((f, a))
    peaks = thin
    print(f" peaks >-50 dB, 25-6000 Hz: {len(peaks)}")
    top = sorted(peaks, key=lambda p: -p[1])[:20]
    print("  strongest 20: " + " ".join(f"{f:.0f}({a:.0f})" for f, a in
                                        sorted(top)))
    # best f0 by harmonic-grid residual scan
    cands = np.arange(30.0, 250.0, 0.05)
    strong = [(f, a) for f, a in peaks if a > -40]
    best_f0, best_err = None, 1e9
    for f0c in cands:
        errs = [abs(f / f0c - round(f / f0c)) for f, _ in strong]
        e = np.mean(errs)
        if e < best_err:
            best_err, best_f0 = e, f0c
    ns = [round(f / best_f0) for f, _ in strong]
    devs = [1200 * np.log2(f / (n * best_f0)) for (f, _), n in zip(strong, ns)
            if n > 0]
    print(f" harmonic-grid fit: f0 = {best_f0:.2f} Hz  "
          f"(mean |dev| {np.mean(np.abs(devs)):.1f} cents over "
          f"{len(strong)} peaks >-40 dB; max |dev| {np.max(np.abs(devs)):.0f}c)")
    meas = [(n, f) for (f, _), n in zip(strong, ns) if n > 0]
    a_, b_ = pa._fit_ab([(n, f, 0) for n, f in meas])
    print(f" stretch fit on those peaks: f0 {np.sqrt(a_):.2f} Hz  "
          f"B = {b_ / a_:.2e}  (real C2 B = 1.23e-4)")
    # attack + knock
    print(" attack:")
    attack_autopsy("fm t1_04", fx, fsr, onset_f, best_f0)
    # per-peak decay of the 10 strongest peaks
    print("  per-peak decay (10 strongest):")
    xa = fx[onset_f:]
    decim = max(1, int(fsr / 200.0))
    rows = []
    for f, a in top[:10]:
        env, sre = pa.heterodyne_env(xa, fsr, f, 20.0, decim)
        db = 20 * np.log10(env + 1e-12)
        j0 = int(0.10 * sre)
        pk_i = j0 + int(np.argmax(env[j0: j0 + int(0.5 * sre)]))
        j_end = min(len(db) - 1, pk_i + int(3.0 * sre))
        tt = (np.arange(pk_i, j_end) - pk_i) / sre
        c = np.polyfit(tt, db[pk_i:j_end], 1)
        rows.append((f, float(-c[0])))
    for f, r in sorted(rows):
        print(f"   {f:7.1f} Hz  {r:6.1f} dB/s")
    tb, s1, s2, sse2, s_sing, sse1 = two_seg_total(fx, fsr, onset_f, 5.5)
    print(f"  total env two-seg: break {tb:.2f}s  {s1:.1f}->{s2:.1f} dB/s  "
          f"sse2/sse1 {sse2 / (sse1 + 1e-12):.2f}")
    cc = [centroid_at(fx, fsr, onset_f, t) for t in (0.2, 1.0, 2.0, 3.0)]
    print("  centroid Hz @0.2/1/2/3s: " + " ".join(f"{v:6.0f}" for v in cc))

    # ------------------------------------------------------------------ D
    print("\n=== D. LOCKED-LAW REALISM CHECK ===")
    print(" per-note fit rate = a*n^b on real per-partial rates (locked b=0.6):")
    for name in ("C1", "C2", "G2", "C3", "C4", "G4", "C5", "C6"):
        dp = DATA[name]["decay_per_partial"]
        a, b, r2 = powfit([d["n"] for d in dp], [d["rate_db_s"] for d in dp])
        h1 = next((d["rate_db_s"] for d in dp if d["n"] == 1), np.nan)
        f0 = DATA[name]["f0_fit"]
        interp = logf_interp(f0, TEMPLATE_H1)
        print(f"  {name:3s} f0 {f0:7.1f}  exp b={b:5.2f} (r2 {r2:4.2f})  "
              f"h1 measured {h1:6.2f} dB/s  template-curve h1 {interp:6.2f}  "
              f"x{DECAY_SCALE}= {DECAY_SCALE * interp:6.2f}")


if __name__ == "__main__":
    main()
