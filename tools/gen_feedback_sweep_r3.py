"""Feedback-loop curve sweep, round 3 (backlog 37) — the DAMPED round.

r2 verdict: 66/100 periodic but "all variants of a buzz" — undamped loops.
r3 runs on the playability kit that landed 2026-09-01: keytracked in-loop
SVF (cutoff = m*f0 via a one-knot expressions Curve), DelayLine `compensate`
(loops on pitch while cutoff moves), amplitude-pin release, and envelope
attacks that are actually instantaneous (min-floor removal).

The curve space is anchored on the "three or four curves nature provides"
(feedback_loop_design.md): bow friction (steep stick, descending slip),
reed table (rises then CLOSES on the positive side), jet sigmoid (smooth
odd tanh — the blown-bottle junction), lip valve (expansive: shallow at
origin, steepening — hard brass onset). Family cells jitter the anchor
knots; "saturating" (r2's winner) and "wild" keep continuity with r2.

Skeletons by family: wind (sustained hiss only: jet, reed, saturating,
wild) vs contact (creak one-shot + faint hiss: bow, lip — lip needs the
kick because its origin gain is < 1 until driven).

Usage:
  python tools/gen_feedback_sweep_r3.py sweep <out_dir> [count] [master_seed]
  python tools/gen_feedback_sweep_r3.py archetypes <out_dir>
"""
import copy
import json
import os
import sys

import numpy as np

# ---------------------------------------------------------------- anchors ---
# Positive-side breakpoints origin-outward; neg side mirrored (odd) or given.
ANCHORS = {
    "bow": {
        "pos": [(0.12, 0.60), (0.50, 0.68), (1.00, 0.55)],   # stick then slip decay
        "neg": None,                                          # odd
        "smoothness": 0.5, "drive": 1.15, "m": 6.0, "res": 0.7,
        "hiss": 0.003, "contact": True,
    },
    "jet": {
        "pos": [(0.18, 0.50), (0.45, 0.78), (1.00, 0.82)],   # tanh-ish sigmoid
        "neg": None,
        "smoothness": 0.9, "drive": 1.05, "m": 1.6, "res": 0.9,
        "hiss": 0.02, "contact": False,
    },
    "reed": {
        "pos": [(0.12, 0.42), (0.40, 0.70), (0.70, 0.45), (1.00, 0.10)],  # closes
        "neg": [(-1.00, -0.90), (-0.55, -0.80), (-0.12, -0.42)],
        "smoothness": 0.6, "drive": 1.10, "m": 4.5, "res": 0.7,
        "hiss": 0.01, "contact": False,
    },
    "lip": {
        # Expansive valve, but with an EXPLICIT near-origin knot: smoothness
        # easing has ~zero slope at segment endpoints, so without it the
        # origin gain is ~0 at any drive and the loop never catches (two
        # silent renders taught this). Slope 0.67 to the first knot x drive
        # 2.6 = 1.74 small-signal gain — the DC-pressure bias a player
        # provides — then the expansive rise takes over.
        "pos": [(0.06, 0.04), (0.25, 0.12), (0.55, 0.50),
                (0.80, 0.92), (1.00, 0.98)],
        "neg": [(-1.00, -0.45), (-0.50, -0.30), (-0.20, -0.10),
                (-0.05, -0.03)],
        "smoothness": 0.5, "drive": 2.60, "m": 7.0, "res": 0.7,
        "hiss": 0.004, "contact": True,
    },
}


def flatten_curve(pos, neg):
    if neg is None:
        neg = [(-x, -y) for x, y in pos]
    pts = sorted(neg + [(0.0, 0.0)] + pos)
    flat, last_x = [], None
    for x, y in pts:
        if last_x is not None and x - last_x < 1e-4:
            continue
        flat += [round(x, 4), round(max(-1.2, min(1.2, y)), 4)]
        last_x = x
    return flat


