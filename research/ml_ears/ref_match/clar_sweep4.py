"""Round 4: articulation made tempo-independent. Fraction-mode envelopes
stretch with note length while loop bloom is absolute physics — so short
notes never develop. Convert Drive_env/Ampl_env to seconds-mode attacks
(engine feature from the 08-06 piano-chuff fix), flatten the in-loop
Ampl_env swell, anchor the drive band at the (re-measured) threshold.
Cells: restore the round-2c spectral lead (c3) + fast-articulation c7/c8."""
import json, copy, os, subprocess, sys, wave, struct, math
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
sys.path.insert(0, r"C:\@dev\repos\mforce\research\ml_ears")
import oboe_ref_compare as orc
from clar_match import build_render

ROOT = r"C:\@dev\repos\mforce"
SRC = os.path.join(ROOT, r"patches\pending\clarinets\clarinet_attempt.json")
PATCH_OUT = os.path.join(ROOT, r"patches\sweep\clar_ref1")
REND_OUT = os.path.join(ROOT, r"renders\dsp\audition\clar_ref1")
CLI = os.path.join(ROOT, r"build\tools\mforce_cli\Release\mforce_cli.exe")

base = json.load(open(SRC))

def nodes(p):
    return {n["id"]: n for n in p["graph"]["nodes"]}

def junc_sym(p):
    j = nodes(p)["Junction"]["params"]
    for key in ("values", "values2"):
        v = j[key]
        pts = [(v[i], v[i + 1]) for i in range(0, len(v), 2)]
        pos = sorted((x, y) for x, y in pts if x > 0.05)
        sym = [(-x, -y) for x, y in reversed(pos)] + pos
        flat = []
        for x, y in sym:
            flat += [round(x, 6), round(y, 6)]
        j[key] = flat

def formants(p):
    ns = nodes(p)
    ns["Formant1"]["params"]["cutoffFreq"] = 1125.0
    ns["SVF2"]["params"]["cutoffFreq"] = 2590.0

def noise(p):
    for k in nodes(p)["Breath_curve"]["params"]["exprKnots"]:
        k["a"] = k["a"] * 2.5

def fast_articulation(p):
    """Seconds-mode envelopes: attack lengths in real time, not fractions."""
    de = nodes(p)["Drive_env"]["params"]
    de["timeMode"] = "seconds"
    de["timeScale"] = 1.0
    de["stages"] = [
        {"startVal": 0.0, "endVal": 1.0, "percent": 0.08, "type": "Expo",
         "power": 2.5, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0},
        {"startVal": 1.0, "endVal": 1.0, "percent": 0.0, "type": "Linear",
         "power": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0},
        {"startVal": 1.0, "endVal": 0.0, "percent": 0.12, "type": "Sine",
         "power": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0},
    ]
    ae = nodes(p)["Ampl_env"]["params"]
    ae["timeMode"] = "seconds"
    ae["timeScale"] = 1.0
    ae["maxValue"] = 1.0
    ae["stages"] = [
        {"startVal": 0.0, "endVal": 1.0, "percent": 0.02, "type": "Linear",
         "power": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0},
        {"startVal": 1.0, "endVal": 1.0, "percent": 0.0, "type": "Linear",
         "power": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0},
        {"startVal": 1.0, "endVal": 0.0, "percent": 0.15, "type": "Sine",
         "power": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0},
    ]

def set_band(p, lo, hi):
    de = nodes(p)["Drive_env"]["params"]
    de["minValue"] = min(0.95, lo)
    de["maxValue"] = min(0.98, hi)

def render_probe(p, dur=0.62):
    q = copy.deepcopy(p)
    q["score"] = [{"time": 0.0, "duration": dur, "note": 62, "velocity": 0.85}]
    q["seconds"] = dur + 0.5
    pj = os.path.join(SCRATCH, "probe4.json")
    pw = os.path.join(SCRATCH, "probe4.wav")
    json.dump(q, open(pj, "w"))
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    if r.returncode != 0:
        return None
    w = wave.open(pw, "rb"); n = w.getnframes(); ch = w.getnchannels()
    s = struct.unpack("<%dh" % (n * ch), w.readframes(n)); w.close()
    return [v / 32768.0 for v in s]

