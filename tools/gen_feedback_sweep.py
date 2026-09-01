"""Feedback-loop curve sweep, round 1 (backlog 37, docs/feedback_loop_design.md).

Generates N Shaper junction curves inside the loop_selfosc skeleton
(self-oscillating tap-closed loop: seeded noise whisper -> Shaper -> DelayLine
-> tap back). The CURVE is the search space: breakpoint count, slope through
zero, shoulder height/asymmetry, interior humps (negative-slope regions),
odd-symmetry vs free, dead zones, fold-backs. Everything else stays fixed so
what you hear differ IS the junction.

Per-patch RNG is seeded from a master seed; every draw is recorded in the
manifest so any cell reproduces exactly.

Usage: python tools/gen_feedback_sweep.py <out_dir> [count] [master_seed]
Writes <out_dir>/fb1_NNN.json + <out_dir>/manifest.json.
"""
import json, os, sys
import numpy as np


def gen_curve(rng):
    """One junction curve as (values_flat, params_dict).

    Round-2 families (Matt's sweep-1 verdict, 2026-08-31): the baselines'
    perceived quality lives on MONOTONE saturating curves — even steep ones
    (slope 4.5-5) sing; humps/folds (multivalued gain) were the chaos
    engine, not steepness. So: mostly monotone, a gently-non-monotone
    middle family, and a wild minority the pitchedness gate can judge.
    Smoothness floor 0.25 (step curves = infinite slope at every point)."""
    p = {}
    p["family"] = str(rng.choice(["saturating", "plateau", "wild"],
                                 p=[0.5, 0.3, 0.2]))
    p["slope0"] = float(rng.uniform(1.2, 6.0))       # gain through zero
    p["x0"] = float(rng.uniform(0.04, 0.25))         # end of the linear core
    p["dead_zone"] = bool(rng.random() < 0.25)       # flat spot at the origin
    p["odd_sym"] = bool(rng.random() < 0.5)          # mirror the negative side
    p["smoothness"] = float(rng.choice([0.25, 0.5, 0.75, 1.0],
                                       p=[0.2, 0.35, 0.25, 0.2]))

    def half_side(sign):
        """Points for one polarity, origin-outward, as [(x, y), ...]."""
        shoulder = rng.uniform(0.5, 0.95)
        y0 = min(p["slope0"] * p["x0"], 0.95)
        # A steep core can top the drawn shoulder (slope*x0 up to 0.95);
        # monotone families lift the shoulder to meet it rather than descend.
        if p["family"] != "wild":
            shoulder = max(shoulder, y0)
        pts = [(p["x0"], y0)]
        fold = False
        if p["family"] == "saturating":
            # Monotone rise: optional soft knee between core and shoulder.
            n_humps = 0
            if rng.random() < 0.5:
                kx = rng.uniform(p["x0"] + 0.1, 0.8)
                ky = rng.uniform(y0, shoulder)       # monotone by construction
                pts.append((float(kx), float(ky)))
        elif p["family"] == "plateau":
            # Gently non-monotone: one dip of at most 0.15 below the running
            # level, then back up to the shoulder.
            n_humps = 1
            kx = rng.uniform(p["x0"] + 0.1, 0.75)
            dip = rng.uniform(0.02, 0.15)
            pts.append((float(kx), float(max(-0.2, y0 - dip))))
        else:  # wild — round-1 behavior, judged by the pitchedness gate
            n_humps = int(rng.integers(1, 3))
            xs = sorted(rng.uniform(p["x0"] + 0.08, 0.9, size=n_humps))
            for x in xs:
                pts.append((float(x), float(rng.uniform(-0.7, 1.0))))
            fold = rng.random() < 0.3
        if shoulder < max(y for _, y in pts) and p["family"] != "wild":
            shoulder = max(y for _, y in pts)        # keep monotone families monotone
        pts.append((1.0, -shoulder if fold else shoulder))
        return [(sign * x, sign * y) for x, y in pts], \
               {"shoulder": float(shoulder), "humps": n_humps, "fold": bool(fold)}

    pos, pos_meta = half_side(+1)
    if p["odd_sym"]:
        neg = [(-x, -y) for x, y in pos]
        neg_meta = pos_meta
    else:
        neg, neg_meta = half_side(-1)
    p["pos"] = pos_meta
    p["neg"] = neg_meta

    pts = sorted(neg + ([(0.0, 0.0)] if p["dead_zone"] or rng.random() < 0.5
                        else []) + pos)
    # de-duplicate x collisions (ascending contract) and clamp y
    flat, last_x = [], None
    for x, y in pts:
        if last_x is not None and x - last_x < 1e-4:
            continue
        flat += [round(x, 4), round(max(-1.2, min(1.2, y)), 4)]
        last_x = x
    return flat, p


def make_patch(idx, rng):
    curve, params = gen_curve(rng)
    params["drive_sustain"] = float(rng.uniform(0.8, 1.3))
    params["ratio"] = float(rng.uniform(0.5, 2.5)) if rng.random() < 0.25 else 1.0
    params["noise_seed"] = int(rng.integers(1, 2**30))
    note = int(rng.choice([41, 48, 55]))
    params["note"] = note

    patch = {
        "_comment": f"feedback curve sweep r1 cell {idx} - see manifest.json "
                    "for the drawn params; skeleton = loop_selfosc "
                    "(docs/feedback_loop_design.md)",
        "sampleRate": 48000,
        "graph": {
            "nodes": [
                {"id": "__perf_freq", "type": "PerformNode",
                 "params": {"field": "frequency"}},
                {"id": "hiss", "type": "WhiteNoiseSource",
                 "params": {"seed": params["noise_seed"], "amplitude": 0.001}},
                {"id": "env", "type": "Envelope",
                 "params": {"preset": "adsr", "attack": 0.01, "decay": 0.1,
                            "sustainLevel": 1.0, "release": 0.2,
                            "minValue": 0.05,
                            "maxValue": params["drive_sustain"]}},
                {"id": "sum", "type": "CombinedSource",
                 "params": {"source1": {"ref": "hiss"},
                            "source2": {"tap": "delay"},
                            "operation": "sum", "gainAdj": 0.0}},
                {"id": "junction", "type": "Shaper",
                 "params": {"source": {"ref": "sum"}, "drive": {"ref": "env"},
                            "smoothness": params["smoothness"],
                            "values": curve}},
                {"id": "delay", "type": "DelayLine",
                 "params": {"source": {"ref": "junction"},
                            "frequency": {"ref": "__perf_freq"},
                            "ratio": params["ratio"]}},
            ],
            "output": "delay",
        },
        "instrument": {"polyphony": 1, "volume": 0.5},
        "score": [{"note": note, "time": 0.0, "duration": 2.2,
                   "velocity": 0.8}],
    }
    return patch, params, curve


def main():
    out_dir = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    master = int(sys.argv[3]) if len(sys.argv) > 3 else 20260831
    os.makedirs(out_dir, exist_ok=True)
    manifest = {"skeleton": "loop_selfosc", "master_seed": master,
                "count": count, "variants": []}
    for i in range(count):
        rng = np.random.default_rng([master, i])
        patch, params, curve = make_patch(i, rng)
        name = f"fb1_{i:03d}"
        with open(os.path.join(out_dir, name + ".json"), "w") as f:
            json.dump(patch, f, indent=1)
        manifest["variants"].append(
            {"id": name, "file": name + ".wav", "patch": name + ".json",
             "params": params, "curve": curve})
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print(f"{count} patches + manifest -> {out_dir}")


main()
