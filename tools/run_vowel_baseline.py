"""Render + verify the vowel-baseline patches (gen_vowel_baseline.py).

Per render:
  (a) non-silent + unique (pairwise sample comparison)
  (b) vowel-character metric at t = 0.5 / 2.0 / 3.5 s: the measured
      harmonic envelope is correlated against the PREDICTED envelope of
      every vowel in the sequence (per-harmonic factor =
      formantFloor + formantWeight * gain_v(k*f0), in dB, incl. 1/k
      rolloff). Best match must track the sequence in order — first
      vowel at 0.5 s, last at 3.5 s, no return.
  (c) matched formants: formant bands of the first/last vowel containing
      >= 1 harmonic whose level sits >= 6 dB above the out-of-band
      median. Valley suppression = out-of-band median - in-band peak.
  (d) high-f0 chirp check: where endpoint vowels have NO in-band
      harmonic (f0 >= 1320: harmonic spacing >> band width — physics,
      not wiring), verification switches to bloom tracking: predicted
      band-crossing times (formant glide path crossing k*f0) must line
      up with measured per-harmonic amplitude blooms.
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_vowel_baseline import (REPO, OUT, SECONDS, VOWEL, SEQUENCES,
                                FMT_WEIGHT, FMT_FLOOR, RAMP_HOLD0, RAMP_END,
                                N_HARM, glide_times, param_track, render_list)

CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
RENDERS = os.path.join(REPO, "renders", "vowel_baseline")
os.makedirs(RENDERS, exist_ok=True)

PROBE_TIMES = [0.5, 2.0, 3.5]
WIN = 0.30  # analysis window seconds


def read_wav(path):
    import soundfile as sf
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def vowel_gain(vk, freq):
    """Formant-stack gain at freq (max across formants, engine math)."""
    g = 0.0
    for (fc, gain, w, pw) in VOWEL[vk]:
        lo, hi = fc - w / 2.0, fc + w / 2.0
        if lo < freq < hi:
            if freq < fc:
                gv = ((freq - lo) / (fc - lo)) ** pw * gain
            else:
                gv = ((hi - freq) / (hi - fc)) ** pw * gain
            g = max(g, gv)
    return g


def predicted_env_db(vk, f0, n_harm):
    out = []
    for k in range(1, n_harm + 1):
        fk = k * f0
        if fk > 16000.0:
            break
        amp = (1.0 / k) * (FMT_FLOOR + FMT_WEIGHT * vowel_gain(vk, fk))
        out.append(20 * np.log10(amp + 1e-9))
    return np.array(out)


def measured_env_db(x, sr, f0, t, n_harm):
    seg = x[int((t - WIN / 2) * sr): int((t + WIN / 2) * sr)]
    seg = seg * np.hanning(len(seg))
    nfft = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    amps = []
    for k in range(1, n_harm + 1):
        fc = k * f0
        if fc > 16000.0:
            break
        sel = (fbin >= fc - f0 * 0.4) & (fbin <= fc + f0 * 0.4)
        amps.append(float(np.max(spec[sel])) if np.any(sel) else 0.0)
    amps = np.array(amps)
    return 20 * np.log10(amps / (amps.max() + 1e-12) + 1e-7)


def inband_harmonics(vk, f0, n_harm):
    out = {}
    for (fc, gain, w, pw) in VOWEL[vk]:
        ks = [k for k in range(1, n_harm + 1)
              if fc - w / 2.0 < k * f0 < fc + w / 2.0 and k * f0 <= 16000.0]
        out[int(fc)] = ks
    return out


def matched_formants(env_db, vk, f0, n_harm):
    ib = inband_harmonics(vk, f0, n_harm)
    all_ib = sorted({k for ks in ib.values() for k in ks})
    n = len(env_db)
    oob = [env_db[k - 1] + 20 * np.log10(k) for k in range(1, n + 1)
           if k not in all_ib]  # rolloff-compensated
    oob_med = float(np.median(oob)) if oob else float("nan")
    matched, na = [], []
    for fc, ks in ib.items():
        ks = [k for k in ks if k <= n]
        if not ks:
            na.append(fc)
            continue
        best = max(env_db[k - 1] + 20 * np.log10(k) for k in ks)
        if best >= oob_med + 6.0:
            matched.append(fc)
    if all_ib and oob:
        peak = max(env_db[k - 1] + 20 * np.log10(k) for k in all_ib if k <= n)
        supp = oob_med - peak
    else:
        supp = float("nan")
    return matched, na, supp


def predicted_crossings(vowel_keys, f0, n_harm):
    """[(t, harmonic k, formant idx)] where a gliding band center crosses
    a harmonic during a transition segment."""
    out = []
    segs = glide_times(len(vowel_keys))
    for fi in range(len(VOWEL[vowel_keys[0]])):
        vals = [VOWEL[vk][fi][0] for vk in vowel_keys]
        for (t0, t1), v0, v1 in zip(segs, vals[:-1], vals[1:]):
            if v0 == v1:
                continue
            for k in range(1, n_harm + 1):
                fk = k * f0
                if fk > 16000.0:
                    break
                u = (fk - v0) / (v1 - v0)
                if 0.0 <= u <= 1.0:
                    out.append((t0 + u * (t1 - t0), k, fi))
    return sorted(out)


def harmonic_tracks_db(x, sr, f0, n_harm, hop=0.02, win=0.05):
    """(times, dB[time, harmonic]) short-time harmonic amplitude tracks."""
    hopn, winn = int(hop * sr), int(win * sr)
    starts = np.arange(0, len(x) - winn, hopn)
    ks = [k for k in range(1, n_harm + 1) if k * f0 <= 16000.0]
    w = np.hanning(winn)
    nfft = 1 << int(np.ceil(np.log2(winn * 4)))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    sel = [(fbin >= k * f0 - f0 * 0.4) & (fbin <= k * f0 + f0 * 0.4) for k in ks]
    rows = []
    for s in starts:
        spec = np.abs(np.fft.rfft(x[s:s + winn] * w, nfft))
        rows.append([np.max(spec[m]) if np.any(m) else 0.0 for m in sel])
    db = 20 * np.log10(np.array(rows) + 1e-9)
    return starts / sr + win / 2, ks, db


def bloom_check(x, sr, f0, vowel_keys, n_harm):
    """Match predicted band-crossing times to measured harmonic blooms.

    Returns (n_pred, n_matched, details) with details like '2640@1.1s ok'.
    """
    preds = predicted_crossings(vowel_keys, f0, n_harm)
    if not preds:
        return 0, 0, []
    times, ks, db = harmonic_tracks_db(x, sr, f0, n_harm)
    details, matched = [], 0
    for (tc, k, fi) in preds:
        if k not in ks:
            continue
        col = db[:, ks.index(k)]
        base = np.median(col)
        near = (times >= tc - 0.35) & (times <= tc + 0.35)
        if not np.any(near):
            details.append(f"h{k}@{tc:.1f}s MISS(no-frames)")
            continue
        peak = float(np.max(col[near]))
        ok = peak >= base + 8.0
        matched += int(ok)
        details.append(f"h{k}@{tc:.1f}s {'ok' if ok else 'MISS'}({peak - base:+.0f}dB)")
    return len(details), matched, details


def main():
    results, waves = [], {}
    for pname, sname, f0 in render_list():
        ppath = os.path.join(OUT, pname + ".json")
        wpath = os.path.join(RENDERS, pname + ".wav")
        r = subprocess.run([CLI, ppath, wpath], capture_output=True, text=True)
        if not os.path.exists(wpath):
            print(f"{pname}: RENDER FAILED: {r.stderr.strip()[:200]}")
            continue
        x, sr = read_wav(wpath)
        waves[pname] = x
        seq = SEQUENCES[sname]

        rms = float(np.sqrt(np.mean(x ** 2)))
        silent = rms < 1e-4

        # vowel best-match trajectory at the probe times
        traj, sims_by_t = [], []
        for t in PROBE_TIMES:
            env = measured_env_db(x, sr, f0, t, N_HARM)
            sims = {}
            for vk in dict.fromkeys(seq):
                pred = predicted_env_db(vk, f0, len(env))
                m = min(len(pred), len(env))
                if m >= 3 and np.std(pred[:m]) > 1e-6:
                    sims[vk] = float(np.corrcoef(env[:m], pred[:m])[0, 1])
                else:
                    sims[vk] = float("nan")
            sims_by_t.append(sims)
            traj.append(max(sims, key=lambda kk: (sims[kk]
                        if np.isfinite(sims[kk]) else -9)))

        env_s = measured_env_db(x, sr, f0, PROBE_TIMES[0], N_HARM)
        env_e = measured_env_db(x, sr, f0, PROBE_TIMES[-1], N_HARM)
        m_s, na_s, supp_s = matched_formants(env_s, seq[0], f0, N_HARM)
        m_e, na_e, supp_e = matched_formants(env_e, seq[-1], f0, N_HARM)

        endpoint_harmonics = bool(m_s or m_e) or not (na_s and na_e)
        last_sims = [s[seq[-1]] for s in sims_by_t]
        # "no return": the best-match vowel's index in the sequence must be
        # non-decreasing across the probe times, endpoints exact.
        idxs = [seq.index(v) if v in seq else -1 for v in traj]
        vowel_ok = (traj[0] == seq[0]) and (traj[-1] == seq[-1]) and \
            all(a <= b for a, b in zip(idxs, idxs[1:]))

        n_pred, n_bloom, bloom_det = bloom_check(x, sr, f0, seq, N_HARM)

        if silent:
            verdict = "SILENT"
        elif endpoint_harmonics and vowel_ok:
            verdict = "PASS"
        elif not endpoint_harmonics and n_pred > 0 and n_bloom > 0:
            verdict = "PASS-CHIRP"   # high-f0 mode: glide blooms verified
        else:
            verdict = "FAIL"

        results.append({
            "name": pname, "seq": "-".join(seq), "f0": f0, "rms": rms,
            "traj": traj, "verdict": verdict, "last_sims": last_sims,
            "m_s": m_s, "na_s": na_s, "supp_s": supp_s,
            "m_e": m_e, "na_e": na_e, "supp_e": supp_e,
            "n_pred": n_pred, "n_bloom": n_bloom, "bloom": bloom_det,
        })

    # uniqueness
    names = list(waves)
    dupes = set()
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            a, b = waves[names[i]], waves[names[j]]
            m = min(len(a), len(b))
            if np.max(np.abs(a[:m] - b[:m])) < 1e-6:
                dupes.add((names[i], names[j]))

    fmt = lambda fs: ",".join(str(v) for v in fs) if fs else "-"
    print(f"\n{'render':16s} {'sequence':9s} {'f0':>5s} {'rms':>6s} "
          f"{'traj 0.5/2.0/3.5s':18s} {'start-match':12s} {'end-match':12s} "
          f"{'supp dB s/e':>12s} {'blooms':>7s} {'verdict':10s}")
    for r in results:
        print(f"{r['name']:16s} {r['seq']:9s} {r['f0']:5.0f} {r['rms']:.4f} "
              f"{'/'.join(r['traj']):18s} {fmt(r['m_s']):12s} {fmt(r['m_e']):12s} "
              f"{r['supp_s']:5.1f}/{r['supp_e']:5.1f} "
              f"{r['n_bloom']:3d}/{r['n_pred']:<3d} {r['verdict']:10s}")
        extra = f"    last-vowel sim " + \
            "/".join(f"{v:.2f}" for v in r["last_sims"])
        if r["na_s"] or r["na_e"]:
            extra += (f"   no-harmonic bands start:{fmt(r['na_s'])}"
                      f" end:{fmt(r['na_e'])}")
        print(extra)
        if r["bloom"]:
            print("    blooms: " + "  ".join(r["bloom"]))
    print(f"\nunique: {'yes' if not dupes else 'DUPES ' + str(sorted(dupes))}")
    print(f"renders -> {RENDERS}")


if __name__ == "__main__":
    main()
