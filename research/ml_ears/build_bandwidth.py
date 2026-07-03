"""Test bandwidth-enhanced partials on the derived viola. Sweep static bandwidth
amount/width + a noisy-attack variant (bw high at onset, settling via bwEnv).
Compare each sustain spectrum to real D4 to see the line-broadening / floor-fill."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")
REND = os.path.join(REPO, "renders", "warmstart", "bandwidth")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
os.makedirs(REND, exist_ok=True)

inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))
nodes = {n["id"]: n for n in inst["graph"]["nodes"]}
body, parts0 = nodes["viola_body"], nodes["vla_partials"]

def patch(extra, ov):
    parts = copy.deepcopy(parts0); parts["params"].update(ov)
    ns = (extra or []) + [
        {"id": "ampEnv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": 0.06, "decay": 0.15,
                    "sustainLevel": 1.0, "release": 0.2}},
        copy.deepcopy(body), parts,
        {"id": "vla", "type": "AdditiveSource",
         "params": {"seed": 7, "frequency": 293.66, "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "viola_body"}, "formantWeight": 1.0,
                    "partials": {"ref": "vla_partials"}}}]
    return {"sampleRate": 48000, "graph": {"nodes": ns, "output": "vla"},
            "instrument": {"paramMap": {"frequency": "vla.frequency"}, "polyphony": 1},
            "score": [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 3.5}]}

hold = {"id": "bwramp", "type": "Envelope",
        "params": {"preset": "adsr", "attack": 0.09, "decay": 0.0,
                   "sustainLevel": 1.0, "release": 0.0}}

VARIANTS = {
    "baseline":  ([], {}),
    "bw_low":    ([], {"bandwidth1": 0.15, "bandwidth2": 0.15, "bandwidthHz": 40}),
    "bw_mid":    ([], {"bandwidth1": 0.40, "bandwidth2": 0.40, "bandwidthHz": 60}),
    "bw_high":   ([], {"bandwidth1": 0.70, "bandwidth2": 0.70, "bandwidthHz": 90}),
    "bw_attack": ([hold], {"bandwidth1": 0.85, "bandwidth2": 0.12,
                           "bandwidthHz": 120, "bwEnv": {"ref": "bwramp"}}),
}

def seg_spec(x, sr, t0=0.6, dur=2.0):
    s = x[int(t0*sr):int((t0+dur)*sr)] * np.hanning(int(dur*sr))
    nfft = 1 << int(np.ceil(np.log2(len(s))))
    sp = np.abs(np.fft.rfft(s, nfft)); f = np.fft.rfftfreq(nfft, 1.0/sr)
    return f, 20*np.log10(sp/(sp.max()+1e-12)+1e-9)

rfile = [g for g in os.listdir(SAMPLE_DIR) if ".sulD." in g and ".D4." in g][0]
rx, rsr = sf.read(os.path.join(SAMPLE_DIR, rfile))
if rx.ndim > 1: rx = rx.mean(axis=1)
frq, drq = seg_spec(rx, rsr)

import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, axes = plt.subplots(len(VARIANTS), 1, figsize=(12, 2.3*len(VARIANTS)), sharex=True)
print("rendering bandwidth tests:")
for ax, (name, (extra, ov)) in zip(axes, VARIANTS.items()):
    p = os.path.join(HERE, "patches", f"bw_{name}.json"); json.dump(patch(extra, ov), open(p, "w"), indent=2)
    out = os.path.join(REND, f"{name}.wav")
    subprocess.run([CLI, p, out], capture_output=True, text=True)
    ok = os.path.exists(out)
    if ok:
        y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
        y = sf.read(out)[0]; y = y.mean(axis=1) if y.ndim > 1 else y
        fy, dy = seg_spec(y, s)
        ax.plot(frq, drq, color="#bbb", lw=0.7, label="real D4")
        ax.plot(fy, dy, color="#d9534f", lw=0.7, label=name)
    print(f"  {name:10s} {'OK' if ok else 'FAIL'}")
    ax.set_xscale("log"); ax.set_xlim(250, 8000); ax.set_ylim(-85, 2)
    ax.legend(fontsize=7, loc="upper right"); ax.set_ylabel("dB")
axes[-1].set_xlabel("Hz (log)")
axes[0].set_title("Bandwidth-enhanced partials vs real viola D4 (gray) — line-broadening + floor fill")
fig.tight_layout()
png = os.path.join(REND, "bandwidth_spectra.png"); fig.savefig(png, dpi=110)
print("plot:", png)
