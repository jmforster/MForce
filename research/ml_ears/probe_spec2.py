"""Spectrogram the real failing renders to SEE where the bandwidth noise sits."""
import os, numpy as np, soundfile as sf
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt

REND = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..",
                                    "renders", "warmstart", "attack_sets"))
files = ["probe_vib_off.wav", "catch_zero_highest.wav"]
fig, axes = plt.subplots(len(files), 1, figsize=(11, 7))
for ax, fn in zip(axes, files):
    y, sr = sf.read(os.path.join(REND, fn))
    if y.ndim > 1: y = y.mean(axis=1)
    win, hop = 2048, 256
    fr = range(0, len(y)-win, hop)
    S = np.array([20*np.log10(np.abs(np.fft.rfft(y[i:i+win]*np.hanning(win)))+1e-6) for i in fr]).T
    f = np.fft.rfftfreq(win, 1/sr); t = np.arange(S.shape[1])*hop/sr
    m = f < 6000
    ax.imshow(S[m], aspect="auto", origin="lower", extent=[0, t[-1], 0, 6000], vmin=-65, vmax=0, cmap="magma")
    ax.set_title(fn + "  (noise should be at LEFT/start if bandwidth ramp is correct)")
    ax.set_ylabel("Hz")
axes[-1].set_xlabel("time (s)")
fig.tight_layout()
png = os.path.join(REND, "probe_spec2.png"); fig.savefig(png, dpi=120); print("plot:", png)
