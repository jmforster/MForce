"""Vowel-sequence set v2 (Matt 2026-07-31): the OO-EE et al sequences
rebuilt on the liar2 architecture that his ears validated — FormantSequence
crossfade, 5-formant spectra, literature widths x WIDTH_MULT (7), flat buzz
(160 partials, rolloff 0), formantWeight 1 / formantFloor 0,
instrument-style (playable + word scales with note length).

Sequences: OO-EE, O-OO-EE, AH-O-OO, EE-AH-OO, OO-AH-EE at f0 110 and 220.
"""
import json
import math
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "vowelseq2")
os.makedirs(OUT, exist_ok=True)

SECONDS = 3.0
WIDTH_MULT = 8.0  # Matt 2026-08-02: x8 "a little more clear than 7" — new default

# 5-formant (freq, gain, litWidth) male-voice tables, same family as liar2.
VOWELS = {
    "AH": [(650, 1.00, 80), (1080, 0.50, 90), (2650, 0.35, 120),
           (2900, 0.40, 130), (3250, 0.10, 140)],
    "EE": [(290, 1.00, 50), (1870, 0.18, 90), (2800, 0.12, 110),
           (3250, 0.10, 130), (3540, 0.03, 140)],
    "O":  [(570, 1.00, 80), (840, 0.60, 90), (2410, 0.25, 120),
           (2900, 0.12, 130), (3300, 0.05, 140)],
    "OO": [(300, 1.00, 55), (870, 0.50, 90), (2240, 0.20, 110),
           (2900, 0.10, 130), (3300, 0.04, 140)],
}
SEQS = {
    "oo_ee":    ["OO", "EE"],
    "o_oo_ee":  ["O", "OO", "EE"],
    "ah_o_oo":  ["AH", "O", "OO"],
    "ee_ah_oo": ["EE", "AH", "OO"],
    "oo_ah_ee": ["OO", "AH", "EE"],
}
F0S = [110, 220]


def blend_env(n):
    """Hold each vowel ~60% of its slot, glide ~40% between (liar2 pacing)."""
    stages = []
    slot = 1.0 / n
    for i in range(n):
        v0 = i / (n - 1) if n > 1 else 0.0
        stages.append({"startVal": v0, "endVal": v0, "type": "Linear",
                       "percent": slot * 0.6})
        if i < n - 1:
            v1 = (i + 1) / (n - 1)
            stages.append({"startVal": v0, "endVal": v1, "type": "Linear",
                           "percent": slot * 0.4})
    stages[-1]["percent"] = 0.0
    return {"id": "blendEnv", "type": "Envelope", "params": {"stages": stages}}


def make_patch(seq, f0):
    nodes = [blend_env(len(seq)),
             {"id": "ampEnv", "type": "Envelope",
              "params": {"preset": "adsr", "attack": 0.03, "decay": 0.05,
                         "sustainLevel": 0.9, "release": 0.0}}]
    spec_refs = []
    for vi, v in enumerate(seq):
        fids = []
        for i, (fr, g, w) in enumerate(VOWELS[v]):
            fid = f"s{vi}{v}_f{i}"
            nodes.append({"id": fid, "type": "Formant",
                          "params": {"frequency": fr, "gain": g,
                                     "width": w * WIDTH_MULT, "power": 2.0}})
            fids.append({"ref": fid})
        sid = f"spec{vi}"
        nodes.append({"id": sid, "type": "FormantSpectrum",
                      "params": {"formants": fids}})
        spec_refs.append({"ref": sid})

    nodes += [
        {"id": "fseq", "type": "FormantSequence",
         "params": {"spectra": spec_refs, "blend": {"ref": "blendEnv"}}},
        {"id": "fp", "type": "FullPartials",
         "params": {"maxPartials": 160, "minMult": 1,
                    "rolloff1": 0.0, "rolloff2": 0.0}},
        {"id": "src", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": float(f0),
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "fseq"},
                    "formantWeight": 1.0, "formantFloor": 0.0,
                    "partials": {"ref": "fp"}}},
    ]
    midi = int(round(69 + 12 * math.log2(f0 / 440.0)))
    return {"sampleRate": 48000,
            "graph": {"nodes": nodes, "output": "src"},
            "instrument": {"paramMap": {"frequency": "src.frequency"},
                           "polyphony": 1},
            "score": [{"note": midi, "velocity": 0.85, "time": 0.0,
                       "duration": SECONDS}]}


def main():
    for name, seq in SEQS.items():
        for f0 in F0S:
            path = os.path.join(OUT, f"{name}_{f0}.json")
            with open(path, "w") as f:
                json.dump(make_patch(seq, f0), f, indent=1)
            print("wrote", path)


if __name__ == "__main__":
    main()
