#!/usr/bin/env python3
"""Generate patches/ks_piano/* — KS piano per Balazs Gyutai's Alpha Forever
description (dsp run 24).

Chain: decaying-envelope excitation -> HammerBank (4 SVF bandpasses, fast
decaying resonance) -> KSPianoString (3 detuned combs w/ 2 biquad allpasses,
nested-allpass inharmonic loop, global negative feedback) -> small Reverb body.

Ladder:
  v1_string : string combs only (no hammer, no dispersion, no inharm, no fb)
  v2_hammer : + hammer chain
  v3_full   : + dispersion + inharmonic loop + negative feedback + reverb body
  v4_apres  : AllpassResonator variant (AF's packaged 4-knob node) + hammer + body
  v3_phrase : v3 graph, short two-hand phrase score

Per-note tracking via instrument.paramMap frequency targets; scalar configs
(t60, brightness, dispersion, ...) ride frequency->curve entries calibrated
against research/ml_ears/out/piano_analysis_report.md (B(f0), double decay).
"""
import json
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "patches", "ks_piano")

C246_SCORE = [
    {"note": 36, "velocity": 0.85, "time": 0.0, "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 4.0, "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 8.0, "duration": 3.5},
]

# Two-hand phrase in C: LH broken octaves/fifths, RH melody line.
PHRASE_SCORE = [
    # LH
    {"note": 36, "velocity": 0.80, "time": 0.0, "duration": 1.0},   # C2
    {"note": 43, "velocity": 0.70, "time": 1.0, "duration": 1.0},   # G2
    {"note": 48, "velocity": 0.72, "time": 2.0, "duration": 1.0},   # C3
    {"note": 43, "velocity": 0.68, "time": 3.0, "duration": 2.0},   # G2
    # RH
    {"note": 64, "velocity": 0.85, "time": 0.0, "duration": 0.5},   # E4
    {"note": 67, "velocity": 0.82, "time": 0.5, "duration": 0.5},   # G4
    {"note": 72, "velocity": 0.88, "time": 1.0, "duration": 0.5},   # C5
    {"note": 71, "velocity": 0.80, "time": 1.5, "duration": 0.5},   # B4
    {"note": 69, "velocity": 0.78, "time": 2.0, "duration": 0.5},   # A4
    {"note": 67, "velocity": 0.80, "time": 2.5, "duration": 0.5},   # G4
    {"note": 64, "velocity": 0.82, "time": 3.0, "duration": 2.0},   # E4
    {"note": 60, "velocity": 0.75, "time": 3.0, "duration": 2.0},   # C4
]

# Calibration curves (log-hz interp in the engine).
# t60: from measured prompt slopes (60/rate dB/s), C4 tamed vs the -27 dB/s
# outlier (unison veering inflates it).
T60_CURVE        = [[65.0, 25.0], [262.0, 15.0], [1047.0, 9.0]]
BRIGHT_CURVE     = [[65.0, 0.65], [262.0, 0.78], [1047.0, 0.93]]
# Dispersion pole: calibrated with PHASE delay (the resonance condition),
# not group delay. Targets Iowa B: C2 1.2e-4 (n12 mult 12.10), C4 2.9e-4
# (n12 mult 12.25), C6 4.4e-3 (n2 mult 2.017).
DISP_CURVE       = [[65.0, 0.855], [262.0, 0.61], [1047.0, 0.50]]
INHARM_CURVE     = [[65.0, 0.50], [262.0, 0.15], [1047.0, 0.03]]
# AllpassResonator equivalents
AR_FB_CURVE      = [[65.0, 0.9995], [262.0, 0.9980], [1047.0, 0.9950]]
AR_DAMP_CURVE    = [[65.0, 0.60], [262.0, 0.72], [1047.0, 0.85]]
AR_STIFF_CURVE   = [[65.0, 0.50], [262.0, 0.35], [1047.0, 0.30]]


def env_node():
    # "the input is not a noise burst, just a simple decaying envelope"
    return {
        "id": "env",
        "type": "Envelope",
        "params": {
            "preset": "adsr", "timeMode": "seconds",
            "attack": 0.0002, "decay": 0.008,
            "sustainLevel": 0.0, "release": 0.0,
        },
    }


