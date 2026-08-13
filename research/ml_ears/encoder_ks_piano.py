"""KS piano CMA-ES encoder (encoder="ks_piano") — the v6 chain, 2026-08-12.

Template: patches/ks_v6_seed.json (gen_ks_piano_v6 v6h_seed — Matt's locked
2026-08-12 verdicts). The chain: noise burst -> HammerBank -> {pitch-tracked
BW LP body, click path, fixed knock} -> level curve -> KSPianoString ->
Reverb. See renders/dsp/pending/ks_piano_v6/README.md for the rung history.

LOCKED (Matt's ears / measured — the clarinet lesson: never let the optimizer
drift away from a human- or measurement-fixed transient):
  - burst attack 2 ms; click band 4-9.5 kHz + its pitch-gain SHAPE;
    knock band 80-500 Hz + 1 ms attack (Matt tuned the band by ear);
  - releaseFb 0.82, damperNoise 0.03 (Matt 2026-08-12), release window;
  - velocity->brightness vcurves (Matt: "on the money");
  - inharmonicity/dispersion CURVE SHAPES (Iowa-fit, run 24) — only scaled;
  - resEnd, numCombs, harm1-4, ap1-3, inharmFb/Hp.

SEARCHED (19 dims), all sigmoid-mapped to (lo, hi) with the warm start at
the current hand-tuned value:
   0 burst_decay      excitation noise burst decay (s)
   1 resStart         hammer bank starting Q
   2 resDecay         bank Q decay time constant (s)
   3 bandTilt         bank band tilt
   4 lp_mult          body LP cutoff as multiple of f0 (curve regenerated)
   5 click_gain_lo    click level at C2
   6 click_gain_hi    click level at C6
   7 click_decay      click envelope decay (s)
   8 knock_decay      knock envelope decay (s)
   9 knock_gain       knock level
  10 t60_scale        multiplies the measured T60 curve
  11 bright_lo        string brightness at 65 Hz (mid = (lo+hi)/2)
  12 bright_hi        string brightness at 1047 Hz
  13 detune_hi        detune cents at C6 (curve low end pinned 0.3)
  14 level_lo         excitation level compensation at C2
  15 level_mid        ... at C4
  16 level_hi         ... at C6
  17 fbCoeff          global negative feedback
  18 inharm_scale     multiplies the measured inharmonic-gain curve
"""
import copy

import numpy as np

# (name, lo, hi, warm)
SCALARS = [
    ("burst_decay",  0.015, 0.090, 0.040),
    ("resStart",     5.0,   60.0,  25.0),
    ("resDecay",     0.004, 0.050, 0.012),
    ("bandTilt",    -0.60,  0.10, -0.25),
    ("lp_mult",      1.5,   4.5,   2.5),
    ("click_gain_lo", 0.005, 0.10, 0.03),
    ("click_gain_hi", 0.05,  0.40, 0.15),
    ("click_decay",  0.020, 0.120, 0.050),
    ("knock_decay",  0.030, 0.150, 0.080),
    ("knock_gain",   0.5,   4.0,   2.0),
    ("t60_scale",    0.6,   1.5,   1.0),
    ("bright_lo",    0.40,  0.90,  0.65),
    ("bright_hi",    0.70,  1.00,  0.93),
    ("detune_hi",    0.5,  20.0,   4.0),   # run1 pinned the old 8.0 bound
    ("level_lo",     1.0,   8.0,   3.558),
    ("level_mid",    0.40,  1.60,  0.871),
    ("level_hi",     0.15,  1.00,  0.458),
    ("fbCoeff",      0.05,  0.60,  0.30),
    ("inharm_scale", 0.5,   2.0,   1.0),
    # pass 2 (Matt: hand-lowering knock lowCutoff to 30 was "dramatic")
    ("knock_lo",     20.0, 150.0,  80.0),
    ("knock_hi",    250.0, 900.0, 500.0),
    # pass 3 (Matt: below middle C harpsichord/bassoon — bank-only excitation
    # starves harmonics 5-20; broadband body path lets the comb self-select):
    ("body_gain",    0.0,   2.0,   0.3),
    ("body_mult",    1.5,  10.0,   3.0),
    ("body_floor", 300.0, 2500.0, 800.0),
    # pass 3 (Matt: above-C6 still louder than bass): 4th level anchor @2093
    ("level_top",    0.10,  1.50,  0.976),
]

