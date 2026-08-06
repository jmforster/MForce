"""Build patches/piano1/template.json — the piano CMA-ES encoder template.

Structure (see out/piano_analysis_report.md, feature mapping):
  ampEnv     Envelope        adsr timeMode=seconds: literal 8 ms attack
                             (run-22 diagnosis: the fractional preset's
                             0.05 s stage floor smeared this into the
                             "chuff"), sustain 1 (all tonal decay comes
                             from the Partials decayRate/decayExp law)
  pp         ExplicitPartials 120 integer partials, 1/n baseline amps,
                             decayExp locked 0.6 (report within-note law)
  pno        AdditiveSource  seed 7
  knockEnv   Envelope        adsr timeMode=seconds, one-shot (sustain 0):
                             5 ms rise, 86 ms linear decay = measured
                             decay-to-10% of 69 ms (run-20 C2 measurement;
                             duration-invariant in seconds mode)
  knockAmp   CombinedSource  multiply: knockEnv x level constant —
                             KNOCK_LEVEL calibrated so the 2.5-8 kHz share
                             of first-50 ms energy at C2 matches the real
                             sample (run 23; was ~150x overweight)
  knockNoise WhiteNoiseSource amplitude = knockAmp
  knockBP    BWBandpassFilter 2500-8000 Hz, LOCKED at the measured band
                             (run-20 smoke converged there unprompted)
  mix        MultiSource     raw sum: pno + knockBP
paramMap (encoder rewrites the curves each eval):
  pp.inharmonicity  LOCKED measured B(f0) table
  pp.decayRate      measured h1-equivalent register curve x searched scale
                    (curve re-fit run 23 by refit_decay_curve.py:
                    energy-weighted 1 s-drop anchors at 10 notes; the old
                    4-anchor curve was C2 3.4x fast / C4-C5 3x slow)
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

# h1-equivalent decay anchors (dB/s), refit_decay_curve.py run 23:
# energy-weighted median of per-partial 1 s-drop rates / mult^0.6.
DECAY_CURVE = [[32.5, 1.18], [65.4, 2.58], [97.9, 7.7], [131.0, 5.82],
               [196.7, 22.75], [262.6, 34.44], [393.8, 32.22],
               [523.4, 15.09], [1041.0, 17.8], [2100.8, 17.44]]

# Hammer-knock level (CombinedSource const): calibrated by
# calibrate_knock_level.py against the real Iowa C2 first-50 ms 2.5-8 kHz
# energy share (2.22e-4; run-22 candidate sat at 0.031 = ~150x overweight).
# Knock-only solve: the template's raw 1/n tonal treble currently exceeds
# the share on its own (that excess belongs to the searched rolloff/env
# dims), so the knock is anchored to contribute exactly the real share by
# itself. Rerun the script if the template's tonal side changes materially.
KNOCK_LEVEL = 0.01335

patch = {
    "sampleRate": 48000,
    "graph": {
        "nodes": [
            {"id": "ampEnv", "type": "Envelope",
             "params": {"preset": "adsr", "timeMode": "seconds",
                        "attack": 0.008, "decay": 0.0,
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
             "params": {"preset": "adsr", "timeMode": "seconds",
                        "attack": 0.005, "decay": 0.086,
                        "sustainLevel": 0.0, "release": 0.02}},
            {"id": "knockAmp", "type": "CombinedSource",
             "params": {"operation": "multiply",
                        "source1": {"ref": "knockEnv"},
                        "source2": KNOCK_LEVEL}},
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
