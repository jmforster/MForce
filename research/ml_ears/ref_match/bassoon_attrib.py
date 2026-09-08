"""Where does the 2.5-8k raucous energy reach the output? Mute each leg
of Combined on the b2 config and measure hiLo of what remains."""
import json, copy, os, sys, wave, struct, math
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
from bassoon_match import build_render, PATCH

base = json.load(open(PATCH))

def nodes(p):
    return {n["id"]: n for n in p["graph"]["nodes"]}

def b2(p):
    ns = nodes(p)
    ns["Formant1"]["params"]["cutoffFreq"] = 500.0
    ns["Formant1"]["params"]["resonance"] = 10.0
    ns["SVF2"]["params"]["cutoffFreq"] = 990.0
    ns["SVF2"]["params"]["resonance"] = 8.0
    ns["Damp_cutoff_curve"]["params"]["exprKnots"].insert(
        0, {"a": 7.0, "b": 0.0, "form": "linear", "x": 150.0})

def hilo(path):
    w = wave.open(path, "rb"); n = w.getnframes(); ch = w.getnchannels()
    s = struct.unpack("<%dh" % (n*ch), w.readframes(n)); w.close()
    x = np.array(s, float) / 32768.0
    if ch == 2: x = x.reshape(-1, 2).mean(1)
    sr = 48000
    seg = x[int(0.8*sr):int(1.1*sr)]
    f = np.fft.rfft(seg * np.hanning(len(seg))); fr = np.fft.rfftfreq(len(seg), 1/sr)
    lo = (np.abs(f)[(fr>150)&(fr<1500)]**2).sum()
    hi = (np.abs(f)[(fr>2500)&(fr<8000)]**2).sum()
    return 10*math.log10(hi/lo + 1e-12)

cases = {
    "full":        lambda p: None,
    "dry_only":    lambda p: nodes(p)["Combined"]["params"].update({"gainAdj": -1.0}),
    "branch_only": lambda p: nodes(p)["Combined"]["params"].update({"source1": 0.0}),
    "no_reverb":   lambda p: nodes(p)["Reverb"]["params"].update({"wet": 0.0}),
}
if __name__ == "__main__":
    for name, mut in cases.items():
        p = copy.deepcopy(base)
        b2(p); mut(p)
        pj = os.path.join(SCRATCH, f"attrib_{name}.json")
        wav = os.path.join(SCRATCH, f"attrib_{name}.wav")
        json.dump(p, open(pj, "w"))
        build_render(pj, wav)
        print(f"{name:12s} hiLo {hilo(wav):6.1f} dB", flush=True)