def jitter_anchor(name, rng):
    """One sweep cell's curve: the anchor with per-knot jitter, occasional
    symmetry flip or extra knot. Returns (flat_values, params)."""
    a = copy.deepcopy(ANCHORS[name])
    scale = float(rng.uniform(0.7, 1.4))       # overall y (gain) scale

    def jig(pts, sign):
        out = []
        for x, y in pts:
            jx = x + rng.uniform(-0.06, 0.06) * abs(x)
            jy = (y * scale + rng.uniform(-0.12, 0.12))
            out.append((float(np.clip(sign * abs(jx), -1.0, 1.0) * (1 if sign > 0 else 1)),
                        float(np.clip(jy, -1.2, 1.2))))
        # keep x strictly inside (0,1], re-sorted by |x|
        out = sorted(((max(0.02, min(1.0, abs(x))) * sign, y) for x, y in out),
                     key=lambda p: abs(p[0]))
        # pin the outermost knot at exactly |x| = 1
        lx, ly = out[-1]
        out[-1] = (sign * 1.0, ly)
        return out

    pos = jig(a["pos"], +1)
    if a["neg"] is None and rng.random() < 0.8:
        neg = None                              # stay odd
    else:
        base_neg = a["neg"] if a["neg"] is not None else [(-x, -y) for x, y in a["pos"]]
        neg = jig(sorted(base_neg, key=lambda p: abs(p[0])), -1)
        neg = sorted(neg)
    if rng.random() < 0.15 and len(pos) < 5:    # occasional extra shoulder knot
        i = int(rng.integers(0, len(pos) - 1))
        x0, y0 = pos[i]
        x1, y1 = pos[i + 1]
        pos.insert(i + 1, (float((x0 + x1) / 2),
                           float((y0 + y1) / 2 + rng.uniform(-0.1, 0.1))))
    flat = flatten_curve(pos, neg)
    p = {
        "family": name,
        "slope0": round(pos[0][1] / pos[0][0], 3) if pos[0][0] > 0 else 0.0,
        "smoothness": float(np.clip(a["smoothness"] + rng.uniform(-0.15, 0.15),
                                    0.25, 1.0)),
        "odd_sym": neg is None,
        "dead_zone": False,
        "pos": {"shoulder": round(max(y for _, y in pos), 3),
                "humps": sum(1 for i in range(1, len(pos))
                             if pos[i][1] < pos[i - 1][1]),
                "fold": False},
        "scale": round(scale, 3),
    }
    p["neg"] = p["pos"]
    return flat, p


def gen_saturating(rng):
    """r2's winner family, kept for continuity (monotone odd saturator)."""
    slope0 = float(rng.uniform(1.5, 5.5))
    x0 = float(rng.uniform(0.06, 0.22))
    y0 = min(slope0 * x0, 0.92)
    shoulder = float(rng.uniform(max(0.55, y0), 0.95))
    pos = [(x0, y0)]
    if rng.random() < 0.5:
        kx = float(rng.uniform(x0 + 0.1, 0.8))
        pos.append((kx, float(rng.uniform(y0, shoulder))))
    pos.append((1.0, shoulder))
    p = {"family": "saturating", "slope0": round(slope0, 3),
         "smoothness": float(rng.choice([0.4, 0.6, 0.8])),
         "odd_sym": True, "dead_zone": False,
         "pos": {"shoulder": round(shoulder, 3), "humps": 0, "fold": False},
         "scale": 1.0}
    p["neg"] = p["pos"]
    return flatten_curve(pos, None), p


def gen_wild(rng):
    n = int(rng.integers(2, 4))
    xs = sorted(rng.uniform(0.08, 0.95, size=n))
    pos = [(float(x), float(rng.uniform(-0.7, 1.0))) for x in xs] + \
          [(1.0, float(rng.uniform(-0.9, 0.9)))]
    p = {"family": "wild", "slope0": round(pos[0][1] / pos[0][0], 3),
         "smoothness": float(rng.choice([0.3, 0.5, 0.8])),
         "odd_sym": bool(rng.random() < 0.5), "dead_zone": False,
         "pos": {"shoulder": round(max(y for _, y in pos), 3),
                 "humps": n, "fold": bool(pos[-1][1] < 0)},
         "scale": 1.0}
    p["neg"] = p["pos"]
    neg = None if p["odd_sym"] else \
        sorted((-float(x), float(rng.uniform(-1.0, 0.7))) for x, _ in pos)
    return flatten_curve(pos, neg), p


