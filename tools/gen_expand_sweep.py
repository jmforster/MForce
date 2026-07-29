"""Recursive partial-expansion regime sweep (dsp BACKLOG item 5, G2).

Matt's hint: recursive partial expansion is under-explored. This generates a
curated batch of ExpandRule regimes on a common FullPartials base, spanning
micro-clusters to wide non-integer spreads, plus deliberate rule-breakers
(non-integer semitone spacing, extreme detune, inverted amplitude taper). Each
renders standalone via mforce_cli; the batch is then ranked by the novelty
metric (research/novelty) so the timbrally-distinct survivors surface for a
listen.

Scope note: recurse is capped at 1 here (partials = base*(count*2+1)^(recurse+1)
= up to 8*49=392). recurse 2-4 explodes into the thousands and is render-bound
until the additive performance work (item 8) lands.

Writes patches to patches/expand_sweep/. Render + rank via _run_expand_sweep.py.
"""
import copy
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "expand_sweep")
os.makedirs(OUT, exist_ok=True)

BASE_MAX_PARTIALS = 8
FREQ = 220.0
SECONDS = 4

# Default ExpandRule (the fadd_expand_test starting point).
DEF = dict(count=3, recurse=0, spacing1=0.3, spacing2=0.3, dt1=0.02, dt2=0.02,
           loPct1=0.15, loPct2=0.15, power1=1.5, power2=1.5, po1=0.0, po2=0.0)

# Curated regimes: (name, overrides). "*" prefix marks a rule-breaking outlier.
REGIMES = [
    ("control_noexpand",      dict(count=0, recurse=0)),
    ("micro_cluster_r0",      dict(count=3, recurse=0, spacing1=0.2, spacing2=0.2)),
    ("micro_cluster_r1",      dict(count=2, recurse=1, spacing1=0.2, spacing2=0.2)),
    ("semitone_r0",           dict(count=3, recurse=0, spacing1=1.0, spacing2=1.0)),
    ("semitone_r1",           dict(count=2, recurse=1, spacing1=1.0, spacing2=1.0)),
    ("fifth_spread_r0",       dict(count=2, recurse=0, spacing1=7.0, spacing2=7.0)),
    ("fifth_spread_r1",       dict(count=2, recurse=1, spacing1=7.0, spacing2=7.0)),
    ("converge_spacing",      dict(count=3, recurse=1, spacing1=2.0, spacing2=0.3)),
    ("diverge_spacing",       dict(count=3, recurse=1, spacing1=0.3, spacing2=2.0)),
    ("phase_swirl_r1",        dict(count=2, recurse=1, spacing1=0.5, po1=0.5, po2=0.5)),
    ("flat_taper",            dict(count=3, recurse=1, spacing1=0.5, loPct1=0.9, loPct2=0.9)),
    ("steep_taper",           dict(count=3, recurse=0, spacing1=0.5, power1=4.0, power2=4.0)),
    # --- rule-breakers (WORKFLOW: a few crazy ones per batch) ---
    ("*noninteger_pi",        dict(count=3, recurse=1, spacing1=3.1416, spacing2=3.1416)),
    ("*golden_spacing",       dict(count=3, recurse=1, spacing1=1.618, spacing2=2.618)),
    ("*extreme_detune",       dict(count=3, recurse=1, spacing1=0.5, dt1=0.35, dt2=0.35)),
    ("*inverted_taper",       dict(count=3, recurse=1, spacing1=0.5, power1=0.3, power2=0.3)),
    ("*wide_microcount",      dict(count=6, recurse=0, spacing1=0.15, spacing2=0.15)),
    ("*asym_phase",           dict(count=3, recurse=1, spacing1=0.7, po1=0.9, po2=0.1)),
]


def make_patch(name, er_over):
    er = copy.deepcopy(DEF)
    er.update(er_over)
    return {
        "sampleRate": 48000,
        "seconds": SECONDS,
        "graph": {
            "nodes": [
                {"id": "ampEnv", "type": "Envelope",
                 "params": {"preset": "adsr", "attack": 0.1, "decay": 0.1,
                            "sustainLevel": 0.7, "release": 0.0}},
                {"id": "roEnv", "type": "Envelope",
                 "params": {"preset": "ar", "attack": 0.0, "attackMax": 0.0}},
                {"id": "fas_partials", "type": "FullPartials",
                 "params": {"maxPartials": BASE_MAX_PARTIALS, "minMult": 1,
                            "evenWeight1": 1.0, "evenWeight2": 1.0,
                            "oddWeight1": 1.0, "oddWeight2": 1.0,
                            "rolloff1": 0.3, "rolloff2": 1.5,
                            "roEnv": {"ref": "roEnv"},
                            "expandRule": er}},
                {"id": "fas", "type": "AdditiveSource",
                 "params": {"seed": 42, "frequency": FREQ,
                            "amplitude": {"ref": "ampEnv"},
                            "partials": {"ref": "fas_partials"}}},
                {"id": "ch1", "type": "SoundChannel",
                 "inputs": {"source": "fas"},
                 "params": {"volume": 0.7, "pan": 0.0}},
                {"id": "mix", "type": "StereoMixer",
                 "inputs": {"channels": ["ch1"]},
                 "params": {"gainL": 1.0, "gainR": 1.0}},
            ],
            "output": "mix",
        },
    }


def main():
    for name, over in REGIMES:
        fname = name.lstrip("*") + ".json"
        with open(os.path.join(OUT, fname), "w") as f:
            json.dump(make_patch(name, over), f, indent=1)
    print(f"wrote {len(REGIMES)} expand-sweep patches -> {OUT}")


if __name__ == "__main__":
    main()
