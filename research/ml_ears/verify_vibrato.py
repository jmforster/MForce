"""Verify the vibrato is real: track f0(t) of the rendered instrument and
measure rate / depth / attack-ramp on the D4 note. Pure measurement, no ear."""
import os, numpy as np, soundfile as sf, librosa

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WAV = os.path.join(REPO, "renders", "warmstart", "viola_instrument.wav")
OUT = os.path.join(os.path.dirname(__file__), "out", "vibrato_f0.png")

y, sr = sf.read(WAV)
if y.ndim > 1: y = y.mean(axis=1)

hop = 256
f0 = librosa.yin(y, fmin=80, fmax=700, sr=sr, frame_length=2048, hop_length=hop)
t = np.arange(len(f0)) * hop / sr
fr = sr / hop  # f0 frame rate

# D4 note plays at score time 4.0..5.5; analyze its steady region 4.2..5.3
def note_stats(t0, t1, label):
    m = (t >= t0) & (t <= t1)
    seg = f0[m]
    seg = seg[np.isfinite(seg) & (seg > 0)]
    if len(seg) < 10:
        print(f"{label}: no pitch"); return
    mean = np.median(seg)
    d = seg - np.mean(seg)
    F = np.abs(np.fft.rfft(d))
    ff = np.fft.rfftfreq(len(d), 1.0 / fr)
    band = (ff >= 3) & (ff <= 9)
    rate = ff[band][np.argmax(F[band])] if np.any(band) else 0
    depth_pct = (np.percentile(seg, 95) - np.percentile(seg, 5)) / mean / 2 * 100
    print(f"{label}: f0~{mean:6.1f}Hz  vibrato rate~{rate:.2f}Hz  depth~±{depth_pct:.2f}%")

print("Per-note pitch (vibrato should show rate~5.5Hz, depth~±2.5%):")
note_stats(4.2, 5.3, "D4 full ")
note_stats(4.15, 4.55, "D4 early")   # during/after attack ramp
note_stats(4.9, 5.3, "D4 late ")     # fully ramped

# plot the whole f0 contour
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, ax = plt.subplots(figsize=(12, 4))
ff = f0.copy(); ff[~np.isfinite(ff) | (ff <= 0)] = np.nan
ax.plot(t, ff, lw=0.7, color="#4a90d9")
ax.set_ylim(100, 480); ax.set_xlabel("time (s)"); ax.set_ylabel("f0 (Hz)")
ax.set_title("Rendered viola f0(t) — vibrato wobble per note (C3/G3/D4/A4)")
fig.tight_layout(); fig.savefig(OUT, dpi=120)
print("f0 plot:", OUT)