# --------------------------------------------------------------- skeleton ---
def build_patch(comment, curve, smoothness, drive, m, res, hiss_amp, contact,
                note_events, noise_seed, ratio=1.0, drive_attack=0.03):
    nodes = [
        {"id": "__perf_freq", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "hiss", "type": "WhiteNoiseSource",
         "params": {"seed": noise_seed, "amplitude": hiss_amp}},
        {"id": "driveenv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": drive_attack, "decay": 0.1,
                    "sustainLevel": 1.0, "release": 0.2,
                    "minValue": 0.06, "maxValue": drive}},
        {"id": "ampenv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": 0.0, "decay": 0.0,
                    "sustainLevel": 1.0, "release": 0.25,
                    "minValue": 0.0, "maxValue": 1.0}},
        {"id": "keytrack", "type": "CurveNode",
         "params": {"mode": "expressions",
                    "exprKnots": [{"x": 440.0, "form": "linear",
                                   "a": m, "b": 0.0}],
                    "source": {"ref": "__perf_freq"}}},
    ]
    if contact:
        nodes.append({"id": "excite", "type": "SegmentSource",
                      "params": {"oneShot": True, "seed": noise_seed,
                                 "values": [2.0, 0.6, 3.0, -0.35,
                                            4.0, 0.15, 6.0, 0.0]}})
        nodes.append({"id": "exsum", "type": "CombinedSource",
                      "params": {"source1": {"ref": "excite"},
                                 "source2": {"ref": "hiss"},
                                 "operation": "sum", "gainAdj": 0.0}})
        drive_in = "exsum"
    else:
        drive_in = "hiss"
    nodes += [
        {"id": "sum", "type": "CombinedSource",
         "params": {"source1": {"ref": drive_in}, "source2": {"tap": "delay"},
                    "operation": "sum", "gainAdj": 0.0}},
        {"id": "junction", "type": "Shaper",
         "params": {"source": {"ref": "sum"}, "drive": {"ref": "driveenv"},
                    "smoothness": smoothness, "values": curve}},
        # In-loop DC blocker: asymmetric junctions (reed, lip) rectify the
        # circulating signal into DC that the LP passes at unity and the
        # curve re-amplifies — measured up to 97% of peak on the lip
        # archetype, pulling it +185 cents sharp. Real bores reflect DC
        # away; KSString blocks it explicitly. The compensate walk picks up
        # this filter's (tiny, negative) phase delay automatically.
        {"id": "dcblock", "type": "SVFSource",
         "params": {"source": {"ref": "junction"}, "mode": "Highpass1P",
                    "cutoffFreq": 12.0, "resonance": 0.7}},
        {"id": "damp", "type": "SVFSource",
         "params": {"source": {"ref": "dcblock"}, "mode": "Lowpass",
                    "cutoffFreq": {"ref": "keytrack"}, "resonance": res}},
        {"id": "delay", "type": "DelayLine",
         "params": {"source": {"ref": "damp"},
                    "frequency": {"ref": "__perf_freq"}, "ratio": ratio,
                    "amplitude": {"ref": "ampenv"}, "compensate": True}},
    ]
    return {"_comment": comment, "sampleRate": 48000,
            "graph": {"nodes": nodes, "output": "delay"},
            "instrument": {"polyphony": 1, "volume": 0.5},
            "score": note_events}


