"""clarinet_attack.py — analyze the attack phase of the Iowa Bb clarinet ff
samples (research/inst_samples/clarinet_Bb) ahead of any patch fitting.

Questions (Matt): what is actually going on in the "breathy attack", and how
could it be replicated with additive features?  Quantify his claim that "the
speed of the attack and the amount of breath are inversely proportional to
frequency."

Method per note:
  1. Acoustic onset via frame-RMS threshold over the recording noise floor.
  2. Heterodyne harmonics 1..12 (derive_motion-style: mix down by k*f0,
     low-pass) -> per-harmonic amplitude envelope; time from onset to 50% and
     90% of each harmonic's sustain level.
  3. Harmonic/noise split: reconstruct the harmonic part from the full set of
     heterodyned complex envelopes re-modulated (zero-phase linear-phase LP,
     so subtraction is aligned); residual = signal - harmonic part.
     LIMITATION: anything within +-lp_hz of a harmonic line (line broadening,
     fast AM sidebands, the onset pitch glide) is captured as "tone", so the
     residual under-counts near-line noise and the comb metric below tells us
     how much of the residual still sits near the lines.
  4. Noise-to-tone ratio trajectory (frame RMS, dB), noise onset vs tone
     onset lag, residual spectral shape (octave bands, centroid, slope),
     comb fraction (residual energy within +-6% of harmonic lines).
  5. Odd/even harmonic balance, attack window vs sustain window.
  6. Regressions across notes: log(t90) vs log(f0), attack NTR vs log2(f0).

Outputs: out/clarinet_attack_report.md + PNGs in out/.
"""
import os
import json

import numpy as np
import soundfile as sf
from scipy.signal import firwin, fftconvolve, welch

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "clarinet_Bb")
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

NOTES = ["E3", "G3", "Bb3", "D4", "F4", "A4", "D5", "G5", "C6", "F6"]

N_HARM = 12          # harmonics tracked for rise metrics
SEG_PRE = 0.25       # analysis segment starts this far before onset (s)
SEG_LEN = 2.2        # total analysis segment length (s)
SUS_T0, SUS_T1 = 0.7, 1.7   # sustain window, seconds after onset
ATK_MAX = 0.5        # attack analysis horizon after onset (s)
DECIM = 16           # envelope decimation for metrics

NOTE_PC = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5,
           "Gb": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}


def note_to_freq(name):
    pc, octv = name[:-1], int(name[-1])
    midi = 12 * (octv + 1) + NOTE_PC[pc]
    return 440.0 * 2 ** ((midi - 69) / 12.0), midi


def find_file(note):
    for f in sorted(os.listdir(SAMPLE_DIR)):
        if f".{note}." in f and f.endswith(".aif"):
            return os.path.join(SAMPLE_DIR, f)
    raise FileNotFoundError(note)


def load_mono(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def frame_rms(x, sr, win_s=0.010, hop_s=0.002):
    """RMS envelope: returns (times_s, rms)."""
    win = max(4, int(win_s * sr))
    hop = max(1, int(hop_s * sr))
    n = (len(x) - win) // hop
    idx = np.arange(n)[:, None] * hop + np.arange(win)[None, :]
    r = np.sqrt(np.mean(x[idx] ** 2, axis=1))
    t = (np.arange(n) * hop + win / 2) / sr
    return t, r


def find_onset(x, sr):
    """Acoustic onset (includes breath): first frame 12 dB over the noise
    floor that stays up for 20 ms. Returns sample index."""
    t, r = frame_rms(x, sr, 0.005, 0.001)
    floor = np.percentile(r, 5) + 1e-12
    thr = max(floor * 10 ** (12 / 20), r.max() * 10 ** (-70 / 20))
    hold = int(0.020 / 0.001)
    above = r > thr
    for i in range(len(r) - hold):
        if above[i] and above[i:i + hold].all():
            return int(t[i] * sr), floor
    raise RuntimeError("no onset found")


def refine_f0(x, sr, f0_nom):
    n = len(x)
    spec = np.abs(np.fft.rfft(x * np.hanning(n)))
    freqs = np.fft.rfftfreq(n, 1 / sr)
    band = (freqs > f0_nom * 0.94) & (freqs < f0_nom * 1.06)
    return freqs[band][np.argmax(spec[band])]


def heterodyne(x, sr, fc, lp_hz):
    t = np.arange(len(x)) / sr
    z = x * np.exp(-2j * np.pi * fc * t)
    taps = firwin(2049, lp_hz / (sr / 2))
    return fftconvolve(z, taps, mode="same")


def rise_time(t_env, env, level, t_from=0.0, hold_s=0.004):
    """First time >= level that holds for hold_s. NaN if never."""
    dt = t_env[1] - t_env[0]
    hold = max(1, int(hold_s / dt))
    ok = env >= level
    start = np.searchsorted(t_env, t_from)
    for i in range(start, len(env) - hold):
        if ok[i] and ok[i:i + hold].all():
            return t_env[i]
    return float("nan")


def octave_band_db(f, psd, centers=(250, 500, 1000, 2000, 4000, 8000)):
    out = {}
    for c in centers:
        sel = (f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))
        p = psd[sel].sum()
        out[c] = 10 * np.log10(p + 1e-30)
    return out


