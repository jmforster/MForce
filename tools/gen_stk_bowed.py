"""STK Bowed port - patch generator + reference comparison.

Builds the MForce port of STK's Bowed (spec
docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md), renders the
same 10-variant x 5-note matrix as tools/stk_ref/bowed_ref.cpp, and
scores the port against the STK ground truth: f0 (cents vs the
reference's own measured f0), RMS envelope correlation + gain fit, and
harmonic profile where the reference slot is cleanly locked.

Usage: python tools/gen_stk_bowed.py [--only variant]
Patches -> patches/sweep/stk_bowed_port/   (machine-gate stage)
Renders -> renders/dsp/sweep/stk_bowed_port/
Reference WAVs are read from renders/scratch/stk_ref/bowed/.
"""
import json
import math
import os
import subprocess
import sys
import wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_bowed_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_bowed_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "bowed")

SR = 48000
NOTES = [48, 60, 72, 84, 96]          # C3..C7, matches the STK driver
TARGETS = [440.0 * 2 ** ((m - 69) / 12.0) for m in NOTES]
SLOT = 2.0
SUSTAIN_WIN = (0.8, 1.5)              # within-slot analysis window

# STK constants (Bowed.cpp at 48 kHz)
POLE = 0.75 - 0.2 * 22050.0 / SR      # 0.658125 (STK's formula at 48k)
LP_B0 = 0.95 * (1.0 - POLE)           # gain folded into b0
# Canonical mode: STK's SOUND lives at its native 22050 (pole 0.55 there).
# The formula under-darkens at 48k (cutoff ~3.2k vs canonical ~2.1k) and
# the per-sample ADSR rates halve gesture times — measured consequence:
# STK@48k double-slips (H2 +31..+46 dB over H1 at C3/C4) where STK@22050
# plays Helmholtz. Canonical mode re-realizes the 22050 model at 48k:
# pole 0.55^(22050/48000)=0.7600 (same cutoff Hz, same 0.95 DC loss),
# gesture times converted to 22050-seconds, delay comp -4 samples@22050
# = -8.707 samples@48k, body48 sections.
POLE_CANON = 0.55 ** (22050.0 / SR)
LP_B0_CANON = 0.95 * (1.0 - POLE_CANON)
COMP_VERBATIM = 4.0
COMP_CANON = 4.0 * SR / 22050.0
# Measured residual vs the 22050 reference after the rate-scaled comp:
# structural z^-1s (taps/lastOut) are worth 2.18x the TIME at 22050, plus
# an f-dependent filter-phase difference. Calibrated at C3..C5 from the
# default variant (delta in 48k samples, interpolated across f; held flat
# above the last measured point).
COMP_DELTA_F = [130.81, 261.63, 523.25, 1046.5, 2093.0]
COMP_DELTA = [-2.43, -1.37, -1.55, -1.82, -1.82]
LOOP_CLOSE = 0.09   # seconds of loop-gain closure at note end (canon)


def canon_ratio_knots(scale):
    """Points-mode CurveNode: note f -> DelayLine ratio, semitones C2..C8,
    with the f-interpolated canonical comp."""
    import numpy as np
    knots = []
    for m in range(36, 109):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        comp_f = COMP_CANON + float(np.interp(f, COMP_DELTA_F, COMP_DELTA))
        knots.append([round(f, 4),
                      round(scale * (1.0 - comp_f * f / SR), 8)])
    return knots
BODY_NATIVE = [                       # Maestre sections, verbatim from Bowed.cpp
    (1.0, 1.5667, 0.3133, -0.5509, -0.3925),
    (1.0, -1.9537, 0.9542, -1.6357, 0.8697),
    (1.0, -1.6683, 0.8852, -1.7674, 0.8735),
    (1.0, -1.8585, 0.9653, -1.8498, 0.9516),
    (1.0, -1.9299, 0.9621, -1.9354, 0.9590),
    (1.0, -1.9800, 0.9888, -1.9867, 0.9923),
]
BODY_GAIN = 0.1248
STK_NATIVE_SR = 22050.0

