#!/usr/bin/env python3
"""Generate patches/pending/ks_piano_v5/* — the FULL Gyutai description in
every patch (Matt 2026-08-08: no more partial ladders).

All seven patches contain all six described elements:
  1. decaying-envelope excitation (no noise burst)
  2. HammerBank: 4 SVF bandpasses at harmonics, fast-decaying resonance
  3. nested-allpass inharmonic loop (3x first-order, HP-filtered comb)
  4. 3 unison-detuned combs with 2 biquad allpasses each (dispersion)
  5. small global negative feedback (double envelope)
  6. small reverb body

Differences vs the v3_full render Matt heard: the direct taps
(hammer.direct 0.25 / string.direct 0.15) that let the raw envelope thump
bypass the resonant chain are removed from the baseline (the description
has no direct path), and exciteGain carries the level instead.

Variations move ONE mechanism each so the audition separates axes:
  v5a_desc    — the description, straight
  v5b_ring    — hammer bands ring hotter/longer (excitation richness axis)
  v5c_bright  — less HF damping in the string loop (brightness axis)
  v5d_double  — stronger negative feedback + inharmonic loop (aftersound axis)
  v5e_flange  — wider unison detune + more dispersion (flanging/metal axis)
  v5f_body    — bigger reverb body (body axis)
  v5g_phrase  — v5a settings, two-hand phrase, polyphonic
"""
import copy
import json
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "patches", "pending", "ks_piano_v5")

C246_SCORE = [
    {"note": 36, "velocity": 0.85, "time": 0.0, "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 4.0, "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 8.0, "duration": 3.5},
]

PHRASE_SCORE = [
    {"note": 36, "velocity": 0.80, "time": 0.0, "duration": 1.0},
    {"note": 43, "velocity": 0.70, "time": 1.0, "duration": 1.0},
    {"note": 48, "velocity": 0.72, "time": 2.0, "duration": 1.0},
    {"note": 43, "velocity": 0.68, "time": 3.0, "duration": 2.0},
    {"note": 64, "velocity": 0.85, "time": 0.0, "duration": 0.5},
    {"note": 67, "velocity": 0.82, "time": 0.5, "duration": 0.5},
    {"note": 72, "velocity": 0.88, "time": 1.0, "duration": 0.5},
    {"note": 71, "velocity": 0.80, "time": 1.5, "duration": 0.5},
    {"note": 69, "velocity": 0.78, "time": 2.0, "duration": 0.5},
    {"note": 67, "velocity": 0.80, "time": 2.5, "duration": 0.5},
    {"note": 64, "velocity": 0.82, "time": 3.0, "duration": 2.0},
    {"note": 60, "velocity": 0.75, "time": 3.0, "duration": 2.0},
]

# Per-variant master gain, applied in Instrument::render BEFORE the soft_clip
# peak guard. Calibrated 2026-08-09 by rendering each variant at volume 0.01
# (clipper inactive) and scaling the measured true peak to 0.85: the raw chain
# runs 3.4-9.5x over full scale, and the flat-topped soft_clip was the "harsh
# clipping-like distortion" in the first audition round.
VOLUMES = {
    "v5a_desc.json":   0.2198,
    "v5b_ring.json":   0.0898,
    "v5c_bright.json": 0.2196,
    "v5d_double.json": 0.1485,
    "v5e_flange.json": 0.2220,
    "v5f_body.json":   0.2468,
    "v5g_phrase.json": 0.1229,
}

# Damper release: after the scored duration the voice rings on for this long
# under an exponential fade to -60 dB, instead of hard-truncating at note-off
# ("starts to decay then cuts off abruptly", first audition round).
RELEASE_SECONDS = 0.35

# Calibration curves from run 24 (Iowa-fit dispersion, measured t60 slopes).
T60_CURVE    = [[65.0, 25.0], [262.0, 15.0], [1047.0, 9.0]]
BRIGHT_CURVE = [[65.0, 0.65], [262.0, 0.78], [1047.0, 0.93]]
DISP_CURVE   = [[65.0, 0.855], [262.0, 0.61], [1047.0, 0.50]]
INHARM_CURVE = [[65.0, 0.50], [262.0, 0.15], [1047.0, 0.03]]