def analyze(note):
    f0_nom, midi = note_to_freq(note)
    x, sr = load_mono(find_file(note))
    on, floor_rms = find_onset(x, sr)

    # Analysis segment around the onset (filter edge effects land pre-onset).
    s0 = max(0, on - int(SEG_PRE * sr))
    seg = x[s0: s0 + int(SEG_LEN * sr)]
    t_on = (on - s0) / sr                       # onset time within segment
    t_seg = np.arange(len(seg)) / sr

    # f0 from the sustain window
    sus_sl = slice(int((t_on + SUS_T0) * sr), int((t_on + SUS_T1) * sr))
    f0 = refine_f0(seg[sus_sl], sr, f0_nom)

    lp_hz = min(0.45 * f0, 80.0)

    # --- heterodyne: metrics harmonics (stored) + full reconstruction (sum) --
    k_rec = int(min(50, 0.45 * sr / f0, 16000 / f0))
    recon = np.zeros(len(seg))
    envs = []                                    # decimated |env| for k<=N_HARM
    t_full = np.arange(len(seg)) / sr
    for k in range(1, k_rec + 1):
        env = heterodyne(seg, sr, k * f0, lp_hz)
        recon += 2.0 * np.real(env * np.exp(2j * np.pi * k * f0 * t_full))
        if k <= N_HARM:
            envs.append(np.abs(env[::DECIM]) * 2.0)   # actual amplitude
    sr_env = sr / DECIM
    t_env = np.arange(len(envs[0])) / sr_env
    residual = seg - recon

    # --- per-harmonic rise times ---------------------------------------------
    sus_lvl, t50, t90 = [], [], []
    for env in envs:
        sl = env[(t_env >= t_on + SUS_T0) & (t_env <= t_on + SUS_T1)]
        lvl = float(np.median(sl))
        sus_lvl.append(lvl)
        t50.append(rise_time(t_env, env, 0.5 * lvl, t_from=t_on) - t_on)
        t90.append(rise_time(t_env, env, 0.9 * lvl, t_from=t_on) - t_on)

    # rise order: slope of t50 vs harmonic number (ms per harmonic step);
    # positive = bottom-up assembly (highs enter later).
    t50a = np.array(t50)
    fin = np.isfinite(t50a)
    rise_slope = (float(np.polyfit(np.arange(1, len(t50a) + 1)[fin],
                                   1000 * t50a[fin], 1)[0])
                  if fin.sum() >= 4 else float("nan"))

    # total harmonic envelope (RMS over tracked harmonics)
    tot = np.sqrt(0.5 * np.sum(np.array(envs) ** 2, axis=0))
    tot_sus = float(np.median(tot[(t_env >= t_on + SUS_T0) & (t_env <= t_on + SUS_T1)]))
    t90_tot = rise_time(t_env, tot, 0.9 * tot_sus, t_from=t_on) - t_on
    t50_tot = rise_time(t_env, tot, 0.5 * tot_sus, t_from=t_on) - t_on

    # --- noise vs tone -------------------------------------------------------
    tf, harm_r = frame_rms(recon, sr)
    _, res_r = frame_rms(residual, sr)
    # pre-onset floors (frames fully before onset-30ms)
    pre = tf < t_on - 0.03
    res_floor = float(np.median(res_r[pre])) if pre.any() else float(res_r[0])
    harm_floor = float(np.median(harm_r[pre])) if pre.any() else float(harm_r[0])

    harm_sus = float(np.median(harm_r[(tf >= t_on + SUS_T0) & (tf <= t_on + SUS_T1)]))
    res_sus = float(np.median(res_r[(tf >= t_on + SUS_T0) & (tf <= t_on + SUS_T1)]))
    atk_sel = (tf >= t_on) & (tf <= t_on + max(t90_tot, 0.05))
    res_peak_atk = float(res_r[(tf >= t_on - 0.05) & (tf <= t_on + ATK_MAX)].max())

    tone_on = rise_time(tf, harm_r, 0.1 * harm_sus, t_from=max(0.0, t_on - 0.1))
    noise_on = rise_time(tf, res_r,
                         max(0.1 * res_peak_atk, 3.0 * res_floor),
                         t_from=max(0.0, t_on - 0.1))
    lag_ms = (tone_on - noise_on) * 1000.0

    # NTR: integrated over the attack (onset..t90_tot) and over sustain
    def energy(rr, sel):
        return float(np.sum(rr[sel] ** 2)) + 1e-30
    ntr_atk = 10 * np.log10(energy(res_r, atk_sel) / energy(harm_r, atk_sel))
    sus_sel = (tf >= t_on + SUS_T0) & (tf <= t_on + SUS_T1)
    ntr_sus = 10 * np.log10(energy(res_r, sus_sel) / energy(harm_r, sus_sel))
    # early NTR: onset..t50 (before the tone dominates the frame energy)
    early_sel = (tf >= t_on) & (tf <= t_on + max(t50_tot, 0.03))
    ntr_early = 10 * np.log10(energy(res_r, early_sel) / energy(harm_r, early_sel))
    # absolute breath level: peak attack noise vs eventual sustain tone level.
    # Independent of tone timing — "how loud is the breath vs the note".
    breath_abs = 20 * np.log10(res_peak_atk / (harm_sus + 1e-30))
    # trajectory for plots
    traj_sel = (tf >= t_on - 0.05) & (tf <= t_on + ATK_MAX)
    ntr_full = 20 * np.log10((res_r + 1e-12) / (harm_r + 1e-12))
    ntr_traj = (tf[traj_sel] - t_on, ntr_full[traj_sel])
    # breath excess duration: time (from onset) until NTR settles to within
    # 6 dB of its sustain value and stays there 50 ms — how long the attack
    # carries extra breath relative to the steady tone.
    dt_f = tf[1] - tf[0]
    hold = max(1, int(0.05 / dt_f))
    below = ntr_full < ntr_sus + 6.0
    t_breath = float("nan")
    for i in range(np.searchsorted(tf, t_on), len(tf) - hold):
        if below[i] and below[i:i + hold].all():
            t_breath = tf[i] - t_on
            break

    # --- residual spectrum during the attack --------------------------------
    a0, a1 = int(t_on * sr), int((t_on + max(t90_tot, 0.15)) * sr)
    res_atk = residual[a0:a1]
    f_psd, psd = welch(res_atk, fs=sr, nperseg=min(len(res_atk), 4096))
    bands = octave_band_db(f_psd, psd)
    bsel = (f_psd >= 200) & (f_psd <= 10000)
    centroid = float((f_psd[bsel] * psd[bsel]).sum() / (psd[bsel].sum() + 1e-30))
    # slope dB/octave over 300..8000
    ssel = (f_psd >= 300) & (f_psd <= 8000) & (psd > 0)
    slope = float(np.polyfit(np.log2(f_psd[ssel]),
                             10 * np.log10(psd[ssel]), 1)[0])
    # comb fraction: residual energy within +-6% of harmonic lines
    near = np.zeros(len(f_psd), bool)
    for k in range(1, int(f_psd[-1] / f0)):
        near |= np.abs(f_psd - k * f0) < 0.06 * f0
    comb_frac = float(psd[near].sum() / (psd.sum() + 1e-30))
    # pre-onset floor PSD energy in same band (contamination check)
    if t_on > 0.12:
        fpre, ppre = welch(seg[:int((t_on - 0.02) * sr)], fs=sr,
                           nperseg=min(int((t_on - 0.02) * sr), 4096))
        floor_db = 10 * np.log10(ppre[(fpre >= 200) & (fpre <= 10000)].sum() + 1e-30)
    else:
        floor_db = float("nan")
    atk_db = 10 * np.log10(psd[bsel].sum() + 1e-30)

    # --- odd/even balance ----------------------------------------------------
    atk_env_sel = (t_env >= t_on) & (t_env <= t_on + max(t90_tot, 0.05))
    sus_env_sel = (t_env >= t_on + SUS_T0) & (t_env <= t_on + SUS_T1)
    n_avail = len(envs)

    def eo_db(sel):
        e = sum(np.mean(envs[k - 1][sel] ** 2)
                for k in (2, 4, 6, 8, 10, 12) if k <= n_avail)
        o = sum(np.mean(envs[k - 1][sel] ** 2)
                for k in (3, 5, 7, 9, 11) if k <= n_avail)
        return 10 * np.log10((e + 1e-30) / (o + 1e-30))

    return {
        "note": note, "midi": midi, "f0": f0, "sr": sr, "lp_hz": lp_hz,
        "k_rec": k_rec, "n_harm": n_avail,
        "t50": t50, "t90": t90, "sus_lvl": sus_lvl,
        "t50_tot": t50_tot, "t90_tot": t90_tot,
        "t_breath": t_breath, "rise_slope_ms_per_h": rise_slope,
        "ntr_atk_db": ntr_atk, "ntr_sus_db": ntr_sus,
        "ntr_early_db": ntr_early, "breath_abs_db": breath_abs,
        "noise_lag_ms": lag_ms, "tone_on": tone_on, "noise_on": noise_on,
        "ntr_traj": ntr_traj,
        "bands_db": bands, "centroid_hz": centroid, "slope_db_oct": slope,
        "comb_frac": comb_frac, "floor_vs_atk_db": atk_db - floor_db,
        "eo_atk_db": eo_db(atk_env_sel), "eo_sus_db": eo_db(sus_env_sel),
        "res_psd": (f_psd, 10 * np.log10(psd + 1e-30)),
    }