# The Maestre coefficients are FIXED numbers designed at STK's native
# 22050 Hz (resonances 261/525/1072/1287/1839 Hz — a violin body). Run
# verbatim at 48 kHz they shift 2.18x up (569..4004 Hz) — the "bogus
# baseline" Matt's ear caught 2026-09-13. Corrected mode re-realizes
# each section at 48 kHz: every pole/zero z -> z^(22050/48000)
# (angle scales to the same physical Hz, radius keeps the same bandwidth
# in Hz; real roots keep their sign on the magnitude power), then one
# global gain matches the native cascade at the 525 Hz main peak.
# --verbatim keeps the uncorrected sections (the STK-null validation).


def _cascade_db(sections, hz, fs):
    import numpy as np
    z = np.exp(-2j * np.pi * np.asarray(hz, dtype=float) / fs)
    h = np.ones_like(z, dtype=complex)
    for b0, b1, b2, a1, a2 in sections:
        h = h * (b0 + b1 * z + b2 * z * z) / (1 + a1 * z + a2 * z * z)
    return 20.0 * np.log10(np.abs(h) + 1e-12)


def rescale_body():
    import numpy as np
    ratio = STK_NATIVE_SR / SR
    out = []
    for b0, b1, b2, a1, a2 in BODY_NATIVE:
        def xf(c3):
            roots = np.roots(c3)
            new = []
            for z in roots:
                if abs(z.imag) < 1e-9:
                    zr = z.real
                    new.append(np.sign(zr) * abs(zr) ** ratio if zr != 0 else 0.0)
                else:
                    new.append(z ** ratio)
            p = np.poly(new)
            return [float(v.real) for v in p]
        nb = xf([b0, b1, b2])
        na = xf([1.0, a1, a2])
        # np.poly returns monic; restore numerator scale (b0 of source)
        out.append((b0 * nb[0], b0 * nb[1], b0 * nb[2], na[1], na[2]))
    # Global gain: match the native cascade's level at the 525 Hz peak.
    ref_db = _cascade_db(BODY_NATIVE, [525.0], STK_NATIVE_SR)[0]
    new_db = _cascade_db(out, [525.0], SR)[0]
    k = 10.0 ** ((ref_db - new_db) / 20.0)
    first = out[0]
    out[0] = (first[0] * k, first[1] * k, first[2] * k, first[3], first[4])
    # Match report over the body's business band.
    import numpy as np
    hz = np.linspace(100, 4000, 800)
    diff = _cascade_db(out, hz, SR) - _cascade_db(BODY_NATIVE, hz, STK_NATIVE_SR)
    print(f"body48: response match vs native 22050 cascade, 100-4000 Hz: "
          f"max |diff| {np.max(np.abs(diff)):.2f} dB, "
          f"mean {np.mean(np.abs(diff)):.2f} dB")
    return out


def body_sections(verbatim):
    src = BODY_NATIVE if verbatim else rescale_body()
    out = []
    for i, (b0, b1, b2, a1, a2) in enumerate(src):
        g = BODY_GAIN if i == 0 else 1.0
        out.append((b0 * g, b1 * g, b2 * g, a1, a2))
    return out

# name: (amp, slope, beta, vibGain, vibFreq) - mirrors bowed_ref.cpp exactly
VARIANTS = {
    "default":       (0.8, 3.0,     0.127236, 0.0,     0.0),
    "press_lo":      (0.8, 4.375,   0.127236, 0.0,     0.0),
    "press_hi":      (0.8, 1.40625, 0.127236, 0.0,     0.0),
    "pos_bridge":    (0.8, 3.0,     0.0625,   0.0,     0.0),
    "pos_middle":    (0.8, 3.0,     0.25,     0.0,     0.0),
    "vibrato":       (0.8, 3.0,     0.127236, 0.28125, 5.625),
    "soft":          (0.3, 3.0,     0.127236, 0.0,     0.0),
    "hard":          (1.0, 3.0,     0.127236, 0.0,     0.0),
    "press_lo_soft": (0.3, 4.375,   0.127236, 0.0,     0.0),
    "press_hi_hard": (1.0, 1.40625, 0.127236, 0.0,     0.0),
}


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def pin_stage(v0, v1, sec):
    # Duration-adaptive envelopes (timeMode fraction): minSec == maxSec
    # pins the stage to exact seconds regardless of note duration;
    # percent must be nonzero (0 means expand-to-fill).
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": 0.1, "power": 0.0, "minSec": sec, "maxSec": sec}


