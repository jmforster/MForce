"""Bassoon round 4: the ref is ~a 500-1k bandpass (steep both sides:
fundamental region -21 dB rel total, >1k falling 24+ dB/oct). The render's
dry delay leg flattens all of that. Cells: branch-only output, and a true
bandpass body. Scored on octave-band deltas vs ref + honk + attack."""
import json, copy, os, sys, wave, struct, math
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
sys.path.insert(0, r"C:\@dev\repos\mforce\research\ml_ears")
import oboe_ref_compare as orc
from bassoon_match import build_render, REF, PATCH

ROOT = r"C:\@dev\repos\mforce"
PATCH_OUT = os.path.join(ROOT, r"patches\sweep\bassoon_ref1")
REND_OUT = os.path.join(ROOT, r"renders\dsp\audition\bassoon_ref1")

base = json.load(open(PATCH))
sr = 48000

def nodes(p):
    return {n["id"]: n for n in p["graph"]["nodes"]}

def b2core(p):
    ns = nodes(p)
    ns["Damp_cutoff_curve"]["params"]["exprKnots"].insert(
        0, {"a": 7.0, "b": 0.0, "form": "linear", "x": 150.0})

def branch_lp(p):   # r6: LP chain body, dry muted
    ns = nodes(p)
    ns["Formant1"]["params"].update({"cutoffFreq": 500.0, "resonance": 10.0, "mode": 0})
    ns["SVF2"]["params"].update({"cutoffFreq": 990.0, "resonance": 8.0, "mode": 0})
    ns["Combined"]["params"]["source1"] = 0.0

def branch_bp(p):   # r7: bandpass body, dry muted
    ns = nodes(p)
    ns["Formant1"]["params"].update({"cutoffFreq": 550.0, "resonance": 6.0, "mode": 2})
    ns["SVF2"]["params"].update({"cutoffFreq": 990.0, "resonance": 2.0, "mode": 0})
    ns["Combined"]["params"]["source1"] = 0.0

def bp_plus_dry(p):  # r8: bandpass body + a little lowpassed dry
    branch_bp(p)
    nl = p["graph"]["nodes"]
    idx = next(i for i, n in enumerate(nl) if n["id"] == "Combined")
    nl.insert(idx, {"id": "Dry_lp", "type": "BWLowpassFilter",
                    "params": {"cutoffFreq": 800.0,
                               "source": {"ref": "Delay_line"}}})
    nodes(p)["Combined"]["params"]["source1"] = {"ref": "Dry_lp"}

def load_wav(path):
    w = wave.open(path, "rb"); n = w.getnframes(); ch = w.getnchannels()
    s = struct.unpack("<%dh" % (n*ch), w.readframes(n)); w.close()
    x = np.array(s, float) / 32768.0
    return x.reshape(-1, 2).mean(1) if ch == 2 else x

def bands(x, t0, t1):
    seg = x[int(t0*sr):int(t1*sr)]
    f = np.fft.rfft(seg * np.hanning(len(seg))); fr = np.fft.rfftfreq(len(seg), 1/sr)
    tot = (np.abs(f)**2).sum()
    out = []
    lo = 125.0
    while lo < 12000:
        hi = lo * 2
        e = (np.abs(f)[(fr>=lo)&(fr<hi)]**2).sum()
        out.append(10*math.log10(e/tot + 1e-12))
        lo = hi
    return np.array(out)

refx = load_wav(REF)
bR = bands(refx, 4.6, 5.3)   # held F#3

tsR, f0R = orc.pitch_track(sr, refx, fmin=70.0, fmax=1200.0)
segR = orc.sustained_segments(tsR, f0R)
refSeg = max((s for s in segR if s[2] < 300), key=lambda s: s[1]-s[0])
hR = orc.harmonic_env(sr, refx, refSeg[0], refSeg[1], refSeg[2]); hR -= hR[0]

CELLS = [("r6_body_lp", branch_lp), ("r7_body_bp", branch_bp),
         ("r8_bp_drylp", bp_plus_dry)]
print(f"{'cell':14s} {'bandErr':>8s} {'honkErr':>8s} {'atk':>5s}   octave deltas 125..8k")
for name, mut in CELLS:
    p = copy.deepcopy(base)
    b2core(p); mut(p)
    pj = os.path.join(PATCH_OUT, f"{name}.json")
    json.dump(p, open(pj, "w"), indent=2)
    wav = os.path.join(REND_OUT, f"{name}.wav")
    build_render(pj, wav)
    x = load_wav(wav)
    bN = bands(x, 0.5, 1.1)
    d = bN - bR
    bandErr = float(np.mean(np.abs(d[:6])))
    tsN, f0N = orc.pitch_track(sr, x, fmin=70.0, fmax=1200.0)
    segN = orc.sustained_segments(tsN, f0N)
    cands = [g for g in segN if abs(1200*np.log2(g[2]/refSeg[2])) < 80]
    honkErr = float("nan"); atk = -1
    if cands:
        b = max(cands, key=lambda g: g[1]-g[0])
        hN = orc.harmonic_env(sr, x, b[0], b[1], b[2]); hN -= hN[0]
        n = min(len(hR), len(hN), 4)
        honkErr = float(np.mean(np.abs((hN[:n]-hR[:n])[1:])))
        a = orc.attack_time(sr, x, b[0], b[2])
        atk = (a or -1) * 1000
    print(f"{name:14s} {bandErr:8.1f} {honkErr:8.1f} {atk:5.0f}   " +
          " ".join(f"{v:+5.1f}" for v in d[:6]))
