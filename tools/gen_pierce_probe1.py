"""1D Pierce probe (REVIEW 61, Matt: "Sure") — sign-dependent termination
stiffness on a plucked delay loop, zero engine code.

Pierce/Van Duyne 1997 (via Chafe 2019): a bridge termination with
differential stiffness by displacement sign makes a plucked tone GAIN
frequency components while decaying (gong-like modal upwelling). Chafe's
2D-mesh edge rule: r(n) = 0.75 + s*x, s = -0.5 for x<=0, +0.003 for
x>0. Here the mechanism goes 1D on a KS-style pluck: WhiteNoise burst →
DelayLine → SVF damp → Biquad (resonance mode) termination whose RADIUS
pin follows the signal through a sign-asymmetric CurveNode. Form
differs from Chafe (resonant 2-pole, not allpass — radius is our only
dynamic filter pin); mechanism (sign-dependent stiffness in the loop
termination) is the thing under test.

Form note: Chafe's edge filter is an ALLPASS (unity magnitude, phase
only) — a magnitude filter with dynamic radius either kills the string
(bandpass) or runs away (unnormalized poles; both observed in probe
v1). The mechanism-faithful form in existing pins: the loop's damp SVF
CUTOFF follows the signal sign — stiffer (brighter reflection) on the
negative half-cycle. Cells: cutoff swing 0 (control) / 1k / 3k / 5k Hz
above the 5 kHz base for x < 0; +30 Hz/unit for x > 0 (Chafe's tiny
positive-side term).

Metric: modal upwelling = high-band (1.5-6 kHz) to low-band (<800 Hz)
energy ratio, early window (0.1-0.6 s) vs late (2.0-3.0 s) per note.
Upwelling = late ratio rises vs early; control should fall.

Outputs: patches/audition/pierce1d_1/ + renders/dsp/audition/pierce1d_1/.
Usage: python tools/gen_pierce_probe1.py
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bandedwg as g

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "audition", "pierce1d_1")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "pierce1d_1")

SR = 48000
DAMP_FC = 5000.0
NOTES = [48, 60]          # C3, C4
SLOT = 5.0

# cell -> cutoff swing (Hz) added on the negative half-cycle
CELLS = {"ctl_s00": 0.0, "s1k": 1000.0, "s3k": 3000.0, "s5k": 5000.0,
         "s8k": 8000.0, "s12k": 12000.0}


def cutoff_knots(swing):
    """fc(x): base + swing*|x| for x<0; + 30 Hz/unit for x>0."""
    return [[-1.0, round(DAMP_FC + swing, 1)],
            [0.0, DAMP_FC],
            [1.0, DAMP_FC + 30.0]]


def make_patch(swing):
    R = lambda i: {"ref": i}
    T = lambda i: {"tap": i}
    nodes = [
        {"id": "__perf_freq", "type": "PerformNode",
         "params": {"field": "frequency"}},
        # 3 ms noise burst = the pluck
        {"id": "Burst_env", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": 0.9,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": [
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": 0.003, "power": 0.0, "minSec": 0.0,
                 "maxSec": 0.0},
                {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
                 "percent": 1.0, "power": 0.0, "minSec": 0.0,
                 "maxSec": 0.0}]}},
        {"id": "Noise", "type": "WhiteNoiseSource", "params": {
            "amplitude": R("Burst_env"), "density": 1.0, "boost": 0.0,
            "continuity": 0.0, "zeroCrossTendency": 0.0}},
        # loop: DIn -> Delay -> Damp -(tap)-> DIn; the damp cutoff
        # follows the signal sign = differential stiffness (Pierce)
        g.add("DIn", R("Noise"), T("Damp_lpf")),
        {"id": "Delay_line", "type": "DelayLine", "params": {
            "source": R("DIn"), "frequency": R("__perf_freq"),
            "ratio": 1.0, "amplitude": 0.998, "compensate": True}},
        g.curve("SignCurve", cutoff_knots(swing), T("Delay_line")),
        {"id": "Damp_lpf", "type": "SVFSource", "params": {
            "source": R("Delay_line"), "cutoffFreq": R("SignCurve"),
            "resonance": 0.7, "mode": "lowpass"}},
        # listen at the delay output
        g.mul("Out", T("Delay_line"), 0.9),
    ]
    return {
        "sampleRate": SR, "seconds": SLOT * len(NOTES) + 2.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": n, "time": SLOT * k, "duration": 4.0,
                   "velocity": 0.8} for k, n in enumerate(NOTES)],
        "graph": {"output": "Out", "nodes": nodes},
        "ui": {"noteFaces": [{"fields": {"frequency": "__perf_freq"},
                              "label": "Note1"}]},
    }


def band_ratio(x, sr, a_s, b_s):
    seg = x[int(a_s * sr):int(b_s * sr)]
    seg = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    hi = sp[(fr > 1500) & (fr < 6000)].sum()
    lo = sp[(fr > 40) & (fr < 800)].sum()
    return hi / lo if lo > 0 else float("nan")


def main():
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    # The output dirs belong to THIS cell set: purge anything else.
    # (2026-09-16 incident: a rename between probe versions left v1
    # runaway WAVs — sustained rms 0.97 — sitting in the audition queue
    # next to quiet v2 plucks. Matt's speakers noticed.)
    keep = {f"pierce_{n}.json" for n in CELLS} | \
           {f"pierce_{n}.wav" for n in CELLS} | {"README.md"}
    for d in (PATCH_OUT, REND_OUT):
        for fn in os.listdir(d):
            if fn not in keep:
                os.remove(os.path.join(d, fn))
                print(f"purged stale: {fn}")
    print("%-8s %10s %10s %8s   %s" % ("cell", "early", "late",
                                       "late/early", "(C4 note)"))
    for name, swing in CELLS.items():
        patch = make_patch(swing)
        ppath = os.path.join(PATCH_OUT, f"pierce_{name}.json")
        wpath = os.path.join(REND_OUT, f"pierce_{name}.wav")
        with open(ppath, "w") as f:
            json.dump(patch, f, indent=1)
        r = subprocess.run([CLI, ppath, wpath], capture_output=True,
                           text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            print(f"{name:8s} RENDER_FAIL {(r.stderr or '')[-100:]}")
            continue
        x, sr = g.read_mono(wpath)
        # audibility floor (the REVIEW-59 lesson)
        note = x[int(SLOT * sr):int((SLOT + 4.0) * sr)]   # C4 slot
        rms = float(np.sqrt((note ** 2).mean()))
        if rms < 1e-4:
            print(f"{name:8s} SILENT (rms {rms:.2e})")
            continue
        peak = float(np.abs(x).max())
        # Level-safety gate: sustained loudness is the speaker hazard,
        # not momentary peak — no 0.5 s window may exceed rms 0.5.
        win = int(0.5 * sr)
        nw = len(x) // win
        wrms = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
        if wrms.max() > 0.5:
            os.remove(ppath)
            os.remove(wpath)
            print(f"{name:8s} REJECT sustained rms {wrms.max():.2f} — culled")
            continue
        early = band_ratio(x, sr, SLOT + 0.1, SLOT + 0.6)
        late = band_ratio(x, sr, SLOT + 2.0, SLOT + 3.0)
        print("%-8s %10.4f %10.4f %8.2f   rms %.4f peak %.3f"
              % (name, early, late, late / early, rms, peak))


if __name__ == "__main__":
    main()
