"""Bowed extensions round 1 — beyond the validated canonical STK port.

Base = gen_stk_bowed canonical default (22050-native model at 48k,
validated). Every cell mutates that base with ZERO engine code, following
the Mohonk05 feature menu + MForce-native ideas (STK_PORT_NOTES):

  control       the canonical port itself (table baseline)
  body_1986     the 2005-era STK body: single 500 Hz resonance
                (BiQuad setResonance(500, 0.85, normalize) * 0.2,
                rate-canonicalized) — what the JOS site's stock bowed used
  body_none     bare bridge output (body ablation)
  tors_c10/c25  simplified torsional-wave loop (speed/5.2, heavy damping)
                coupled at the bow junction — Mohonk 't' feature, folded
  hyst_*        junction swap: BowTable -> hysteresis Shaper (stick/slip
                curves + regime constants from the native string harness)
  bite          bow-pressure gesture: slope 4.5 -> 3.0 over 150 ms
                (pressure is a live pin — STK can't do this per-note)
  disp_a30/a55  string stiffness dispersion: first-order allpass in the
                bridge path (Biquad b0=a, b1=1, a1=a), knots re-comped
  perf          canon + vibrato + light reverb (listening dressing)

Machine gates (self-verdict, run contract): f0 within +-35c of the note
at C3..C5 AND sustain flutter < 0.25 -> audition; else killed (renders
deleted, patch stays in sweep/ as the grid record). C6/C7 reported, not
gated (fragile in the canonical model itself).

Usage: python tools/gen_stk_bowed_ext.py
Outputs: patches/sweep/stk_bowed_ext1/, renders/dsp/sweep/stk_bowed_ext1/,
survivors -> renders/dsp/audition/stk_bowed_ext1/ (normalized) + README.
"""
import copy
import json
import math
import os
import shutil
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bowed as B  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_bowed_ext1")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_bowed_ext1")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "stk_bowed_ext1")

SR = B.SR
TARGETS = B.TARGETS
NORM_PEAK = 0.7

# Harness stick/slip curves (gen_string_harness1, round-5 regime).
STICK = [-1.0, -1.0, -0.6, -1.0, -0.15, -0.3, 0.0, 0.0,
         0.15, 0.3, 0.6, 1.0, 1.0, 1.0]
SLIP = [-1.0, -0.25, -0.6, -0.35, -0.15, -1.0, 0.0, 0.0,
        0.15, 1.0, 0.6, 0.35, 1.0, 0.25]


# The canonical port matches STK@22050's OWN intonation (+7/+25/+49c at
# C3/C4/C5 vs equal temperament — the reference is simply sharp). The
# port keeps that faithfully; the EXTENSIONS retune to equal temperament
# (delta calibrated on the control cell).
# (delta ADDS to comp = shortens the loop = sharper; measured +7/+25/+49
# needs ~-4 samples across the board — i.e. equal temperament is
# approximately "drop STK's -4 fudge share".)
B.COMP_DELTA = [-3.91, -4.00, -4.11, -4.42, -4.42]
_BODY48 = B.body_sections(False)   # compute once (quiet + fast)


def base_patch():
    return B.make_patch(0.8, 3.0, 0.127236, 0.0, 0.0,
                        body=_BODY48, canon=True)


def node(patch, nid):
    return next(n for n in patch["graph"]["nodes"] if n["id"] == nid)


def node_index(patch, nid):
    return next(i for i, n in enumerate(patch["graph"]["nodes"])
                if n["id"] == nid)


def drop_nodes(patch, ids):
    patch["graph"]["nodes"] = [n for n in patch["graph"]["nodes"]
                               if n["id"] not in ids]


# ---------------------------------------------------------------- variants --
def v_control(p):
    return p


def v_body_1986(p):
    # setResonance(500, 0.85, normalize=true), gain 0.2, canonicalized:
    # radius 0.85^(22050/48000), same 500 Hz.
    r = 0.85 ** (22050.0 / SR)
    w0 = 2.0 * math.pi * 500.0 / SR
    a1 = -2.0 * r * math.cos(w0)
    a2 = r * r
    b0 = (0.5 - 0.5 * a2) * 0.2
    drop_nodes(p, [f"Body{i}" for i in range(6)])
    p["graph"]["nodes"].append({"id": "Body1986", "type": "Biquad", "params": {
        "source": {"ref": "BridgeDelay"},
        "b0": round(b0, 8), "b1": 0.0, "b2": round(-b0, 8),
        "a1": round(a1, 8), "a2": round(a2, 8)}})
    p["graph"]["output"] = "Body1986"
    return p


def v_body_none(p):
    drop_nodes(p, [f"Body{i}" for i in range(6)])
    p["graph"]["nodes"].append({"id": "Raw", "type": "CombinedSource",
                                "params": {"gainAdj": 0.0, "operation": 1,
                                           "source1": {"ref": "BridgeDelay"},
                                           "source2": 0.15}})
    p["graph"]["output"] = "Raw"
    return p


