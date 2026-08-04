#!/usr/bin/env python3
"""Gate for the noise-family `amplitude` param (dsp run 19).

White/Pink/Blue/VioletNoiseSource had no `amplitude` param at all, so patches
setting it were silently ignored by the loader. This asserts the param is now
real and LINEAR: rendering the same source at amplitude a should scale RMS by a.

Generates its own patches under renders/noise_amp/ rather than adding fixtures
to patches/ (which the linter and the sweep tools walk).

Usage: python tools/test_noise_amplitude.py
"""
import json
import os
import subprocess
import sys
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "noise_amp")

TYPES = ["WhiteNoiseSource", "PinkNoiseSource", "BlueNoiseSource", "VioletNoiseSource"]
AMPS = [1.0, 0.5, 0.25]
TOL = 0.02  # these are seeded generators, so the ratio should be near-exact


def patch(type_name, amp):
    """Bare generator -> SoundChannel -> StereoMixer. `amp` omitted when None."""
    params = {} if amp is None else {"amplitude": amp}
    return {
        "sampleRate": 48000,
        "seconds": 2,
        "graph": {
            "nodes": [
                {"id": "src", "type": type_name, "params": params},
                {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "src"},
                 "params": {"volume": 1.0, "pan": 0.0}},
                {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
                 "params": {"gainL": 1.0, "gainR": 1.0}},
            ],
            "output": "mix",
        },
    }


def rms(path):
    with wave.open(path, "rb") as w:
        raw = w.readframes(w.getnframes())
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return float(np.sqrt(np.mean(x * x)))


def render(doc, name):
    os.makedirs(OUT, exist_ok=True)
    pj = os.path.join(OUT, name + ".json")
    wv = os.path.join(OUT, name + ".wav")
    with open(pj, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
    r = subprocess.run([CLI, pj, wv], capture_output=True, text=True)
    if r.returncode != 0:
        print("    render FAILED %s: %s" % (name, (r.stdout + r.stderr).strip()[:120]))
        return None
    return rms(wv)


def main():
    print("%-20s %10s %10s %10s %8s" % ("type", "amp", "rms", "ratio", "verdict"))
    failures = 0
    for t in TYPES:
        base = render(patch(t, None), "%s_default" % t)
        if base is None or base <= 0:
            print("%-20s  no signal at default amplitude -- FAIL" % t)
            failures += 1
            continue
        for a in AMPS:
            got = render(patch(t, a), "%s_a%s" % (t, str(a).replace(".", "")))
            if got is None:
                failures += 1
                continue
            ratio = got / base
            ok = abs(ratio - a) <= TOL
            failures += 0 if ok else 1
            print("%-20s %10.2f %10.6f %10.3f %8s"
                  % (t, a, got, ratio, "ok" if ok else "FAIL"))
    print()
    if failures:
        print("FAIL: %d case(s) -- amplitude is not applied linearly" % failures)
        return 1
    print("PASS: amplitude scales RMS linearly on all %d noise types" % len(TYPES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
