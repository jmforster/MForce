"""Compare the YouTube oboe reference against the oboe_default OTJ render.
Outputs: pitch/key recon, long-term average spectrum + formant peaks,
harmonic envelope on the longest sustained matched note, vibrato rate/
depth, attack rise times, inter-harmonic noise."""
import numpy as np
import scipy.signal as sig
import scipy.io.wavfile as wavfile
import sys

SCRATCH = r"C:\Users\MATTFO~1\AppData\Local\Temp\claude\C---dev-repos-mforce\fc801209-1475-4d6c-9cee-e7caf210f88d\scratchpad"
REF = SCRATCH + r"\oboe_ref.wav"
REN = r"C:\@dev\repos\mforce\renders\scratch\otj_oboe_eb6.wav"


def load(path):
    sr, d = wavfile.read(path)
    if d.dtype != np.float32 and d.dtype != np.float64:
        d = d.astype(np.float64) / np.iinfo(d.dtype).max
    if d.ndim > 1:
        d = d.mean(axis=1)
    return sr, d


def pitch_track(sr, x, fmin=150.0, fmax=2000.0, hop=0.01, win=0.04):
    """Autocorrelation pitch track. Returns times, f0 (nan = unvoiced)."""
    H, W = int(hop * sr), int(win * sr)
    lo, hi = int(sr / fmax), int(sr / fmin)
    ts, f0s = [], []
    for i in range(0, len(x) - W, H):
        fr = x[i:i + W]
        if np.sqrt(np.mean(fr ** 2)) < 1e-3:
            ts.append(i / sr); f0s.append(np.nan); continue
        fr = fr - fr.mean()
        ac = sig.correlate(fr, fr, mode="full")[W - 1:]
        ac /= (ac[0] + 1e-12)
        seg = ac[lo:hi]
        pk = np.argmax(seg) + lo
        # parabolic refine
        if 1 <= pk < len(ac) - 1:
            a, b, c = ac[pk - 1], ac[pk], ac[pk + 1]
            d = 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
            pk = pk + np.clip(d, -1, 1)
        f0 = sr / pk
        ts.append(i / sr)
        f0s.append(f0 if ac[int(round(pk))] > 0.4 else np.nan)
    return np.array(ts), np.array(f0s)


