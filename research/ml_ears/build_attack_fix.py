"""Fix the chaos->order attack: make amplitude rise FAST so the high-bandwidth
bow-catch is audible, then let bandwidth settle. Render variants (vibrato off to
isolate) and measure the flatness trajectory — we want noise to FALL over the note."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf
import measure_attack as M

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "bandwidth")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))
nodes = {n["id"]: n for n in inst["graph"]["nodes"]}
body, parts0 = nodes["viola_body"], nodes["vla_partials"]

def patch(amp_atk, bw_atk, bw1, bw2, bwhz):
    parts = copy.deepcopy(parts0)
    parts["params"].update({"bandwidth1": bw1, "bandwidth2": bw2,
                            "bandwidthHz": bwhz, "bwEnv": {"ref": "bwramp"}})
    ns = [
        {"id": "ampEnv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": amp_atk, "decay": 0.15,
                    "sustainLevel": 1.0, "release": 0.2}},
        {"id": "bwramp", "type": "Envelope",
         "params": {"preset": "adsr", "attack": bw_atk, "decay": 0.0,
                    "sustainLevel": 1.0, "release": 0.0}},
        copy.deepcopy(body), parts,
        {"id": "vla", "type": "AdditiveSource",
         "params": {"seed": 7, "frequency": 293.66, "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "viola_body"}, "formantWeight": 1.0,
                    "partials": {"ref": "vla_partials"}}}]
    return {"sampleRate": 48000, "graph": {"nodes": ns, "output": "vla"},
            "instrument": {"paramMap": {"frequency": "vla.frequency"}, "polyphony": 1},
            "score": [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 3.5}]}

# amp_atk (fast), bw_atk (settle time), bw1 high, bw2 low, bwHz
VARIANTS = {
    "fix1_amp02_bw15": (0.015, 0.15, 0.85, 0.10, 200),
    "fix2_amp02_bw30": (0.015, 0.30, 0.90, 0.08, 200),
    "fix3_amp01_bw20": (0.010, 0.20, 0.95, 0.05, 300),
}
for name, args in VARIANTS.items():
    p = os.path.join(HERE, "patches", f"bwfix_{name}.json")
    json.dump(patch(*args), open(p, "w"), indent=2)
    out = os.path.join(REND, f"{name}.wav")
    subprocess.run([CLI, p, out], capture_output=True, text=True)
    if os.path.exists(out):
        y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
        t, f = M.flatness_over_time(out, 0.05, 3.2)
        rel = t - 0.05
        early = np.mean(f[(rel > 0.02) & (rel < 0.30)])
        late  = np.mean(f[(rel > 1.0) & (rel < 2.8)])
        trend = "CHAOS->order (GOOD)" if early > late * 1.15 else \
                "order->chaos (still backwards)" if late > early * 1.15 else "flat"
        print(f"{name:18s} early={early:.4f} late={late:.4f}  -> {trend}")