def expand_stage(v):
    return {"type": "Linear", "startVal": v, "endVal": v,
            "percent": 0.0, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(amp, slope, beta, vib_gain, vib_freq, body=None,
               canon=True):
    max_vel = 0.03 + 0.2 * amp
    if canon:
        # STK@22050 gesture times (rates are per-sample there).
        attack = 1.0 / (22.05 * amp)
        release = 0.9 / (0.0025 * 22050.0)
        pole, lp_b0, comp = POLE_CANON, LP_B0_CANON, COMP_CANON
        # Duration-adaptive: attack/decay/release pinned in seconds,
        # sustain expands to fill the note (no more truncation clicks on
        # short Passage notes).
        # Tail: the string rings past bow-off (rc ~0.98, ~-80 dB/s) and a
        # voice cut mid-ring is an audible click (measured 0.044 step at
        # note end). LOOP_CLOSE seconds of read-gain ramp on NeckDelay
        # close the loop smoothly after the bow release — an MForce-native
        # fix, not STK behavior (STK's driver truncates too).
        env_stages = [pin_stage(0.0, 1.0, round(attack, 6)),
                      pin_stage(1.0, 0.9, 0.005),
                      expand_stage(0.9),
                      pin_stage(0.9, 0.0, round(release, 6)),
                      pin_stage(0.0, 0.0, LOOP_CLOSE)]
        env_extra = {"timeMode": "fraction"}
    else:
        attack = 1.0 / (48.0 * amp)   # STK rate amp*0.001/sample at 48k
        release = 0.0075
        pole, lp_b0, comp = POLE, LP_B0, COMP_VERBATIM
        sustain_sec = 1.7 - attack - 0.005
        env_stages = [lin_stage(0.0, 1.0, round(attack, 6)),
                      lin_stage(1.0, 0.9, 0.005),
                      lin_stage(0.9, 0.9, round(sustain_sec, 6)),
                      lin_stage(0.9, 0.0, release)]
        env_extra = {"timeMode": "seconds"}

    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f3", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f4", "type": "PerformNode",
         "params": {"field": "frequency"}},
        # STK ADSR: attack to maxVelocity, 5 ms decay to 0.9, hold, then
        # release at bow lift (canon: duration-adaptive, 22050 gestures).
        {"id": "BowEnv", "type": "Envelope", "params": dict({
            "minValue": 0.0, "maxValue": round(max_vel, 6),
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeScale": 1.0, "stages": env_stages}, **env_extra)},
        # (sr/f - comp) * beta as ratio(f): affine (verbatim) or calibrated
        # knot table (canon; comp varies with f).
        {"id": "RatioBridge", "type": "CurveNode", "params": (
            {"exprKnots": [], "interp": "linear",
             "knots": canon_ratio_knots(beta), "mode": "points",
             "source": {"ref": "__perf_f3"}} if canon else
            {"exprKnots": [{"a": -comp * beta / SR, "b": beta,
                            "form": "linear", "x": 440.0}],
             "interp": "linear", "knots": [], "mode": "expressions",
             "source": {"ref": "__perf_f3"}})},
        {"id": "RatioNeck", "type": "CurveNode", "params": (
            {"exprKnots": [], "interp": "linear",
             "knots": canon_ratio_knots(1.0 - beta), "mode": "points",
             "source": {"ref": "__perf_f4"}} if canon else
            {"exprKnots": [{"a": -comp * (1.0 - beta) / SR, "b": 1.0 - beta,
                            "form": "linear", "x": 440.0}],
             "interp": "linear", "knots": [], "mode": "expressions",
             "source": {"ref": "__perf_f4"}})},
        # One-pole string loss, reading the bridge delay's previous sample
        # (tap == STK lastOut). Positive form; negation for the reflection
        # is a separate Multiply so DeltaV can reuse the positive value.
        {"id": "StringLP", "type": "Biquad", "params": {
            "source": {"tap": "BridgeDelay"},
            "b0": round(lp_b0, 8), "b1": 0.0, "b2": 0.0,
            "a1": round(-pole, 8), "a2": 0.0}},
        # dv = bowVel + LP(bridge) + neck   (= bowVel - br - nr)
        {"id": "Sum1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "BowEnv"}, "source2": {"ref": "StringLP"}}},
        {"id": "DeltaV", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Sum1"}, "source2": {"tap": "NeckDelay"}}},
        {"id": "NewVel", "type": "BowTable", "params": {
            "source": {"ref": "DeltaV"},
            "slope": round(slope, 6), "offset": 0.0,
            "minOutput": 0.01, "maxOutput": 0.98}},
        {"id": "NegLP", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "StringLP"}, "source2": -1.0}},
        {"id": "NegNeck", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"tap": "NeckDelay"}, "source2": -1.0}},
        {"id": "BridgeIn", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "NegNeck"}, "source2": {"ref": "NewVel"}}},
        {"id": "NeckIn", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "NegLP"}, "source2": {"ref": "NewVel"}}},
        {"id": "BridgeDelay", "type": "DelayLine", "params": {
            "source": {"ref": "BridgeIn"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": {"ref": "RatioBridge"},
            "amplitude": 1.0, "compensate": False}},
        {"id": "NeckDelay", "type": "DelayLine", "params": {
            "source": {"ref": "NeckIn"},
            "frequency": {"ref": "__perf_f2"},
            "ratio": {"ref": "RatioNeck"},
            "amplitude": {"ref": "LoopGate"} if canon else 1.0,
            "compensate": False}},
    ]

    if canon:
        at = next(i for i, n in enumerate(nodes) if n["id"] == "NeckDelay")
        nodes[at:at] = [
            {"id": "LoopGate", "type": "Envelope", "params": {
                "minValue": 0.0, "maxValue": 1.0,
                "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
                "timeMode": "fraction", "timeScale": 1.0,
                "stages": [expand_stage(1.0),
                           pin_stage(1.0, 0.0, LOOP_CLOSE)]}}]

    if vib_gain > 0.0:
        vib_nodes = [
            {"id": "__perf_f5", "type": "PerformNode",
             "params": {"field": "frequency"}},
            {"id": "VibSine", "type": "SineSource", "params": {
                "frequency": vib_freq, "amplitude": 1.0}},
            {"id": "VibAmp", "type": "CurveNode", "params": {
                "exprKnots": [{"a": -4.0 * vib_gain / SR, "b": vib_gain,
                               "form": "linear", "x": 440.0}],
                "interp": "linear", "knots": [], "mode": "expressions",
                "source": {"ref": "__perf_f5"}}},
            {"id": "VibMul", "type": "CombinedSource", "params": {
                "gainAdj": 0.0, "operation": 1,
                "source1": {"ref": "VibSine"}, "source2": {"ref": "VibAmp"}}},
            {"id": "NeckRatio", "type": "CombinedSource", "params": {
                "gainAdj": 0.0, "operation": 3,
                "source1": {"ref": "RatioNeck"}, "source2": {"ref": "VibMul"}}},
        ]
        # The loader resolves refs backward: insert before NeckDelay.
        at = next(i for i, n in enumerate(nodes) if n["id"] == "NeckDelay")
        nodes[at:at] = vib_nodes
        for n in nodes:
            if n["id"] == "NeckDelay":
                n["params"]["ratio"] = {"ref": "NeckRatio"}

    for i, (b0, b1, b2, a1, a2) in enumerate(body or body_sections(True)):
        src = "BridgeDelay" if i == 0 else f"Body{i - 1}"
        nodes.append({"id": f"Body{i}", "type": "Biquad", "params": {
            "source": {"ref": src},
            "b0": round(b0, 8), "b1": round(b1, 8), "b2": round(b2, 8),
            "a1": round(a1, 8), "a2": round(a2, 8)}})

    # One noteFace per PerformNode: the UI save path consolidates perf
    # nodes not bound to a face (drops their ids on roundtrip).
    perf_ids = [n["id"] for n in nodes if n["type"] == "PerformNode"]
    # Canon: bow-off at the reference's 1.7 s = duration minus the pinned
    # release + loop-closure tail stages.
    dur = round(1.7 + release + LOOP_CLOSE, 4) if canon else SLOT
    return {
        "sampleRate": SR, "seconds": 10.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": n, "time": SLOT * k, "duration": dur,
                   "velocity": 0.8} for k, n in enumerate(NOTES)],
        "graph": {"output": "Body5", "nodes": nodes},
        "ui": {"noteFaces": [{"fields": {"frequency": p},
                              "label": f"Note{i + 1}"}
                             for i, p in enumerate(perf_ids)]},
    }


