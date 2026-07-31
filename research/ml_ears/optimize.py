"""CMA-ES stage c — full patch optimizer loop.

Encodes a normalised CMA-ES vector into an additive viola patch (warm-started
at v5_04_cal_sustain), renders + scores it against the Iowa reference, and runs
CMA-ES to minimise the weighted timbre distance. Per-generation checkpoint +
best patch/WAV under research/ml_ears/cmaes_runs/<run>/.

Only engine-honored params are searched (verified against partials.h /
full_additive_source.h / formant.h). 31 dims:
  8 harmonic-envelope knots (multiplicative over the measured ampl baseline)
  formantWeight + 4 BandSpectrum region scalers
  motionDepth motionHz motionEvolve  shimmerDepth shimmerHz shimmerCoherence
    shimmerEvolve                              (motionCoherence pinned 0 = Iowa)
  bandwidth bandwidthHz                        (inter-harmonic broadband / term3)
  onsetFade onsetSpread onsetTilt              (attack / term4)
  vibDepth vibSpeed
  resSustain50 resSustain400                   (fadeEnv.sustainLevel freq curve;
    residue-cluster sustain, v6 expand+fadeEnv structure added by the encoder)
  bwFloor50 bwFloor400                         (vla_partials.bandwidth1 freq
    curve — per-note bandwidth floor; overrides the static bandwidth scalar)

The curve dims are emitted as instrument.paramMap frequency-curve entries with
breakpoints fixed at 50/400 Hz (shape as patches/fable1_v6/v6_01 and v6_04).

Each internal coord is unbounded; param = lo+(hi-lo)*sigmoid(z), warm start =
logit of the v5_04 value (curve dims: v6_02 sustain mids, v6_04 floor).
Determinism: fixed seed; render seed pinned in patch.

Usage: python optimize.py [--evals 100] [--run smoke] [--config viola]
                          [--warm-from <summary.json>] [--resume]
  --warm-from: start the CMA-ES mean at a previous run's best z (summary.json
    "z" list). A shorter vector (e.g. viola1's 27-dim) fills the leading dims;
    the remaining dims keep their default warm starts.
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

# frequency-curve dims: value at 50 Hz / value at 400 Hz, breakpoints fixed.
# Emitted as instrument.paramMap curve entries (v6_01 / v6_04 JSON shape).
CURVE_FREQS = (50, 400)
CURVES = [
    ("resSustain50",  0.0, 0.6, 0.12),    # fadeEnv.sustainLevel @50   (v6_02)
    ("resSustain400", 0.0, 0.6, 0.32),    # fadeEnv.sustainLevel @400  (v6_02)
    ("bwFloor50",     0.0, 0.2, 0.015),   # vla_partials.bandwidth1 @50  (v6_04)
    ("bwFloor400",    0.0, 0.2, 0.08),    # vla_partials.bandwidth1 @400 (v6_04)
]
DIM = ENV_KNOTS + BAND_REGIONS + len(SCALARS) + len(CURVES)


def _logit(u):
    u = np.clip(u, 1e-4, 1 - 1e-4)
    return np.log(u / (1 - u))


def _sig(z):
    return 1.0 / (1.0 + np.exp(-z))


def warm_start(z_init=None):
    """Internal z-vector placing gen-0 mean at the v5_04 baseline.

    z_init: optional leading z values from a previous run (e.g. viola1's
    27-dim best); remaining dims keep their default warm starts.
    """
    z = []
    for _ in range(ENV_KNOTS + BAND_REGIONS):     # knots + band scalers base=1.0
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
    """z (DIM,) -> dict of concrete param values."""
    u = _sig(z)
    out = {}
    kr = KNOT_RANGE
    out["env_knots"] = kr[0] + (kr[1] - kr[0]) * u[:ENV_KNOTS]
    out["band_scale"] = kr[0] + (kr[1] - kr[0]) * u[ENV_KNOTS:ENV_KNOTS + BAND_REGIONS]
    j = ENV_KNOTS + BAND_REGIONS
    for (name, lo, hi, _), uj in zip(SCALARS + CURVES, u[j:]):
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

    # residue layer (v6_01 structure): expand cluster + fadeEnv as amplEnv.
    # Base partials have ampl1 == ampl2 so amplE only shapes the expanded
    # cluster (loPct/power asymmetry) — fadeEnv.sustainLevel = residue sustain.
    parts["expandRule"] = {"count": 2, "recurse": 0,
                           "spacing1": 0.3, "spacing2": 0.3,
                           "dt1": 0.06, "dt2": 0.06,
                           "loPct1": 0.0, "loPct2": 0.25,
                           "power1": 6.0, "power2": 1.0}
    parts["amplEnv"] = {"ref": "fadeEnv"}
    nodes = p["graph"]["nodes"]
    if not any(n.get("id") == "fadeEnv" for n in nodes):
        nodes.insert(0, {"id": "fadeEnv", "type": "Envelope",
                         "params": {"preset": "adsr", "attack": 0.005,
                                    "decay": 0.15,
                                    "sustainLevel": float(d["resSustain400"]),
                                    "release": 0.1}})

    # frequency-curve paramMap entries (per-note residue sustain + bw floor)
    inst = p.setdefault("instrument", {"polyphony": 1})
    pmap = inst.setdefault("paramMap", {})
    freq = pmap.get("frequency", "vib.frequency")
    if not isinstance(freq, list):
        freq = [freq]
    freq = [e for e in freq if not isinstance(e, dict)]   # drop stale curves
    freq.append({"target": "fadeEnv.sustainLevel",
                 "curve": [[CURVE_FREQS[0], float(d["resSustain50"])],
                           [CURVE_FREQS[1], float(d["resSustain400"])]]})
    freq.append({"target": "vla_partials.bandwidth1",
                 "curve": [[CURVE_FREQS[0], float(d["bwFloor50"])],
                           [CURVE_FREQS[1], float(d["bwFloor400"])]]})
    pmap["frequency"] = freq
    return p


def get_encoder(cfg):
    """(dim, warm_start_fn, encode_fn) for the config's encoder.

    Default = the viola layout defined in this module; "clarinet" =
    encoder_clarinet (noise-bed dims, vibrato pinned off, no residue
    cluster)."""
    if cfg.get("encoder") == "clarinet":
        import encoder_clarinet as enc
        return enc.DIM, enc.warm_start, enc.encode
    return DIM, warm_start, encode


def make_objective(template, ref, note_midis, enc=None, idx=[0]):
    enc = enc or encode
    def objective(z):
        idx[0] += 1
        try:
            patch = sc.set_score(enc(z, template), note_midis)
            s = sc.score(sc.measure(patch, f"opt{idx[0] % 8}", note_midis), ref)
            return s["total"], s
        except Exception as e:  # noqa: BLE001 — bad param combo -> penalty
            return 100.0, {"error": str(e)[:120]}
    return objective


def main():
    evals = int(_arg("--evals", 100))
    run = _arg("--run", "smoke")
    cfg = sc.load_config(_arg("--config", "viola"))
    resume = "--resume" in sys.argv
    warm_from = _arg("--warm-from", None)
    rundir = os.path.join(cfg.get("run_root") or RUNS, run)
    os.makedirs(rundir, exist_ok=True)
    template = json.load(open(cfg.get("template") or TEMPLATE))
    ref = json.load(open(cfg.get("reference_path") or sc.REF_PATH))
    note_midis = cfg.get("eval_note_midis") or sc.NOTE_MIDIS

    dim, warm_fn, enc_fn = get_encoder(cfg)
    z0 = json.load(open(warm_from))["z"] if warm_from else None
    x0 = warm_fn(z0)
    es = cmaes.CMAES(x0, 0.25, seed=42)
    ckpt = os.path.join(rundir, "state.pkl")
    if resume and os.path.exists(ckpt):
        es.set_state(pickle.load(open(ckpt, "rb")))
        print(f"resumed at gen {es.gen}")

    objective = make_objective(template, ref, note_midis, enc_fn)
    best_f, best_s, best_z = np.inf, None, None
    bestmeta = os.path.join(rundir, "best_meta.json")
    if resume and os.path.exists(bestmeta):     # carry best across resumes
        bm = json.load(open(bestmeta))
        best_f, best_s = bm["best_f"], bm["terms"]
        best_z = np.array(bm["z"])
        print(f"carried best={best_f:.4f} from checkpoint")
    done = es.gen * es.lam
    print(f"config={cfg['name']}  DIM={dim}  lambda={es.lam}  target evals={evals}"
          + (f"  warm-from={warm_from}" if warm_from else ""))

    if not resume:
        # score the warm-start mean first so the run's best can never regress
        f0v, s0 = objective(x0)
        best_f, best_s, best_z = float(f0v), s0, x0.copy()
        bp = sc.set_score(enc_fn(x0, template), note_midis)
        json.dump(bp, open(os.path.join(rundir, "best_patch.json"), "w"), indent=2)
        if "term1_harm" in s0:                      # warm eval rendered OK
            import shutil
            sc.render(bp, "best")
            shutil.copy(os.path.join(sc.SCRATCH, "cand_best.wav"),
                        os.path.join(rundir, "best.wav"))
            json.dump({"best_f": best_f, "z": best_z.tolist(),
                       "terms": best_s}, open(bestmeta, "w"), indent=2)
        print(f"warm-start mean score = {f0v:.4f}"
              + (f"  [t1={s0['term1_harm']:.2f} t2={s0['term2_motion']:.2f} "
                 f"t3={s0['term3_broadband']:.2f} t4={s0['term4_attack']:.2f}]"
                 if "term1_harm" in s0 else ""), flush=True)

    while done < evals:
        X = es.ask()
        results = [objective(x) for x in X]
        F = np.array([r[0] for r in results])
        es.tell(X, F)
        done += es.lam
        i = int(np.argmin(F))
        if F[i] < best_f:
            best_f, best_s, best_z = float(F[i]), results[i][1], X[i].copy()
            bp = sc.set_score(enc_fn(best_z, template), note_midis)
            json.dump(bp, open(os.path.join(rundir, "best_patch.json"), "w"), indent=2)
            sc.render(bp, "best")  # writes scratch; copy for keeping
            import shutil
            shutil.copy(os.path.join(sc.SCRATCH, "cand_best.wav"),
                        os.path.join(rundir, "best.wav"))
            json.dump({"best_f": best_f, "z": best_z.tolist(),
                       "terms": best_s}, open(bestmeta, "w"), indent=2)
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
               "z": best_z.tolist(), "evals": done, "dim": dim,
               "config": cfg["name"]},
              open(os.path.join(rundir, "summary.json"), "w"), indent=2)


def _arg(flag, default):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


if __name__ == "__main__":
    main()
