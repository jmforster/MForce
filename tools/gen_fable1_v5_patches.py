"""Fable1 v5 — Iowa-grounded motion parameters (dsp/BACKLOG item 2, G1a).

Sustain motion/shimmer values come from measurement, not hand-tuning
(research/ml_ears/derive_motion.py on 8 Iowa viola samples):
  motion: 13 cents, 8.3 Hz, coherence 0.0   (line broadening, incoherent)
  shimmer: depth 0.9, 1.4 Hz, coherence 0.5 (amplitude fluctuation)
evolve isn't directly measurable yet — kept mild (0.3).
Attack side (cluster residue / bw floor / bloom) carries over from v4_04
unchanged; it wasn't part of this measurement.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_fable1_patches import node  # noqa: E402
from gen_fable1_v3_patches import load_base96  # noqa: E402
from gen_fable1_v4_patches import cluster_res, burst_res  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(REPO, "patches", "fable1_v5")
os.makedirs(OUT, exist_ok=True)


def iowa_sustain(p, shimmer_depth=0.9):
    node(p, "vla_partials")["params"].update({
        "motionDepth1": 13.0, "motionDepth2": 13.0, "motionHz": 8.3,
        "motionCoherence": 0.0, "motionEvolve": 0.3,
        "shimmerDepth1": shimmer_depth, "shimmerDepth2": shimmer_depth,
        "shimmerHz": 1.4, "shimmerCoherence": 0.5, "shimmerEvolve": 0.3,
        "tradeDepth": 0.0,  # explicit: trade is NOT in the measured recipe
    })


VARIANTS = {}


def variant(name):
    def deco(fn):
        VARIANTS[name] = fn
        return fn
    return deco


@variant("v5_01_iowa_sustain")
def a(p):
    """Grounded sustain only — isolate the measured values."""
    iowa_sustain(p)


@variant("v5_02_iowa_full")
def b(p):
    """v4_04 attack recipe + grounded sustain."""
    iowa_sustain(p)
    cluster_res(p, 0.15)
    burst_res(p, 0.05)


@variant("v5_03_iowa_shim60")
def c(p):
    """Full recipe but shimmer backed off to 0.6 in case the measured 0.9
    reads as pumping (measured fluctuation includes bow-stroke dynamics the
    walk shape may exaggerate)."""
    iowa_sustain(p, shimmer_depth=0.6)
    cluster_res(p, 0.15)
    burst_res(p, 0.05)


def main():
    for name, fn in sorted(VARIANTS.items()):
        p = load_base96()
        fn(p)
        with open(os.path.join(OUT, name + ".json"), "w") as fh:
            json.dump(p, fh, indent=2)
        print("wrote", name)


if __name__ == "__main__":
    main()
