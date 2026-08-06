"""Piano CMA-ES encoder (encoder="piano") — see out/piano_analysis_report.md.

LOCKED from measurement (never searched):
  - inharmonicity B(f0): the measured per-note table rides in the template's
    paramMap curve (pp.inharmonicity) untouched.
  - decayExp = 0.6 (within-note decay-vs-partial exponent, report sec 2).
  - attack: ampEnv attack 8 ms LITERAL (timeMode=seconds, run-23 engine fix;
    measured 10-90 rise 4-55 ms), onsetSpread 0.
  - knock (run 23, REVIEW 16 fix 2 — the clarinet lesson: optimizer drift
    away from a measured transient is what broke the breath): rise 5 ms,
    decay 86 ms (= measured 69 ms decay-to-10%), level 0.01335 (calibrated
    to the real C2 2.5-8 kHz first-50 ms energy share by
    calibrate_knock_level.py), band 2500-8000 Hz (measured; the run-20
    smoke converged there unprompted). All live in the template.

SEARCHED (14 dims):
   0-7   env knots (8)  multiplicative over the 1/n baseline, log-interp
   8     rolloffLo      pp.rolloff1 @65 Hz   (per-note spectral tilt curve)
   9     rolloffHi      pp.rolloff1 @1046 Hz
  10     decayScale     multiplies the template's re-fit h1-equivalent
                        register curve (pp.decayRate paramMap values);
                        narrow band around 1.0 now that the curve itself is
                        measured per register (refit_decay_curve.py)
  11     shimmerDepth   unison-string beating (seeded 0.15 — measured depth
                        0.106-0.285 multi-strung; rate unmeasurable, so:)
  12     shimmerHz      searchable
  13     shimmerCoherence searchable
"""
import copy

import numpy as np

ENV_KNOTS = 8
N_PARTIALS = 120
ENV_HARMS = np.unique(np.round(np.geomspace(1, N_PARTIALS, ENV_KNOTS)).astype(int))
KNOT_RANGE = (0.25, 4.0)

# (name, lo, hi, warm)
SCALARS = [
    ("rolloffLo",        0.0, 1.5, 0.2),
    ("rolloffHi",        0.0, 1.5, 0.2),
    ("decayScale",       0.7, 1.4, 1.0),
    ("shimmerDepth",     0.0, 0.6, 0.15),
    ("shimmerHz",        0.2, 10.0, 2.0),
    ("shimmerCoherence", 0.0, 1.0, 0.5),
]

DIM = ENV_KNOTS + len(SCALARS)

ROLLOFF_FREQS = (65.4, 1046.5)


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


def _node(patch, nid):
    return next(n for n in patch["graph"]["nodes"] if n.get("id") == nid)


def encode(z, template):
    """Build a concrete piano patch from the CMA-ES vector."""
    p = copy.deepcopy(template)
    d = decode(z)
    parts = _node(p, "pp")["params"]

    # harmonic envelope: knots interp over log-harmonic axis x 1/n baseline
    n = len(parts["ampl1"])
    harm_idx = np.arange(1, n + 1)
    scale = np.exp(np.interp(np.log(harm_idx), np.log(ENV_HARMS),
                             np.log(d["env_knots"])))
    new_amp = (np.array(parts["ampl1"]) * scale).tolist()
    parts["ampl1"] = new_amp
    parts["ampl2"] = list(new_amp)

    # unison-beating shimmer (per-partial amp wander)
    parts["shimmerDepth1"] = parts["shimmerDepth2"] = float(d["shimmerDepth"])
    parts["shimmerHz"] = float(d["shimmerHz"])
    parts["shimmerCoherence"] = float(d["shimmerCoherence"])

    # hammer knock: LOCKED in the template (measured rise/decay/level/band —
    # run 23); the encoder no longer touches knockEnv/knockAmp/knockBP.

    # paramMap curves: keep locked B curve; scale the measured decay-rate
    # register curve; rewrite the rolloff tilt breakpoints.
    freq = p["instrument"]["paramMap"]["frequency"]
    for e in freq:
        if not isinstance(e, dict):
            continue
        if e["target"] == "pp.decayRate":
            e["curve"] = [[f, round(v * float(d["decayScale"]), 4)]
                          for f, v in e["curve"]]
        elif e["target"] == "pp.rolloff1":
            e["curve"] = [[ROLLOFF_FREQS[0], float(d["rolloffLo"])],
                          [ROLLOFF_FREQS[1], float(d["rolloffHi"])]]
    return p
