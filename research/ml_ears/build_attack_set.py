"""Bandwidth as a transient bow-CATCH only: high at onset, decaying to a floor
FAST (well before vibrato ramps in). 3 'high' levels x 3 floors (zero / tiny /
small) = 9 renders, single sustained D4 with vibrato on."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
os.makedirs(REND, exist_ok=True)

inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))

BW_HZ = 250.0
BW_ATTACK = 0.06     # bandwidth settles by ~0.18s on a 3s note (vibrato attack=0.4 → ~1.2s)
AMP_ATTACK = 0.015   # fast so the catch is audible, not masked by the fade-in

FLOORS = {"zero": 0.0, "tiny": 0.03, "small": 0.07}
HIGHS  = {"high": 0.55, "higher": 0.80, "highest": 1.0}

def build(bw1, bw2):
    p = copy.deepcopy(inst)
    nmap = {n["id"]: n for n in p["graph"]["nodes"]}
    nmap["ampEnv"]["params"]["attack"] = AMP_ATTACK
    nmap["vla_partials"]["params"].update(
        {"bandwidth1": bw1, "bandwidth2": bw2, "bandwidthHz": BW_HZ,
         "bwEnv": {"ref": "bwramp"}})
    bwramp = {"id": "bwramp", "type": "ASEnvelope",    # attack→sustain: ramps then HOLDS
              "params": {"attack": BW_ATTACK}}
    p["graph"]["nodes"] = [bwramp] + p["graph"]["nodes"]
    p["score"] = [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 3.0}]
    return p

print("bow-catch renders (high -> floor, fast decay, single D4):")
for fname, bw2 in FLOORS.items():
    for hname, bw1 in HIGHS.items():
        name = f"catch_{fname}_{hname}"
        pp = os.path.join(HERE, "patches", f"{name}.json")
        json.dump(build(bw1, bw2), open(pp, "w"), indent=2)
        out = os.path.join(REND, f"{name}.wav")
        subprocess.run([CLI, pp, out], capture_output=True, text=True)
        if os.path.exists(out):
            y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
        print(f"  {name:24s} bw1={bw1:<4} bw2={bw2:<5} {'OK' if os.path.exists(out) else 'FAIL'}")
print("dir:", REND)