# ------------------------------------------------------------------ modes ---
def run_sweep(out_dir, count, master):
    os.makedirs(out_dir, exist_ok=True)
    fams = ["bow", "jet", "reed", "lip", "saturating", "wild"]
    weights = [0.10, 0.15, 0.15, 0.15, 0.30, 0.15]
    manifest = {"skeleton": "damped-keytracked-compensated (r3)",
                "master_seed": master, "count": count, "variants": []}
    for i in range(count):
        rng = np.random.default_rng([master, i])
        fam = str(rng.choice(fams, p=weights))
        if fam in ANCHORS:
            curve, p = jitter_anchor(fam, rng)
            base = ANCHORS[fam]
            drive = float(base["drive"] * rng.uniform(0.85, 1.25))
            m = float(base["m"] * rng.uniform(0.7, 1.5))
            hiss = float(base["hiss"] * 10 ** rng.uniform(-0.4, 0.4))
            contact = base["contact"]
        else:
            curve, p = (gen_saturating if fam == "saturating" else gen_wild)(rng)
            drive = float(rng.uniform(0.85, 1.35))
            m = float(10 ** rng.uniform(np.log10(1.3), np.log10(8.0)))
            hiss = float(10 ** rng.uniform(-2.7, -1.6))
            contact = False
        res = float(rng.uniform(0.6, 2.0))
        ratio = float(rng.uniform(0.5, 2.5)) if rng.random() < 0.2 else 1.0
        note = int(rng.choice([41, 48, 55, 62, 72]))
        p.update({"drive_sustain": round(drive, 3), "m": round(m, 3),
                  "res": round(res, 3), "hiss": round(hiss, 5),
                  "ratio": round(ratio, 3), "note": note,
                  "noise_seed": int(rng.integers(1, 2 ** 30))})
        name = f"fb3_{i:03d}"
        patch = build_patch(
            f"feedback sweep r3 cell {i} ({fam}) - manifest.json has the "
            "drawn params; damped/keytracked/compensated skeleton",
            curve, p["smoothness"], drive, round(m, 3), round(res, 3),
            round(hiss, 5), contact,
            [{"note": note, "time": 0.0, "duration": 2.2, "velocity": 0.8}],
            p["noise_seed"], ratio=round(ratio, 3))
        with open(os.path.join(out_dir, name + ".json"), "w") as f:
            json.dump(patch, f, indent=1)
        manifest["variants"].append({"id": name, "file": name + ".wav",
                                     "patch": name + ".json",
                                     "params": p, "curve": curve})
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print(f"{count} patches + manifest -> {out_dir}")


def run_archetypes(out_dir):
    """The four un-jittered nature curves as playable audition patches."""
    os.makedirs(out_dir, exist_ok=True)
    scores = {
        "jet":  [41, 48, 55],    # bottle hoots low
        "reed": [48, 55, 62],
        "lip":  [41, 48, 55],
        "bow":  [48, 55, 62],
    }
    for name, a in ANCHORS.items():
        curve = flatten_curve(a["pos"], a["neg"])
        events = [{"note": n, "time": j * 1.6, "duration": 1.3,
                   "velocity": 0.8} for j, n in enumerate(scores[name])]
        patch = build_patch(
            f"junction archetype '{name}' - one of the three-or-four curves "
            "nature provides (feedback_loop_design.md), un-jittered, on the "
            "damped/keytracked/compensated skeleton. 2026-09-01.",
            curve, a["smoothness"], a["drive"], a["m"], a["res"], a["hiss"],
            a["contact"], events, noise_seed=774000 + hash(name) % 1000)
        fn = os.path.join(out_dir, f"junction_{name}.json")
        with open(fn, "w") as f:
            json.dump(patch, f, indent=1)
        print("wrote", fn)


def main():
    mode = sys.argv[1]
    if mode == "sweep":
        run_sweep(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 100,
                  int(sys.argv[4]) if len(sys.argv) > 4 else 20260901)
    elif mode == "archetypes":
        run_archetypes(sys.argv[2])
    else:
        raise SystemExit("mode must be 'sweep' or 'archetypes'")


if __name__ == "__main__":     # r4 imports build_patch/flatten_curve/ANCHORS
    main()
