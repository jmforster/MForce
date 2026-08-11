#!/usr/bin/env python3
"""Generate patches/pending/ks_piano_v6/* — cumulative improvement ladder from
the 2021 AF working-video analysis (docs/research/afpiano_2021/ANALYSIS.md).

Rung plan (each rung baselines on Matt's pick from the previous):
  v6a_anchor  — v5a_desc unchanged (A/B anchor)
  v6b_*       — RUNG 1: noise excitation. The hammer bank's input becomes an
                enveloped white-noise burst (WhiteNoiseSource.amplitude = env)
                instead of the bare decaying envelope. Three burst lengths of
                the SAME mechanism — Matt picks one, it becomes v6c's base.
  (v6c velocity->brightness, v6d in-loop releaseFb damper, v6e detune-vs-pitch
   curve follow in later rungs.)

Volumes are peak-calibrated (true peak -> 0.85 pre-clip) by
tools/render_ks_piano_v6.py, which writes them back into the emitted patches.
"""
import copy
import json
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "patches", "pending", "ks_piano_v6")

C246_SCORE = [
    {"note": 36, "velocity": 0.85, "time": 0.0, "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 4.0, "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 8.0, "duration": 3.5},
]

# Calibration curves from run 24 (Iowa-fit dispersion, measured t60 slopes).
T60_CURVE    = [[65.0, 25.0], [262.0, 15.0], [1047.0, 9.0]]
BRIGHT_CURVE = [[65.0, 0.65], [262.0, 0.78], [1047.0, 0.93]]
DISP_CURVE   = [[65.0, 0.855], [262.0, 0.61], [1047.0, 0.50]]
INHARM_CURVE = [[65.0, 0.50], [262.0, 0.15], [1047.0, 0.03]]

# Peak-calibrated by tools/render_ks_piano_v6.py 2026-08-10.
VOLUMES = {
    "v6a_anchor.json":      0.2198,   # = v5a_desc
    "v6b_noise_short.json": 0.1471,
    "v6b_noise_med.json":   0.1408,
    "v6b_noise_long.json":  0.1169,
    "v6c_shaped.json":      0.2220,
    "v6c_shaped_alt.json":  0.5627,
    "v6c2_shaped.json":     1.0,
    "v6c2_bloom.json":      1.0,
}

RELEASE_SECONDS = 0.35


