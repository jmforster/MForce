"""The catch is audible at wide band but too SHORT (reads as a 'hit'). Hold the
wide band (bwHz=9000) and stretch the decay (bwramp attack) so it lasts long
enough to read as a chiff/scratch. Vibrato off. Single D4."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))

DUR = 3.0
def build(bw_attack, bwhz=9000):
    p = copy.deepcopy(inst)
    nmap = {n["id"]: n for n in p["graph"]["nodes"]}
    nmap["ampEnv"]["params"]["attack"] = 0.015
    nmap["vla_partials"]["params"].update(
        {"bandwidth1": 1.0, "bandwidth2": 0.0, "bandwidthHz": bwhz, "bwEnv": {"ref": "bwramp"}})
    for n in p["graph"]["nodes"]:
        if n["id"] == "vla": n["params"]["frequency"] = 293.66
    p["instrument"]["paramMap"]["frequency"] = "vla.frequency"
    p["graph"]["nodes"] = [{"id": "bwramp", "type": "ASEnvelope", "params": {"attack": bw_attack}}] + p["graph"]["nodes"]
    p["score"] = [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": DUR}]
    return p

def flat(seg):
    sp = np.abs(np.fft.rfft(seg*np.hanning(len(seg)))) + 1e-9
    return np.exp(np.mean(np.log(sp)))/np.mean(sp)

# bw_attack as fraction of the 3s note -> approx catch decay seconds
print("catch-duration sweep (bwHz=9000, bw1=1->0, vib off):")
for atk in [0.05, 0.12, 0.25, 0.45]:
    secs = atk * DUR
    p = build(atk)
    pp = os.path.join(HERE, "patches", f"dur_{atk}.json"); json.dump(p, open(pp, "w"), indent=2)
    out = os.path.join(REND, f"dur_{atk}.wav")
    subprocess.run([CLI, pp, out], capture_output=True, text=True)
    if os.path.exists(out):
        y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
        y = sf.read(out)[0]; y = y.mean(axis=1) if y.ndim > 1 else y
        # how long does the noise stay elevated? report flatness in 0.1s windows
        wins = [flat(y[int(t*s):int((t+0.1)*s)]) for t in [0.05, 0.2, 0.4, 0.7, 1.2]]
        print(f"  bw_attack={atk} (~{secs:.2f}s decay)  flat@[0.05,0.2,0.4,0.7,1.2]s = " +
              " ".join(f"{w:.3f}" for w in wins))
print("dir:", REND, "(files dur_*.wav)")
