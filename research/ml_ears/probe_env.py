"""Render a tone whose AMPLITUDE is the exact bwramp envelope, and plot its RMS
shape over time. Definitively shows whether 'adsr sustain 1.0' ramps 0->1-hold
(my assumption) or something else."""
import os, json, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

def tone_with_amp(ampnode):
    return {"sampleRate": 48000, "graph": {"nodes": [
        ampnode,
        {"id": "parts", "type": "ExplicitPartials",
         "params": {"mult1": [1], "mult2": [1], "ampl1": [1.0], "ampl2": [1.0], "rolloff1": 0.0, "rolloff2": 0.0}},
        {"id": "tone", "type": "AdditiveSource",
         "params": {"seed": 1, "frequency": 293.66, "amplitude": {"ref": "amp"}, "partials": {"ref": "parts"}}},
    ], "output": "tone"},
    "instrument": {"paramMap": {"frequency": "tone.frequency"}, "polyphony": 1},
    "score": [{"note": 62, "velocity": 1.0, "time": 0.0, "duration": 2.0}]}

ENVS = {
    "adsr_sus1": {"id": "amp", "type": "Envelope",
                  "params": {"preset": "adsr", "attack": 0.06, "decay": 0.0, "sustainLevel": 1.0, "release": 0.0}},
    "ar":        {"id": "amp", "type": "Envelope",
                  "params": {"preset": "ar", "attack": 0.06, "attackMax": 2.0}},
}

import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, ax = plt.subplots(figsize=(11, 4))
for name, env in ENVS.items():
    p = os.path.join(HERE, "patches", f"probe_env_{name}.json"); json.dump(tone_with_amp(env), open(p, "w"), indent=2)
    out = os.path.join(REND, f"probe_env_{name}.wav")
    subprocess.run([CLI, p, out], capture_output=True, text=True)
    y, sr = sf.read(out)
    if y.ndim > 1: y = y.mean(axis=1)
    w = int(0.01*sr)
    rms = np.sqrt(np.convolve(y**2, np.ones(w)/w, "same"))
    t = np.arange(len(rms))/sr
    ax.plot(t, rms/ (rms.max()+1e-9), lw=1.2, label=f"preset={name}")
    print(f"{name}: rms[start]={rms[int(0.01*sr)]:.3f} rms[mid]={rms[int(1.0*sr)]:.3f} rms[end]={rms[int(1.9*sr)]:.3f}")
ax.set_xlabel("time (s)"); ax.set_ylabel("normalized RMS (envelope shape)")
ax.set_title("Actual envelope output: does 'adsr sustain 1.0' ramp UP-and-hold?")
ax.legend(); fig.tight_layout()
png = os.path.join(REND, "probe_env.png"); fig.savefig(png, dpi=120); print("plot:", png)
