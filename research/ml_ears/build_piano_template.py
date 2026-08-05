"""Build patches/piano1/template.json — the piano CMA-ES encoder template.

Structure (see out/piano_analysis_report.md, feature mapping):
  ampEnv     Envelope        near-instant attack, sustain 1 (all tonal decay
                             comes from the Partials decayRate/decayExp law)
  pp         ExplicitPartials 120 integer partials, 1/n baseline amps,
                             decayExp locked 0.6 (report within-note law)
  pno        AdditiveSource  seed 7
  knockEnv   Envelope        one-shot AR (sustain 0) for the hammer knock
  knockAmp   CombinedSource  multiply: knockEnv x level constant
  knockNoise WhiteNoiseSource amplitude = knockAmp
  knockBP    BWBandpassFilter knock band (encoder sets low/high cutoffs)
  mix        MultiSource     raw sum: pno + knockBP
paramMap (encoder rewrites the curves each eval):
  pp.inharmonicity  LOCKED measured B(f0) table
  pp.decayRate      measured h1-rate register curve x searched scale
  pp.rolloff1       searched per-note spectral tilt (lo/hi breakpoints)
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "patches", "piano1", "template.json")

N = 120
mult = [float(i) for i in range(1, N + 1)]
amp = [1.0 / i for i in range(1, N + 1)]

# Measured B(f0), piano_analysis_report.md table 1 (track fits + C7/C8 pair).
B_CURVE = [[30.76, 2.41e-4], [43.52, 1.11e-4], [65.42, 1.23e-4],
           [130.98, 1.14e-4], [262.57, 2.89e-4], [523.38, 9.59e-4],
           [1040.96, 4.37e-3], [2100.79, 7.33e-3], [3988.69, 2.48e-2]]

# h1 decay rate (dB/s) register anchors: C1(n2) 1.4, C3 14.2, C7 19.1, C8 38.3.
DECAY_CURVE = [[32.7, 1.4], [130.8, 14.0], [2093.0, 19.0], [4186.0, 38.3]]

patch = {
    "sampleRate": 48000,
    "graph": {
        "nodes": [
            {"id": "ampEnv", "type": "Envelope",
             "params": {"preset": "adsr", "attack": 0.008, "decay": 0.0,
                        "sustainLevel": 1.0, "release": 0.1}},
            {"id": "pp", "type": "ExplicitPartials",
             "params": {"mult1": mult, "mult2": mult,
                        "ampl1": amp, "ampl2": amp,
                        "rolloff1": 0.0, "rolloff2": 0.0,
                        "detune1": 0.0, "detune2": 0.0,
                        "decayRate": 8.0, "decayExp": 0.6,
                        "shimmerDepth1": 0.15, "shimmerDepth2": 0.15,
                        "shimmerHz": 2.0, "shimmerCoherence": 0.5,
                        "shimmerEvolve": 0.2}},
            {"id": "pno", "type": "AdditiveSource",
             "params": {"seed": 7,
                        "frequency": 261.63,
                        "amplitude": {"ref": "ampEnv"},
                        "partials": {"ref": "pp"}}},
            {"id": "knockEnv", "type": "Envelope",
             "params": {"preset": "adsr", "attack": 0.002, "decay": 0.08,
                        "sustainLevel": 0.0, "release": 0.02}},
            {"id": "knockAmp", "type": "CombinedSource",
             "params": {"operation": "multiply",
                        "source1": {"ref": "knockEnv"},
                        "source2": 0.05}},
            {"id": "knockNoise", "type": "WhiteNoiseSource",
             "params": {"seed": 11, "amplitude": {"ref": "knockAmp"}}},
            {"id": "knockBP", "type": "BWBandpassFilter",
             "params": {"source": {"ref": "knockNoise"},
                        "lowCutoff": 2500.0, "highCutoff": 8000.0}},
            {"id": "mix", "type": "MultiSource",
             "params": {"source": [{"ref": "pno"}, {"ref": "knockBP"}]}},
        ],
        "output": "mix",
    },
    "instrument": {
        "polyphony": 1,
        "paramMap": {
            "frequency": [
                "pno.frequency",
                {"target": "pp.inharmonicity", "curve": B_CURVE},
                {"target": "pp.decayRate", "curve": DECAY_CURVE},
                {"target": "pp.rolloff1", "curve": [[65.4, 0.2], [1046.5, 0.2]]},
            ]
        },
    },
    "score": [
        {"note": 36, "velocity": 0.85, "time": 0.0, "duration": 2.2},
        {"note": 60, "velocity": 0.85, "time": 2.5, "duration": 2.2},
        {"note": 72, "velocity": 0.85, "time": 5.0, "duration": 2.2},
    ],
}

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(patch, open(OUT, "w"), indent=2)
print("wrote", OUT)