# ---------------------------------------------------------------- analysis --
def read_mono(path):
    w = wave.open(path)
    sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x / 32768.0, sr


def slot_seg(x, sr, i, win=SUSTAIN_WIN):
    return x[int((i * SLOT + win[0]) * sr):int((i * SLOT + win[1]) * sr)]


def f0_autocorr(seg, sr, tg):
    if len(seg) == 0 or np.sqrt(np.mean(seg ** 2)) < 1e-4:
        return None
    nfft = 1 << int(np.ceil(np.log2(2 * len(seg))))
    S = np.fft.rfft(seg, nfft)
    ac = np.fft.irfft(S * np.conj(S))[:len(seg)]
    lo, hi = max(2, int(sr / (tg * 1.9))), min(len(seg) - 1, int(sr / (tg / 1.9)))
    if hi <= lo:
        return None
    lag = lo + int(np.argmax(ac[lo:hi + 1]))
    if ac[lag] <= 0:
        return None
    # Parabolic peak interpolation: integer lags quantize cents badly at
    # high f (one lag = 19c at C5@48k); sub-sample refinement fixes it.
    if 1 <= lag < len(ac) - 1:
        y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
        den = y0 - 2 * y1 + y2
        if den < 0:
            lag = lag + 0.5 * (y0 - y2) / den
    if not np.isfinite(lag) or lag <= 0:
        return None
    return sr / lag


