"""Minimal isolation: 3-partial tone, bandwidth1=1 -> bandwidth2=0 via bwEnv ramp,
NO formant, NO vibrato. Spectrogram shows exactly when the noise occurs."""
import os, json, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

patch = {
  "sampleRate": 48000,
  "graph": {"nodes": [
    {"id": "ampEnv", "type": "Envelope",
     "params": {"preset": "adsr", "attack": 0.02, "decay": 0.1, "sustainLevel": 1.0, "release": 0.05}},
    {"id": "bwramp", "type": "Envelope",
     "params": {"preset": "adsr", "attack": 0.12, "decay": 0.0, "sustainLevel": 1.0, "release": 0.0}},
    {"id": "parts", "type": "ExplicitPartials",
     "params": {"mult1": [1, 2, 3], "mult2": [1, 2, 3], "ampl1": [1.0, 0.4, 0.2], "ampl2": [1.0, 0.4, 0.2],
                "rolloff1": 0.0, "rolloff2": 0.0,
                "bandwidth1": 1.0, "bandwidth2": 0.0, "bandwidthHz": 250, "bwEnv": {"ref": "bwramp"}}},
    {"id": "tone", "type": "AdditiveSource",
     "params": {"seed": 1, "frequency": 293.66, "amplitude": {"ref": "ampEnv"}, "partials": {"ref": "parts"}}},
  ], "output": "tone"},
  "instrument": {"paramMap": {"frequency": "tone.frequency"}, "polyphony": 1},
  "score": [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 2.0}],
}
p = os.path.join(HERE, "patches", "probe_minimal.json"); json.dump(patch, open(p, "w"), indent=2)
out = os.path.join(REND, "probe_minimal.wav")
subprocess.run([CLI, p, out], capture_output=True, text=True)
y, sr = sf.read(out)
if y.ndim > 1: y = y.mean(axis=1)

# crude STFT spectrogram
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
win = 2048; hop = 256
frames = range(0, len(y) - win, hop)
S = np.array([20*np.log10(np.abs(np.fft.rfft(y[i:i+win]*np.hanning(win)))+1e-6) for i in frames]).T
f = np.fft.rfftfreq(win, 1/sr); t = np.arange(len(list(frames)))*hop/sr
fig, ax = plt.subplots(figsize=(11, 4.5))
fmask = f < 6000
ax.imshow(S[fmask], aspect="auto", origin="lower", extent=[0, t[-1], 0, 6000],
          vmin=-70, vmax=0, cmap="magma")
ax.set_xlabel("time (s)"); ax.set_ylabel("Hz")
ax.set_title("Minimal: bandwidth1=1 -> bandwidth2=0. Noise should be at START (left) if ramp is correct")
fig.tight_layout()
png = os.path.join(REND, "probe_minimal_spec.png"); fig.savefig(png, dpi=120)
# early vs late flatness
def flat(seg): sp = np.abs(np.fft.rfft(seg*np.hanning(len(seg))))+1e-9; return np.exp(np.mean(np.log(sp)))/np.mean(sp)
e = flat(y[int(0.03*sr):int(0.10*sr)]); l = flat(y[int(1.2*sr):int(1.8*sr)])
print(f"early(0.03-0.10s)={e:.4f}  late(1.2-1.8s)={l:.4f} -> " +
      ("CHAOS->order (correct)" if e > l*1.2 else "order->chaos (BUG)" if l > e*1.2 else "flat"))
print("spectrogram:", png)
