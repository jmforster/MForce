"""Clarinet CMA-ES encoder (config encoder="clarinet", optimize.py dispatch).

Differences from the viola layout:
- vibrato PINNED OFF (no Vibrato node in the template);
- viola residue-cluster attack dims DROPPED (no expandRule/fadeEnv, no
  BandSpectrum region scalers, no formantWeight);
- noise-bed dims ADDED (the landed AdditiveSource noiseBed* configs):
  scalar level/freq/width plus per-register curve dims for noiseBedDelay
  (bed leads the tone; noise-lead law f0^-0.5, warm 0.15 s @150 Hz /
  0.04 s @1000 Hz) and noiseBedFade (the tone's t50 rise law, warm
  0.4 s @150 Hz / 0.09 s @1000 Hz), emitted as instrument.paramMap
  frequency-curve entries — same mechanism as the viola residue curves.

Attack structure (report caveat): the bed level tracks the amplitude
param, so the template's ampEnv attack is FAST; the tone's delay is
noiseBedDelay and its slow rise is noiseBedFade. The "amp-envelope
attack" knots therefore live on clr.noiseBedFade, not ampEnv.attack —
putting them on ampEnv would drag the bed down with the tone and destroy
the noise-before-tone structure.

Dims (27): 8 env knots + 12 scalars + 3 bed scalars + 4 curve knots.
"""
import copy

import numpy as np

ENV_KNOTS = 8
N_PARTIALS = 40
ENV_HARMS = np.unique(np.round(np.geomspace(1, N_PARTIALS, ENV_KNOTS)).astype(int))
KNOT_RANGE = (0.25, 4.0)

# (name, lo, hi, warm) — warm values from clarinet_reference motion medians
# (resid 11.4c @9.7 Hz, amp_frac 0.39 @1.8 Hz coh 0.51 via the /0.55 walk-RMS
# mapping) and the attack report's feature table.
SCALARS = [
    ("motionDepth",      0.0, 25.0, 15.0),
    ("motionHz",         1.0, 30.0, 10.0),
    ("motionEvolve",     0.0, 1.0, 0.3),
    ("shimmerDepth",     0.0, 2.0, 0.7),
    ("shimmerHz",        0.2, 10.0, 1.8),
    ("shimmerCoherence", 0.0, 1.0, 0.5),
    ("shimmerEvolve",    0.0, 1.0, 0.3),
    ("bandwidth",        0.0, 0.6, 0.07),   # sustain line texture only
    ("bandwidthHz",      20.0, 400.0, 45.0),
    ("onsetFade",        0.0, 0.5, 0.10),
    ("onsetSpread",      0.0, 0.5, 0.10),
    ("onsetTilt",       -1.0, 1.0, 0.9),    # bottom-up rise order (report)
    ("noiseBedLevel",    0.0, 0.05, 0.016),
    ("noiseBedFreq",  1500.0, 5000.0, 2900.0),
    ("noiseBedWidth",  500.0, 3000.0, 1500.0),
]

CURVE_FREQS = (150, 1000)
CURVES = [
    # noiseBedDelay: tone delayed behind the bed (noise-lead law)
    ("bedDelay150",  0.0, 0.3, 0.15),   # clr.noiseBedDelay @150 Hz
    ("bedDelay1000", 0.0, 0.1, 0.04),   # clr.noiseBedDelay @1000 Hz
    # noiseBedFade: the tone's rise after the delay (t50 law) — the
    # "amp-envelope attack" knots, carried here per the caveat above.
    ("toneRise150",  0.05, 1.0, 0.4),   # clr.noiseBedFade @150 Hz
    ("toneRise1000", 0.02, 0.5, 0.09),  # clr.noiseBedFade @1000 Hz
]

DIM = ENV_KNOTS + len(SCALARS) + len(CURVES)


def _logit(u):
    u = np.clip(u, 1e-4, 1 - 1e-4)
    return np.log(u / (1 - u))


def _sig(z):
    return 1.0 / (1.0 + np.exp(-z))


