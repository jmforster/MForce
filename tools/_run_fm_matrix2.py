"""Render + verify the fm_matrix2 bracket/ramp batch (see gen_fm_matrix2.py).

Renders patches/fm_matrix2/{modulated,ramped}/ to the mirrored folders under
renders/fm_matrix2/, then verifies mechanically:

  1. every render non-silent (peak > 0.01)
  2. every render unique (md5)
  3. spot-check 3 patches: lo/med/hi order by measured effect magnitude
     (mean spectral bandwidth + centroid sd over time)
  4. spot-check ramped renders: the effect dies by the specified ramp time
     (spectral-flux decay time vs 0.1 / 0.2 / 0.5 s)
"""
import glob
import hashlib
import os
import re
import subprocess
import wave

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PATCHES = os.path.join(REPO, "patches", "fm_matrix2")
OUT = os.path.join(REPO, "renders", "fm_matrix2")

RE_PEAK = re.compile(r"peak=([\d.eE+-]+) rms=([\d.eE+-]+)")

SPOT_MOD = ["t2_11_all_noise_wander", "t3_19_fm_drives_fm_depth",
            "t3_20_index60_aliased"]
SPOT_RAMP = ["t1_03_mratio_audiorate", "t2_12_both_ratios_audiorate",
             "t3_20_index60_aliased"]
RAMP_T = {"ramp01": 0.1, "ramp02": 0.2, "ramp05": 0.5}


def render_all():
    results = {}
    for sub in ("modulated", "ramped"):
        dst = os.path.join(OUT, sub)
        os.makedirs(dst, exist_ok=True)
        patches = sorted(glob.glob(os.path.join(PATCHES, sub, "*.json")))
        print(f"rendering {len(patches)} {sub} patches...")
        for pj in patches:
            name = os.path.splitext(os.path.basename(pj))[0]
            wav = os.path.join(dst, name + ".wav")
            r = subprocess.run([CLI, pj, wav], capture_output=True, text=True)
            m = RE_PEAK.search(r.stderr)
            peak = float(m.group(1)) if m else 0.0
            rms = float(m.group(2)) if m else 0.0
            ok = r.returncode == 0 and os.path.exists(wav)
            flag = "OK " if ok else "ERR"
            if ok and peak >= 0.999:
                flag = "CLIP"
            if ok and peak <= 0.01:
                flag = "SILENT"
            print(f"  {flag:6s} {sub}/{name:44s} peak={peak:.3f} rms={rms:.4f}")
            if not ok:
                print(f"         stderr: {r.stderr.strip()[:300]}")
            results[f"{sub}/{name}"] = (wav, peak, rms, ok)
    return results


def read_wav_mono(path):
    w = wave.open(path, "rb")
    sr = w.getframerate()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    a = a.astype(np.float32) / 32768.0
    a = a.reshape(-1, w.getnchannels()).mean(axis=1)
    w.close()
    return a, sr


