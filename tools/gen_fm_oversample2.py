#!/usr/bin/env python3
"""FM alias A/B v2 — Matt on v1: "Cannot hear difference. Modulating
frequency makes the aliasing obvious, and aliasing gets worse at high
frequencies: regen with a 5x/second sine LFO on frequency, and play
C4/5/6/7/8 in each case."

Same aliasing-heavy recipe as run 8 (gen_fm_oversample_test.py: high mod
index 12, non-integer ratio 2.7 so folded aliases land off the harmonic
grid), but the FMSource frequency is a 5 Hz sine vibrato at +-3% depth
(SineSource -> RangeSource with min/max at the absolute Hz values around
each note), rendered at note frequencies C4..C8 x oversample {1, 8}.

Writes 10 patches to patches/fm_oversample2/. Render via
_run_fm_oversample2.py -> renders/fm_oversample2/fm_alias_C4_os1.wav etc.
"""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "patches", "fm_oversample2")
os.makedirs(OUT, exist_ok=True)

BASE = dict(carrierRatio=1.0, modRatio=2.7, depth=12.0)
NOTES = [("C4", 261.63), ("C5", 523.25), ("C6", 1046.5),
         ("C7", 2093.0), ("C8", 4186.0)]
FACTORS = [1, 8]
VIB_HZ, VIB_DEPTH = 5.0, 0.03


def patch(freq, m):
    return {
        "sampleRate": 48000,
        "seconds": 2,
        "graph": {
            "nodes": [
                {"id": "vibOsc", "type": "SineSource",
                 "params": {"frequency": VIB_HZ, "amplitude": 1.0}},
                {"id": "vibFreq", "type": "RangeSource",
                 "params": {"min": freq * (1.0 - VIB_DEPTH),
                            "max": freq * (1.0 + VIB_DEPTH),
                            "normalized": False,
                            "var": {"ref": "vibOsc"}}},
                {"id": "ampEnv", "type": "Envelope",
                 "params": {"preset": "ar", "attack": 0.01, "attackMin": 0.0,
                            "attackMax": 1.0}},
                {"id": "fm", "type": "FMSource",
                 "params": {"frequency": {"ref": "vibFreq"},
                            "amplitude": {"ref": "ampEnv"},
                            "carrierRatio": BASE["carrierRatio"],
                            "modRatio": BASE["modRatio"],
                            "depth": BASE["depth"],
                            "oversample": m}},
                {"id": "ch1", "type": "SoundChannel",
                 "inputs": {"source": "fm"},
                 "params": {"volume": 0.8, "pan": 0.0}},
                {"id": "mix", "type": "StereoMixer",
                 "inputs": {"channels": ["ch1"]},
                 "params": {"gainL": 1.0, "gainR": 1.0}},
            ],
            "output": "mix",
        },
    }


def main():
    n = 0
    for note, freq in NOTES:
        for m in FACTORS:
            p = os.path.join(OUT, f"fm_alias_{note}_os{m}.json")
            with open(p, "w") as f:
                json.dump(patch(freq, m), f, indent=2)
            n += 1
    print(f"wrote {n} fm-oversample-2 patches -> {OUT}")


if __name__ == "__main__":
    main()
