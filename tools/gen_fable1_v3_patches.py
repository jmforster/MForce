"""Fable1 v3 batch — 96 extrapolated partials, wide note ladder, cluster 2x2,
sustain/attack stacks. See docs/Fable1_results.md v3 section.

Partial extrapolation: power-law fit (log amp vs log n) over harmonics 25-48
of the Iowa-derived viola amplitudes, anchored to continue smoothly from
harmonic 48, extended to 96. mult2/ampl2 mirror mult1/ampl1 (no evolution in
the base sound, same as the 48-partial original).

Note ladder: C2 G2 C3 G3 C4 G4 C5 G5 C6 D6 E6 — exercises the new partial
count at the bottom and the 16 kHz cutoff behavior at the top.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_fable1_patches import load_base, node, add_env  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(REPO, "patches", "fable1_v3")
os.makedirs(OUT, exist_ok=True)

NOTES = [36, 43, 48, 55, 60, 67, 72, 79, 84, 86, 88]
N_EXT = 96
FIT_LO, FIT_HI = 25, 48  # 1-based harmonic range for the tail fit


def extend_partials(params):
    a = params["ampl1"]
    n0 = len(a)
    # Least-squares line in log-log space over the fit window.
    xs = [math.log(n) for n in range(FIT_LO, FIT_HI + 1)]
    ys = [math.log(max(a[n - 1], 1e-12)) for n in range(FIT_LO, FIT_HI + 1)]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / \
        sum((x - mx) ** 2 for x in xs)
    # Anchor at the last real harmonic so the extension continues smoothly.
    anchor = max(a[n0 - 1], 1e-12)
    ext = [anchor * math.exp(slope * (math.log(n) - math.log(n0)))
           for n in range(n0 + 1, N_EXT + 1)]
    params["ampl1"] = a + ext
    params["ampl2"] = params["ampl1"][:]
    params["mult1"] = list(range(1, N_EXT + 1))
    params["mult2"] = params["mult1"][:]


def load_base96():
    p = load_base()
    extend_partials(node(p, "vla_partials")["params"])
    p["score"] = [
        {"note": n, "velocity": 0.85, "time": 2.0 * i, "duration": 1.6}
        for i, n in enumerate(NOTES)
    ]
    return p


def sustain_stack(p):
    """The three audition-validated evolution effects at moderate levels."""
    node(p, "vla_partials")["params"].update({
        "motionDepth1": 18.0, "motionDepth2": 18.0, "motionHz": 3.0,
        "motionCoherence": 0.0, "motionEvolve": 0.3,
        "tradeDepth": 0.5, "tradeHz": 1.2,
        "shimmerDepth1": 0.4, "shimmerDepth2": 0.4, "shimmerHz": 2.5,
        "shimmerCoherence": 0.3, "shimmerEvolve": 0.5,
    })


def cluster(p, spacing, lo_pct, decay):
    """Stationary inharmonic cluster, amplitude-only decay (Matt's
    addition/subtraction rule)."""
    add_env(p, "fadeEnv", 0.005, decay)
    node(p, "vla_partials")["params"].update({
        "expandRule": {
            "count": 2, "recurse": 0,
            "spacing1": spacing, "spacing2": spacing,
            "dt1": 0.06, "dt2": 0.06,
            "loPct1": 0.0, "loPct2": lo_pct,
            "power1": 6.0, "power2": 1.0,
        },
        "amplEnv": {"ref": "fadeEnv"},
    })


VARIANTS = {}


def variant(name):
    def deco(fn):
        VARIANTS[name] = fn
        return fn
    return deco


@variant("v3_00_control96")
def a(p):
    """Base 96-partial viola, no motion features — the new baseline."""


@variant("v3_01_mot30_fix")
def b(p):
    """v2_03 rerun after the cutoff break->continue fix: 30-cent incoherent
    motion. High notes should lose the aliasing-like chatter."""
    node(p, "vla_partials")["params"].update({
        "motionDepth1": 30.0, "motionDepth2": 30.0, "motionHz": 3.0,
        "motionCoherence": 0.0, "motionEvolve": 0.3,
    })


@variant("v3_02_cluster_tight")
def c(p):
    """Cluster 2x2 axis 1: original level/decay, tight 0.3-semi spacing —
    cluster as blur around each harmonic instead of extra pitches."""
    cluster(p, 0.3, 0.5, 0.4)


@variant("v3_03_cluster_low")
def d(p):
    """Axis 2: original wide 1.2-semi spacing, but lower level and 0.15 s
    decay."""
    cluster(p, 1.2, 0.25, 0.15)


@variant("v3_04_cluster_tight_low")
def e(p):
    """Both axes: tight spacing + low level + fast decay."""
    cluster(p, 0.3, 0.25, 0.15)


@variant("v3_05_sustain_stack")
def f(p):
    """mot18 incoherent + trade 0.5 @ 1.2 Hz + shimmer 0.4 — candidate
    viola sustain."""
    sustain_stack(p)


@variant("v3_06_attack_stack")
def g(p):
    """First full recipe: sustain stack + 50 ms bandwidth burst + 150 ms
    low-first bloom + tight/low/fast cluster."""
    sustain_stack(p)
    cluster(p, 0.3, 0.25, 0.15)
    add_env(p, "burstEnv", 0.003, 0.05)
    node(p, "vla_partials")["params"].update({
        "bandwidth1": 0.0, "bandwidth2": 0.85, "bandwidthHz": 120.0,
        "bwEnv": {"ref": "burstEnv"},
        "onsetSpread": 0.15, "onsetTilt": 1.0, "onsetFade": 0.05,
    })


def main():
    for name, fn in sorted(VARIANTS.items()):
        p = load_base96()
        fn(p)
        path = os.path.join(OUT, name + ".json")
        with open(path, "w") as fh:
            json.dump(p, fh, indent=2)
        print("wrote", path)


if __name__ == "__main__":
    main()
