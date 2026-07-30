#!/usr/bin/env python3
"""Generate FM aliasing-test patches at several oversample factors.

Item 7 (G3) verification. One heavily-aliasing FM tone (high carrier, high
mod index, non-integer ratio so sidebands are inharmonic and folded aliases
land off the harmonic grid) rendered at oversample = 1,2,4,8,16. M=16 is the
clean ground-truth reference for measure.py.

Writes patches to patches/fm_oversample/. Seeds n/a (deterministic tone).
"""
import json, os

OUT = os.path.join(os.path.dirname(__file__), "..", "patches", "fm_oversample")
os.makedirs(OUT, exist_ok=True)

# Aliasing-heavy tone: modFreq = 3000*2.7 = 8100 Hz, index 12 -> sidebands
# 3000 +/- k*8100 for k up to ~13; most fold at 48 kHz. Non-integer ratio
# keeps the true spectrum inharmonic so folded aliases are distinguishable.
BASE = dict(frequency=3000.0, carrierRatio=1.0, modRatio=2.7, depth=12.0)
FACTORS = [1, 2, 4, 8, 16]

def patch(m):
    return {
        "sampleRate": 48000,
        "seconds": 2,
        "graph": {
            "nodes": [
                {"id": "ampEnv", "type": "Envelope",
                 "params": {"preset": "ar", "attack": 0.01, "attackMin": 0.0,
                            "attackMax": 1.0}},
                {"id": "fm", "type": "FMSource",
                 "params": {"frequency": BASE["frequency"],
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

for m in FACTORS:
    p = os.path.join(OUT, f"fm_alias_os{m:02d}.json")
    with open(p, "w") as f:
        json.dump(patch(m), f, indent=2)
    print("wrote", p)