def ltas(sr, x, nfft=8192):
    f, p = sig.welch(x, sr, nperseg=nfft, noverlap=nfft // 2)
    return f, 10 * np.log10(p + 1e-18)


def spectral_envelope_peaks(f, db, lo=400, hi=6000, smooth_hz=180):
    """Smooth the LTAS into an envelope and report peaks (formant-ish)."""
    m = (f >= lo) & (f <= hi)
    fs_, db_ = f[m], db[m]
    kw = max(3, int(smooth_hz / (f[1] - f[0])) | 1)
    env = sig.savgol_filter(db_, kw, 2)
    pk, props = sig.find_peaks(env, prominence=1.0)
    return fs_, env, [(fs_[i], env[i], props["prominences"][j])
                      for j, i in enumerate(pk)]


def sustained_segments(ts, f0s, min_len=0.35, cents_tol=60):
    """Group voiced frames into near-constant-pitch runs."""
    segs = []
    i = 0
    n = len(f0s)
    while i < n:
        if np.isnan(f0s[i]):
            i += 1; continue
        j = i + 1
        while j < n and not np.isnan(f0s[j]) and \
                abs(1200 * np.log2(f0s[j] / f0s[i])) < cents_tol:
            j += 1
        if ts[j - 1] - ts[i] >= min_len:
            segs.append((ts[i], ts[j - 1], np.nanmedian(f0s[i:j])))
        i = j
    return segs


def harmonic_env(sr, x, t0, t1, f0, nh=20):
    """Harmonic amplitudes (dB) over [t0,t1] via Goertzel-style DFT probes."""
    seg = x[int(t0 * sr):int(t1 * sr)]
    seg = seg * np.hanning(len(seg))
    N = len(seg)
    out = []
    fft = np.fft.rfft(seg)
    freqs = np.fft.rfftfreq(N, 1 / sr)
    for h in range(1, nh + 1):
        fh = f0 * h
        if fh > sr / 2 - 100:
            break
        idx = np.argmin(np.abs(freqs - fh))
        w = 3
        amp = np.abs(fft[max(0, idx - w):idx + w + 1]).max()
        out.append(20 * np.log10(amp / N + 1e-12))
    return np.array(out)


def interharmonic_noise(sr, x, t0, t1, f0):
    """Median level between harmonics vs median harmonic level (dB gap)."""
    seg = x[int(t0 * sr):int(t1 * sr)]
    seg = seg * np.hanning(len(seg))
    N = len(seg)
    fft = 20 * np.log10(np.abs(np.fft.rfft(seg)) / N + 1e-12)
    freqs = np.fft.rfftfreq(N, 1 / sr)
    harm, mid = [], []
    for h in range(1, 15):
        fh, fm = f0 * h, f0 * (h + 0.5)
        if fm > 8000:
            break
        harm.append(fft[np.argmin(np.abs(freqs - fh)) - 3:
                        np.argmin(np.abs(freqs - fh)) + 4].max())
        i = np.argmin(np.abs(freqs - fm))
        mid.append(np.median(fft[i - 5:i + 6]))
    return np.median(harm), np.median(mid)


def vibrato(ts, f0s, t0, t1):
    m = (ts >= t0) & (ts <= t1) & ~np.isnan(f0s)
    if m.sum() < 30:
        return None
    f = f0s[m]
    cents = 1200 * np.log2(f / np.median(f))
    cents -= sig.savgol_filter(cents, min(31, len(cents) // 2 * 2 - 1), 2)
    sp = np.abs(np.fft.rfft(cents * np.hanning(len(cents))))
    fr = np.fft.rfftfreq(len(cents), d=ts[1] - ts[0])
    band = (fr >= 3) & (fr <= 9)
    if not band.any() or sp[band].max() < 3 * np.median(sp[fr > 1]):
        return (0.0, float(np.std(cents)))
    rate = fr[band][np.argmax(sp[band])]
    # depth: RMS of the vibrato band * sqrt2 ~ peak cents
    depth = np.std(cents) * 1.414
    return (float(rate), float(depth))


def attack_time(sr, x, t_on, f0):
    """10-90% rise of the amplitude envelope after onset."""
    seg = np.abs(x[int(t_on * sr):int((t_on + 0.6) * sr)])
    env = sig.savgol_filter(seg, 481, 2)
    pk = env.max()
    try:
        i10 = np.argmax(env > 0.1 * pk)
        i90 = np.argmax(env > 0.9 * pk)
        return (i90 - i10) / sr
    except Exception:
        return None


def analyze(name, sr, x):
    print(f"=== {name}: {len(x)/sr:.1f}s @ {sr} Hz, rms {np.sqrt(np.mean(x**2)):.4f}")
    ts, f0s = pitch_track(sr, x)
    v = f0s[~np.isnan(f0s)]
    if len(v):
        print(f"  pitch range: {np.percentile(v,5):.0f}-{np.percentile(v,95):.0f} Hz, median {np.median(v):.0f}")
    segs = sustained_segments(ts, f0s)
    print(f"  sustained segments: {len(segs)}")
    for t0, t1, f0 in segs[:12]:
        print(f"    {t0:6.2f}-{t1:6.2f}s  {f0:7.1f} Hz  ({1200*np.log2(f0/440):+5.0f} cents vs A4)")
    return ts, f0s, segs


def main():
    srR, ref = load(REF)
    srN, ren = load(REN)
    tsR, f0R, segR = analyze("REFERENCE (YouTube)", srR, ref)
    tsN, f0N, segN = analyze("RENDER (oboe_default OTJ)", srN, ren)

    print("\n=== LTAS spectral-envelope peaks (formant picture) ===")
    for nm, sr, x in (("ref", srR, ref), ("render", srN, ren)):
        f, db = ltas(sr, x)
        _, _, peaks = spectral_envelope_peaks(f, db)
        tops = sorted(peaks, key=lambda p: -p[2])[:5]
        tops = sorted(tops)
        print(f"  {nm}: " + "; ".join(f"{p[0]:.0f} Hz ({p[1]:.1f} dB, prom {p[2]:.1f})" for p in tops))

    # pick the longest sustained seg of each, closest in pitch
    if segR and segN:
        best = None
        for a in segR:
            for b in segN:
                dc = abs(1200 * np.log2(a[2] / b[2]))
                dur = min(a[1] - a[0], b[1] - b[0])
                score = dc - 40 * dur
                if best is None or score < best[0]:
                    best = (score, a, b)
        _, a, b = best
        print(f"\n=== matched sustained notes: ref {a[2]:.1f} Hz ({a[1]-a[0]:.2f}s) vs render {b[2]:.1f} Hz ({b[1]-b[0]:.2f}s) ===")
        hR = harmonic_env(srR, ref, a[0], a[1], a[2])
        hN = harmonic_env(srN, ren, b[0], b[1], b[2])
        n = min(len(hR), len(hN))
        hR, hN = hR[:n] - hR[0], hN[:n] - hN[0]   # normalize to H1
        print("  harmonic (dB rel H1):  " + " ".join(f"{i+1}" for i in range(n)))
        print("    ref:    " + " ".join(f"{v:6.1f}" for v in hR))
        print("    render: " + " ".join(f"{v:6.1f}" for v in hN))
        print("    delta:  " + " ".join(f"{v:6.1f}" for v in (hN - hR)))
        hlR, mR = interharmonic_noise(srR, ref, a[0], a[1], a[2])
        hlN, mN = interharmonic_noise(srN, ren, b[0], b[1], b[2])
        print(f"  harmonic-to-interharmonic gap: ref {hlR-mR:.1f} dB, render {hlN-mN:.1f} dB")
        vR = vibrato(tsR, f0R, a[0], a[1])
        vN = vibrato(tsN, f0N, b[0], b[1])
        print(f"  vibrato: ref {vR}, render {vN}  (rate Hz, ~peak cents)")
        aR = attack_time(srR, ref, a[0], a[2])
        aN = attack_time(srN, ren, b[0], b[2])
        print(f"  attack 10-90% on matched note: ref {aR*1000 if aR else -1:.0f} ms, render {aN*1000 if aN else -1:.0f} ms")


if __name__ == "__main__":
    main()
