"""Broadening test batch (tests 1-4). Takes the derived viola source+formant
from viola_instrument.json, applies each broadening strategy on a sustained D4
(vibrato OFF to isolate the effect), renders, and compares spectra to real D4.

Strategies:
  baseline : source + formant, no broadening (reference)
  T1a/T1b  : static PartialGroups — each harmonic -> cluster of sub-partials
  T2       : chaos->order — groups start wide+scattered, condense to narrow (multEnv)
  T3       : T2 + rolloff "catch" (bright at onset, settles) via roEnv
  T4       : fast phase-offset jitter that decays (poEnv <- decaying RedNoise)
"""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")
REND = os.path.join(REPO, "renders", "warmstart", "broadening")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
os.makedirs(REND, exist_ok=True)

inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))
nodes = {n["id"]: n for n in inst["graph"]["nodes"]}
body = nodes["viola_body"]
parts0 = nodes["vla_partials"]

def base_nodes(extra=None, partials_overrides=None):
    parts = copy.deepcopy(parts0)
    if partials_overrides:
        parts["params"].update(partials_overrides)
    ns = [
        {"id": "ampEnv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": 0.06, "decay": 0.15,
                    "sustainLevel": 1.0, "release": 0.2}},
        copy.deepcopy(body),
        parts,
        {"id": "vla", "type": "AdditiveSource",
         "params": {"seed": 7, "frequency": 293.66, "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "viola_body"}, "formantWeight": 1.0,
                    "partials": {"ref": "vla_partials"}}},
    ]
    if extra:
        ns = extra + ns
    return ns

def make_patch(extra, partials_overrides):
    return {"sampleRate": 48000,
            "graph": {"nodes": base_nodes(extra, partials_overrides), "output": "vla"},
            "instrument": {"paramMap": {"frequency": "vla.frequency"}, "polyphony": 1},
            "score": [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 3.5}]}

# ramp-and-hold envelope (adsr, sustain 1.0): 0->1 over `attack`, then hold
def hold_env(nid, attack):
    return {"id": nid, "type": "Envelope",
            "params": {"preset": "adsr", "attack": attack, "decay": 0.0,
                       "sustainLevel": 1.0, "release": 0.0}}

GROUP_STATIC_A = {"expandRule": {"count": 4, "spacing1": 0.25, "spacing2": 0.25,
                                 "dt1": 0.06, "dt2": 0.06, "loPct1": 0.12, "loPct2": 0.12,
                                 "power1": 2.0, "power2": 2.0}}
GROUP_STATIC_B = {"expandRule": {"count": 6, "spacing1": 0.45, "spacing2": 0.45,
                                 "dt1": 0.12, "dt2": 0.12, "loPct1": 0.12, "loPct2": 0.12,
                                 "power1": 2.0, "power2": 2.0}}
GROUP_CONDENSE = {"expandRule": {"count": 5, "spacing1": 0.9, "spacing2": 0.05,
                                 "dt1": 0.4, "dt2": 0.0, "loPct1": 0.4, "loPct2": 0.1,
                                 "power1": 1.0, "power2": 2.0},
                  "multEnv": {"ref": "condense"}}

VARIANTS = {
    "baseline": ([], {}),
    "T1a_group_narrow": ([], GROUP_STATIC_A),
    "T1b_group_wide":   ([], GROUP_STATIC_B),
    "T2_condense":      ([hold_env("condense", 0.09)], GROUP_CONDENSE),
    "T3_condense_catch":([hold_env("condense", 0.09), hold_env("catch", 0.09)],
                         {**GROUP_CONDENSE, "rolloff1": -1.0, "rolloff2": 0.0,
                          "roEnv": {"ref": "catch"}}),
    "T4_po_jitter":     ([{"id": "poDecay", "type": "Envelope",
                           "params": {"preset": "ar", "attack": 0.02, "attackMax": 0.05}},
                          {"id": "poJitter", "type": "RedNoiseSource",
                           "params": {"frequency": 80.0, "amplitude": {"ref": "poDecay"},
                                      "smoothness": 1.0}}],
                         {"unitPO1": 0.0, "unitPO2": 0.4, "poEnv": {"ref": "poJitter"}}),
}

def render(name, patch):
    p = os.path.join(HERE, "patches", f"broad_{name}.json")
    json.dump(patch, open(p, "w"), indent=2)
    out = os.path.join(REND, f"{name}.wav")
    r = subprocess.run([CLI, p, out], capture_output=True, text=True)
    ok = os.path.exists(out)
    if ok:                                   # normalize for fair A/B
        y, s = sf.read(out); sf.write(out, y / (np.max(np.abs(y)) + 1e-12) * 0.7, s)
    print(f"  {name:18s} {'OK' if ok else 'FAIL ' + r.stderr.strip()[-160:]}")
    return out if ok else None

def seg_spec(x, sr, t0=0.6, dur=2.0):
    s = x[int(t0 * sr):int((t0 + dur) * sr)]
    s = s * np.hanning(len(s))
    nfft = 1 << int(np.ceil(np.log2(len(s))))
    sp = np.abs(np.fft.rfft(s, nfft)); f = np.fft.rfftfreq(nfft, 1.0 / sr)
    return f, 20 * np.log10(sp / (sp.max() + 1e-12) + 1e-9)

# real D4 reference
rx, rsr = sf.read([os.path.join(SAMPLE_DIR, g) for g in os.listdir(SAMPLE_DIR)
                   if ".sulD." in g and ".D4." in g][0])
if rx.ndim > 1: rx = rx.mean(axis=1)
frq, drq = seg_spec(rx, rsr)

import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, axes = plt.subplots(len(VARIANTS), 1, figsize=(12, 2.4 * len(VARIANTS)), sharex=True)
print("rendering:")
for ax, (name, (extra, ov)) in zip(axes, VARIANTS.items()):
    out = render(name, make_patch(extra, ov))
    ax.plot(frq, drq, color="#bbb", lw=0.7, label="real D4")
    if out:
        y, s = sf.read(out); y = y.mean(axis=1) if y.ndim > 1 else y
        fy, dy = seg_spec(y, s)
        ax.plot(fy, dy, color="#d9534f", lw=0.7, label=name)
    ax.set_xscale("log"); ax.set_xlim(250, 8000); ax.set_ylim(-85, 2)
    ax.legend(fontsize=7, loc="upper right"); ax.set_ylabel("dB")
axes[-1].set_xlabel("Hz (log)")
axes[0].set_title("Broadening tests vs real viola D4 (gray) — does the inter-harmonic floor fill?")
fig.tight_layout()
png = os.path.join(REND, "broadening_spectra.png")
fig.savefig(png, dpi=110); print("plot:", png)
