"""Clarinet CMA-ES encoder with Matt's LOCKED noise bed (encoder="clarinet_locked").

Derived from encoder_clarinet.py with the bed REMOVED from the search space:
- the 3 bed scalar dims (noiseBedLevel/Freq/Width) are gone;
- the 4 per-register curve dims (bedDelay150/1000, toneRise150/1000) are gone —
  Matt revoked the register law; the bed is frequency-INDEPENDENT;
- the emitted patch carries his hand-tuned scalars verbatim (LOCKED_BED below)
  and NO paramMap bed-curve entries (stale curve dicts are stripped).

Everything else matches encoder_clarinet.py exactly (same ranges, same order),
so the surviving leading dims of a clarinet1 z-vector map to identical params:
old z[0:20] == new z[0:20].

Dims (20): 8 env knots + 12 scalars.
"""
import copy

import numpy as np

ENV_KNOTS = 8
N_PARTIALS = 40
ENV_HARMS = np.unique(np.round(np.geomspace(1, N_PARTIALS, ENV_KNOTS)).astype(int))
KNOT_RANGE = (0.25, 4.0)

# Matt's locked, frequency-independent bed (patches/clarinet_locked.json, node clr)
LOCKED_BED = {
    "noiseBedFreq": 1000.0,
    "noiseBedWidth": 1000.0,
    "noiseBedLevel": 0.003,
    "noiseBedDelay": 0.15,
    "noiseBedFade": 0.5,
    "noiseBedFadePow": 3.0,
}

# (name, lo, hi, warm) — identical to encoder_clarinet.py minus the bed scalars.
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
]

DIM = ENV_KNOTS + len(SCALARS)


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
    for _, lo, hi, base in SCALARS:
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
    for (name, lo, hi, _), uj in zip(SCALARS, u[ENV_KNOTS:]):
        out[name] = lo + (hi - lo) * uj
    return out


def _node(patch, ntype):
    return next(n for n in patch["graph"]["nodes"] if n.get("type") == ntype)


def encode(z, template):
    """Build a concrete clarinet patch from the CMA-ES vector (bed locked)."""
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

    # noise bed: Matt's locked scalars verbatim, frequency-independent
    src = _node(p, "AdditiveSource")["params"]
    src.update(LOCKED_BED)

    # paramMap: plain frequency mapping only — strip any stale bed-curve dicts,
    # append nothing.
    inst = p.setdefault("instrument", {"polyphony": 1})
    pmap = inst.setdefault("paramMap", {})
    freq = pmap.get("frequency", "clr.frequency")
    if not isinstance(freq, list):
        freq = [freq]
    freq = [e for e in freq if not isinstance(e, dict)]
    pmap["frequency"] = freq
    return p
