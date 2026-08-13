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
    "v6c2_shaped.json":     0.2213,
    "v6c2_bloom.json":      0.2218,
    "v6c3_level.json":      0.4839,
    "v6d_knock.json":       0.2989,
    "v6d_knock_tight.json": 0.3939,
    "v6e_vel.json":         0.3293,
    "v6e_flat.json":        0.3293,
    "v6f_damper.json":      0.2561,
    "v6f_ctrl.json":        0.2561,
    "v6g_detune.json":      0.2548,
    "v6h_seed.json":        0.2548,
    "v6k_seed.json":        1.0,
    "v6f3_dnoise_lo.json":  0.2561,
    "v6f3_dnoise_hi.json":  0.2561,
    "v6f2_ctrl.json":       0.2561,
    "v6f2_damper82.json":   0.2561,
    "v6f2_damper60.json":   0.2561,
    "v6g2_detune12.json":   0.2548,
}

RELEASE_SECONDS = 0.35

# Damper/detune audition score: same C2/C4/C6 but 8 s apart so the 2 s
# release tail is heard in silence instead of under the next onset.
C246_GAPPED_SCORE = [
    {"note": 36, "velocity": 0.85, "time": 0.0,  "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 8.0,  "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 16.0, "duration": 3.5},
]

# Rung-3 audition score: each register at soft/medium/hard velocity.
VEL_LADDER_SCORE = [
    {"note": n, "velocity": v, "time": float(i * 4), "duration": 3.5}
    for i, (n, v) in enumerate(
        [(36, 0.35), (36, 0.60), (36, 0.85),
         (60, 0.35), (60, 0.60), (60, 0.85),
         (84, 0.35), (84, 0.60), (84, 0.85)])
]


