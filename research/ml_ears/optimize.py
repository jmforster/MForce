"""CMA-ES stage c — full patch optimizer loop.

Encodes a normalised CMA-ES vector into an additive viola patch (warm-started
at v5_04_cal_sustain), renders + scores it against the Iowa reference, and runs
CMA-ES to minimise the weighted timbre distance. Per-generation checkpoint +
best patch/WAV under research/ml_ears/cmaes_runs/<run>/.

Only engine-honored params are searched (verified against partials.h /
full_additive_source.h / formant.h). 27 dims:
  8 harmonic-envelope knots (multiplicative over the measured ampl baseline)
  formantWeight + 4 BandSpectrum region scalers
  motionDepth motionHz motionEvolve  shimmerDepth shimmerHz shimmerCoherence
    shimmerEvolve                              (motionCoherence pinned 0 = Iowa)
  bandwidth bandwidthHz                        (inter-harmonic broadband / term3)
  onsetFade onsetSpread onsetTilt              (attack / term4)
  vibDepth vibSpeed

Each internal coord is unbounded; param = lo+(hi-lo)*sigmoid(z), warm start =
logit of the v5_04 value. Determinism: fixed seed; render seed pinned in patch.

Usage: python optimize.py [--evals 100] [--run smoke] [--resume]
"""
import copy
import json
import os
import pickle
import sys

import numpy as np

import cmaes
import refmetrics as rm
import score_candidate as sc

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "..", "..", "patches", "fable1_v5", "v5_04_cal_sustain.json")
RUNS = os.path.join(HERE, "cmaes_runs")

# --- parameter layout: (name, lo, hi, base) --------------------------------
ENV_KNOTS = 8
ENV_HARMS = np.unique(np.round(np.geomspace(1, 48, ENV_KNOTS)).astype(int))  # knot harmonic indices
BAND_REGIONS = 4

# scalar params after the 8 env knots + 4 band scalers
SCALARS = [
    ("formantWeight", 0.0, 2.0, 1.0),
    ("motionDepth",   0.0, 25.0, 8.0),
    ("motionHz",      1.0, 30.0, 18.0),
    ("motionEvolve",  0.0, 1.0, 0.3),
    ("shimmerDepth",  0.0, 2.0, 0.9),
    ("shimmerHz",     0.2, 10.0, 1.4),
    ("shimmerCoherence", 0.0, 1.0, 0.5),
    ("shimmerEvolve", 0.0, 1.0, 0.3),
    ("bandwidth",     0.0, 0.6, 0.02),
    ("bandwidthHz",   20.0, 400.0, 60.0),
    ("onsetFade",     0.0, 0.5, 0.02),
    ("onsetSpread",   0.0, 0.5, 0.02),
    ("onsetTilt",    -1.0, 1.0, 0.0),
    ("vibDepth",      0.0, 0.05, 0.025),
    ("vibSpeed",      3.0, 8.0, 5.5),
]
KNOT_RANGE = (0.25, 4.0)   # multiplicative bounds for env knots and band scalers
DIM = ENV_KNOTS + BAND_REGIONS + len(SCALARS)


def _logit(u):
    u = np.clip(u, 1e-4, 1 - 1e-4)
    return np.log(u / (1 - u))


def _sig(z):
    return 1.0 / (1.0 + np.exp(-z))


def warm_start():
    """Internal z-vector placing gen-0 mean at the v5_04 baseline."""
    z = []
    for _ in range(ENV_KNOTS + BAND_REGIONS):     # knots + band scalers base=1.0
        u = (1.0 - KNOT_RANGE[0]) / (KNOT_RANGE[1] - KNOT_RANGE[0])
        z.append(_logit(u))
    for _, lo, hi, base in SCALARS:
        z.append(_logit((base - lo) / (hi - lo)))
    return np.array(z)


def decode(z):
    """z (DIM,) -> dict of concrete param values."""
    u = _sig(z)
    out = {}
    kr = KNOT_RANGE
    out["env_knots"] = kr[0] + (kr[1] - kr[0]) * u[:ENV_KNOTS]
    out["band_scale"] = kr[0] + (kr[1] - kr[0]) * u[ENV_KNOTS:ENV_KNOTS + BAND_REGIONS]
    j = ENV_KNOTS + BAND_REGIONS
    for (name, lo, hi, _), uj in zip(SCALARS, u[j:]):
        out[name] = lo + (hi - lo) * uj
    return out


def _node(patch, ntype):
    return next(n for n in patch["graph"]["nodes"] if n.get("type") == ntype)


