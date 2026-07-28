"""Generate the 10 Fable1 partial-motion exploration patches.

Base = research/ml_ears/patches/viola_instrument.json (48 explicit viola
harmonics + BandSpectrum body + Vibrato). Each variant layers motion/shimmer/
onset/trade/expand/bandwidth techniques per docs/superpowers/specs/
2026-07-03-partial-motion-design.md. Score = C major scale C4..C5.
"""
import copy
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
BASE = os.path.join(REPO, "research", "ml_ears", "patches", "viola_instrument.json")
OUT = os.path.join(REPO, "patches", "fable1")
os.makedirs(OUT, exist_ok=True)

SCALE = [60, 62, 64, 65, 67, 69, 71, 72]  # C major, octave 4


def load_base():
    with open(BASE) as f:
        p = json.load(f)
    p["score"] = [
        {"note": n, "velocity": 0.85, "time": 2.0 * i, "duration": 1.6}
        for i, n in enumerate(SCALE)
    ]
    return p


def node(p, node_id):
    for n in p["graph"]["nodes"]:
        if n["id"] == node_id:
            return n
    raise KeyError(node_id)


def add_env(p, env_id, attack, decay, sustain=0.0, release=0.1):
    """Fast-decay per-note envelope: 0 -> 1 in `attack`, back to `sustain`
    over `decay`. Used to gate chaos onto the attack (env=1 early -> the _2
    endpoint of a blended config pair)."""
    p["graph"]["nodes"].insert(0, {
        "id": env_id,
        "type": "Envelope",
        "params": {"preset": "adsr", "attack": attack, "decay": decay,
                   "sustainLevel": sustain, "release": release},
    })


VARIANTS = {}


def variant(name):
    def deco(fn):
        VARIANTS[name] = fn
        return fn
    return deco


@variant("01_mot_settle")
def v01(p):
    """Frequency-motion attack: 90 cents of semi-coherent scatter collapsing
    to 4 cents over ~0.35 s."""
    add_env(p, "motEnv", 0.005, 0.35)
    node(p, "vla_partials")["params"].update({
        "motionDepth1": 4.0, "motionDepth2": 90.0, "motionHz": 8.0,
        "motionCoherence": 0.6, "motionEvolve": 0.3,
        "motionEnv": {"ref": "motEnv"},
    })


@variant("02_mot_coherent")
def v02(p):
    """Sustained coherent shimmer: constant 6-cent motion, coherence 0.9,
    strongly evolving segment character."""
    node(p, "vla_partials")["params"].update({
        "motionDepth1": 6.0, "motionDepth2": 6.0, "motionHz": 3.0,
        "motionCoherence": 0.9, "motionEvolve": 0.6,
    })


@variant("03_mot_incoherent")
def v03(p):
    """A/B control for 02: identical depth/rate but coherence 0 — the
    known segregation failure mode, kept for direct comparison."""
    node(p, "vla_partials")["params"].update({
        "motionDepth1": 6.0, "motionDepth2": 6.0, "motionHz": 3.0,
        "motionCoherence": 0.0, "motionEvolve": 0.0,
    })


@variant("04_onset_bloom")
def v04(p):
    """Pure onset dispersion: harmonics enter low-to-high across 150 ms."""
    node(p, "vla_partials")["params"].update({
        "onsetSpread": 0.15, "onsetTilt": 1.0, "onsetFade": 0.06,
    })


@variant("05_onset_scatter")
def v05(p):
    """Random onset order across 250 ms + motion settle 60 -> 5 cents."""
    add_env(p, "motEnv", 0.005, 0.3)
    node(p, "vla_partials")["params"].update({
        "onsetSpread": 0.25, "onsetTilt": 0.0, "onsetFade": 0.04,
        "motionDepth1": 5.0, "motionDepth2": 60.0, "motionHz": 7.0,
        "motionCoherence": 0.5, "motionEvolve": 0.3,
        "motionEnv": {"ref": "motEnv"},
    })


@variant("06_trade_slosh")
def v06(p):
    """Energy trading between neighbor partials + mild coherent motion."""
    node(p, "vla_partials")["params"].update({
        "tradeDepth": 0.5, "tradeHz": 1.2,
        "motionDepth1": 4.0, "motionDepth2": 4.0, "motionHz": 2.5,
        "motionCoherence": 0.9, "motionEvolve": 0.4,
    })


@variant("07_shimmer_amp")
def v07(p):
    """Amplitude shimmer only: per-partial gain wander, low coherence,
    evolving."""
    node(p, "vla_partials")["params"].update({
        "shimmerDepth1": 0.45, "shimmerDepth2": 0.45, "shimmerHz": 2.5,
        "shimmerCoherence": 0.3, "shimmerEvolve": 0.5,
    })


@variant("08_expand_converge")
def v08(p):
    """Matt's order-out-of-chaos idea with existing features: each harmonic
    carries a wide detuned sub-partial cluster (spacing2/dt2) that collapses
    onto the clean harmonic (spacing1=0) as the shared envelope decays.
    multEnv moves the cluster positions, amplEnv fades the sub-partials."""
    add_env(p, "chaosEnv", 0.005, 0.4)
    node(p, "vla_partials")["params"].update({
        "expandRule": {
            "count": 2, "recurse": 0,
            "spacing1": 0.0, "spacing2": 1.2,
            "dt1": 0.0, "dt2": 0.06,
            "loPct1": 0.0, "loPct2": 0.5,
            "power1": 4.0, "power2": 1.0,
        },
        "multEnv": {"ref": "chaosEnv"},
        "amplEnv": {"ref": "chaosEnv"},
    })


@variant("09_bw_coupled_attack")
def v09(p):
    """Noise burst coupled through the partials themselves: bandwidth env
    0.85 -> 0 in ~120 ms + upward onset bloom + motion settle. No parallel
    noise source anywhere."""
    add_env(p, "burstEnv", 0.003, 0.12)
    node(p, "vla_partials")["params"].update({
        "bandwidth1": 0.0, "bandwidth2": 0.85, "bandwidthHz": 120.0,
        "bwEnv": {"ref": "burstEnv"},
        "onsetSpread": 0.12, "onsetTilt": 1.0, "onsetFade": 0.05,
        "motionDepth1": 4.0, "motionDepth2": 45.0, "motionHz": 6.0,
        "motionCoherence": 0.5, "motionEvolve": 0.3,
        "motionEnv": {"ref": "burstEnv"},
    })


@variant("10_kitchen_sink")
def v10(p):
    """Best-guess combination: onset bloom + motion settle + evolving
    sustain motion + energy trade + light shimmer."""
    add_env(p, "motEnv", 0.005, 0.3)
    node(p, "vla_partials")["params"].update({
        "onsetSpread": 0.12, "onsetTilt": 0.7, "onsetFade": 0.05,
        "motionDepth1": 5.0, "motionDepth2": 70.0, "motionHz": 5.0,
        "motionCoherence": 0.65, "motionEvolve": 0.5,
        "motionEnv": {"ref": "motEnv"},
        "tradeDepth": 0.35, "tradeHz": 1.5,
        "shimmerDepth1": 0.25, "shimmerDepth2": 0.25, "shimmerHz": 2.0,
        "shimmerCoherence": 0.3, "shimmerEvolve": 0.5,
    })


def main():
    for name, fn in sorted(VARIANTS.items()):
        p = load_base()
        fn(p)
        path = os.path.join(OUT, name + ".json")
        with open(path, "w") as f:
            json.dump(p, f, indent=2)
        print("wrote", path)


if __name__ == "__main__":
    main()
