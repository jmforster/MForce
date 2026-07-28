"""Fable1 v6 — frequency-dependent residue/floor curves (Matt's v4 verdict:
static residue values are wrong across register; 25% right at 400 Hz, too
much at 50 Hz). Uses the new paramMap curve mechanism (legacy
ParameterMapping Function port): any param or config can be driven by a
frequency→value breakpoint curve, evaluated per note.

v6_01..03 = full calibrated recipe with three cluster-residue curves
(Matt's rough guesses: 8-15% @ 50 Hz -> 25-40% @ 400 Hz).
v6_04 = bandwidth floor isolated with a curve (was constant 0.06 —
read as low-frequency "rumble"); now 1.5% @ 50 Hz -> 8% @ 400 Hz.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_fable1_v3_patches import load_base96  # noqa: E402
from gen_fable1_v4_patches import cluster_res, burst_res  # noqa: E402
from gen_fable1_v5_patches import cal_sustain  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(REPO, "patches", "fable1_v6")
os.makedirs(OUT, exist_ok=True)

RESIDUE_CURVES = {
    "v6_01_res_curve_lo":  [[50, 0.08], [400, 0.25]],
    "v6_02_res_curve_mid": [[50, 0.12], [400, 0.32]],
    "v6_03_res_curve_hi":  [[50, 0.15], [400, 0.40]],
}
BW_FLOOR_CURVE = [[50, 0.015], [400, 0.08]]


def add_freq_target(p, target, curve):
    pm = p["instrument"]["paramMap"]
    cur = pm["frequency"]
    if not isinstance(cur, list):
        cur = [cur]
    cur.append({"target": target, "curve": curve})
    pm["frequency"] = cur


def main():
    for name, curve in RESIDUE_CURVES.items():
        p = load_base96()
        cal_sustain(p)
        cluster_res(p, 0.15)   # nominal; curve overrides per note
        burst_res(p, 0.05)
        add_freq_target(p, "fadeEnv.sustainLevel", curve)
        with open(os.path.join(OUT, name + ".json"), "w") as fh:
            json.dump(p, fh, indent=2)
        print("wrote", name)

    p = load_base96()
    burst_res(p, 0.06)
    add_freq_target(p, "vla_partials.bandwidth1", BW_FLOOR_CURVE)
    with open(os.path.join(OUT, "v6_04_bwfloor_curve.json"), "w") as fh:
        json.dump(p, fh, indent=2)
    print("wrote v6_04_bwfloor_curve")


if __name__ == "__main__":
    main()