def base_patch(excite_noise=None, shaping=None, click_env=None, level_curve=None,
               knock=None, vel_bright=False, score=None, release_fb=None,
               detune_curve=None, release=None, damper_noise=None,
               body_noise=None):
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
        if body_noise is not None:
            # Pass 3 (Matt: below middle C = harpsichord attack, bassoon
            # sustain): the bank rings ONLY h1-4, starving the string loop of
            # harmonics 5-20 exactly where real pianos are rich (Iowa C2 piles
            # energy on h4-h6). Broadband body path — the raw burst through a
            # pitch-tracked LP with a FLOOR — feeds the comb, which selects
            # the harmonics itself (Balazs's actual architecture: shaped
            # noise in, the string does the picking).
            bn_mult, bn_floor, bn_gain = body_noise
            shaping_nodes += [
                {"id": "exc_body", "type": "BWLowpassFilter", "params": {
                    "source": {"ref": "noise"}, "sections": 2,
                    "cutoffFreq": 1000.0}},
                {"id": "exc_bodygain", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_body"}, "source2": bn_gain,
                    "operation": "multiply"}},
                {"id": "exc_mixb", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_mix"},
                    "source2": {"ref": "exc_bodygain"},
                    "operation": "sum"}},
            ]
            string_source = {"ref": "exc_mixb"}
            extra_map += [{"target": "exc_body.cutoffFreq",
                           "curve": [[f, max(bn_mult * f, bn_floor)]
                                     for f in (65.0, 262.0, 1047.0)]}]
        if knock is not None:
            # v6d: pitch-FIXED knock — the hammer/soundboard body real pianos
            # share across the keyboard (commuted-synthesis style). Own
            # envelope -> fixed BW bandpass on the raw noise -> gain -> summed.
            ka, kd, k_lo, k_hi, k_gain = knock
            shaping_nodes += [
                {"id": "env_knock", "type": "Envelope", "params": {
                    "preset": "adsr", "timeMode": "seconds",
                    "attack": ka, "decay": kd,
                    "sustainLevel": 0.0, "release": 0.0}},
                {"id": "noise_knock", "type": "WhiteNoiseSource", "params": {
                    "amplitude": {"ref": "env_knock"}}},
                {"id": "exc_knock", "type": "BWBandpassFilter", "params": {
                    "source": {"ref": "noise_knock"}, "sections": 2,
                    "lowCutoff": k_lo, "highCutoff": k_hi}},
                {"id": "exc_knockgain", "type": "CombinedSource", "params": {
                    "source1": {"ref": "exc_knock"}, "source2": k_gain,
                    "operation": "multiply"}},
                {"id": "exc_mix2", "type": "CombinedSource", "params": {
                    "source1": dict(string_source),
                    "source2": {"ref": "exc_knockgain"},
                    "operation": "sum"}},
            ]
            string_source = {"ref": "exc_mix2"}
        if level_curve is not None:
            # v6c3: measured per-register gain compensation — constant-Q bank
            # passes noise energy ~ absolute bandwidth, leaving C2 ~12 dB under
            # C6 (Matt obs 1). Curve equalizes excitation RMS across register.
            shaping_nodes += [
                {"id": "exc_level", "type": "CombinedSource", "params": {
                    "source1": dict(string_source), "source2": 1.0,
                    "operation": "multiply"}},
            ]
            string_source = {"ref": "exc_level"}
            extra_map += [{"target": "exc_level.source2",
                           "curve": [list(pt) for pt in level_curve]}]
        lp_entry = {"target": "exc_lp.cutoffFreq",
                    "curve": [[65.0, lp_mult * 65.0],
                              [262.0, lp_mult * 262.0],
                              [1047.0, lp_mult * 1047.0]]}
        if vel_bright:
            # Rung 3: velocity->brightness (AF "Veloc" mod inputs). Soft
            # notes darker + less click; hard notes brighter + clickier.
            lp_entry["vcurve"] = [[0.2, 0.55], [0.85, 1.0], [1.0, 1.25]]
            for m in extra_map:
                if m.get("target") == "exc_clickgain.source2":
                    m["vcurve"] = [[0.2, 0.30], [0.85, 1.0], [1.0, 1.6]]
        extra_map += [lp_entry]

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
                        **({"releaseFb": release_fb} if release_fb is not None else {}),
                        **({"damperNoise": damper_noise} if damper_noise is not None else {}),
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
            "release": release if release is not None else RELEASE_SECONDS,
            "paramMap": {
                "frequency": [
                    "string.frequency",
                    "hammer.frequency",
                    {"target": "string.t60",        "curve": copy.deepcopy(T60_CURVE)},
                    {"target": "string.brightness", "curve": copy.deepcopy(BRIGHT_CURVE)},
                    {"target": "string.dispersion", "curve": copy.deepcopy(DISP_CURVE)},
                    {"target": "string.inharmGain", "curve": copy.deepcopy(INHARM_CURVE)},
                ] + extra_map
                  + ([{"target": "string.detune",
                       "curve": [list(pt) for pt in detune_curve]}]
                     if detune_curve is not None else []),
            },
        },
        "score": score if score is not None else C246_SCORE,
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
        # v6c3: + measured level compensation (opt_ks_piano_v6c3.py:
        # per-note excitation RMS equalized to 2%; nothing else changed —
        # the "loose tail" turned out to be the UI envelope round-trip bug,
        # canonical tails measure 40-45 ms).
        "v6c3_level.json":      base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 2.284),
                                                        (261.63, 1.0),
                                                        (1046.5, 0.458)]),
        # v6d: pitch-fixed knock (80-300 Hz, own envelope) + FULL-CHAIN level
        # calibration (opt_ks_piano_v6d.py). v6c3 equalized excitation RMS but
        # the string loop loses another ~9 dB at C2 (loop buildup ~ burst
        # periods spanned); these curves flatten the rendered attack peaks
        # C2==C4==C6. Two knock decays for the ears.
        "v6d_knock.json":       base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 8.505),
                                                        (261.63, 1.233),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 300.0, 1.0)),
        "v6d_knock_tight.json": base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 9.277),
                                                        (261.63, 1.277),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.030, 80.0, 300.0, 1.0)),
        # v6e: RUNG 3 velocity->brightness (engine vcurve, 2026-08-12) on the
        # Matt-adjusted knock base (band 80-500 per his ears, gain 2.0 so it
        # reads in the full patch; level curve recalibrated full-chain).
        # Velocity-ladder score: C2/C4/C6 x vel 0.35/0.60/0.85.
        # v6e_flat = same base, velocity scales gain only (the A/B control).
        "v6e_vel.json":         base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           score=VEL_LADDER_SCORE),
        # v6f: RUNG 4 in-loop damper (engine releaseFb; his 0.82 at gate-off).
        # C246 score; release window widened to 0.6 s so the damper's
        # darkening curve is audible before the output-fade guard. v6f_ctrl =
        # same base without the damper.
        "v6f_damper.json":      base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=0.6),
        "v6f_ctrl.json":        base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release=0.6),
        # v6g: RUNG 5 detune-vs-pitch curve (his Custom Function 5: ~0 cents
        # at the bass -> 5 cents at the top; replaces our fixed 1.8 cents).
        "v6g_detune.json":      base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=0.6,
                                           detune_curve=[(65.0, 0.3),
                                                         (1046.5, 5.0)]),
        # v6f2/v6g2: the v6f/v6g A/B was masked by the 0.6 s output fade (both
        # arms fade together; the damper only changed a fraction of a second
        # of quiet tail). Exposure fix: 2.0 s release window so ctrl rings on
        # while the damper chokes; plus a harder 0.60 choke and an exaggerated
        # detune (0.3 -> 12 cents) to audition each AXIS clearly.
        "v6f2_ctrl.json":       base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release=2.0,
                                           score=C246_GAPPED_SCORE),
        "v6f2_damper82.json":   base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=2.0,
                                           score=C246_GAPPED_SCORE),
        "v6f2_damper60.json":   base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.60, release=2.0,
                                           score=C246_GAPPED_SCORE),
        # v6f3: damper-CONTACT noise (Matt 2026-08-12: real pianos thud at
        # note-off; ours was a noiseless fade). Burst injected into the loop
        # at note-off, scaled by the string's ring level at that instant.
        "v6f3_dnoise_lo.json":  base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=2.0,
                                           damper_noise=0.3,
                                           score=C246_GAPPED_SCORE),
        "v6f3_dnoise_hi.json":  base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=2.0,
                                           damper_noise=1.0,
                                           score=C246_GAPPED_SCORE),
        "v6g2_detune12.json":   base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=2.0,
                                           detune_curve=[(65.0, 0.3),
                                                         (1046.5, 12.0)],
                                           score=C246_GAPPED_SCORE),
        # v6h: Matt-locked seed (2026-08-12 verdicts: damper 0.82 in, contact
        # noise 0.03 "about right", detune audible but 12 too much -> 4 as the
        # CMA-ES warm start). This is the optimization template.
        "v6h_seed.json":        base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           vel_bright=True,
                                           release_fb=0.82, release=2.0,
                                           damper_noise=0.03,
                                           detune_curve=[(65.0, 0.3),
                                                         (1046.5, 4.0)],
                                           score=C246_GAPPED_SCORE),
        # v6k_seed: pass-3 optimization template — run2 winner values baked,
        # + broadband body path (warm: gentle) + 4-point level curve (top
        # anchor regulates above-C6 loudness, Matt: "high still louder").
        "v6k_seed.json":        base_patch(excite_noise=(0.002, 0.0202),
                                           shaping=(4.497, 0.3836),
                                           click_env=(0.002, 0.0846, 0.0221, 0.3836),
                                           level_curve=[(65.0, 2.3291),
                                                        (261.63, 0.4007),
                                                        (1046.5, 0.9763),
                                                        (2093.0, 0.9763)],
                                           knock=(0.001, 0.1411, 71.73, 494.47, 1.1693),
                                           vel_bright=True,
                                           release_fb=0.82, release=2.0,
                                           damper_noise=0.03,
                                           detune_curve=[(65.0, 0.3),
                                                         (1046.5, 5.2604)],
                                           body_noise=(3.0, 800.0, 0.3),
                                           score=C246_GAPPED_SCORE),
        "v6e_flat.json":        base_patch(excite_noise=(0.002, 0.040),
                                           shaping=(2.5, 0.15),
                                           click_env=(0.002, 0.050, 0.03, 0.15),
                                           level_curve=[(65.0, 3.558),
                                                        (261.63, 0.871),
                                                        (1046.5, 0.458)],
                                           knock=(0.001, 0.080, 80.0, 500.0, 2.0),
                                           score=VEL_LADDER_SCORE),
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