DIM = len(SCALARS)


def _logit(u):
    u = np.clip(u, 1e-4, 1 - 1e-4)
    return np.log(u / (1 - u))


def _sig(z):
    return 1.0 / (1.0 + np.exp(-z))


def warm_start(z_init=None):
    z = np.array([_logit((w - lo) / (hi - lo)) for (_, lo, hi, w) in SCALARS])
    if z_init is not None:
        z_init = np.asarray(z_init, dtype=float)
        if len(z_init) > DIM:
            raise ValueError(f"init vector dim {len(z_init)} > DIM {DIM}")
        z[:len(z_init)] = z_init
    return z


def _values(z):
    z = np.asarray(z, dtype=float)
    if len(z) != DIM:
        raise ValueError(f"z dim {len(z)} != DIM {DIM}")
    out = {}
    for (name, lo, hi, _), zi in zip(SCALARS, z):
        out[name] = lo + (hi - lo) * float(_sig(zi))
    return out


def _node(p, node_id):
    for n in p["graph"]["nodes"]:
        if n["id"] == node_id:
            return n["params"]
    raise KeyError(node_id)


def _curve_entry(p, target):
    for e in p["instrument"]["paramMap"]["frequency"]:
        if isinstance(e, dict) and e.get("target") == target:
            return e
    raise KeyError(target)


def encode(z, template):
    v = _values(z)
    p = copy.deepcopy(template)

    _node(p, "env")["decay"] = v["burst_decay"]
    hammer = _node(p, "hammer")
    hammer["resStart"] = v["resStart"]
    hammer["resDecay"] = v["resDecay"]
    hammer["bandTilt"] = v["bandTilt"]

    _curve_entry(p, "exc_lp.cutoffFreq")["curve"] = [
        [f, v["lp_mult"] * f] for f in (65.0, 262.0, 1047.0)]
    # NOTE: hammer.resStart curve does not exist in the seed (flat resStart);
    # the scalar above covers it.

    ce = _curve_entry(p, "exc_clickgain.source2")
    ce["curve"] = [[65.0, v["click_gain_lo"]], [1046.5, v["click_gain_hi"]]]
    _node(p, "env_click")["decay"] = v["click_decay"]

    _node(p, "env_knock")["decay"] = v["knock_decay"]
    _node(p, "exc_knockgain")["source2"] = v["knock_gain"]
    if "knock_lo" in v:
        _node(p, "exc_knock")["lowCutoff"] = v["knock_lo"]
        _node(p, "exc_knock")["highCutoff"] = v["knock_hi"]

    _curve_entry(p, "string.t60")["curve"] = [
        [65.0, 25.0 * v["t60_scale"]],
        [262.0, 15.0 * v["t60_scale"]],
        [1047.0, 9.0 * v["t60_scale"]]]
    blo, bhi = v["bright_lo"], v["bright_hi"]
    _curve_entry(p, "string.brightness")["curve"] = [
        [65.0, blo], [262.0, 0.5 * (blo + bhi)], [1047.0, bhi]]
    _curve_entry(p, "string.detune")["curve"] = [
        [65.0, 0.3], [1046.5, v["detune_hi"]]]
    lvl = [[65.0, v["level_lo"]], [261.63, v["level_mid"]],
           [1046.5, v["level_hi"]]]
    if "level_top" in v:
        lvl.append([2093.0, v["level_top"]])
    _curve_entry(p, "exc_level.source2")["curve"] = lvl
    if "body_gain" in v:
        _node(p, "exc_bodygain")["source2"] = v["body_gain"]
        _curve_entry(p, "exc_body.cutoffFreq")["curve"] = [
            [f, max(v["body_mult"] * f, v["body_floor"])]
            for f in (65.0, 262.0, 1047.0)]
    ig = _curve_entry(p, "string.inharmGain")
    ig["curve"] = [[65.0, 0.50 * v["inharm_scale"]],
                   [262.0, 0.15 * v["inharm_scale"]],
                   [1047.0, 0.03 * v["inharm_scale"]]]

    _node(p, "string")["fbCoeff"] = v["fbCoeff"]

    # Keep every candidate well under the soft_clip knee: features are
    # relative, absolute level is not scored.
    p["instrument"]["volume"] = 0.05
    return p
