"""STK Saxofony port — patch generator + reference comparison.

The "blowed string": the bowed port's two-delay string skeleton (one
rigid, one lossy termination, blow POSITION splits the line) with the
clarinet port's reed/breath idiom at the junction. No new engine nodes.

Per the rate discipline (STK_PORT_NOTES round 5): this validates the
port AT NATIVE RATE — patches carry sampleRate 22050 and compare
against the 22050 references. Canonicalization to 48k is a separate,
later step for variants that survive ears.

Tick transcription (Saxofony.h):
  breath = env * (1 + ng*noise) * (1 + vg*vib)
  temp   = -0.95 * OneZero(D0.lastOut)          D0 = (1-pos) of the line
  out    = temp - D1.lastOut                    D1 = pos of the line
  pd     = breath - out
  D1 <- temp
  D0 <- breath - pd*reed(pd) - temp
  return out * outputGain
Delay identity: total = sr/f - 0.5 (OneZero) - 1 (lastOut), split
(1-pos)/pos. STRUCT_COMP below absorbs any MForce-vs-STK structural
sample difference — measured, not assumed (bowed-port precedent).

ReedTable slope is POSITIVE here (0.3 default): rc = 0.7 + slope*pd
clamped [-1,1] -> ascending Shaper curve.

Usage: python tools/gen_stk_saxofony.py [--only variant]
"""
import json
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import (NOTES, NOTE_NAMES, SLOT, compare,  # noqa: E402
                           )

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_saxofony_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_saxofony_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "saxofony22k")

SR = 22050  # native-rate validation
VIB_FREQ = 5.735
STRUCT_COMP = 1.5   # STK: 0.5 OneZero phase + 1.0 lastOut; adjust by measure

# name: (amp, position, reed_slope, reed_offset, noise_gain, vib_gain)
VARIANTS = {
    "default":    (0.8, 0.20, 0.3,  0.7,  0.2, 0.1),
    "pos_bridge": (0.8, 0.05, 0.3,  0.7,  0.2, 0.1),
    "pos_mid":    (0.8, 0.50, 0.3,  0.7,  0.2, 0.1),
    "pos_35":     (0.8, 0.35, 0.3,  0.7,  0.2, 0.1),
    "reed_soft":  (0.8, 0.20, 0.2,  0.7,  0.2, 0.1),
    "reed_hard":  (0.8, 0.20, 0.46, 0.7,  0.2, 0.1),
    "aper_wide":  (0.8, 0.20, 0.3,  0.94, 0.2, 0.1),
    "noise_hi":   (0.8, 0.20, 0.3,  0.7,  0.3, 0.1),
    "noise_off":  (0.8, 0.20, 0.3,  0.7,  0.0, 0.1),
    "vib_off":    (0.8, 0.20, 0.3,  0.7,  0.2, 0.0),
}


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(amp, pos, slope, offset, noise_gain, vib_gain):
    target = 0.55 + 0.30 * amp
    attack = target / (0.005 * amp * SR)      # STK rates are per-sample
    release = target / (0.005 * SR)           # noteOff(0.5) -> rate 0.005
    out_gain = amp + 0.001
    # ReedTable, positive slope: rc = offset + slope*pd in [-1,1];
    # ascending (x, y) pairs, Shaper clamps flat beyond the ends = STK.
    x_neg = (-1.0 - offset) / slope           # rc = -1
    x_pos = (1.0 - offset) / slope            # rc = +1
    sustain_sec = 1.7 - attack

    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f3", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f4", "type": "PerformNode",
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
            "frequency": VIB_FREQ, "amplitude": vib_gain}},
        {"id": "NoisePlus1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": 1.0, "source2": {"ref": "BreathNoise"}}},
        {"id": "VibPlus1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": 1.0, "source2": {"ref": "VibSine"}}},
        {"id": "Mods", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "NoisePlus1"}, "source2": {"ref": "VibPlus1"}}},
        {"id": "Breath", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "BreathEnv"}, "source2": {"ref": "Mods"}}},
        # ratio(f) = share * (1 - STRUCT_COMP * f / sr)
        {"id": "RatioD0", "type": "CurveNode", "params": {
            "exprKnots": [{"a": -(1.0 - pos) * STRUCT_COMP / SR,
                           "b": 1.0 - pos, "form": "linear", "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": {"ref": "__perf_f3"}}},
        {"id": "RatioD1", "type": "CurveNode", "params": {
            "exprKnots": [{"a": -pos * STRUCT_COMP / SR,
                           "b": pos, "form": "linear", "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": {"ref": "__perf_f4"}}},
        # temp = -0.95 * OneZero(D0.lastOut)
        {"id": "StringLP", "type": "Biquad", "params": {
            "source": {"tap": "D0"},
            "b0": 0.5, "b1": 0.5, "b2": 0.0, "a1": 0.0, "a2": 0.0}},
        {"id": "Temp", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "StringLP"}, "source2": -0.95}},
        # out = temp - D1.lastOut
        {"id": "NegD1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"tap": "D1"}, "source2": -1.0}},
        {"id": "OutRaw", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Temp"}, "source2": {"ref": "NegD1"}}},
        # pd = breath - out
        {"id": "NegOut", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "OutRaw"}, "source2": -1.0}},
        {"id": "Pd", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Breath"}, "source2": {"ref": "NegOut"}}},
        {"id": "ReedRc", "type": "Shaper", "params": {
            "source": {"ref": "Pd"}, "drive": 1.0, "smoothness": 0.5,
            "morph": 0.0, "breakaway": 0.6, "capture": 0.0,
            "values": [round(x_neg, 6), -1.0, round(x_pos, 6), 1.0]}},
        {"id": "Scatter", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Pd"}, "source2": {"ref": "ReedRc"}}},
        {"id": "NegScatter", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Scatter"}, "source2": -1.0}},
        {"id": "NegTemp", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Temp"}, "source2": -1.0}},
        {"id": "D0InA", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Breath"}, "source2": {"ref": "NegScatter"}}},
        {"id": "D0In", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "D0InA"}, "source2": {"ref": "NegTemp"}}},
        {"id": "D0", "type": "DelayLine", "params": {
            "source": {"ref": "D0In"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": {"ref": "RatioD0"},
            "amplitude": 1.0, "compensate": False}},
        {"id": "D1", "type": "DelayLine", "params": {
            "source": {"ref": "Temp"},
            "frequency": {"ref": "__perf_f2"},
            "ratio": {"ref": "RatioD1"},
            "amplitude": 1.0, "compensate": False}},
        {"id": "Out", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "OutRaw"}, "source2": round(out_gain, 6)}},
    ]

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
        pj = os.path.join(PATCH_OUT, f"stk_saxofony_{base}.json")
        pw = os.path.join(REND_OUT, f"stk_saxofony_{base}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{base:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(REF_DIR, f"stk_saxofony_{base}.wav")
        for i, dc, ec, g, hd in compare(base, pw, ref):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{base:12s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