def v_torsion(p, cpl):
    # Simplified folded torsional loop: round trip = transverse/5.2,
    # heavy damping (one-pole p=0.9, loss 0.85 folded into b0 with the
    # reflection sign), fed a `cpl` share of the junction velocity; its
    # state couples back into the differential velocity with the same
    # weight. Inharmonic vs the main string by construction (5.2 is not
    # an integer ratio) — the Mohonk 't' graininess, folded to one rail.
    nodes = p["graph"]["nodes"]
    # Backward-ref rule: only taps may point forward, so the tap readers
    # (TorsLP, TorsBack) go BEFORE the junction, the loop body after it.
    at = node_index(p, "NewVel")
    pre = [
        {"id": "TorsRatio", "type": "CurveNode", "params": {
            "exprKnots": [], "interp": "linear",
            "knots": [[k[0], round(k[1] / 5.2, 8)]
                      for k in B.canon_ratio_knots(1.0)],
            "mode": "points", "source": {"ref": "__perf_f3"}}},
        {"id": "TorsLP", "type": "Biquad", "params": {
            "source": {"tap": "TorsDelay"},
            "b0": round(-0.85 * 0.1, 8), "b1": 0.0, "b2": 0.0,
            "a1": -0.9, "a2": 0.0}},
        {"id": "TorsBack", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"tap": "TorsDelay"}, "source2": cpl}},
        {"id": "DeltaVT", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "DeltaV"}, "source2": {"ref": "TorsBack"}}},
    ]
    nodes[at:at] = pre
    node(p, "NewVel")["params"]["source"] = {"ref": "DeltaVT"}
    at = node_index(p, "NewVel") + 1
    post = [
        {"id": "TorsFeed", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "NewVel"}, "source2": cpl}},
        {"id": "TorsIn", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "TorsFeed"}, "source2": {"ref": "TorsLP"}}},
        {"id": "TorsDelay", "type": "DelayLine", "params": {
            "source": {"ref": "TorsIn"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": {"ref": "TorsRatio"},
            "amplitude": 1.0, "compensate": False}},
    ]
    nodes[at:at] = post
    # TorsDelay is tap-only-consumed -> advance list, like NeckDelay.
    return p


def v_hyst(p, capture, drive):
    i = node_index(p, "NewVel")
    p["graph"]["nodes"][i] = {
        "id": "NewVel", "type": "Shaper", "params": {
            "source": {"ref": "DeltaV"},
            "drive": drive, "smoothness": 0.6, "morph": 0.0,
            "hysteresis": True, "breakaway": 0.6, "capture": capture,
            "values": list(STICK), "values2": list(SLIP)}}
    # Scale the +-1 curve range back to bow-table velocity scale.
    p["graph"]["nodes"].insert(i + 1, {
        "id": "NewVelScale", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "NewVel"},
            "source2": round(1.0 / drive, 6)}})
    for nid in ("BridgeIn", "NeckIn"):
        prm = node(p, nid)["params"]
        for k in ("source1", "source2"):
            if prm[k] == {"ref": "NewVel"}:
                prm[k] = {"ref": "NewVelScale"}
    return p


def v_bite(p):
    at = node_index(p, "NewVel")
    p["graph"]["nodes"][at:at] = [
        {"id": "BiteEnv", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": 1.5,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [B.pin_stage(1.0, 0.0, 0.15),
                       B.expand_stage(0.0)]}},
        {"id": "SlopeSum", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": 3.0, "source2": {"ref": "BiteEnv"}}},
    ]
    node(p, "NewVel")["params"]["slope"] = {"ref": "SlopeSum"}
    return p


def _ap_phase(a, f):
    """Phase delay (samples) of H = (a + z^-1)/(1 + a z^-1)."""
    w = 2.0 * math.pi * f / SR
    num = math.atan2(-math.sin(w), a + math.cos(w))
    den = math.atan2(-a * math.sin(w), 1 + a * math.cos(w))
    return -(num - den) / w


def v_disp(p, a):
    # Stiffness dispersion: first-order allpass in the bridge input path;
    # its (frequency-dependent) phase delay is subtracted from the bridge
    # knots so nominal tuning holds while upper partials stretch.
    at = node_index(p, "BridgeDelay")
    p["graph"]["nodes"][at:at] = [
        {"id": "Disp", "type": "Biquad", "params": {
            "source": {"ref": "BridgeIn"},
            "b0": a, "b1": 1.0, "b2": 0.0, "a1": a, "a2": 0.0}}]
    node(p, "BridgeDelay")["params"]["source"] = {"ref": "Disp"}
    rb = node(p, "RatioBridge")["params"]
    rb["knots"] = [[f, round(r - _ap_phase(a, f) * f / SR, 8)]
                   for f, r in rb["knots"]]
    return p


def v_perf(p):
    # Vibrato (canonical variant params) + light reverb dressing.
    p2 = B.make_patch(0.8, 3.0, 0.127236, 0.28125, 5.625,
                      body=_BODY48, canon=True)
    p2["graph"]["nodes"].append({"id": "Verb", "type": "Reverb", "params": {
        "source": {"ref": "Body5"},
        "damping": 0.6, "dry": 0.75, "roomSize": 0.25, "wet": 0.15}})
    p2["graph"]["output"] = "Verb"
    return p2