def encode(z, template):
    """Build a concrete patch from the CMA-ES vector."""
    p = copy.deepcopy(template)
    d = decode(z)
    parts = _node(p, "ExplicitPartials")["params"]

    # harmonic envelope: interp knots over log-harmonic axis, multiply baseline
    n = len(parts["ampl1"])
    harm_idx = np.arange(1, n + 1)
    scale = np.exp(np.interp(np.log(harm_idx), np.log(ENV_HARMS), np.log(d["env_knots"])))
    base = np.array(parts["ampl1"])
    new_amp = (base * scale).tolist()
    parts["ampl1"] = new_amp
    parts["ampl2"] = list(new_amp)

    # motion / shimmer / broadband / attack (motionCoherence pinned 0)
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

    # BandSpectrum region scalers
    body = _node(p, "BandSpectrum")["params"]
    gains = np.array(body["gains"])
    edges = np.linspace(0, len(gains), BAND_REGIONS + 1).astype(int)
    for r in range(BAND_REGIONS):
        gains[edges[r]:edges[r + 1]] *= d["band_scale"][r]
    body["gains"] = gains.tolist()

    # formantWeight, vibrato
    _node(p, "AdditiveSource")["params"]["formantWeight"] = float(d["formantWeight"])
    vib = _node(p, "Vibrato")["params"]
    vib["depth"] = float(d["vibDepth"])
    vib["speed"] = float(d["vibSpeed"])
    return p


def make_objective(template, ref, idx=[0]):
    def objective(z):
        idx[0] += 1
        try:
            patch = sc.set_score(encode(z, template), sc.NOTE_MIDIS)
            s = sc.score(sc.measure(patch, f"opt{idx[0] % 8}"), ref)
            return s["total"], s
        except Exception as e:  # noqa: BLE001 — bad param combo -> penalty
            return 100.0, {"error": str(e)[:120]}
    return objective


def main():
    evals = int(_arg("--evals", 100))
    run = _arg("--run", "smoke")
    resume = "--resume" in sys.argv
    rundir = os.path.join(RUNS, run)
    os.makedirs(rundir, exist_ok=True)
    template = json.load(open(TEMPLATE))
    ref = json.load(open(sc.REF_PATH))

    es = cmaes.CMAES(warm_start(), 0.25, seed=42)
    ckpt = os.path.join(rundir, "state.pkl")
    if resume and os.path.exists(ckpt):
        es.set_state(pickle.load(open(ckpt, "rb")))
        print(f"resumed at gen {es.gen}")

    objective = make_objective(template, ref)
    best_f, best_s, best_z = np.inf, None, None
    done = es.gen * es.lam
    print(f"DIM={DIM}  lambda={es.lam}  target evals={evals}")

    while done < evals:
        X = es.ask()
        results = [objective(x) for x in X]
        F = np.array([r[0] for r in results])
        es.tell(X, F)
        done += es.lam
        i = int(np.argmin(F))
        if F[i] < best_f:
            best_f, best_s, best_z = float(F[i]), results[i][1], X[i].copy()
            bp = sc.set_score(encode(best_z, template), sc.NOTE_MIDIS)
            json.dump(bp, open(os.path.join(rundir, "best_patch.json"), "w"), indent=2)
            sc.render(bp, "best")  # writes scratch; copy for keeping
            import shutil
            shutil.copy(os.path.join(sc.SCRATCH, "cand_best.wav"),
                        os.path.join(rundir, "best.wav"))
        pickle.dump(es.get_state(), open(ckpt, "wb"))
        t = best_s
        print(f"gen {es.gen:3d} evals {done:4d}  genbest={F[i]:.4f}  best={best_f:.4f}  "
              f"sigma={es.sigma:.3f}"
              + (f"  [t1={t['term1_harm']:.2f} t2={t['term2_motion']:.2f} "
                 f"t3={t['term3_broadband']:.2f} t4={t['term4_attack']:.2f}]"
                 if t and "term1_harm" in t else ""), flush=True)

    print(f"\nbest total = {best_f:.4f} after {done} evals")
    print("per-term:", {k: round(v, 4) for k, v in best_s.items()
                        if k.startswith("term")})
    print("best patch:", os.path.join(rundir, "best_patch.json"))
    json.dump({"best_f": best_f, "terms": {k: v for k, v in best_s.items()
                                           if k.startswith("term") or k == "total"},
               "z": best_z.tolist(), "evals": done, "dim": DIM},
              open(os.path.join(rundir, "summary.json"), "w"), indent=2)


def _arg(flag, default):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


if __name__ == "__main__":
    main()
