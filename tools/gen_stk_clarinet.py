"""STK Clarinet port - patch generator + reference comparison.

Same campaign/conventions as gen_stk_bowed.py (spec 2026-09-13). The
clarinet needs NO new engine nodes: OneZero = Biquad(0.5, 0.5), ReedTable
= a two-point linear Shaper curve (offset 0.7, slope per stiffness,
clamped +-1 exactly as STK), breath = Envelope * (1 + noise + vibrato).

Delay: len = 0.5*sr/f - 0.5 (OneZero linear phase) - 1 (lastOut)
       => ratio(f) = 0.5 - 1.5*f/sr.

NOTE: STK's Noise seeds from time(), so noise realizations never match
sample-wise; f0 / envelope / harmonic-peak metrics are noise-robust.

Usage: python tools/gen_stk_clarinet.py [--only variant]
"""
import json
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import (NOTES, NOTE_NAMES, SLOT, SR, compare,  # noqa: E402
                           pin_stage, expand_stage)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_clarinet_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_clarinet_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "clarinet")

# name: (amp, reed_slope, noise_gain, vib_gain) - mirrors clarinet_ref.cpp
VARIANTS = {
    "default":   (0.8, -0.3,   0.2, 0.1),
    "reed_soft": (0.8, -0.401, 0.2, 0.1),
    "reed_hard": (0.8, -0.206, 0.2, 0.1),
    "noise_hi":  (0.8, -0.3,   0.3, 0.1),
    "noise_off": (0.8, -0.3,   0.0, 0.1),
    "vib_off":   (0.8, -0.3,   0.2, 0.0),
    "soft":      (0.3, -0.3,   0.2, 0.1),
    "hard":      (1.0, -0.3,   0.2, 0.1),
}
VIB_FREQ = 5.735

# ---- canonical mode (default; --verbatim = STK@48k null form) -------------
# The 22050-native clarinet re-realized at 48k, same recipe as the bowed
# canon (STK_PORT_NOTES): loop filter refit in Hz, gestures in
# 22050-seconds, duration-adaptive envelope, loop-closure gate, delay from
# the exact loop-time identity total = 0.5/f, calibrated vs the 22050 ref.
# Loop filter: OneZero(0.5,0.5)@22050 has |H| = cos(2*pi*f/44100), zero at
# 11025. 48k biquad fit with the zero pinned on the unit circle at 11025
# and DC = 1 (scipy fit 2026-09-13: band err max 0.075, |H|<=0.32 above).
CANON_LP = (0.293627, -0.074746, 0.293627, -0.782753, 0.295260)
LOOP_CLOSE = 0.06
CANON_STRUCT = 1.0   # tap z^-1; residual absorbed by COMP_DELTA
COMP_DELTA_F = [130.81, 261.63, 523.25, 1046.5, 2093.0]
# Sub-sample f0 measurement (round 2) showed the loop-identity knots are
# essentially exact: all five notes converge on the same tiny residual.
COMP_DELTA = [-0.04, -0.04, -0.04, -0.04, -0.04]


def lp_phase_delay(f):
    """Exact phase delay (samples at 48k) of CANON_LP at frequency f."""
    b0, b1, b2, a1, a2 = CANON_LP
    w = 2.0 * math.pi * f / SR
    zr, zi = math.cos(-w), math.sin(-w)
    z2r, z2i = math.cos(-2 * w), math.sin(-2 * w)
    nr = b0 + b1 * zr + b2 * z2r
    ni = b1 * zi + b2 * z2i
    dr = 1 + a1 * zr + a2 * z2r
    di = a1 * zi + a2 * z2i
    ph = math.atan2(ni, nr) - math.atan2(di, dr)
    return -ph / w


def canon_knots():
    """Note f -> bore ratio; loop identity: len = 0.5*sr/f - pd - struct."""
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        delta = _interp(f, COMP_DELTA_F, COMP_DELTA)
        length = 0.5 * SR / f - lp_phase_delay(f) - CANON_STRUCT + delta
        knots.append([round(f, 4), round(length * f / SR, 8)])
    return knots


