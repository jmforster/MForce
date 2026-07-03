"""Make the catch sound INTEGRATED, not a white-noise burst. Two knobs:
moderate bandwidthHz (noise stays formant/harmonic-colored, not white) and
bw1<1 (the tone stays present under the noise instead of being replaced).
Duration ~0.2s (bw_attack 0.08 on a 3s note). Vibrato off to judge the catch."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))

def build(bwhz, bw1, bw_attack=0.08):
    p = copy.deepcopy(inst)
    nmap = {n["id"]: n for n in p["graph"]["nodes"]}
    nmap["ampEnv"]["params"]["attack"] = 0.015
    nmap["vla_partials"]["params"].update(
        {"bandwidth1": bw1, "bandwidth2": 0.0, "bandwidthHz": bwhz, "bwEnv": {"ref": "bwramp"}})
    for n in p["graph"]["nodes"]:
        if n["id"] == "vla": n["params"]["frequency"] = 293.66
    p["instrument"]["paramMap"]["frequency"] = "vla.frequency"
    p["graph"]["nodes"] = [{"id": "bwramp", "type": "ASEnvelope", "params": {"attack": bw_attack}}] + p["graph"]["nodes"]
    p["score"] = [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 3.0}]
    return p

print("integration test (~0.2s catch, vib off): moderate bwHz x bw1<1")
for bwhz in [1500, 3500]:
    for bw1 in [0.4, 0.6]:
        name = f"int_hz{bwhz}_bw{int(bw1*100)}"
        pp = os.path.join(HERE, "patches", f"{name}.json"); json.dump(build(bwhz, bw1), open(pp, "w"), indent=2)
        out = os.path.join(REND, f"{name}.wav")
        subprocess.run([CLI, pp, out], capture_output=True, text=True)
        if os.path.exists(out):
            y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
        print(f"  {name:18s} {'OK' if os.path.exists(out) else 'FAIL'}")
print("dir:", REND, "(files int_*.wav)")