VARIANTS = {
    "control":   (v_control, "canonical port, table baseline"),
    "body_1986": (v_body_1986, "2005-era single 500 Hz body resonance"),
    "body_none": (v_body_none, "bare bridge (body ablation)"),
    "tors_c10":  (lambda p: v_torsion(p, 0.10), "torsional loop, coupling 0.10"),
    "tors_c25":  (lambda p: v_torsion(p, 0.25), "torsional loop, coupling 0.25"),
    "hyst_c25_d3": (lambda p: v_hyst(p, 0.25, 3.0), "stick/slip junction, cap .25 drive 3"),
    "hyst_c12_d3": (lambda p: v_hyst(p, 0.12, 3.0), "stick/slip junction, cap .12 drive 3"),
    "hyst_c25_d6": (lambda p: v_hyst(p, 0.25, 6.0), "stick/slip junction, cap .25 drive 6"),
    "hyst_c12_d6": (lambda p: v_hyst(p, 0.12, 6.0), "stick/slip junction, cap .12 drive 6"),
    "bite":      (v_bite, "bow-bite pressure gesture 4.5->3.0 over 150 ms"),
    "disp_a30":  (lambda p: v_disp(p, 0.30), "stiffness allpass a=0.30"),
    "disp_a55":  (lambda p: v_disp(p, 0.55), "stiffness allpass a=0.55"),
    "perf":      (v_perf, "canon + vibrato + reverb (dressing)"),
}


# ------------------------------------------------------------------ gates --
def read_mono(path):
    w = wave.open(path)
    sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x / 32768.0, sr


def gate(path):
    """Per-slot (cents-vs-target, flutter); gate on C3..C5."""
    x, sr = read_mono(path)
    rows = []
    for i, tg in enumerate(TARGETS):
        seg = x[int((i * 2 + 0.8) * sr):int((i * 2 + 1.5) * sr)]
        f0 = B.f0_autocorr(seg, sr, tg)
        c = 1200 * math.log2(f0 / tg) if f0 else None
        n = int(0.05 * sr)
        k = len(seg) // n
        env = np.sqrt(np.mean(seg[:k * n].reshape(k, n) ** 2, axis=1) + 1e-20)
        flut = float(np.std(env) / (np.mean(env) + 1e-12))
        rows.append((c, flut))
    ok = all(r[0] is not None and abs(r[0]) < 35 and r[1] < 0.25
             for r in rows[:3])
    return ok, rows


def normalize_copy(src, dst):
    x, sr = read_mono(src)
    g = NORM_PEAK / max(1e-6, np.max(np.abs(x)))
    y = np.clip(np.rint(x * g * 32767), -32768, 32767).astype(np.int16)
    st = np.empty(y.size * 2, dtype=np.int16)
    st[0::2] = y
    st[1::2] = y
    o = wave.open(dst, "wb")
    o.setnchannels(2)
    o.setsampwidth(2)
    o.setframerate(sr)
    o.writeframes(st.tobytes())
    o.close()


def main():
    for d in (PATCH_OUT, REND_OUT, AUD_OUT):
        os.makedirs(d, exist_ok=True)
    results = {}
    print(f"{'cell':14s} gate  C3(c,fl)      C4          C5          C6          C7")
    for name, (fn, why) in VARIANTS.items():
        p = fn(copy.deepcopy(base_patch()))
        pj = os.path.join(PATCH_OUT, f"ext_{name}.json")
        pw = os.path.join(REND_OUT, f"ext_{name}.wav")
        json.dump(p, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:14s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:160]}")
            continue
        ok, rows = gate(pw)
        if name == "perf":
            ok = True   # vibrato defeats the f0/flutter gate; dressing cell
        cells = " ".join(
            (f"{c:+6.0f},{fl:4.2f}" if c is not None else f"  ----,{fl:4.2f}")
            for c, fl in rows)
        print(f"{name:14s} {'PASS' if ok else 'KILL'}  {cells}")
        results[name] = (ok, why)
        if ok:
            normalize_copy(pw, os.path.join(AUD_OUT, f"ext_{name}.wav"))
        else:
            os.remove(pw)  # failed render = derived data, deleted

    passed = [n for n, (ok, _) in results.items() if ok]
    with open(os.path.join(AUD_OUT, "README.md"), "w") as f:
        f.write("# stk_bowed_ext1 — first extensions past the validated "
                "canonical STK bowed port\n\n"
                "Base: stk_bowed_default_canon (validated vs STK@22050). "
                "Each cell = one feature, zero engine code. Gate: f0 "
                "+-35c and flutter <0.25 at C3..C5. Killed cells listed "
                "at the bottom (patches kept in sweep/).\n\n")
        for n in passed:
            f.write(f"- ext_{n}.wav — {VARIANTS[n][1]}\n")
        killed = [n for n, (ok, _) in results.items() if not ok]
        if killed:
            f.write("\nKilled by gates: "
                    + ", ".join(f"{n} ({VARIANTS[n][1]})" for n in killed)
                    + "\n")
    print(f"\n{len(passed)}/{len(results)} cells to audition: {AUD_OUT}")


if __name__ == "__main__":
    main()