def _interp(x, xs, ys):
    if x <= xs[0]:
        return ys[0]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            t = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
            return ys[i - 1] + t * (ys[i] - ys[i - 1])
    return ys[-1]


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(amp, slope, noise_gain, vib_gain, canon=True):
    target = 0.55 + 0.30 * amp            # STK breath target
    rate_sr = 22050.0 if canon else SR    # STK rates are per-sample
    attack = target / (0.005 * amp * rate_sr)
    release = target / (0.005 * rate_sr)  # noteOff(0.5): rate 0.005
    out_gain = amp + 0.001
    # ReedTable: rc = 0.7 + slope*pd clamped to [-1, 1]; slope < 0 so the
    # curve runs from (x at rc=+1) up to (x at rc=-1), ends clamped = STK.
    x_hi = (1.0 - 0.7) / slope            # rc = +1 (negative x)
    x_lo = (-1.0 - 0.7) / slope           # rc = -1 (positive x)

    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "BreathEnv", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": round(target, 6),
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction" if canon else "seconds",
            "timeScale": 1.0,
            "stages": ([
                pin_stage(0.0, 1.0, round(attack, 6)),
                expand_stage(1.0),
                pin_stage(1.0, 0.0, round(release, 6)),
                pin_stage(0.0, 0.0, LOOP_CLOSE)] if canon else [
                lin_stage(0.0, 1.0, round(attack, 6)),
                lin_stage(1.0, 1.0, round(1.7 - attack, 6)),
                lin_stage(1.0, 0.0, round(release, 6))])}},
        {"id": "BreathNoise", "type": "WhiteNoiseSource", "params": {
            "amplitude": noise_gain, "boost": 0.0, "continuity": 0.0,
            "density": 1.0, "zeroCrossTendency": 0.0}},
        {"id": "VibSine", "type": "SineSource", "params": {
            "frequency": VIB_FREQ, "amplitude": vib_gain}},
        {"id": "ModSum", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "BreathNoise"}, "source2": {"ref": "VibSine"}}},
        {"id": "ModPlus1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": 1.0, "source2": {"ref": "ModSum"}}},
        {"id": "Breath", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "BreathEnv"}, "source2": {"ref": "ModPlus1"}}},
        {"id": "NegBreath", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Breath"}, "source2": -1.0}},
        # Commuted bore loss: -0.95 * loop filter (canon: 48k refit of the
        # 22050 OneZero; verbatim: OneZero(0.5, 0.5) as-is)
        {"id": "BoreLP", "type": "Biquad", "params": dict(
            {"source": {"tap": "Bore"}},
            **(dict(zip(("b0", "b1", "b2", "a1", "a2"), CANON_LP)) if canon
               else {"b0": 0.5, "b1": 0.5, "b2": 0.0,
                     "a1": 0.0, "a2": 0.0}))},
        {"id": "Refl", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "BoreLP"}, "source2": -0.95}},
        {"id": "Pd", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Refl"}, "source2": {"ref": "NegBreath"}}},
        {"id": "ReedRc", "type": "Shaper", "params": {
            "source": {"ref": "Pd"}, "drive": 1.0, "smoothness": 0.5,
            "morph": 0.0, "breakaway": 0.6, "capture": 0.0,
            "values": [round(x_hi, 6), 1.0, round(x_lo, 6), -1.0]}},
        {"id": "Scatter", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Pd"}, "source2": {"ref": "ReedRc"}}},
        {"id": "BoreIn", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Breath"}, "source2": {"ref": "Scatter"}}},
        {"id": "RatioBore", "type": "CurveNode", "params": (
            {"exprKnots": [], "interp": "linear",
             "knots": canon_knots(), "mode": "points",
             "source": {"ref": "__perf_f2"}} if canon else
            {"exprKnots": [{"a": -1.5 / SR, "b": 0.5,
                            "form": "linear", "x": 440.0}],
             "interp": "linear", "knots": [], "mode": "expressions",
             "source": {"ref": "__perf_f2"}})},
        {"id": "Bore", "type": "DelayLine", "params": {
            "source": {"ref": "BoreIn"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": {"ref": "RatioBore"},
            "amplitude": {"ref": "LoopGate"} if canon else 1.0,
            "compensate": False}},
        {"id": "Out", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Bore"}, "source2": round(out_gain, 6)}},
    ]

    if canon:
        at = next(i for i, n in enumerate(nodes) if n["id"] == "Bore")
        nodes[at:at] = [
            {"id": "LoopGate", "type": "Envelope", "params": {
                "minValue": 0.0, "maxValue": 1.0,
                "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
                "timeMode": "fraction", "timeScale": 1.0,
                "stages": [expand_stage(1.0),
                           pin_stage(1.0, 0.0, LOOP_CLOSE)]}}]

    dur = round(1.7 + release + LOOP_CLOSE, 4) if canon else SLOT
    perf_ids = [n["id"] for n in nodes if n["type"] == "PerformNode"]
    return {
        "sampleRate": SR, "seconds": 10.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": n, "time": SLOT * k, "duration": dur,
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

    verbatim = "--verbatim" in sys.argv
    suffix = "" if verbatim else "_canon"
    ref_dir = REF_DIR if verbatim else REF_DIR + "22k"
    print(f"{'variant':12s} slot   f0-vs-ref   envCorr   gainFit   Hprof(dB)")
    for base, (amp, slope, ng, vg) in VARIANTS.items():
        if only and base != only:
            continue
        name = base + suffix
        patch = make_patch(amp, slope, ng, vg, canon=not verbatim)
        pj = os.path.join(PATCH_OUT, f"stk_clarinet_{name}.json")
        pw = os.path.join(REND_OUT, f"stk_clarinet_{name}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(ref_dir, f"stk_clarinet_{base}.wav")
        for i, dc, ec, g, hd in compare(name, pw, ref):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{name:12s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
