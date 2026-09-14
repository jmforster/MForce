"""STK Brass port - patch generator + reference comparison.

Same campaign/conventions as the other gen_stk_*.py. Uses the Biquad
resonance mode (frequency/radius pins) for the keytracked lip filter —
lip position -> area is Lip^2 with an upper clamp (identity Shaper),
scattering is a dp-controlled crossfade mouth/bore.

Delay: STK DelayA len = 2*sr/f + 3 => ratio(f) = 2 + 3f/sr (affine).
KNOWN DEVIATION: STK uses an ALLPASS-interpolated delay; MForce
DelayLine interpolates linearly. Expect small cents/brightness residue,
measured against the reference below.

Usage: python tools/gen_stk_brass.py [--only variant]
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import (NOTES, NOTE_NAMES, SLOT, SR, compare,  # noqa: E402
                           pin_stage, expand_stage)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_brass_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_brass_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "brass")

# name: (amp, lip_mult, slide_mult, vib_gain) - mirrors brass_ref.cpp
VARIANTS = {
    "default":     (0.8, 1.0, 1.0,  0.0),
    "lip_lo":      (0.8, 0.5, 1.0,  0.0),
    "lip_hi":      (0.8, 2.0, 1.0,  0.0),
    "slide_short": (0.8, 1.0, 0.75, 0.0),
    "vibrato":     (0.8, 1.0, 1.0,  0.28125),
    "soft":        (0.3, 1.0, 1.0,  0.0),
    "hard":        (1.0, 1.0, 1.0,  0.0),
}
VIB_FREQ = 6.137

# ---- canonical mode (default; --verbatim = STK@48k null form) -------------
# 22050-native brass re-realized at 48k (STK_PORT_NOTES recipe). Lip: same
# resonance Hz, same bandwidth in Hz => radius 0.997^(22050/48000); peak
# gain preserved by impulse-invariant residue scaling b0 *= 22050/48000.
# DC blocker pole likewise. Gestures in 22050-seconds. Slide: canonical
# loop time = (2*22050/f + 3 + structural)/22050 s; the lip filter's
# resonant phase is note-dependent, so the residual is calibrated
# empirically on locked slots (COMP_DELTA, 48k samples).
LIP_R_CANON = 0.997 ** (22050.0 / SR)          # 0.9986208
LIP_B0_CANON = 0.03 * (22050.0 / SR)           # 0.0137813
DC_POLE_CANON = 0.99 ** (22050.0 / SR)
LOOP_CLOSE = 0.08
COMP_CANON = 4.0 * SR / 22050.0 - 1.0   # (3 fudge + 1 lastOut)@22050 - tap@48k
COMP_DELTA_F = [130.81, 261.63, 523.25, 1046.5, 2093.0]
COMP_DELTA = [0.7, 0.7, 0.73, 0.7, 0.7]  # round-1 from C5 +6.6c


def _interp(x, xs, ys):
    if x <= xs[0]:
        return ys[0]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            t = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
            return ys[i - 1] + t * (ys[i] - ys[i - 1])
    return ys[-1]


def canon_slide_knots(slide_mult):
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        comp = COMP_CANON + _interp(f, COMP_DELTA_F, COMP_DELTA)
        length = (2.0 * SR / f + comp) * slide_mult
        knots.append([round(f, 4), round(length * f / SR, 8)])
    return knots


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(amp, lip_mult, slide_mult, vib_gain, canon=True):
    rate_sr = 22050.0 if canon else SR    # STK ADSR rates are per-sample
    attack = 1.0 / (0.001 * amp * rate_sr)
    release = 1.0 / (0.0025 * rate_sr)    # noteOff(0.5): rate 0.0025
    lip_r = LIP_R_CANON if canon else 0.997
    lip_b0 = LIP_B0_CANON if canon else 0.03
    dc_pole = DC_POLE_CANON if canon else 0.99

    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f3", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "BreathEnv", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": amp,
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
        {"id": "VibSine", "type": "SineSource", "params": {
            "frequency": VIB_FREQ, "amplitude": vib_gain}},
        {"id": "Breath", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "BreathEnv"}, "source2": {"ref": "VibSine"}}},
        {"id": "Mouth", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Breath"}, "source2": 0.3}},
        {"id": "Bore", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"tap": "Slide"}, "source2": 0.85}},
        {"id": "NegBore", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Bore"}, "source2": -1.0}},
        {"id": "Dp0", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Mouth"}, "source2": {"ref": "NegBore"}}},
        {"id": "LipFreq", "type": "CurveNode", "params": {
            "exprKnots": [{"a": lip_mult, "b": 0.0,
                           "form": "linear", "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": {"ref": "__perf_f3"}}},
        # Canonical DC correction (root cause proven 09-13): the discrete
        # lip's DC gain at 48k is a constant ~2.17x the 22050 prototype's
        # across notes (den ratio ~w^2-dominated), while the peak is
        # preserved by the impulse-invariant b0. A fixed low-shelf ahead
        # of the lip attenuates DC by 1/2.17 with unity gain and ~zero
        # phase above ~50 Hz: b = [1, -0.99849], a1 = -0.99673 (corner
        # 25 Hz, DC 0.461).
        {"id": "LipDCShelf", "type": "Biquad", "params": {
            "source": {"ref": "Dp0"},
            "b0": 1.0, "b1": -0.999246, "b2": 0.0,
            "a1": -0.998365, "a2": 0.0}},
        {"id": "Lip", "type": "Biquad", "params": {
            "source": {"ref": "LipDCShelf"} if canon else {"ref": "Dp0"},
            "mode": 1,
            "frequency": {"ref": "LipFreq"}, "radius": round(lip_r, 8),
            "b0": round(lip_b0, 8), "b1": 0.0, "b2": 0.0,
            "a1": 0.0, "a2": 0.0}},
        {"id": "DpSq", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Lip"}, "source2": {"ref": "Lip"}}},
        {"id": "DpClamp", "type": "Shaper", "params": {
            "source": {"ref": "DpSq"}, "drive": 1.0, "smoothness": 0.5,
            "morph": 0.0, "breakaway": 0.6, "capture": 0.0,
            "values": [0.0, 0.0, 1.0, 1.0]}},
        {"id": "MixDp", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "DpClamp"}, "source2": {"ref": "Dp0"}}},
        {"id": "Mix", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Bore"}, "source2": {"ref": "MixDp"}}},
        {"id": "DCBlock", "type": "Biquad", "params": {
            "source": {"ref": "Mix"},
            "b0": 1.0, "b1": -1.0, "b2": 0.0,
            "a1": round(-dc_pole, 8), "a2": 0.0}},
        {"id": "RatioSlide", "type": "CurveNode", "params": (
            {"exprKnots": [], "interp": "linear",
             "knots": canon_slide_knots(slide_mult), "mode": "points",
             "source": {"ref": "__perf_f2"}} if canon else
            {"exprKnots": [{"a": 3.0 * slide_mult / SR, "b": 2.0 * slide_mult,
                            "form": "linear", "x": 440.0}],
             "interp": "linear", "knots": [], "mode": "expressions",
             "source": {"ref": "__perf_f2"}})},
        {"id": "Slide", "type": "DelayLine", "params": {
            "source": {"ref": "DCBlock"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": {"ref": "RatioSlide"},
            "amplitude": {"ref": "LoopGate"} if canon else 1.0,
            "compensate": False}},
    ]

    if canon:
        at = next(i for i, n in enumerate(nodes) if n["id"] == "Slide")
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
        "graph": {"output": "Slide", "nodes": nodes},
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
    for base, (amp, lm, sm, vg) in VARIANTS.items():
        if only and base != only:
            continue
        name = base + suffix
        patch = make_patch(amp, lm, sm, vg, canon=not verbatim)
        pj = os.path.join(PATCH_OUT, f"stk_brass_{name}.json")
        pw = os.path.join(REND_OUT, f"stk_brass_{name}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(ref_dir, f"stk_brass_{base}.wav")
        for i, dc, ec, g, hd in compare(name, pw, ref):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{name:12s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
