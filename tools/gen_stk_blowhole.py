"""STK BlowHole port — patch generator + reference comparison.

Clarinet + two-port register vent + three-port dynamic tonehole
(Scavone & Cook 1998). Native-rate validation (patches carry
sampleRate 22050, compared against the 22050 references); canon later.

Tick transcription (BlowHole.h):
  breath = env*(1+ng*noise)*(1+vg*vib)         vg default 0.01
  pd  = D0.lastOut - breath
  pa  = breath + pd*reed(pd)                    reed slope -0.3 (clarinet)
  pb  = D1.lastOut
  vent<- pa+pb                                  PoleZero, CURRENT-sample
  out = D0 <- vent.out + pb ; out *= outputGain
  pa += vent.out
  pb2 = D2.lastOut ; pth = TH.lastOut           TH = tonehole PoleZero
  temp = scatter*(pa + pb2 - 2*pth)
  D2 <- OneZero(pa+temp) * -0.95
  D1 <- pb2 + temp
  TH <- pa + pb2 - pth + temp                   self z^-1 via {"tap": TH}
Delays at 22050: D0 = 5, D2 = 4 (fixed), D1 = 0.5*sr/f - 3.5 - 9.
C6/C7 are OUT OF MODEL RANGE at 22050 (D1 goes negative, the reference
itself clamps off-pitch) — documented, compared for parity only.

Usage: python tools/gen_stk_blowhole.py [--only variant]
"""
import json
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import NOTES, NOTE_NAMES, SLOT, compare  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_blowhole_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_blowhole_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "blowhole22k")

SR = 22050
VIB_FREQ = 5.735
VIB_GAIN = 0.01
# Loop-length compensation, measured vs reference (see RatioD1 comment).
# Per-note anchors, linear between, flat outside: the model is BISTABLE
# in register within a ~0.03-sample comp window at C4/C5 (its fixed
# 9-sample junction spacing vs short bores), so a global law can't hold
# every note in the reference's register — anchors can.
COMP_F = [130.81, 261.63, 523.25]
COMP_S = [0.587, 0.600, 0.576]


def comp_interp(f):
    if f <= COMP_F[0]:
        return COMP_S[0]
    for i in range(1, len(COMP_F)):
        if f <= COMP_F[i]:
            t = (f - COMP_F[i - 1]) / (COMP_F[i] - COMP_F[i - 1])
            return COMP_S[i - 1] + t * (COMP_S[i] - COMP_S[i - 1])
    return COMP_S[-1]

# Physical constants from the BlowHole constructor (sr-dependent).
RB = 0.0075          # main bore radius
RTH = 0.003          # tonehole radius
R_RH = 0.0015        # register vent radius
SCATTER = -(RTH ** 2) / (RTH ** 2 + 2 * RB ** 2)

TE_TH = 1.4 * RTH
TH_COEFF = (TE_TH * 2 * SR - 347.23) / (TE_TH * 2 * SR + 347.23)

ZETA = 347.23        # xi = 0
PSI = 2 * math.pi * RB ** 2 * (1.4 * R_RH) / (math.pi * R_RH ** 2)
RH_COEFF = (ZETA - 2 * SR * PSI) / (ZETA + 2 * SR * PSI)
RH_GAIN = -347.23 / (ZETA + 2 * SR * PSI)

# name: (amp, tonehole_openness, vent_openness, reed_slope, noise_gain)
VARIANTS = {
    "default":   (0.8, 1.0, 0.0, -0.3,   0.2),
    "th_closed": (0.8, 0.0, 0.0, -0.3,   0.2),
    "th_half":   (0.8, 0.5, 0.0, -0.3,   0.2),
    "vent_open": (0.8, 1.0, 1.0, -0.3,   0.2),
    "both":      (0.8, 0.0, 1.0, -0.3,   0.2),
    "reed_soft": (0.8, 1.0, 0.0, -0.401, 0.2),
    "reed_hard": (0.8, 1.0, 0.0, -0.206, 0.2),
    "noise_hi":  (0.8, 1.0, 0.0, -0.3,   0.3),
    "noise_off": (0.8, 1.0, 0.0, -0.3,   0.0),
}


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def mul(nid, a, b):
    return {"id": nid, "type": "CombinedSource", "params": {
        "gainAdj": 0.0, "operation": 1, "source1": a, "source2": b}}