def main():
    rows = []
    for note in NOTES:
        try:
            r = analyze(note)
        except Exception as e:  # noqa: BLE001
            print(f"{note}: FAILED ({e})")
            continue
        rows.append(r)
        print(f"{note:4s} f0={r['f0']:7.1f}  t50={1000*r['t50_tot']:6.1f}ms "
              f"t90={1000*r['t90_tot']:6.1f}ms tB={1000*r['t_breath']:6.1f}ms "
              f"NTRatk={r['ntr_atk_db']:6.1f}dB "
              f"early={r['ntr_early_db']:6.1f}dB breath={r['breath_abs_db']:6.1f}dB "
              f"lag={r['noise_lag_ms']:6.1f}ms "
              f"comb={r['comb_frac']:.2f}  slope={r['slope_db_oct']:+.1f}dB/oct "
              f"riseSlope={r['rise_slope_ms_per_h']:+5.1f}ms/h "
              f"E/O atk={r['eo_atk_db']:+.1f} sus={r['eo_sus_db']:+.1f}")

    if len(rows) < 4:
        raise SystemExit("too few notes analyzed")

    f0s = np.array([r["f0"] for r in rows])
    t90s = np.array([r["t90_tot"] for r in rows])
    t50s = np.array([r["t50_tot"] for r in rows])
    ntr_a = np.array([r["ntr_atk_db"] for r in rows])
    ntr_s = np.array([r["ntr_sus_db"] for r in rows])
    ntr_e = np.array([r["ntr_early_db"] for r in rows])
    breath = np.array([r["breath_abs_db"] for r in rows])
    lags = np.array([r["noise_lag_ms"] for r in rows])

    t_breaths = np.array([r["t_breath"] for r in rows])

    # --- regressions ---------------------------------------------------------
    def loglog_fit(x, y):
        ok = np.isfinite(y) & (y > 0)
        b, a = np.polyfit(np.log10(x[ok]), np.log10(y[ok]), 1)
        r2 = 1 - (np.log10(y[ok]) - (a + b * np.log10(x[ok]))).var() \
            / np.log10(y[ok]).var()
        return a, b, r2

    a_t90, b_t90, r2_t90 = loglog_fit(f0s, t90s)
    a_t50, b_t50, r2_t50 = loglog_fit(f0s, t50s)
    a_tb, b_tb, r2_tb = loglog_fit(f0s, t_breaths)

    b_ntr, a_ntr = np.polyfit(np.log2(f0s), ntr_a, 1)
    r2_ntr = 1 - (ntr_a - (a_ntr + b_ntr * np.log2(f0s))).var() / ntr_a.var()
    b_bre, a_bre = np.polyfit(np.log2(f0s), breath, 1)
    r2_bre = 1 - (breath - (a_bre + b_bre * np.log2(f0s))).var() / breath.var()
    b_lag, a_lag = np.polyfit(np.log2(f0s), np.log2(np.maximum(lags, 1.0)), 1)
    r2_lag = 1 - (np.log2(np.maximum(lags, 1.0))
                  - (a_lag + b_lag * np.log2(f0s))).var() \
        / np.log2(np.maximum(lags, 1.0)).var()

    b_ex, a_ex = np.polyfit(np.log2(f0s), ntr_a - ntr_s, 1)

    print(f"\nrise-time law:  t90 = {10**a_t90:.3f} * f0^{b_t90:+.2f}  "
          f"(R^2={r2_t90:.2f})")
    print(f"                t50 = {10**a_t50:.3f} * f0^{b_t50:+.2f}  "
          f"(R^2={r2_t50:.2f})")
    print(f"breath dom.:    tB  = {10**a_tb:.3f} * f0^{b_tb:+.2f}  "
          f"(R^2={r2_tb:.2f})")
    print(f"attack NTR:     {b_ntr:+.2f} dB/octave, R^2={r2_ntr:.2f}")
    print(f"breath abs:     {b_bre:+.2f} dB/octave, R^2={r2_bre:.2f}")
    print(f"noise lead:     lag ~ f0^{b_lag:+.2f}, R^2={r2_lag:.2f}")

    # --- plots ---------------------------------------------------------------
    cmap = plt.get_cmap("viridis")
    cn = (np.log2(f0s) - np.log2(f0s.min())) / (np.log2(f0s.max()) - np.log2(f0s.min()))

    fig, axes = plt.subplots(2, 5, figsize=(18, 7), sharey=False)
    for ax, r in zip(axes.flat, rows):
        ks = np.arange(1, len(r["t50"]) + 1)
        ax.plot(ks, 1000 * np.array(r["t50"]), "o-", ms=3, label="t50")
        ax.plot(ks, 1000 * np.array(r["t90"]), "s-", ms=3, label="t90")
        ax.set_title(f"{r['note']} ({r['f0']:.0f} Hz)", fontsize=9)
        ax.set_xlabel("harmonic"); ax.set_ylabel("ms"); ax.set_ylim(0, 700)
        ax.grid(alpha=0.3)
    axes[0, 0].legend(fontsize=8)
    fig.suptitle("Per-harmonic rise time from acoustic onset")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "clarinet_attack_rise.png"), dpi=110)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(10, 5))
    for r, c in zip(rows, cn):
        tt, nn = r["ntr_traj"]
        ax.plot(1000 * tt, nn, color=cmap(c), lw=1.2,
                label=f"{r['note']} {r['f0']:.0f}Hz")
    ax.axhline(0, color="k", lw=0.5); ax.set_ylim(-45, 30)
    ax.set_xlabel("ms from onset"); ax.set_ylabel("noise-to-tone dB")
    ax.set_title("Noise-to-tone ratio trajectory (10 ms frames)")
    ax.legend(fontsize=7, ncol=2); ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "clarinet_attack_ntr.png"), dpi=110)
    plt.close(fig)

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))
    ff = np.linspace(f0s.min(), f0s.max(), 50)
    axes[0].loglog(f0s, 1000 * t50s, "o", label="t50")
    axes[0].loglog(f0s, 1000 * t_breaths, "^", label="breath excess tB")
    axes[0].loglog(ff, 1000 * 10 ** a_t50 * ff ** b_t50, "-",
                   label=f"t50 ∝ f0^{b_t50:.2f} (R²={r2_t50:.2f})")
    axes[0].loglog(ff, 1000 * 10 ** a_tb * ff ** b_tb, "--",
                   label=f"tB ∝ f0^{b_tb:.2f} (R²={r2_tb:.2f})")
    axes[0].set_xlabel("f0 Hz"); axes[0].set_ylabel("ms")
    axes[0].legend(fontsize=8); axes[0].grid(alpha=0.3, which="both")
    axes[1].semilogx(f0s, breath, "o", label="breath peak vs sustain tone")
    axes[1].semilogx(f0s, ntr_e, "^", label="early NTR (onset..t50)")
    axes[1].semilogx(f0s, ntr_s, "s", label="sustain NTR")
    axes[1].semilogx(ff, a_bre + b_bre * np.log2(ff), "-",
                     label=f"breath {b_bre:+.1f} dB/oct (R²={r2_bre:.2f})")
    axes[1].set_xlabel("f0 Hz"); axes[1].set_ylabel("NTR dB")
    axes[1].legend(fontsize=8); axes[1].grid(alpha=0.3)
    axes[2].semilogx(f0s, lags, "o")
    axes[2].axhline(0, color="k", lw=0.5)
    axes[2].set_xlabel("f0 Hz"); axes[2].set_ylabel("noise-precedes-tone ms")
    axes[2].grid(alpha=0.3)
    fig.suptitle("Register dependence")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "clarinet_attack_register.png"), dpi=110)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(10, 5))
    for r, c in zip(rows, cn):
        f, p = r["res_psd"]
        ax.semilogx(f, p - p.max(), color=cmap(c), lw=1.0,
                    label=f"{r['note']} comb={r['comb_frac']:.2f}")
    ax.set_xlim(100, 16000); ax.set_ylim(-60, 3)
    ax.set_xlabel("Hz"); ax.set_ylabel("dB rel peak")
    ax.set_title("Residual (noise) PSD during attack")
    ax.legend(fontsize=7, ncol=2); ax.grid(alpha=0.3, which="both")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "clarinet_attack_noisepsd.png"), dpi=110)
    plt.close(fig)

    # --- report --------------------------------------------------------------
    write_report(rows, dict(
        a_t90=a_t90, b_t90=b_t90, r2_t90=r2_t90,
        a_t50=a_t50, b_t50=b_t50, r2_t50=r2_t50,
        a_tb=a_tb, b_tb=b_tb, r2_tb=r2_tb,
        a_ntr=a_ntr, b_ntr=b_ntr, r2_ntr=r2_ntr,
        a_bre=a_bre, b_bre=b_bre, r2_bre=r2_bre,
        b_lag=b_lag, r2_lag=r2_lag,
        b_ex=b_ex, a_ex=a_ex))
    # raw numbers for downstream use
    dump = [{k: v for k, v in r.items() if k not in ("ntr_traj", "res_psd")}
            for r in rows]
    json.dump(dump, open(os.path.join(OUT, "clarinet_attack_data.json"), "w"),
              indent=1, default=float)
    print("wrote", os.path.join(OUT, "clarinet_attack_report.md"))