def warm_start(z_init=None):
    z = []
    for _ in range(ENV_KNOTS):
        u = (1.0 - KNOT_RANGE[0]) / (KNOT_RANGE[1] - KNOT_RANGE[0])
        z.append(_logit(u))
    for _, lo, hi, base in SCALARS + CURVES:
        z.append(_logit((base - lo) / (hi - lo)))
    z = np.array(z)
    if z_init is not None:
        z_init = np.asarray(z_init, dtype=float)
        if len(z_init) > DIM:
            raise ValueError(f"init vector dim {len(z_init)} > DIM {DIM}")
        z[:len(z_init)] = z_init
    return z


def decode(z):
    u = _sig(z)
    out = {}
    kr = KNOT_RANGE
    out["env_knots"] = kr[0] + (kr[1] - kr[0]) * u[:ENV_KNOTS]
    for (name, lo, hi, _), uj in zip(SCALARS + CURVES, u[ENV_KNOTS:]):
        out[name] = lo + (hi - lo) * uj
    return out


def _node(patch, ntype):
    return next(n for n in patch["graph"]["nodes"] if n.get("type") == ntype)


def encode(z, template):
    """Build a concrete clarinet patch from the CMA-ES vector."""
    p = copy.deepcopy(template)
    d = decode(z)
    parts = _node(p, "ExplicitPartials")["params"]

    # harmonic envelope: knots interp over log-harmonic axis x baseline
    n = len(parts["ampl1"])
    harm_idx = np.arange(1, n + 1)
    scale = np.exp(np.interp(np.log(harm_idx), np.log(ENV_HARMS),
                             np.log(d["env_knots"])))
    new_amp = (np.array(parts["ampl1"]) * scale).tolist()
    parts["ampl1"] = new_amp
    parts["ampl2"] = list(new_amp)

    # motion / shimmer / line texture / onset dispersion
    parts["motionDepth1"] = parts["motionDepth2"] = float(d["motionDepth"])
    parts["motionHz"] = float(d["motionHz"])
    parts["motionCoherence"] = 0.0
    parts["motionEvolve"] = float(d["motionEvolve"])
    parts["shimmerDepth1"] = parts["shimmerDepth2"] = float(d["shimmerDepth"])
    parts["shimmerHz"] = float(d["shimmerHz"])
    parts["shimmerCoherence"] = float(d["shimmerCoherence"])
    parts["shimmerEvolve"] = float(d["shimmerEvolve"])
    parts["bandwidth1"] = parts["bandwidth2"] = float(d["bandwidth"])
    parts["bandwidthHz"] = float(d["bandwidthHz"])
    parts["onsetFade"] = float(d["onsetFade"])
    parts["onsetSpread"] = float(d["onsetSpread"])
    parts["onsetTilt"] = float(d["onsetTilt"])

    # noise bed scalars
    src = _node(p, "AdditiveSource")["params"]
    src["noiseBedLevel"] = float(d["noiseBedLevel"])
    src["noiseBedFreq"] = float(d["noiseBedFreq"])
    src["noiseBedWidth"] = float(d["noiseBedWidth"])

    # per-register curves: bed lead (noiseBedDelay) + tone rise (noiseBedFade)
    inst = p.setdefault("instrument", {"polyphony": 1})
    pmap = inst.setdefault("paramMap", {})
    freq = pmap.get("frequency", "clr.frequency")
    if not isinstance(freq, list):
        freq = [freq]
    freq = [e for e in freq if not isinstance(e, dict)]   # drop stale curves
    freq.append({"target": "clr.noiseBedDelay",
                 "curve": [[CURVE_FREQS[0], float(d["bedDelay150"])],
                           [CURVE_FREQS[1], float(d["bedDelay1000"])]]})
    freq.append({"target": "clr.noiseBedFade",
                 "curve": [[CURVE_FREQS[0], float(d["toneRise150"])],
                           [CURVE_FREQS[1], float(d["toneRise1000"])]]})
    pmap["frequency"] = freq
    return p