def stft_mag(a, sr, nfft=2048, hop=512):
    win = np.hanning(nfft)
    nfrm = max(1, (len(a) - nfft) // hop)
    M = np.empty((nfrm, nfft // 2 + 1), dtype=np.float32)
    for i in range(nfrm):
        M[i] = np.abs(np.fft.rfft(a[i * hop:i * hop + nfft] * win))
    t = (np.arange(nfrm) * hop + nfft / 2) / sr
    f = np.fft.rfftfreq(nfft, 1.0 / sr)
    return M, t, f


def effect_magnitude(path):
    """(mean spectral bandwidth Hz, centroid sd Hz, mean spectral entropy)
    over energy-bearing frames."""
    a, sr = read_wav_mono(path)
    M, t, f = stft_mag(a, sr)
    e = M.sum(axis=1)
    keep = e > 0.01 * e.max()   # drop the near-silent release tail
    M = M[keep]
    p = M / (M.sum(axis=1, keepdims=True) + 1e-12)
    cen = (p * f).sum(axis=1)
    bw = np.sqrt((p * (f - cen[:, None]) ** 2).sum(axis=1))
    P = M ** 2
    pp = P / (P.sum(axis=1, keepdims=True) + 1e-12)
    ent = (-pp * np.log(pp + 1e-12)).sum(axis=1)
    return float(bw.mean()), float(cen.std()), float(ent.mean())


def flux_decay_time(path):
    """Time (s) after which normalized spectral flux stays below 10% of its
    early-window mean. Flux = sum |d p| between consecutive L1-normalized
    spectra, i.e. spectral-shape change per frame — a static tone -> ~0.
    Frames below 2% of max energy are excluded (normalized spectra of the
    near-silent release tail are quantization noise, not effect)."""
    a, sr = read_wav_mono(path)
    M, t, f = stft_mag(a, sr, nfft=1024, hop=256)
    e = M.sum(axis=1)
    p = M / (e[:, None] + 1e-12)
    flux = np.abs(np.diff(p, axis=0)).sum(axis=1)
    ft = t[1:]
    gate = e[1:] > 0.02 * e.max()
    flux, ft = flux[gate], ft[gate]
    early = flux[(ft >= 0.01) & (ft <= 0.08)]
    thresh = 0.10 * early.mean()
    above_idx = np.where(flux >= thresh)[0]
    if len(above_idx) == 0:
        return 0.0, thresh
    return float(ft[above_idx[-1]]), float(thresh)


def main():
    results = render_all()

    # --- 1. non-silence ---
    silent = [k for k, (_, pk, _, ok) in results.items() if not ok or pk <= 0.01]
    n = len(results)
    print(f"\n[1] non-silent: {n - len(silent)}/{n} pass"
          + (f"  FAILED: {silent}" if silent else ""))

    # --- 2. uniqueness ---
    md5s = {}
    for k, (wav, _, _, ok) in results.items():
        if ok:
            md5s[k] = hashlib.md5(open(wav, "rb").read()).hexdigest()
    seen, dupes = {}, []
    for k, h in md5s.items():
        if h in seen:
            dupes.append((seen[h], k))
        seen[h] = k
    print(f"[2] unique md5: {len(md5s) - len(dupes)}/{len(md5s)} unique"
          + (f"  DUPES: {dupes}" if dupes else ""))

    # --- 3. lo/med/hi ordering on spot-check patches ---
    print("\n[3] bracket ordering (spectral bandwidth / centroid sd / entropy):")
    for base in SPOT_MOD:
        vals = {}
        for lvl in ("lo", "med", "hi"):
            wav = os.path.join(OUT, "modulated", f"{base}_{lvl}.wav")
            vals[lvl] = effect_magnitude(wav)
        ordered = [i for i, m in enumerate(("bw", "censd", "entropy"))
                   if vals["lo"][i] < vals["med"][i] < vals["hi"][i]]
        verdict = "ORDERED" if ordered else "NOT-ORDERED"
        print(f"  {base}: {verdict}")
        for lvl in ("lo", "med", "hi"):
            print(f"      {lvl:3s} bw={vals[lvl][0]:8.1f}  censd={vals[lvl][1]:8.1f}"
                  f"  entropy={vals[lvl][2]:6.3f}")

    # --- 4. ramp die-time on spot-check patches ---
    print("\n[4] ramp effect-death (spectral flux < 10% of early mean):")
    for base in SPOT_RAMP:
        for tag, T in RAMP_T.items():
            wav = os.path.join(OUT, "ramped", f"{base}_{tag}.wav")
            died, thr = flux_decay_time(wav)
            # allow ~0.1 s of analysis smear (1024-pt frames, decay tails)
            ok = died <= T + 0.12
            print(f"  {base}_{tag}: effect dead by {died:.3f}s "
                  f"(spec {T:.1f}s) {'PASS' if ok else 'FAIL'}")

    print(f"\nrenders -> {OUT}")


if __name__ == "__main__":
    main()