def hammer_node(source="env"):
    return {
        "id": "hammer",
        "type": "HammerBank",
        "params": {
            "source": {"ref": source},
            "frequency": 220.0,
            "numBands": 4,
            "harm1": 1.0, "harm2": 2.0, "harm3": 3.0, "harm4": 4.0,
            "resStart": 25.0, "resEnd": 1.2, "resDecay": 0.012,
            "bandTilt": -0.25, "direct": 0.25, "gain": 1.0,
        },
    }


def string_node(source, full):
    p = {
        "source": {"ref": source},
        "frequency": 220.0,
        "numCombs": 3,
        "detune": 1.8 if full else 1.5,
        "t60": 6.0,
        "brightness": 0.6,
        "exciteGain": 0.1,
        "direct": 0.15,
    }
    if full:
        p.update({
            "dispersion": 0.16,
            "inharmGain": 0.25,
            "inharmFb": 0.90, "inharmHp": 150.0,
            "ap1": 0.55, "ap2": 0.35, "ap3": 0.20,
            "fbCoeff": 0.3,   # headroom-normalized (see ks_piano_string.h)
        })
    else:
        p.update({"dispersion": 0.0, "inharmGain": 0.0, "fbCoeff": 0.0})
    return {"id": "string", "type": "KSPianoString", "params": p}


def verb_node(source):
    # "The body is simulated using a small reverb."
    return {
        "id": "verb",
        "type": "Reverb",
        "params": {
            "source": {"ref": source},
            "roomSize": 0.18, "damping": 0.55, "wet": 0.18, "dry": 0.85,
        },
    }


def string_map(full, with_hammer):
    targets = ["string.frequency"]
    if with_hammer:
        targets.append("hammer.frequency")
    targets += [
        {"target": "string.t60",        "curve": T60_CURVE},
        {"target": "string.brightness", "curve": BRIGHT_CURVE},
    ]
    if full:
        targets += [
            {"target": "string.dispersion", "curve": DISP_CURVE},
            {"target": "string.inharmGain", "curve": INHARM_CURVE},
        ]
    return {"frequency": targets}


def patch(nodes, output, param_map, score, polyphony=1):
    return {
        "sampleRate": 48000,
        "graph": {"nodes": nodes, "output": output},
        "instrument": {"polyphony": polyphony, "paramMap": param_map},
        "score": score,
    }


def build_all():
    patches = {}

    # v1 — string combs only
    patches["v1_string.json"] = patch(
        [env_node(), string_node("env", full=False)],
        "string", string_map(full=False, with_hammer=False), C246_SCORE)

    # v2 — + hammer chain
    patches["v2_hammer.json"] = patch(
        [env_node(), hammer_node(), string_node("hammer", full=False)],
        "string", string_map(full=False, with_hammer=True), C246_SCORE)

    # v3 — the full description
    patches["v3_full.json"] = patch(
        [env_node(), hammer_node(), string_node("hammer", full=True),
         verb_node("string")],
        "verb", string_map(full=True, with_hammer=True), C246_SCORE)

    # v4 — AllpassResonator variant
    ar = {
        "id": "apres",
        "type": "AllpassResonator",
        "params": {
            "source": {"ref": "hammer"},
            "frequency": 220.0,
            "feedback": 0.996, "damping": 0.6,
            "stiffness": 0.15, "tension": 0.30,
            "exciteGain": 0.3, "direct": 0.15,
        },
    }
    ar_map = {"frequency": [
        "apres.frequency", "hammer.frequency",
        {"target": "apres.feedback",  "curve": AR_FB_CURVE},
        {"target": "apres.damping",   "curve": AR_DAMP_CURVE},
        {"target": "apres.stiffness", "curve": AR_STIFF_CURVE},
    ]}
    patches["v4_apres.json"] = patch(
        [env_node(), hammer_node(), ar, verb_node("apres")],
        "verb", ar_map, C246_SCORE)

    # v3 phrase — two hands
    patches["v3_phrase.json"] = patch(
        [env_node(), hammer_node(), string_node("hammer", full=True),
         verb_node("string")],
        "verb", string_map(full=True, with_hammer=True), PHRASE_SCORE,
        polyphony=6)

    return patches


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, p in build_all().items():
        path = os.path.join(OUT, name)
        with open(path, "w") as f:
            json.dump(p, f, indent=2)
        print("wrote", path)


if __name__ == "__main__":
    main()