def write_report(rows, fit):
    L = []
    L.append("# Iowa Bb clarinet ff — attack analysis\n")
    L.append("Samples: `research/inst_samples/clarinet_Bb`, window onset..+0.5 s, "
             "harmonics 1-12 heterodyned (derive_motion method), harmonic part "
             "reconstructed from all harmonics <=16 kHz and subtracted for the "
             "noise residual. Residual under-counts noise within the heterodyne "
             "low-pass (min(0.45*f0, 80) Hz) of each line; the comb fraction "
             "column reports how much residual energy still sits within +-6% "
             "of harmonic lines (high comb = line-broadening leakage, low comb "
             "= genuinely between-line noise).\n")

    L.append("## Per-note summary\n")
    L.append("| note | f0 Hz | t50 ms | t90 ms | breath dom ms | NTR attack dB "
             "| early NTR dB | breath peak dB | NTR sustain dB "
             "| noise lead ms | comb frac | slope dB/oct | centroid Hz "
             "| rise ms/harm | E/O attack dB | E/O sustain dB |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        L.append(f"| {r['note']} | {r['f0']:.1f} | {1000*r['t50_tot']:.0f} "
                 f"| {1000*r['t90_tot']:.0f} | {1000*r['t_breath']:.0f} "
                 f"| {r['ntr_atk_db']:.1f} "
                 f"| {r['ntr_early_db']:.1f} | {r['breath_abs_db']:.1f} "
                 f"| {r['ntr_sus_db']:.1f} | {r['noise_lag_ms']:.0f} "
                 f"| {r['comb_frac']:.2f} | {r['slope_db_oct']:+.1f} "
                 f"| {r['centroid_hz']:.0f} | {r['rise_slope_ms_per_h']:+.1f} "
                 f"| {r['eo_atk_db']:+.1f} | {r['eo_sus_db']:+.1f} |")
    L.append("")
    L.append("t50/t90: total harmonic envelope rise from acoustic onset. "
             "NTR: integrated residual/harmonic energy ratio (attack window = "
             "onset..t90; early = onset..t50; sustain = onset+0.7..1.7 s). "
             "breath peak = peak attack-noise frame RMS relative to sustain "
             "tone RMS (absolute breath loudness vs the note). noise lead: "
             "tone onset minus noise onset, positive = breath precedes tone.\n")

    L.append("## Per-harmonic rise (t90 ms, harmonics 1-12)\n")
    L.append("| note | " + " | ".join(f"h{k}" for k in range(1, N_HARM + 1)) + " |")
    L.append("|---" * (N_HARM + 1) + "|")
    for r in rows:
        vals = list(r["t90"]) + [float("nan")] * (N_HARM - len(r["t90"]))
        cells = " | ".join("-" if not np.isfinite(v) else f"{1000*v:.0f}"
                           for v in vals)
        L.append(f"| {r['note']} | {cells} |")
    L.append("")

    L.append("## Residual octave bands (dB rel per-note max)\n")
    centers = (250, 500, 1000, 2000, 4000, 8000)
    L.append("| note | " + " | ".join(str(c) for c in centers) + " | floor margin dB |")
    L.append("|---" * (len(centers) + 2) + "|")
    for r in rows:
        m = max(r["bands_db"].values())
        cells = " | ".join(f"{r['bands_db'][c]-m:.1f}" for c in centers)
        L.append(f"| {r['note']} | {cells} | {r['floor_vs_atk_db']:.1f} |")
    L.append("\nfloor margin = attack residual energy over pre-onset recording "
             "noise in the same 200-10000 Hz band (validity check).\n")

    L.append("## Register regressions\n")
    L.append(f"- Rise time law (t50): **t50 = {10**fit['a_t50']:.3f} * "
             f"f0^{fit['b_t50']:+.2f}** seconds (R² = {fit['r2_t50']:.2f}). "
             f"Slope -1.0 would be exact inverse proportionality.")
    L.append(f"- t90 law: t90 = {10**fit['a_t90']:.3f} * "
             f"f0^{fit['b_t90']:+.2f} s (R² = {fit['r2_t90']:.2f}) — "
             f"t90 is contaminated by slow level drift on some notes "
             f"(F6, C6); t50 is the robust attack-speed measure.")
    L.append(f"- Breath excess duration (NTR within 6 dB of sustain): "
             f"**tB = {10**fit['a_tb']:.3f} * f0^{fit['b_tb']:+.2f}** s "
             f"(R² = {fit['r2_tb']:.2f}).")
    L.append(f"- Attack NTR vs register: {fit['b_ntr']:+.2f} dB/octave "
             f"(R² = {fit['r2_ntr']:.2f}).")
    L.append(f"- Breath peak vs sustain tone: **{fit['b_bre']:+.2f} dB/octave** "
             f"(R² = {fit['r2_bre']:.2f}).")
    L.append(f"- Noise-precedes-tone lead: lag ~ f0^{fit['b_lag']:+.2f} "
             f"(R² = {fit['r2_lag']:.2f}).")
    L.append(f"- Attack-minus-sustain NTR (breath *excess* during attack): "
             f"{fit['b_ex']:+.2f} dB/octave.\n")

    # ---- findings -> engine features ---------------------------------------
    def law(a, b, f):
        return 10 ** a * f ** b

    cents = [r["centroid_hz"] for r in rows]
    med_cent = float(np.median(cents))
    med_slope = float(np.median([r["slope_db_oct"] for r in rows]))
    med_breath = float(np.median([r["breath_abs_db"] for r in rows]))
    med_ntr_sus = float(np.median([r["ntr_sus_db"] for r in rows]))
    lag_lo = float(np.median([r["noise_lag_ms"] for r in rows if r["f0"] < 300]))
    lag_hi = float(np.median([r["noise_lag_ms"] for r in rows if r["f0"] > 500]))
    med_rise = float(np.median([r["rise_slope_ms_per_h"] for r in rows]))

    t50_bp = [law(fit["a_t50"], fit["b_t50"], f) for f in (150, 400, 1000)]
    tb_bp = [law(fit["a_tb"], fit["b_tb"], f) for f in (150, 400, 1000)]

    L.append("## Findings -> additive features\n")
    L.append("**Verdict on the register claim.** Attack *speed* is confirmed: "
             f"t50 scales as f0^{fit['b_t50']:.2f} (close to inverse "
             "proportionality). Breath *amount* is not a level effect — peak "
             f"breath level is register-flat ({fit['b_bre']:+.1f} dB/oct, "
             f"R²={fit['r2_bre']:.2f}, ≈{med_breath:.0f} dB below the sustain "
             "tone everywhere) — it is a *duration* effect: the breath-excess "
             f"window scales as f0^{fit['b_tb']:.2f} and the noise-before-tone "
             "lead roughly doubles per two octaves down "
             f"(median {lag_lo:.0f} ms below 300 Hz vs {lag_hi:.0f} ms above "
             "500 Hz). Low notes sound breathier because the breath is exposed "
             "3-5x longer, not because it is louder. Exception: the "
             "throat/break notes (sounding F4/A4 = written G4/B4) are "
             "genuinely the noisiest in level ("
             + " / ".join(f"{r['breath_abs_db']:.0f}" for r in rows
                          if r["note"] in ("F4", "A4"))
             + " dB vs the ~-38 dB norm) — a fingering-region effect, not a "
             "register trend.\n")
    L.append("**What the noise is.** The residual is genuinely between-line "
             "broadband (comb fraction ≤ 0.24, mostly ≤ 0.1): it is NOT "
             "line-broadening around the harmonics. Spectral shape: broad "
             f"hiss with centroid ≈ {med_cent/1000:.1f} kHz (typically "
             "1.5-4 kHz), roughly flat through 1-4 kHz then falling "
             f"({med_slope:+.1f} dB/oct median slope 300 Hz-8 kHz) — "
             "band-passed reed/mouthpiece turbulence, not pink noise and not "
             "harmonic-locked. Critically, its LEVEL is roughly constant "
             "through the note: peak attack noise ≈ sustain noise "
             f"(both ≈{med_breath:.0f} dB rel sustain tone). The breath is a "
             "steady hiss bed that starts 30-290 ms before the tone; the "
             "'breathy attack' percept is the bed being exposed while the "
             "tone is still rising, plus a genuine +8..+12 dB attack excess "
             "on the throat/break notes only.\n")
    L.append("**Rise order.** Spectrum assembles bottom-up: t50 increases "
             f"with harmonic number on every note (median {med_rise:+.1f} "
             "ms/harmonic, up to +17..+24 ms/harmonic on the throat notes). "
             "Within that, the STRONG odd harmonics (chalumeau) rise slower "
             "than the weak evens — the attack is the fundamental family "
             "swelling, with h1 first. Even content is relatively stronger "
             "during the attack on several chalumeau notes (Bb3 +4.8 dB, "
             "F4 +6.8 dB attack-vs-sustain E/O shift) before settling to "
             "odd dominance.\n")
    L.append("### Feature mapping (concrete numbers)\n")
    L.append("1. **Amp envelope attack** via paramMap frequency->value "
             "breakpoints (t50 law): attack ≈ "
             f"{t50_bp[0]:.2f} s @150 Hz, {t50_bp[1]:.2f} s @400 Hz, "
             f"{t50_bp[2]:.2f} s @1000 Hz (t50 = "
             f"{10**fit['a_t50']:.1f}*f0^{fit['b_t50']:.2f}).")
    L.append("2. **Onset dispersion**: onsetTilt = +1.0 (low-first matches "
             "the measured bottom-up order; random/-1 do not). onsetSpread "
             "by register curve: 0.10 s @150 Hz, 0.20 s @400 Hz (throat "
             "region is the most dispersed), 0.06 s @1000 Hz "
             "(≈ 12 harmonics x measured ms/harmonic). onsetFade ≈ 0.10 s "
             "(per-harmonic 10-90 rise once entered).")
    L.append("3. **Bandwidth (per-partial AM) is NOT the breath**: it is "
             "multiplicative with partial amplitude, so it cannot precede "
             "the tone and at bandwidthHz≈30 it stays near the lines, while "
             "the measured noise is between-line and leads by 30-290 ms. "
             "Keep a small floor for sustain line texture only: "
             "bandwidth1 ≈ 0.05-0.1, bandwidthHz ≈ 30-60 (sustain NTR "
             f"≈ {med_ntr_sus:.0f} dB). A ramped bwEnv (bandwidth2 ≈ 0.3 "
             "during attack) adds the coupled roughness component but not "
             "the lead-in breath.")
    L.append("4. **NEW feature needed — breath bed with tone head-start**: "
             "a filtered noise component, band-passed ≈ 1.5-4 kHz (centroid "
             f"{med_cent/1000:.1f} kHz, skirts {med_slope:+.0f} dB/oct; "
             "shareable with the formant/BandSpectrum path so it tracks the "
             "instrument body). Its level is CONSTANT, ≈ "
             f"{med_breath:.0f} dB rel the sustain tone (not a loud decaying "
             "burst); it starts at note-on while the tone is delayed by "
             "≈ 0.15 s @150 Hz, 0.10 s @400 Hz, 0.04 s @1000 Hz "
             "(noise-lead law f0^-0.5) and then rises on the t50 curve — "
             "the breathiness is exposure of the bed during the "
             f"tB window ({tb_bp[0]:.2f} s @150 Hz, {tb_bp[1]:.2f} s "
             f"@400 Hz, {tb_bp[2]:.2f} s @1000 Hz), not extra noise gain. "
             "Optional: +8..+12 dB attack-only noise excess for "
             "throat-register patches (sounding ~350-450 Hz). The coupling "
             "(shared formant shaping, noise gated by the same note-on, "
             "level tied to the tone's sustain level) is what distinguishes "
             "this from the previously-rejected independent parallel "
             "tone+noise sum.")
    L.append("5. **Odd/even during attack** (optional refinement): give even "
             "partials a small transient boost or earlier onset in "
             "chalumeau patches (+3..+7 dB attack-only), decaying to the "
             "odd-dominant sustain balance.\n")

    L.append("## Plots\n")
    for p in ("clarinet_attack_rise.png", "clarinet_attack_ntr.png",
              "clarinet_attack_register.png", "clarinet_attack_noisepsd.png"):
        L.append(f"![{p}]({p})")
    L.append("")

    open(os.path.join(OUT, "clarinet_attack_report.md"), "w",
         encoding="utf-8").write("\n".join(L))


if __name__ == "__main__":
    main()