def add(nid, a, b):
    return {"id": nid, "type": "CombinedSource", "params": {
        "gainAdj": 0.0, "operation": 3, "source1": a, "source2": b}}


def make_patch(amp, th_open, vent_open, slope, noise_gain):
    target = 0.55 + 0.30 * amp
    attack = target / (0.005 * amp * SR)
    release = target / (0.005 * SR)
    out_gain = amp + 0.001
    sustain_sec = 1.7 - attack
    # Clarinet-style reed (negative slope): rc = 0.7 + slope*pd, [-1,1].
    x_hi = (1.0 - 0.7) / slope     # rc=+1 (negative x)
    x_lo = (-1.0 - 0.7) / slope    # rc=-1 (positive x)
    # Tonehole coefficient for this openness.
    th_c = th_open * (TH_COEFF - 0.9995) + 0.9995
    vent_g = vent_open * RH_GAIN

    R = lambda i: {"ref": i}
    T = lambda i: {"tap": i}
    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f3", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f4", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f5", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f6", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "BreathEnv", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": round(target, 6),
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": [lin_stage(0.0, 1.0, round(attack, 6)),
                       lin_stage(1.0, 1.0, round(sustain_sec, 6)),
                       lin_stage(1.0, 0.0, round(release, 6))]}},
        {"id": "BreathNoise", "type": "WhiteNoiseSource", "params": {
            "amplitude": noise_gain, "boost": 0.0, "continuity": 0.0,
            "density": 1.0, "zeroCrossTendency": 0.0}},
        {"id": "VibSine", "type": "SineSource", "params": {
            "frequency": VIB_FREQ, "amplitude": VIB_GAIN}},
        add("NoisePlus1", 1.0, R("BreathNoise")),
        add("VibPlus1", 1.0, R("VibSine")),
        mul("Mods", R("NoisePlus1"), R("VibPlus1")),
        mul("Breath", R("BreathEnv"), R("Mods")),
        # pd = D0.lastOut - breath
        mul("NegBreath", R("Breath"), -1.0),
        add("Pd", T("D0"), R("NegBreath")),
        {"id": "ReedRc", "type": "Shaper", "params": {
            "source": R("Pd"), "drive": 1.0, "smoothness": 0.5,
            "morph": 0.0, "breakaway": 0.6, "capture": 0.0,
            "values": [round(x_hi, 6), 1.0, round(x_lo, 6), -1.0]}},
        mul("Scatter0", R("Pd"), R("ReedRc")),
        add("Pa0", R("Breath"), R("Scatter0")),      # pa before vent add
        # vent: PoleZero(b0=1,b1=1,a1=RH_COEFF) * vent_g, CURRENT sample
        add("PaPb", R("Pa0"), T("D1")),
        {"id": "Vent", "type": "Biquad", "params": {
            "source": R("PaPb"),
            "b0": round(vent_g, 8), "b1": round(vent_g, 8), "b2": 0.0,
            "a1": round(RH_COEFF, 8), "a2": 0.0}},
        # D0 input = vent.out + pb ; D0 output IS the instrument output
        add("D0In", R("Vent"), T("D1")),
        {"id": "RatioD0", "type": "CurveNode", "params": {
            "exprKnots": [{"a": 5.0 / SR, "b": 0.0,
                           "form": "linear", "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": R("__perf_f3")}},
        {"id": "D0", "type": "DelayLine", "params": {
            "source": R("D0In"), "frequency": R("__perf_f1"),
            "ratio": R("RatioD0"), "amplitude": 1.0, "compensate": False}},
        # pa = pa0 + vent.out
        add("Pa", R("Pa0"), R("Vent")),
        # temp = scatter * (pa + pb2 - 2*pth)
        mul("NegPth2", T("ThFilt"), -2.0),
        add("PaPb2", R("Pa"), T("D2")),
        add("JuncSum", R("PaPb2"), R("NegPth2")),
        mul("Temp", R("JuncSum"), SCATTER),
        # D2 input = OneZero(pa + temp) * -0.95
        add("PaTemp", R("Pa"), R("Temp")),
        {"id": "BoreLP", "type": "Biquad", "params": {
            "source": R("PaTemp"),
            "b0": 0.5, "b1": 0.5, "b2": 0.0, "a1": 0.0, "a2": 0.0}},
        mul("D2In", R("BoreLP"), -0.95),
        {"id": "RatioD2", "type": "CurveNode", "params": {
            "exprKnots": [{"a": 4.0 / SR, "b": 0.0,
                           "form": "linear", "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": R("__perf_f4")}},
        {"id": "D2", "type": "DelayLine", "params": {
            "source": R("D2In"), "frequency": R("__perf_f5"),
            "ratio": R("RatioD2"), "amplitude": 1.0, "compensate": False}},
        # D1 input = pb2 + temp
        add("D1In", T("D2"), R("Temp")),
        {"id": "RatioD1", "type": "CurveNode", "params": {
            "exprKnots": [{"a": 0.5 - 12.5 / SR * 0.0, "b": -12.5,
                           "form": "hz_ratio_placeholder", "x": 0.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": R("__perf_f6")}},
        {"id": "D1", "type": "DelayLine", "params": {
            "source": R("D1In"), "frequency": R("__perf_f2"),
            "ratio": R("RatioD1"), "amplitude": 1.0, "compensate": False}},
        # tonehole: PoleZero(b0=th_c, b1=-1, a1=-th_c); input self-taps
        mul("NegPth", T("ThFilt"), -1.0),
        add("ThInA", R("PaPb2"), R("NegPth")),
        add("ThIn", R("ThInA"), R("Temp")),
        {"id": "ThFilt", "type": "Biquad", "params": {
            "source": R("ThIn"),
            "b0": round(th_c, 8), "b1": -1.0, "b2": 0.0,
            "a1": round(-th_c, 8), "a2": 0.0}},
        mul("Out", R("D0"), round(out_gain, 6)),
    ]

    # D1 ratio: len = 0.5*sr/f - 12.5 + comp(f), as a knot table (the
    # comp term makes ratio quadratic in f). comp is MEASURED against the
    # reference (noise_off, round 1: port sharp 11.8/24.7/53.6c at
    # C3/C4/C5 = loop short by 0.577/0.606/0.664 samples — near-constant
    # with a small f-slope; junction-phase residual, same class the bowed
    # port calibrated with COMP_DELTA). comp(f) = COMP_A + COMP_B*f.
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        length = 0.5 * SR / f - 12.5 + comp_interp(f)
        knots.append([round(f, 4), round(max(0.0, length) * f / SR, 8)])
    for n in nodes:
        if n["id"] == "RatioD1":
            n["params"]["exprKnots"] = []
            n["params"]["mode"] = "points"
            n["params"]["knots"] = knots

    perf_ids = [n["id"] for n in nodes if n["type"] == "PerformNode"]
    return {
        "sampleRate": SR, "seconds": 10.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": n, "time": SLOT * k, "duration": SLOT,
                   "velocity": 0.8} for k, n in enumerate(NOTES)],
        "graph": {"output": "Out", "nodes": nodes},
        "ui": {"noteFaces": [{"fields": {"frequency": p},
                              "label": f"Note{i + 1}"}
                             for i, p in enumerate(perf_ids)]},
    }


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)

    print(f"{'variant':12s} slot   f0-vs-ref   envCorr   gainFit   Hprof(dB)")
    for base, args in VARIANTS.items():
        if only and base != only:
            continue
        patch = make_patch(*args)
        pj = os.path.join(PATCH_OUT, f"stk_blowhole_{base}.json")
        pw = os.path.join(REND_OUT, f"stk_blowhole_{base}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{base:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(REF_DIR, f"stk_blowhole_{base}.wav")
        for i, dc, ec, g, hd in compare(base, pw, ref):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{base:12s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
