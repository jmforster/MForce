"""Fable1 v2 batch — dial-ups and reworks from Matt's audition of v1.

Findings applied (Matt, 2026-07-03):
- Independent partial movement segregates only at large depths -> depth
  ladder for incoherent motion (10/18/30 cents).
- Evolution features (trade/shimmer) were too subtle -> dial up.
- Noise via partials must be addition/subtraction of partials, not movement
  of resident partials -> stationary decaying cluster (08 rework), short
  bandwidth burst with no motion settle (09 rework).
- Onset bloom possibly inaudible at 150 ms -> 400 ms probe.
No engine changes; existing configs only. Outputs patches/fable1_v2/.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_fable1_patches import load_base, node, add_env  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(REPO, "patches", "fable1_v2")
os.makedirs(OUT, exist_ok=True)

VARIANTS = {}


def variant(name):
    def deco(fn):
        VARIANTS[name] = fn
        return fn
    return deco


def motion(p, depth, coh, evolve=0.3, hz=3.0):
    node(p, "vla_partials")["params"].update({
        "motionDepth1": depth, "motionDepth2": depth, "motionHz": hz,
        "motionCoherence": coh, "motionEvolve": evolve,
    })


@variant("v2_01_mot10")
def a(p): motion(p, 10.0, 0.0)


@variant("v2_02_mot18")
def b(p): motion(p, 18.0, 0.0)


@variant("v2_03_mot30")
def c(p): motion(p, 30.0, 0.0)


@variant("v2_04_trade80")
def d(p):
    node(p, "vla_partials")["params"].update({"tradeDepth": 0.8, "tradeHz": 1.2})


@variant("v2_05_trade50_3hz")
def e(p):
    node(p, "vla_partials")["params"].update({"tradeDepth": 0.5, "tradeHz": 3.0})


@variant("v2_06_shimmer70")
def f(p):
    node(p, "vla_partials")["params"].update({
        "shimmerDepth1": 0.7, "shimmerDepth2": 0.7, "shimmerHz": 2.5,
        "shimmerCoherence": 0.3, "shimmerEvolve": 0.5,
    })


@variant("v2_07_shimmer_mot")
def g(p):
    """Combined sustain evolution: shimmer + moderate incoherent motion."""
    node(p, "vla_partials")["params"].update({
        "shimmerDepth1": 0.55, "shimmerDepth2": 0.55, "shimmerHz": 2.5,
        "shimmerCoherence": 0.3, "shimmerEvolve": 0.5,
    })
    motion(p, 10.0, 0.0)


@variant("v2_08_cluster_decay")
def h(p):
    """Matt's addition/subtraction rule, literally: inharmonic sub-partial
    cluster at fixed positions (multEnv untouched = no movement), faded out
    by amplEnv over 0.4 s. Cluster is subtracted, never moved."""
    add_env(p, "fadeEnv", 0.005, 0.4)
    node(p, "vla_partials")["params"].update({
        "expandRule": {
            "count": 2, "recurse": 0,
            "spacing1": 1.2, "spacing2": 1.2,
            "dt1": 0.06, "dt2": 0.06,
            "loPct1": 0.0, "loPct2": 0.5,
            "power1": 6.0, "power2": 1.0,
        },
        "amplEnv": {"ref": "fadeEnv"},
    })


@variant("v2_09_burst_short")
def i(p):
    """v1-09 with the offending motion settle removed and the burst cut to
    ~50 ms. Bandwidth AM + onset dispersion only."""
    add_env(p, "burstEnv", 0.003, 0.05)
    node(p, "vla_partials")["params"].update({
        "bandwidth1": 0.0, "bandwidth2": 0.85, "bandwidthHz": 120.0,
        "bwEnv": {"ref": "burstEnv"},
        "onsetSpread": 0.06, "onsetTilt": 1.0, "onsetFade": 0.03,
    })


@variant("v2_10_bloom400")
def j(p):
    """Audibility probe for onset dispersion: 400 ms low-first bloom."""
    node(p, "vla_partials")["params"].update({
        "onsetSpread": 0.4, "onsetTilt": 1.0, "onsetFade": 0.08,
    })


def main():
    for name, fn in sorted(VARIANTS.items()):
        p = load_base()
        fn(p)
        path = os.path.join(OUT, name + ".json")
        with open(path, "w") as fh:
            json.dump(p, fh, indent=2)
        print("wrote", path)


if __name__ == "__main__":
    main()