def base_patch(score, polyphony=1):
    return {
        "sampleRate": 48000,
        "graph": {
            "nodes": [
                {
                    "id": "env",
                    "type": "Envelope",
                    "params": {
                        "preset": "adsr", "timeMode": "seconds",
                        "attack": 0.0002, "decay": 0.008,
                        "sustainLevel": 0.0, "release": 0.0,
                    },
                },
                {
                    "id": "hammer",
                    "type": "HammerBank",
                    "params": {
                        "source": {"ref": "env"},
                        "frequency": 220.0,
                        "numBands": 4,
                        "harm1": 1.0, "harm2": 2.0, "harm3": 3.0, "harm4": 4.0,
                        "resStart": 25.0, "resEnd": 1.2, "resDecay": 0.012,
                        "bandTilt": -0.25,
                        "direct": 0.0,          # description has no dry path
                        "gain": 2.0,
                    },
                },
                {
                    "id": "string",
                    "type": "KSPianoString",
                    "params": {
                        "source": {"ref": "hammer"},
                        "frequency": 220.0,
                        "numCombs": 3,
                        "detune": 1.8,
                        "t60": 6.0,
                        "brightness": 0.6,
                        "exciteGain": 1.0,      # level lives here, not in direct taps
                        "direct": 0.0,
                        "dispersion": 0.16,
                        "inharmGain": 0.25,
                        "inharmFb": 0.90, "inharmHp": 150.0,
                        "ap1": 0.55, "ap2": 0.35, "ap3": 0.20,
                        "fbCoeff": 0.3,
                    },
                },
                {
                    "id": "verb",
                    "type": "Reverb",
                    "params": {
                        "source": {"ref": "string"},
                        "roomSize": 0.18, "damping": 0.55,
                        "wet": 0.18, "dry": 0.85,
                    },
                },
            ],
            "output": "verb",
        },
        "instrument": {
            "polyphony": polyphony,
            "release": RELEASE_SECONDS,
            "paramMap": {
                "frequency": [
                    "string.frequency",
                    "hammer.frequency",
                    {"target": "string.t60",        "curve": copy.deepcopy(T60_CURVE)},
                    {"target": "string.brightness", "curve": copy.deepcopy(BRIGHT_CURVE)},
                    {"target": "string.dispersion", "curve": copy.deepcopy(DISP_CURVE)},
                    {"target": "string.inharmGain", "curve": copy.deepcopy(INHARM_CURVE)},
                ],
            },
        },
        "score": score,
    }


def node(p, node_id):
    for n in p["graph"]["nodes"]:
        if n["id"] == node_id:
            return n["params"]
    raise KeyError(node_id)


def curve_target(p, target):
    for t in p["instrument"]["paramMap"]["frequency"]:
        if isinstance(t, dict) and t["target"] == target:
            return t
    raise KeyError(target)


def build_all():
    patches = {}

    # a — the description, straight
    patches["v5a_desc.json"] = base_patch(C246_SCORE)

    # b — hammer bands ring hotter and longer
    p = base_patch(C246_SCORE)
    node(p, "hammer").update({"resStart": 60.0, "resEnd": 2.0,
                              "resDecay": 0.035, "bandTilt": 0.0})
    patches["v5b_ring.json"] = p

    # c — less HF damping in the string loop
    p = base_patch(C246_SCORE)
    curve_target(p, "string.brightness")["curve"] = [
        [65.0, 0.80], [262.0, 0.90], [1047.0, 0.97]]
    patches["v5c_bright.json"] = p

    # d — double-envelope mechanism emphasized
    p = base_patch(C246_SCORE)
    node(p, "string")["fbCoeff"] = 0.6
    curve_target(p, "string.inharmGain")["curve"] = [
        [65.0, 1.00], [262.0, 0.30], [1047.0, 0.06]]
    patches["v5d_double.json"] = p

    # e — unison/flanging axis
    p = base_patch(C246_SCORE)
    node(p, "string")["detune"] = 3.5
    curve_target(p, "string.dispersion")["curve"] = [
        [65.0, 0.90], [262.0, 0.72], [1047.0, 0.60]]
    patches["v5e_flange.json"] = p

    # f — bigger body
    p = base_patch(C246_SCORE)
    node(p, "verb").update({"roomSize": 0.35, "damping": 0.40,
                            "wet": 0.32, "dry": 0.75})
    patches["v5f_body.json"] = p

    # g — two-hand phrase on the straight description
    patches["v5g_phrase.json"] = base_patch(PHRASE_SCORE, polyphony=6)

    return patches


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, p in build_all().items():
        p["instrument"]["volume"] = VOLUMES[name]
        path = os.path.join(OUT, name)
        with open(path, "w") as f:
            json.dump(p, f, indent=2)
        print("wrote", path)


if __name__ == "__main__":
    main()
