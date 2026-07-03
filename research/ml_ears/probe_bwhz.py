"""Is the catch too subtle because the band is too narrow? Sweep bandwidthHz hard
(bw1=1 -> bw2=0, vibrato OFF to isolate). Wide noise should make an audible
broadband scratch at the attack. Measures attack vs sustain flatness + saves WAVs."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart", "attack_sets")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))

def build(bwhz):
    p = copy.deepcopy(inst)
    nmap = {n["id"]: n for n in p["graph"]["nodes"]}
    nmap["ampEnv"]["params"]["attack"] = 0.015
    nmap["vla_partials"]["params"].update(
        {"bandwidth1": 1.0, "bandwidth2": 0.0, "bandwidthHz": bwhz, "bwEnv": {"ref": "bwramp"}})
    # vibrato OFF: point AdditiveSource.frequency at a constant, map note there
    for n in p["graph"]["nodes"]:
        if n["id"] == "vla": n["params"]["frequency"] = 293.66
    p["instrument"]["paramMap"]["frequency"] = "vla.frequency"
    p["graph"]["nodes"] = [{"id": "bwramp", "type": "ASEnvelope", "params": {"attack": 0.08}}] + p["graph"]["nodes"]
    p["score"] = [{"note": 62, "velocity": 0.85, "time": 0.0, "duration": 2.5}]
    return p

def flat(seg):
    sp = np.abs(np.fft.rfft(seg*np.hanning(len(seg)))) + 1e-9
    return np.exp(np.mean(np.log(sp)))/np.mean(sp)

print("bandwidthHz sweep (bw1=1 -> 0, vibrato off):")
for bwhz in [250, 800, 2000, 5000, 9000]:
    p = build(bwhz)
    pp = os.path.join(HERE, "patches", f"bwhz_{bwhz}.json"); json.dump(p, open(pp, "w"), indent=2)
    out = os.path.join(REND, f"bwhz_{bwhz}.wav")
    subprocess.run([CLI, pp, out], capture_output=True, text=True)
    if os.path.exists(out):
        y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
        y = sf.read(out)[0]; y = y.mean(axis=1) if y.ndim > 1 else y
        atk = y[int(0.02*s):int(0.15*s)]; sus = y[int(1.0*s):int(2.0*s)]
        print(f"  bwHz={bwhz:<5} flat_attack={flat(atk):.4f} flat_sustain={flat(sus):.4f} "
              f"ratio={flat(atk)/flat(sus):.2f} (want >>1)")
print("dir:", REND, "(files bwhz_*.wav)")
