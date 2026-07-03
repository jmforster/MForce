"""Full viola = derived source + shared formant + vibrato + bandwidth-enhanced
partials. Two bandwidth options for audition, playing the 4-note phrase."""
import os, json, copy, subprocess
import numpy as np, soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

inst = json.load(open(os.path.join(HERE, "patches", "viola_instrument.json")))

def variant(name, parts_ov, extra_nodes=None, amp_attack=None, vib_attack=None):
    p = copy.deepcopy(inst)
    nmap = {n["id"]: n for n in p["graph"]["nodes"]}
    nmap["vla_partials"]["params"].update(parts_ov)
    if amp_attack is not None:
        nmap["ampEnv"]["params"]["attack"] = amp_attack   # fast attack = audible bow-catch
    if vib_attack is not None:
        nmap["vib"]["params"]["attack"] = vib_attack       # vibrato in fast so it isn't the changing element
    if extra_nodes:
        p["graph"]["nodes"] = extra_nodes + p["graph"]["nodes"]
    path = os.path.join(HERE, "patches", f"viola_full_{name}.json")
    json.dump(p, open(path, "w"), indent=2)
    out = os.path.join(REND, f"viola_full_{name}.wav")
    subprocess.run([CLI, path, out], capture_output=True, text=True)
    if os.path.exists(out):
        y, s = sf.read(out); sf.write(out, y/(np.max(np.abs(y))+1e-12)*0.7, s)
    print(f"  viola_full_{name}: {'OK' if os.path.exists(out) else 'FAIL'} -> {out}")

# Longer bandwidth settle (~1s) so the catch resolves in the LOUD part of the
# note, not during the silent amp fade-in (that's what made the attack read
# backwards). Paired with a fast amp attack below.
bwramp = {"id": "bwramp", "type": "Envelope",
          "params": {"preset": "adsr", "attack": 0.30, "decay": 0.0,
                     "sustainLevel": 1.0, "release": 0.0}}

print("full viola renders (vibrato + bandwidth):")
variant("bwmid", {"bandwidth1": 0.35, "bandwidth2": 0.35, "bandwidthHz": 60})
variant("bwattack", {"bandwidth1": 0.90, "bandwidth2": 0.08, "bandwidthHz": 200,
                     "bwEnv": {"ref": "bwramp"}}, extra_nodes=[bwramp], amp_attack=0.015)
print("(compare against viola_instrument.wav = no bandwidth)")
