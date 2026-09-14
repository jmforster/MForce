"""STK Flute port - patch generator + reference comparison.

Same campaign/conventions as gen_stk_bowed.py / gen_stk_clarinet.py.
No new engine nodes: jet nonlinearity x^3 - x built from Multiply nodes
with an identity Shaper providing the +-1 clamp; OnePole bore loss and
the DC blocker (1 - z^-1)/(1 - 0.99 z^-1) are Biquads.

Delays: STK sets bore = sr/f' - phaseDelay_OnePole(f') - 1 with
f' = 0.66666*f (overblown), jet = bore * jetRatio. The one-pole phase
delay is not affine in f, so the ratio pins get points-mode CurveNodes
with knots at every semitone C2..C8 computed from the exact formula —
exact at every playable note, interpolated (sub-cent) between.

Usage: python tools/gen_stk_flute.py [--only variant]
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_flute_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_flute_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "flute")

POLE = 0.7 - 0.1 * 22050.0 / SR        # 0.6540625 (STK's formula at 48k)
LP_B0 = 1.0 - POLE                     # OnePole unity-ish gain form
OVERBLOW = 0.66666                     # STK's literal constant
# Canonical: the 22050-native flute re-realized at 48k (same recipe as
# bowed/clarinet canon — STK_PORT_NOTES). Native pole 0.7-0.1 = 0.6 at
# 22050; same cutoff Hz at 48k => 0.6^(22050/48000). DC blocker pole
# likewise. Gestures in 22050-seconds. Delay from the loop identity
# (total loop time = 1/f'), jet as exact canonical transport seconds.
POLE_CANON = 0.6 ** (22050.0 / SR)     # 0.790846
DC_POLE_CANON = 0.99 ** (22050.0 / SR)
LOOP_CLOSE = 0.05
COMP_DELTA_F = [130.81, 261.63, 523.25, 1046.5, 2093.0]
COMP_DELTA = [0.0, 0.0, 0.0, 0.0, 0.0]


def _interp(x, xs, ys):
    if x <= xs[0]:
        return ys[0]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            t = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
            return ys[i - 1] + t * (ys[i] - ys[i - 1])
    return ys[-1]


def pole_phase_delay(p, f, fs):
    """Phase delay (samples at fs) of y = (1-p)x + p*y1 at f."""
    w = 2.0 * math.pi * f / fs
    return math.atan2(p * math.sin(w), 1.0 - p * math.cos(w)) / w

# name: (amp, jet_ratio, noise_gain, vib_gain) - mirrors flute_ref.cpp
VARIANTS = {
    "default":   (0.8, 0.32, 0.15, 0.05),
    "jet_lo":    (0.8, 0.20, 0.15, 0.05),
    "jet_hi":    (0.8, 0.44, 0.15, 0.05),
    "noise_hi":  (0.8, 0.32, 0.30, 0.05),
    "noise_off": (0.8, 0.32, 0.00, 0.05),
    "vib_off":   (0.8, 0.32, 0.15, 0.00),
    "soft":      (0.3, 0.32, 0.15, 0.05),
    "hard":      (1.0, 0.32, 0.15, 0.05),
}
VIB_FREQ = 5.925


def onepole_phase_delay(f):
    """Samples of delay of y = (1-p)x + p*y1 at frequency f (STK OnePole)."""
    w = 2.0 * math.pi * f / SR
    return math.atan2(POLE * math.sin(w), 1.0 - POLE * math.cos(w)) / w


def ratio_knots(scale):
    """CurveNode points: note frequency -> DelayLine ratio, semitones C2..C8.

    len(f) = scale * (sr/f' - pd(f') - 1), f' = OVERBLOW*f;
    ratio = len * f / sr (DelayLine: len = sr/f * ratio).
    """
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        fp = OVERBLOW * f
        length = SR / fp - onepole_phase_delay(fp) - 1.0
        knots.append([round(f, 4), round(scale * length * f / SR, 8)])
    return knots


def canon_bore_knots():
    """Loop identity: total bore loop time = 1/f' seconds. At 48k:
    len = 48000/f' - pd(new pole at f') - 1 (tap) + calibration delta."""
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        fp = OVERBLOW * f
        delta = _interp(f, COMP_DELTA_F, COMP_DELTA)
        length = SR / fp - pole_phase_delay(POLE_CANON, fp, SR) - 1.0 + delta
        knots.append([round(f, 4), round(length * f / SR, 8)])
    return knots


def canon_jet_knots(jet_ratio):
    """Jet = pure transport: canonical seconds = L22*jet_ratio/22050."""
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        fp = OVERBLOW * f
        l22 = 22050.0 / fp - pole_phase_delay(0.6, fp, 22050.0) - 1.0
        length = l22 * jet_ratio * (SR / 22050.0)
        knots.append([round(f, 4), round(length * f / SR, 8)])
    return knots


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(amp, jet_ratio, noise_gain, vib_gain, canon=True):
    max_pressure = (1.1 + 0.2 * amp) / 0.8
    rate_sr = 22050.0 if canon else SR     # STK ADSR rates are per-sample
    attack = 1.0 / (0.02 * amp * rate_sr)
    release = 0.8 / (0.01 * rate_sr)       # noteOff(0.5): rate 0.01, from 0.8
    out_const = 0.3 * (amp + 0.001)
    pole = POLE_CANON if canon else POLE
    lp_b0 = 1.0 - pole
    dc_pole = DC_POLE_CANON if canon else 0.99

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
            "minValue": 0.0, "maxValue": round(max_pressure, 6),
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction" if canon else "seconds",
            "timeScale": 1.0,
            "stages": ([
                pin_stage(0.0, 1.0, round(attack, 6)),
                pin_stage(1.0, 0.8, 0.01),
                expand_stage(0.8),
                pin_stage(0.8, 0.0, round(release, 6)),
                pin_stage(0.0, 0.0, LOOP_CLOSE)] if canon else [
                lin_stage(0.0, 1.0, round(attack, 6)),
                lin_stage(1.0, 0.8, 0.01),
                lin_stage(0.8, 0.8, round(1.7 - attack - 0.01, 6)),
                lin_stage(0.8, 0.0, round(release, 6))])}},
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
        # temp = -OnePole(bore.lastOut): negation folded into b0.
        {"id": "TempF", "type": "Biquad", "params": {
            "source": {"tap": "Bore"},
            "b0": round(-lp_b0, 8), "b1": 0.0, "b2": 0.0,
            "a1": round(-pole, 8), "a2": 0.0}},
        # pd0 = breath - 0.5*temp
        {"id": "JetRefl", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "TempF"}, "source2": -0.5}},
        {"id": "Pd0", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Breath"}, "source2": {"ref": "JetRefl"}}},
        {"id": "RatioJet", "type": "CurveNode", "params": {
            "exprKnots": [], "interp": "linear",
            "knots": canon_jet_knots(jet_ratio) if canon
                     else ratio_knots(jet_ratio),
            "mode": "points", "source": {"ref": "__perf_f3"}}},
        {"id": "JetDelay", "type": "DelayLine", "params": {
            "source": {"ref": "Pd0"},
            "frequency": {"ref": "__perf_f2"},
            "ratio": {"ref": "RatioJet"},
            "amplitude": 1.0, "compensate": False}},
        # jet nonlinearity x^3 - x, saturated +-1 by an identity Shaper
        {"id": "JetSq", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "JetDelay"}, "source2": {"ref": "JetDelay"}}},
        {"id": "JetSqM1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "JetSq"}, "source2": -1.0}},
        {"id": "JetPoly", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "JetDelay"}, "source2": {"ref": "JetSqM1"}}},
        {"id": "JetClamp", "type": "Shaper", "params": {
            "source": {"ref": "JetPoly"}, "drive": 1.0, "smoothness": 0.5,
            "morph": 0.0, "breakaway": 0.6, "capture": 0.0,
            "values": [-1.0, -1.0, 1.0, 1.0]}},
        {"id": "DCBlock", "type": "Biquad", "params": {
            "source": {"ref": "JetClamp"},
            "b0": 1.0, "b1": -1.0, "b2": 0.0,
            "a1": round(-dc_pole, 8), "a2": 0.0}},
        {"id": "EndRefl", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "TempF"}, "source2": 0.5}},
        {"id": "BoreIn", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "DCBlock"}, "source2": {"ref": "EndRefl"}}},
        {"id": "RatioBore", "type": "CurveNode", "params": {
            "exprKnots": [], "interp": "linear",
            "knots": canon_bore_knots() if canon else ratio_knots(1.0),
            "mode": "points", "source": {"ref": "__perf_f4"}}},
        {"id": "Bore", "type": "DelayLine", "params": {
            "source": {"ref": "BoreIn"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": {"ref": "RatioBore"},
            "amplitude": {"ref": "LoopGate"} if canon else 1.0,
            "compensate": False}},
        {"id": "Out", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Bore"}, "source2": round(out_const, 6)}},
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
    for base, (amp, jr, ng, vg) in VARIANTS.items():
        if only and base != only:
            continue
        name = base + suffix
        patch = make_patch(amp, jr, ng, vg, canon=not verbatim)
        pj = os.path.join(PATCH_OUT, f"stk_flute_{name}.json")
        pw = os.path.join(REND_OUT, f"stk_flute_{name}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(ref_dir, f"stk_flute_{base}.wav")
        for i, dc, ec, g, hd in compare(name, pw, ref):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{name:12s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