def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))

def bloom_ms(x, sr=48000):
    win = int(0.025 * sr)
    r = [rms(x[i:i + win]) for i in range(0, len(x) - win, win)]
    settled = max(r)
    for i, v in enumerate(r):
        if v >= 0.9 * settled:
            return i * 25
    return -1

def find_threshold(p):
    lo, hi = 0.3, 1.4
    t = copy.deepcopy(p); set_band(t, hi, hi)
    if rms(render_probe(t, 1.5) or [0]) < 0.02:
        return None
    for _ in range(8):
        mid = (lo + hi) / 2
        t = copy.deepcopy(p); set_band(t, mid, mid)
        x = render_probe(t, 1.5)
        if x is not None and rms(x) > 0.02:
            hi = mid
        else:
            lo = mid
    return hi

REFWAV = os.path.join(SCRATCH, "clarinet_ref.wav")
srR, ref = orc.load(REFWAV)
tsR, f0R = orc.pitch_track(srR, ref)
segR = orc.sustained_segments(tsR, f0R)
refSeg = max(segR, key=lambda s: s[1] - s[0])
hR = orc.harmonic_env(srR, ref, refSeg[0], refSeg[1], refSeg[2]); hR -= hR[0]

def measure(name, p):
    pj = os.path.join(PATCH_OUT, f"{name}.json")
    json.dump(p, open(pj, "w"), indent=2)
    wav = os.path.join(REND_OUT, f"{name}.wav")
    build_render(pj, wav, name)
    srN, ren = orc.load(wav)
    tsN, f0N = orc.pitch_track(srN, ren)
    segN = orc.sustained_segments(tsN, f0N)
    cands = [g for g in segN if abs(1200 * np.log2(g[2] / refSeg[2])) < 80]
    if not cands:
        print(f"{name:16s} no matched note"); return
    b = max(cands, key=lambda g: g[1] - g[0])
    hN = orc.harmonic_env(srN, ren, b[0], b[1], b[2]); hN -= hN[0]
    n = min(len(hR), len(hN), 10)
    d = hN[:n] - hR[:n]
    evenErr = float(np.mean(np.abs(d[1:n:2])))
    oddErr = float(np.mean(np.abs(d[2:n:2])))
    hlN, mN = orc.interharmonic_noise(srN, ren, b[0], b[1], b[2])
    aN = orc.attack_time(srN, ren, b[0], b[2])
    print(f"{name:16s} {evenErr:8.1f} {oddErr:7.1f} {hlN-mN:6.1f} "
          f"{(aN or -1)*1000:5.0f}")

if __name__ == "__main__":
    # Restore the round-2c spectral lead as c3 (soft drive x1.06, original envs)
    p3 = copy.deepcopy(base); junc_sym(p3); formants(p3)
    set_band(p3, 0.5 * 1.06, 0.6 * 1.06)
    print(f"{'cell':16s} {'evenErr':>8s} {'oddErr':>7s} {'gap':>6s} {'atk':>5s}")
    measure("c3_sym_fmt", p3)

    # Fast articulation: threshold re-measured WITH the new envelopes
    p7 = copy.deepcopy(base); junc_sym(p7); formants(p7); fast_articulation(p7)
    thr = find_threshold(p7)
    print(f"# fast-art threshold = {thr}")
    if thr:
        best = None
        for hiScale in (1.15, 1.3):
            t = copy.deepcopy(p7); set_band(t, thr * 0.98, thr * hiScale)
            x = render_probe(t)
            bm = bloom_ms(x) if x else 9999
            print(f"# band {thr*0.98:.2f}..{thr*hiScale:.2f}: quarter bloom {bm} ms")
            if best is None or bm < best[0]:
                best = (bm, hiScale)
        set_band(p7, thr * 0.98, thr * best[1])
        measure("c7_fast", p7)
        p8 = copy.deepcopy(p7); noise(p8)
        measure("c8_fast_breath", p8)
