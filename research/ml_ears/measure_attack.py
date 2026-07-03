"""Measure the time-course of 'noisiness' (spectral flatness) across the D4 note
of the bandwidth-attack render, to see objectively whether bandwidth goes
chaos->order (decreasing) or order->chaos (increasing)."""
import os, numpy as np, soundfile as sf

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart")

def flatness_over_time(wav, t0, t1, win=0.05):
    y, sr = sf.read(wav)
    if y.ndim > 1: y = y.mean(axis=1)
    n = int(win * sr); hop = n // 2
    out_t, out_f = [], []
    i = int(t0 * sr)
    while i + n < int(t1 * sr):
        seg = y[i:i+n] * np.hanning(n)
        sp = np.abs(np.fft.rfft(seg)) + 1e-9
        # spectral flatness = geometric mean / arithmetic mean (0=tonal, 1=noise)
        gm = np.exp(np.mean(np.log(sp))); am = np.mean(sp)
        out_t.append(i / sr); out_f.append(gm / am)
        i += hop
    return np.array(out_t), np.array(out_f)

# D4 note plays t=4.0..5.5 in the phrase
for name in ["viola_full_bwattack", "viola_full_bwmid", "viola_instrument"]:
    wav = os.path.join(REND, name + ".wav")
    if not os.path.exists(wav):
        print(name, "missing"); continue
    t, f = flatness_over_time(wav, 4.0, 5.45)
    rel = t - 4.0
    early = np.mean(f[(rel > 0.0) & (rel < 0.25)])
    late  = np.mean(f[(rel > 0.8) & (rel < 1.4)])
    trend = "CHAOS->order (noise falls)" if early > late * 1.15 else \
            "order->CHAOS (noise rises)" if late > early * 1.15 else "flat"
    print(f"{name:22s} early_flatness={early:.4f} late={late:.4f}  -> {trend}")
