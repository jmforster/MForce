"""STK BandedWG port — patch generator + reference comparison.

Banded waveguides (Essl & Cook): N mode loops, each bandpass+delay,
struck (short DC burst into each line) or bowed (BowTable across the
mode sum). Native-rate validation (sampleRate 22050 vs 22050 refs).

Per-mode loop (BandedWG::tick):
  BP_k <- input + gains_k * D_k.lastOut ;  D_k <- BP_k ;  out = 4 * sum BP_k
Bandpass = STK setResonance(f*mode_k, R, normalize=True), R = 1-32pi/sr
(f-independent) => H = k(1 - z^-2)/(1 + a1 z^-1 + a2 z^-2), k = .5(1-R^2):
engine = raw Biquad (1,0,-1) in series with resonance-mode Biquad (b0=k,
frequency pin = f*mode_k, radius R). Delay len = int((sr/f)/mode_k).

f-dependent structure (mode drops when len<=2, the 1/nModes divisor,
f>1568 clamp) is baked into CurveNodes on note frequency — knots are
computed at semitone frequencies so the 5 score notes read exact values;
mode-drop steps use 1 Hz-wide knot pairs (piecewise-linear "step").

Struck: STK pre-fills each line with int(len_k/min_len) samples of
excitation_k*amp/nModes before tick 0; here the same burst enters the
delay input over the first N_k ticks (<=1 ms smear, below the metric
floor). Burst length tracks note via Envelope timeScale <- CurveNode.

Bowed: input = (adsr*maxVel - baseGain*sum(live D_k taps)) through
BowTable (slope 3.0) / nModes. Bowed slots are 5 s (model blooms ~4 s;
same as the reference driver). bar_bowed is NOT ported: the reference
itself is silent there (uniform bar does not self-oscillate at this
pressure) — nothing to compare.

Usage: python tools/gen_stk_bandedwg.py [--only variant]
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import (read_mono, f0_autocorr, harmonics_db,  # noqa
                           cents)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_bandedwg_port")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_bandedwg_port")
REF_DIR = os.path.join(ROOT, "renders", "scratch", "stk_ref", "bandedwg22k")

SR = 22050
NOTES = [48, 60, 72, 84, 96]
NOTE_NAMES = ["C3", "C4", "C5", "C6", "C7"]
TARGETS = [440.0 * 2 ** ((m - 69) / 12.0) for m in NOTES]
RADIUS = 1.0 - math.pi * 32.0 / SR
KNORM = 0.5 * (1.0 - RADIUS * RADIUS)
BASE_GAIN = 0.999
F_CLAMP = 1568.0

PRESETS = {
    "bar":   ([1.0, 2.756, 5.404, 8.933],
              [0.9 ** (i + 1) for i in range(4)], [1.0] * 4),
    "tbar":  ([1.0, 4.0198391420, 10.7184986595, 18.0697050938],
              [0.999 ** (i + 1) for i in range(4)], [1.0] * 4),
    "glass": ([1.0, 2.32, 4.25, 6.63, 9.38],
              [0.999 ** (i + 1) for i in range(5)], [1.0] * 5),
    "bowl":  ([0.996108344, 1.0038916562, 2.979178, 2.99329767,
               5.704452, 5.704452, 8.9982, 9.01549726, 12.83303,
               12.807382, 17.2808219, 21.97602739726],
              [0.999925960128219, 0.999925960128219, 0.999982774366897,
               0.999982774366897, 1.0, 1.0, 1.0, 1.0, 0.999965497558225,
               0.999965497558225, 1.0, 0.999999999999999965497558225],
              [1.1900357, 1.1900357, 1.0914886, 1.0914886, 4.2995041,
               4.2995041, 4.0063034, 4.0063034, 0.7063034, 0.7063034,
               5.7063034, 5.7063034]),
}

# variant: (preset, bowed)
VARIANTS = {
    "bar_struck":   ("bar", False),
    "tbar_struck":  ("tbar", False),
    "tbar_bowed":   ("tbar", True),
    "glass_struck": ("glass", False),
    "glass_bowed":  ("glass", True),
    "bowl_struck":  ("bowl", False),
    "bowl_bowed":   ("bowl", True),
}
AMP = 0.8


def fc(f):
    return min(f, F_CLAMP)


def mode_len(f, m):
    return int((SR / fc(f)) / m)


def live_modes(f, modes):
    n = 0
    for m in modes:
        if mode_len(f, m) > 2:
            n += 1
        else:
            break
    return n


def semis():
    return [440.0 * 2 ** ((m - 69) / 12.0) for m in range(36, 109)]


def step_knots(fn):
    """Piecewise 'step' curve: value fn(f) sampled at semitones, with
    1 Hz-wide double knots wherever the value changes."""
    ks = []
    prev = None
    for f in semis():
        v = fn(f)
        if prev is not None and v != prev:
            ks.append([round(f - 1.0, 4), round(prev, 8)])
        ks.append([round(f, 4), round(v, 8)])
        prev = v
    # dedupe consecutive same-value knots (keep first/last of runs)
    out = [ks[0]]
    for a in ks[1:]:
        if a[1] == out[-1][1] and len(out) > 1 and out[-2][1] == a[1]:
            out[-1] = a
        else:
            out.append(a)
    return out


def mul(nid, a, b):
    return {"id": nid, "type": "CombinedSource", "params": {
        "gainAdj": 0.0, "operation": 1, "source1": a, "source2": b}}


def add(nid, a, b):
    return {"id": nid, "type": "CombinedSource", "params": {
        "gainAdj": 0.0, "operation": 3, "source1": a, "source2": b}}


def curve(nid, knots, src):
    return {"id": nid, "type": "CurveNode", "params": {
        "exprKnots": [], "interp": "linear", "knots": knots,
        "mode": "points", "source": src}}


def lin_stage(v0, v1, sec):
    return {"type": "Linear", "startVal": v0, "endVal": v1,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def make_patch(preset, bowed):
    modes, basegains, excitation = PRESETS[preset]
    nmax = len(modes)
    slot = 5.0 if bowed else 2.0
    off = 4.5 if bowed else 1.7

    R = lambda i: {"ref": i}
    T = lambda i: {"tap": i}
    perf = 0

    def pf():
        nonlocal perf
        perf += 1
        nid = f"__perf_f{perf}"
        nodes.append({"id": nid, "type": "PerformNode",
                      "params": {"field": "frequency"}})
        return nid

    nodes = []
    fbase = pf()
    # clamped fundamental
    nodes.append(curve("Fc", [[20.0, 20.0], [F_CLAMP, F_CLAMP],
                              [22050.0, F_CLAMP]], R(fbase)))
    # 1/nModes(f)
    nodes.append(curve("InvN", step_knots(
        lambda f: 1.0 / max(1, live_modes(f, modes))), R(pf())))

    # ---- excitation ----
    if bowed:
        max_vel = 0.03 + 0.1 * AMP
        attack = 1.0 / (AMP * 0.001 * SR)          # rate amp*0.001/sample
        release = 0.9 / (0.0025 * SR)              # stopBowing (1-.5)*.005
        nodes.append({"id": "AdsrEnv", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": round(max_vel, 6),
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": [lin_stage(0.0, 1.0, round(attack, 6)),
                       lin_stage(1.0, 0.9, 0.005),
                       lin_stage(0.9, 0.9, round(off - attack - 0.005, 6)),
                       lin_stage(0.9, 0.0, round(release, 6))]}})
    else:
        # struck: per-mode DC bursts added at the delay inputs; the
        # shared per-note scale is amp/nModes.
        pass

    # velocityInput = baseGain * sum(gate_k * tap D_k)   (bowed only)
    mode_ids = []
    for k, m in enumerate(modes):
        # per-mode live gate
        nodes.append(curve(f"Gate{k}", step_knots(
            lambda f, m=m, k=k: 1.0 if live_modes(f, modes) > k else 0.0),
            R(pf())))
        mode_ids.append(k)

    if bowed:
        acc = None
        for k in mode_ids:
            nodes.append(mul(f"VTap{k}", T(f"D{k}"), R(f"Gate{k}")))
            if acc is None:
                acc = f"VTap{k}"
            else:
                nodes.append(add(f"VSum{k}", R(acc), R(f"VTap{k}")))
                acc = f"VSum{k}"
        nodes.append(mul("VelIn", R(acc), BASE_GAIN))
        nodes.append(mul("NegVel", R("VelIn"), -1.0))
        nodes.append(add("Dv", R("AdsrEnv"), R("NegVel")))
        # engine BowTable outputs dv * rc(dv) directly (the multiply is
        # folded into the node) — no extra Dv multiply here.
        nodes.append({"id": "Friction", "type": "BowTable", "params": {
            "source": R("Dv"), "slope": 3.0, "offset": 0.0,
            "minOutput": 0.0, "maxOutput": 1.0}})
        nodes.append(mul("Input", R("Friction"), R("InvN")))
    else:
        nodes.append({"id": "Input", "type": "VarSource",
                      "params": {"value": 0.0}})

    # ---- per-mode loops ----
    out_acc = None
    for k, m in enumerate(modes):
        fk = f"FM{k}"
        nodes.append(mul(fk, R("Fc"), float(m)))
        # delay length knots: len = int((sr/fc)/m), ratio = len*f/sr
        # (ratio uses the RAW note f pin; len from clamped f)
        lk = []
        for f in semis():
            ln = max(3, mode_len(f, m))     # dropped modes are gated off
            lk.append([round(f, 4), round(ln * f / SR, 8)])
        nodes.append(curve(f"RatioD{k}", lk, R(pf())))
        # burst (struck): exc_k * amp / nModes for N_k samples
        if not bowed:
            # STK pre-fills N_k = int(len_k/min_len) samples. Envelope
            # 1-sample stages never fire (measured; 2-sample stages do), so a
            # burst runs for
            # n_eff = max(N_k, 2) samples with amplitude scaled by
            # N_k/n_eff — same injected energy, still well inside one
            # loop period for every mode that has one.
            MIN_B = 2

            def nk_fn(f, k=k):
                ls = [mode_len(f, mm) for mm in modes]
                live = [l for l in ls if l > 2]
                if not live or ls[k] <= 2:
                    return 0
                return max(1, int(ls[k] / min(live)))

            n_ref = max(nk_fn(TARGETS[0]), 1)
            n_eff_ref = max(n_ref, MIN_B)

            def ts_fn(f, k=k):
                nk = nk_fn(f)
                if nk == 0:
                    return 1e-6
                return max(nk, MIN_B) / n_eff_ref

            def ac_fn(f, k=k):
                nk = nk_fn(f)
                if nk == 0:
                    return 0.0
                return nk / max(nk, MIN_B)
            nodes.append(curve(f"BTs{k}", step_knots(ts_fn), R(pf())))
            nodes.append(curve(f"BAc{k}", step_knots(ac_fn), R(pf())))
            nodes.append({"id": f"Burst{k}", "type": "Envelope", "params": {
                "minValue": 0.0, "maxValue": round(excitation[k] * AMP, 6),
                "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
                "timeMode": "seconds", "timeScale": 1.0,
                "dynamicPins": None,
                "stages": [lin_stage(1.0, 1.0, round(n_eff_ref / SR, 8)),
                           lin_stage(0.0, 0.0, 1.0)]}})
            nodes[-1]["dynamicPins"] = {"timeScale": {"ref": f"BTs{k}"}}
            nodes.append(mul(f"BurstA{k}", R(f"Burst{k}"), R(f"BAc{k}")))
            nodes.append(mul(f"BurstN{k}", R(f"BurstA{k}"), R("InvN")))
            in_id = "Input"
        else:
            in_id = "Input"
        # BP input = input + gains_k * D_k.lastOut, gated
        nodes.append(mul(f"Fb{k}", T(f"D{k}"), float(basegains[k])))
        nodes.append(add(f"BPin{k}", R(in_id), R(f"Fb{k}")))
        nodes.append(mul(f"BPing{k}", R(f"BPin{k}"), R(f"Gate{k}")))
        # zeros (1 - z^-2) then resonant poles with b0 = KNORM
        nodes.append({"id": f"Z{k}", "type": "Biquad", "params": {
            "source": R(f"BPing{k}"),
            "b0": 1.0, "b1": 0.0, "b2": -1.0, "a1": 0.0, "a2": 0.0}})
        nodes.append({"id": f"BP{k}", "type": "Biquad", "params": {
            "source": R(f"Z{k}"), "mode": 1, "b0": round(KNORM, 8),
            "frequency": R(fk), "radius": RADIUS}})
        # struck: the burst enters the LINE directly (STK pre-fills the
        # delay, not the bandpass input — the BP zeros at z=+-1 would
        # kill a DC burst entirely).
        if not bowed:
            nodes.append(add(f"DIn{k}", R(f"BP{k}"), R(f"BurstN{k}")))
            d_src = f"DIn{k}"
        else:
            d_src = f"BP{k}"
        nodes.append({"id": f"D{k}", "type": "DelayLine", "params": {
            "source": R(d_src), "frequency": R(pf()),
            "ratio": R(f"RatioD{k}"), "amplitude": 1.0,
            "compensate": False}})
        nodes.append(mul(f"BPg{k}", R(f"BP{k}"), R(f"Gate{k}")))
        if out_acc is None:
            out_acc = f"BPg{k}"
        else:
            nodes.append(add(f"OSum{k}", R(out_acc), R(f"BPg{k}")))
            out_acc = f"OSum{k}"

    nodes.append(mul("Out", R(out_acc), 4.0))

    perf_ids = [n["id"] for n in nodes if n["type"] == "PerformNode"]
    return {
        "sampleRate": SR, "seconds": 30.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": n, "time": slot * k, "duration": off,
                   "velocity": 0.8} for k, n in enumerate(NOTES)],
        "graph": {"output": "Out", "nodes": nodes},
        "ui": {"noteFaces": [{"fields": {"frequency": p},
                              "label": f"Note{i + 1}"}
                             for i, p in enumerate(perf_ids)]},
    }, slot


def compare_slots(port_path, ref_path, slot):
    xp, srp = read_mono(port_path)
    xr, srr = read_mono(ref_path)
    rows = []
    for i, tg in enumerate(TARGETS):
        sp = xp[int((i * slot + 0.2) * srp):int((i * slot + slot) * srp)]
        sn = xr[int((i * slot + 0.2) * srr):int((i * slot + slot) * srr)]
        f0p, f0r = f0_autocorr(sp, srp, tg), f0_autocorr(sn, srr, tg)
        n = int(0.05 * srp)
        kp = len(sp) // n
        rp = np.sqrt(np.mean(sp[:kp * n].reshape(kp, n) ** 2, axis=1) + 1e-20)
        n = int(0.05 * srr)
        kr = len(sn) // n
        rr = np.sqrt(np.mean(sn[:kr * n].reshape(kr, n) ** 2, axis=1) + 1e-20)
        mn = min(len(rp), len(rr))
        rp, rr = rp[:mn], rr[:mn]
        active = rr > rr.max() * 0.05
        ec = float(np.corrcoef(rp, rr)[0, 1]) if mn > 3 else 0.0
        g = float(np.median(rp[active] / rr[active])) if active.any() else 0.0
        if f0p and f0r:
            dc = cents(f0p, f0r)
            hp = harmonics_db(sp, srp, f0p)
            hr = harmonics_db(sn, srr, f0r)
            hd = float(np.max(np.abs((hp - hp[0]) - (hr - hr[0]))[:8]))
            rows.append((i, dc, ec, g, hd))
        else:
            rows.append((i, None, ec, g, None))
    return rows


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)

    print(f"{'variant':14s} slot   f0-vs-ref   envCorr   gainFit   Hprof(dB)")
    for base, (preset, bowed) in VARIANTS.items():
        if only and base != only:
            continue
        patch, slot = make_patch(preset, bowed)
        pj = os.path.join(PATCH_OUT, f"stk_bandedwg_{base}.json")
        pw = os.path.join(REND_OUT, f"stk_bandedwg_{base}.wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{base:14s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:200]}")
            continue
        ref = os.path.join(REF_DIR, f"stk_bandedwg_{base}.wav")
        for i, dc, ec, g, hd in compare_slots(pw, ref, slot):
            dcs = f"{dc:+7.1f}c" if dc is not None else "  unlock"
            hds = f"{hd:7.2f}" if hd is not None else "     --"
            print(f"{base:14s} {NOTE_NAMES[i]}  {dcs}   {ec:7.3f}   "
                  f"{g:7.3f}   {hds}", flush=True)


if __name__ == "__main__":
    main()
