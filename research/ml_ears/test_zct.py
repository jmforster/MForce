"""Isolate zeroCrossTendency: render the SAME note with zct=0 vs zct=1, all
other jitter OFF (speedVar=depthVar=0). If zct works, zct=1 is a clean regular
wobble and zct=0 is irregular (same-sign runs). If they're identical, zct isn't
being applied."""
import os, json, subprocess
import numpy as np, soundfile as sf, librosa

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

def patch(zct):
    return {
        "sampleRate": 48000,
        "graph": {"nodes": [
            {"id": "ampEnv", "type": "Envelope",
             "params": {"preset": "adsr", "attack": 0.05, "decay": 0.1,
                        "sustainLevel": 1.0, "release": 0.1}},
            {"id": "parts", "type": "ExplicitPartials",
             "params": {"mult1": [1, 2, 3], "mult2": [1, 2, 3],
                        "ampl1": [1.0, 0.3, 0.1], "ampl2": [1.0, 0.3, 0.1],
                        "rolloff1": 0.0, "rolloff2": 0.0}},
            {"id": "vib", "type": "Vibrato",
             "params": {"frequency": 293.66, "speed": 5.5, "depth": 0.04,
                        "attack": 0.1, "speedVar": 0.0, "depthVar": 0.0,
                        "zeroCrossTendency": zct}},
            {"id": "tone", "type": "AdditiveSource",
             "params": {"seed": 1, "frequency": {"ref": "vib"},
                        "amplitude": {"ref": "ampEnv"}, "partials": {"ref": "parts"}}},
        ], "output": "tone"},
        "instrument": {"paramMap": {"frequency": "vib.frequency"}, "polyphony": 1},
        "score": [{"note": 62, "velocity": 0.8, "time": 0.0, "duration": 3.0}],
    }

def f0_of(wav):
    y, sr = sf.read(wav)
    if y.ndim > 1: y = y.mean(axis=1)
    hop = 256
    f0 = librosa.yin(y, fmin=200, fmax=400, sr=sr, frame_length=2048, hop_length=hop)
    t = np.arange(len(f0)) * hop / sr
    m = (t > 0.4) & (t < 2.9)
    return t[m], f0[m]

import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, ax = plt.subplots(figsize=(12, 4))
for zct, col in [(0.0, "#d9534f"), (1.0, "#4a90d9")]:
    p = os.path.join(HERE, "patches", f"zct_{zct}.json")
    json.dump(patch(zct), open(p, "w"), indent=2)
    out = os.path.join(REND, f"zct_{zct}.wav")
    subprocess.run([CLI, p, out], capture_output=True, text=True)
    t, f0 = f0_of(out)
    # regularity: std of intervals between upward zero-crossings of detrended f0
    d = f0 - np.median(f0)
    ups = np.where((d[:-1] < 0) & (d[1:] >= 0))[0]
    if len(ups) > 2:
        iv = np.diff(ups) * 256 / 48000
        reg = f"period {np.mean(iv)*1000:.0f}ms  std {np.std(iv)*1000:.1f}ms  n={len(ups)}"
    else:
        reg = "too few crossings"
    print(f"zct={zct}: {reg}")
    ax.plot(t, f0, color=col, lw=0.9, label=f"zct={zct}")
ax.set_xlabel("time (s)"); ax.set_ylabel("f0 (Hz)")
ax.set_title("Vibrato zeroCrossTendency: 0 (wander) vs 1 (regular), jitter off")
ax.legend(); fig.tight_layout()
png = os.path.join(HERE, "out", "zct_compare.png")
fig.savefig(png, dpi=120); print("plot:", png)
