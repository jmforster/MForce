"""Definitive: render catch_zero_highest WITH and WITHOUT vibrato, measure the
noise (spectral flatness) trajectory finely, and plot. Tells us whether the
bandwidth ramp itself is backwards, or whether vibrato creates the rising noise."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

base = json.load(open(os.path.join(HERE, "patches", "catch_zero_highest.json")))

def render(patch, name):
    p = os.path.join(HERE, "patches", f"probe_{name}.json"); json.dump(patch, open(p, "w"), indent=2)
    out = os.path.join(REND, f"probe_{name}.wav")
    subprocess.run([CLI, p, out], capture_output=True, text=True)
    if os.path.exists(out):
        y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
    return out

# vibrato ON = base as-is. vibrato OFF = point AdditiveSource.frequency at a
# constant and map the note there directly (bypass the Vibrato node).
on = copy.deepcopy(base)
off = copy.deepcopy(base)
for n in off["graph"]["nodes"]:
    if n["id"] == "vla": n["params"]["frequency"] = 293.66
off["instrument"]["paramMap"]["frequency"] = "vla.frequency"

w_on = render(on, "vib_on")
w_off = render(off, "vib_off")

def flat_traj(wav, win=0.04, hop=0.02, t0=0.05, t1=2.9):
    y, sr = sf.read(wav)
    if y.ndim > 1: y = y.mean(axis=1)
    n = int(win*sr); h = int(hop*sr); ts, fs = [], []
    i = int(t0*sr)
    while i + n < int(t1*sr):
        seg = y[i:i+n]*np.hanning(n); sp = np.abs(np.fft.rfft(seg))+1e-9
        fs.append(np.exp(np.mean(np.log(sp)))/np.mean(sp)); ts.append(i/sr); i += h
    return np.array(ts), np.array(fs)

import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, ax = plt.subplots(figsize=(11, 4))
for wav, lbl, col in [(w_off, "vibrato OFF", "#d9534f"), (w_on, "vibrato ON", "#4a90d9")]:
    t, f = flat_traj(wav)
    ax.plot(t, f, color=col, lw=1.0, label=lbl)
    e = np.mean(f[(t > 0.05) & (t < 0.30)]); l = np.mean(f[(t > 1.0) & (t < 2.8)])
    trend = "CHAOS->order" if e > l*1.1 else "order->chaos" if l > e*1.1 else "flat"
    print(f"{lbl:12s} early={e:.4f} late={l:.4f} -> {trend}")
ax.axvline(0.18, color="#888", ls=":", lw=0.8); ax.text(0.19, ax.get_ylim()[1]*0.9, "bw settle ~0.18s", fontsize=7)
ax.set_xlabel("time (s)"); ax.set_ylabel("spectral flatness (noisiness)")
ax.set_title("catch_zero_highest: noise over time — bandwidth ramp vs vibrato")
ax.legend(); fig.tight_layout()
png = os.path.join(REND, "probe_vib.png"); fig.savefig(png, dpi=120); print("plot:", png)