def rms_frames(x, sr, i, frame=0.05):
    seg = x[int(i * SLOT * sr):int((i + 1) * SLOT * sr)]
    n = int(frame * sr)
    k = len(seg) // n
    return np.sqrt(np.mean(seg[:k * n].reshape(k, n) ** 2, axis=1) + 1e-20)


def harmonics_db(seg, sr, f0, count=12):
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    out = []
    for h in range(1, count + 1):
        m = (f > f0 * h * 0.94) & (f < f0 * h * 1.06)
        out.append(20 * np.log10(S[m].max() + 1e-12) if m.any() else -240.0)
    return np.array(out)


def cents(a, b):
    return 1200.0 * math.log2(a / b)


def compare(name, port_path, ref_path):
    xp, srp = read_mono(port_path)
    xr, srr = read_mono(ref_path)
    rows = []
    for i, tg in enumerate(TARGETS):
        sp, sn = slot_seg(xp, srp, i), slot_seg(xr, srr, i)
        f0p, f0r = f0_autocorr(sp, srp, tg), f0_autocorr(sn, srr, tg)
        rp, rr = rms_frames(xp, srp, i), rms_frames(xr, srr, i)
        m = min(len(rp), len(rr))
        rp, rr = rp[:m], rr[:m]
        active = rr > rr.max() * 0.05
        env_corr = float(np.corrcoef(rp, rr)[0, 1]) if m > 3 else 0.0
        gain = float(np.median(rp[active] / rr[active])) if active.any() else 0.0
        if f0p and f0r:
            dc = cents(f0p, f0r)
            hp = harmonics_db(sp, srp, f0p)
            hr = harmonics_db(sn, srr, f0r)
            hd = float(np.max(np.abs((hp - hp[0]) - (hr - hr[0]))[:8]))
            rows.append((i, dc, env_corr, gain, hd))
        else:
            rows.append((i, None, env_corr, gain, None))
    return rows


NOTE_NAMES = ["C3", "C4", "C5", "C6", "C7"]


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    verbatim = "--verbatim" in sys.argv
    body = body_sections(verbatim)
    suffix = "" if verbatim else "_canon"
    ref_dir = REF_DIR if verbatim else REF_DIR + "22k"
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)

    if not verbatim:
        print("CANONICAL mode (default): 22050-native model re-realized at"
              " 48k (string pole, gesture seconds, delay comp, body48);"
              " compared against the 22050 reference renders.")
    print(f"{'variant':15s} slot   f0-vs-ref   envCorr   gainFit   Hprof(dB)")
    for name, (amp, slope, beta, vg, vf) in VARIANTS.items():
        if only and name != only:
            continue
        patch = make_patch(amp, slope, beta, vg, vf, body=body,
                           canon=not verbatim)
        base = name
        name = name + suffix
        pj = os.path.join(PATCH_OUT, f"stk_bowed_{name}.json")
        pw = os.path.join(REND_OUT, f"stk_bowed_{name}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:15s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(ref_dir, f"stk_bowed_{base}.wav")
        for i, dc, ec, g, hd in compare(name, pw, ref):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{name:15s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