def base_patch(excite_noise=None, shaping=None, click_env=None):
    """excite_noise: None = v5a bare-envelope excitation; else (attack, decay)
    for the noise-burst envelope, with the bank fed by enveloped white noise.
    shaping: None = bank feeds the string directly (rung 1); else
    (lp_mult, click_gain) — rung 2 excitation shaping, optimized by
    tools/opt_ks_piano_v6c.py against the 2021-video band targets: pitch-
    tracked Butterworth LP body path (bank) in parallel with a 4-9.5 kHz
    noise click path (pre-bank tap — the bank has no HF left to extract)."""
    if excite_noise is None:
        env = {"id": "env", "type": "Envelope", "params": {
            "preset": "adsr", "timeMode": "seconds",
            "attack": 0.0002, "decay": 0.008,
            "sustainLevel": 0.0, "release": 0.0}}
        excite_nodes = [env]
        hammer_source = {"ref": "env"}
    else:
        attack, decay = excite_noise
        env = {"id": "env", "type": "Envelope", "params": {
            "preset": "adsr", "timeMode": "seconds",
            "attack": attack, "decay": decay,
            "sustainLevel": 0.0, "release": 0.0}}
        # 2021 AF hammer: envelope gates white noise, noise feeds the bank.
        noise = {"id": "noise", "type": "WhiteNoiseSource", "params": {
            "amplitude": {"ref": "env"}}}
        excite_nodes = [env, noise]
        hammer_source = {"ref": "noise"}

    hammer_node = {
        "id": "hammer",
        "type": "HammerBank",
        "params": {
            "source": hammer_source,
            "frequency": 220.0,
            "numBands": 4,
            "harm1": 1.0, "harm2": 2.0, "harm3": 3.0, "harm4": 4.0,
            "resStart": 25.0, "resEnd": 1.2, "resDecay": 0.012,
            "bandTilt": -0.25,
            "direct": 0.0,
            "gain": 2.0,
        },
    }

    shaping_nodes = []
    string_source = {"ref": "hammer"}
    extra_map = []
    if shaping is not None:
        lp_mult, click_gain = shaping
        click_src = {"ref": "noise"}
        if click_env is not None:
            # v6c2: click path gets its OWN envelope + pitch-dependent gain
            # (Matt's "drumstick, turned up too high" fix). His string-input
            # hits measure ~0.002 attack-window click; the constant-gain
            # shared-burst click slammed the low registers.
            ca, cd, g_lo, g_hi = click_env
            shaping_nodes += [
                {"id": "env_click", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": ca, "decay": cd,
                    "sustainLevel": 0.0, "release": 0.0}},
                {"id": "noise_click", "type": "WhiteNoiseSource", "params": {
                    "amplitude": {"ref": "env_click"}}},
            ]
            click_src = {"ref": "noise_click"}
            extra_map += [{"target": "exc_clickgain.source2",
                           "curve": [[65.0, g_lo], [1046.5, g_hi]]}]
        shaping_nodes += [
            {"id": "exc_lp", "type": "BWLowpassFilter", "params": {
                "source": {"ref": "hammer"}, "sections": 2,
                "cutoffFreq": 800.0}},
            {"id": "exc_click", "type": "BWBandpassFilter", "params": {
                "source": click_src, "sections": 2,
                "lowCutoff": 4000.0, "highCutoff": 9500.0}},
            {"id": "exc_clickgain", "type": "CombinedSource", "params": {
                "source1": {"ref": "exc_click"}, "source2": click_gain,
                "operation": "multiply"}},
            {"id": "exc_mix", "type": "CombinedSource", "params": {
                "source1": {"ref": "exc_lp"},
                "source2": {"ref": "exc_clickgain"},
                "operation": "sum"}},
        ]
        string_source = {"ref": "exc_mix"}
        extra_map += [{"target": "exc_lp.cutoffFreq",
                       "curve": [[65.0, lp_mult * 65.0],
                                 [262.0, lp_mult * 262.0],
                                 [1047.0, lp_mult * 1047.0]]}]

    patch = {
        "sampleRate": 48000,
        "graph": {
            "nodes": excite_nodes + [hammer_node] + shaping_nodes + [
                {
                    "id": "string",
                    "type": "KSPianoString",
                    "params": {
                        "source": string_source,
                        "frequency": 220.0,
                        "numCombs": 3,
                        "detune": 1.8,
                        "t60": 6.0,
                        "brightness": 0.6,
                        "exciteGain": 1.0,
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
            "polyphony": 1,
            "release": RELEASE_SECONDS,
            "paramMap": {
                "frequency": [
                    "string.frequency",
                    "hammer.frequency",
                    {"target": "string.t60",        "curve": copy.deepcopy(T60_CURVE)},
                    {"target": "string.brightness", "curve": copy.deepcopy(BRIGHT_CURVE)},
                    {"target": "string.dispersion", "curve": copy.deepcopy(DISP_CURVE)},
                    {"target": "string.inharmGain", "curve": copy.deepcopy(INHARM_CURVE)},
                ] + extra_map,
            },
        },
        "score": C246_SCORE,
    }
    return patch


def build_all():
    return {
        "v6a_anchor.json":      base_patch(),
        # AF 2021 attack was 4.7 ms; hit-length envelopes ranged 0 ms to 462 ms
        # release. Three burst lengths spanning that space:
        "v6b_noise_short.json": base_patch(excite_noise=(0.002,  0.020)),
        "v6b_noise_med.json":   base_patch(excite_noise=(0.0047, 0.080)),
        "v6b_noise_long.json":  base_patch(excite_noise=(0.0047, 0.462)),
        # Rung 2: excitation shaping. Grid winner (err 0.0245) + the rank-2
        # alternate (less body rolloff, hotter click) from opt_ks_piano_v6c.py.
        # Burst decay re-optimized objectively to 0.040 (the LP removes the
        # noise hash that made long bursts read as distortion in rung 1).
        "v6c_shaped.json":      base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15)),
        "v6c_shaped_alt.json":  base_patch(excite_noise=(0.002, 0.020),
                                           shaping=(1.5, 0.30)),
        # v6c2: click de-drumsticked — own envelope + pitch-dependent gain
        # (grid winners from opt_ks_piano_v6c2.py, attack-window scored).
        "v6c2_shaped.json":     base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15)),
        "v6c2_bloom.json":      base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.015, 0.050, 0.03, 0.15)),
    }


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
