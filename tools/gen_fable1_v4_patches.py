"""Fable1 v4 batch — attack residue: the winning attack effects no longer
decay to zero but settle at a low floor, blurring the attack/sustain
transition (Matt's v3 feedback). Base = 96-partial viola, C2..E6 ladder.

Residue knobs:
- cluster: fade envelope sustainLevel > 0 keeps a fraction of the sub-partial
  cluster alive through the note.
- bandwidth: bandwidth1 > 0 leaves permanent slight per-partial noisiness
  after the burst decays (matches the ml-ears finding that real viola sustain
  carries inter-harmonic broadband energy).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_fable1_patches import node, add_env            # noqa: E402
from gen_fable1_v3_patches import load_base96, sustain_stack  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(REPO, "patches", "fable1_v4")
os.makedirs(OUT, exist_ok=True)


def cluster_res(p, sustain, spacing=0.3, lo_pct=0.25, decay=0.15):
    """v3_04 winner (tight/low/fast) with a residual floor: the fade env
    settles at `sustain` instead of 0, so that fraction of the cluster
    persists through the note."""
    add_env(p, "fadeEnv", 0.005, decay, sustain=sustain)
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


def burst_res(p, floor, spread=0.15):
    """50 ms bandwidth burst decaying to a permanent noisiness floor."""
    add_env(p, "burstEnv", 0.003, 0.05)
    node(p, "vla_partials")["params"].update({
        "bandwidth1": floor, "bandwidth2": 0.85, "bandwidthHz": 120.0,
        "bwEnv": {"ref": "burstEnv"},
        "onsetSpread": spread, "onsetTilt": 1.0, "onsetFade": 0.05,
    })


VARIANTS = {}


def variant(name):
    def deco(fn):
        VARIANTS[name] = fn
        return fn
    return deco


@variant("v4_01_cluster_res10")
def a(p):
    """cluster_tight_low + 10% residual cluster."""
    cluster_res(p, 0.10)


@variant("v4_02_cluster_res25")
def b(p):
    """cluster_tight_low + 25% residual cluster."""
    cluster_res(p, 0.25)


@variant("v4_03_bw_floor")
def c(p):
    """Bandwidth burst settling at 0.06 permanent noisiness (no cluster),
    isolated so the floor's contribution is auditable on its own."""
    burst_res(p, 0.06)


@variant("v4_04_full_res")
def d(p):
    """Full recipe with residue: sustain stack + cluster residue 15% +
    bw floor 0.05 + 150 ms bloom."""
    sustain_stack(p)
    cluster_res(p, 0.15)
    burst_res(p, 0.05)


@variant("v4_05_full_res_hi")
def e(p):
    """Same recipe, residue turned up: cluster 30%, bw floor 0.10."""
    sustain_stack(p)
    cluster_res(p, 0.30)
    burst_res(p, 0.10)


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
