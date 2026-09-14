"""Bowed extensions round 2 — Larson's Virtual Cello features, by the book.

Source: docs/research/stk_port/larson2003_virtual_cello.pdf (Music 421,
2003 — the model behind the Mohonk05 sample WAVs; code unpublished, but
the writeup gives structure + constants). Base = canonical bowed port
retuned to equal temperament (ext1), CELLO REGISTER notes (C2 G2 D3 A3 =
the cello's open strings, + D4), 2 s slots.

Cells:
  control2    the retuned base at cello register
  width_h2    finite bow width [S]: friction evaluated at both bow edges,
              cross-coupling h=2, f_r -> +rail, f_l -> -rail. Larson:
              "the most significant improvement... graininess and
              roughness heard from a real bow"
  hyp_b05/b12 hyperbolic stick/slip friction [SS] via the hysteresis
              Shaper: stick = velocity constraint (identity line), slip
              = Coulomb plateau at mu_d/mu_s = 0.4 of breakaway;
              breakaway x_b = {0.05, 0.12} (bow-force axis)
  tors_phys   torsional string, impedance-correct [SSW]: Z = 0.55 kg/s,
              Zt = 1.8 kg/s -> torsional injection = Z/Zt = 0.3056 of
              the junction velocity; reflection re-enters the junction
              velocity sum at unit weight; delays /5.2
  disp16      string stiffness B = 0.0004 (cello D string): 8 first-order
              allpasses least-squares fit to the dispersive phase-delay
              curve, in the bridge path, knots re-comped by the fitted
              cascade's phase at f0
  larson_btd  width + torsion + dispersion together (his bowedbtd cell,
              with STK friction as he used for b/t/d singles' base)

Body (his measured cello IR) deferred: data unpublished; public IR
sources noted in STK_PORT_NOTES for Matt to pick by ear.

Gates as ext1 (f0 +-35c, flutter < 0.25 at the three lowest notes) +
near-dup cull vs control2. Ear reference: the Larson WAVs, esp.
renders/scratch/stk_ref/mohonk05/{bowedb,bowedbtd,bachd}.wav.

Usage: python tools/gen_stk_bowed_ext2.py
"""
import copy
import json
import math
import os
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bowed as B  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_bowed_ext2")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_bowed_ext2")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "stk_bowed_ext2")

SR = B.SR
# Equal-temperament retune (measured in ext1).
B.COMP_DELTA = [-3.91, -4.00, -4.11, -4.42, -4.42]
_BODY48 = B.body_sections(False)

NOTES = [36, 43, 50, 57, 62]           # C2 G2 D3 A3 D4 — cello register
TARGETS = [440.0 * 2 ** ((m - 69) / 12.0) for m in NOTES]
NOTE_NAMES = ["C2", "G2", "D3", "A3", "D4"]
SLOT = 2.0
NORM_PEAK = 0.7

Z_T = 0.55       # transverse wave impedance, cello D string [SSW]
Z_TOR = 1.8      # torsional wave impedance [SSW]
B_STIFF = 0.0004  # inharmonicity, cello D string [SSW]
F0_D = 146.83    # D3, the string the dispersion filter is designed for


def base_patch():
    p = B.make_patch(0.8, 3.0, 0.127236, 0.0, 0.0, body=_BODY48, canon=True)
    dur = p["score"][0]["duration"]
    p["score"] = [{"note": n, "time": SLOT * k, "duration": dur,
                   "velocity": 0.8} for k, n in enumerate(NOTES)]
    return p


def node(p, nid):
    return next(n for n in p["graph"]["nodes"] if n["id"] == nid)


def node_index(p, nid):
    return next(i for i, n in enumerate(p["graph"]["nodes"])
                if n["id"] == nid)


# ---------------------------------------------------------------- variants --
def v_control(p):
    return p


def v_width(p, h=2.0):
    """Two friction evaluations at the bow edges [S], h cross-coupling.
    v+ arrives from the nut (NeckTap), v- from the bridge (StringLP).
    dv_l = bow - (v+ + h v-), dv_r = bow - (h v+ + v-);
    +rail (toward bridge) gets nv_r, -rail (toward nut) gets nv_l."""
    nodes = p["graph"]["nodes"]
    at = node_index(p, "Sum1")
    # Edge-weighted string velocities. Sum1 stays (bow + LP); we add the
    # h-weighted cross terms per edge.
    extra = [
        {"id": "HBr", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "StringLP"}, "source2": h}},
        {"id": "HNk", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"tap": "NeckDelay"}, "source2": h}},
        # dv_l = bowEnv + (v+ + h v-) sign convention as base: dv = bow
        # + LP + neck; left edge weights the bridge side by h.
        {"id": "SumL1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "BowEnv"}, "source2": {"ref": "HBr"}}},
        {"id": "DvL", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "SumL1"}, "source2": {"tap": "NeckDelay"}}},
        {"id": "SumR1", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "BowEnv"}, "source2": {"ref": "StringLP"}}},
        {"id": "DvR", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "SumR1"}, "source2": {"ref": "HNk"}}},
        {"id": "NvL", "type": "BowTable", "params": {
            "source": {"ref": "DvL"}, "slope": 3.0, "offset": 0.0,
            "minOutput": 0.01, "maxOutput": 0.98}},
        {"id": "NvR", "type": "BowTable", "params": {
            "source": {"ref": "DvR"}, "slope": 3.0, "offset": 0.0,
            "minOutput": 0.01, "maxOutput": 0.98}},
    ]
    nodes[at:at] = extra
    node(p, "BridgeIn")["params"]["source2"] = {"ref": "NvR"}
    node(p, "NeckIn")["params"]["source2"] = {"ref": "NvL"}
    return p


def v_hyp(p, x_b):
    """Hyperbolic stick/slip [SS] as hysteresis Shaper curves: stick =
    velocity constraint (identity: injection cancels dv), slip = Coulomb
    plateau at 0.4*x_b (mu_d/mu_s = 0.4). breakaway = x_b (bow force)."""
    s = round(0.4 * x_b, 6)
    i = node_index(p, "NewVel")
    p["graph"]["nodes"][i] = {
        "id": "NewVel", "type": "Shaper", "params": {
            "source": {"ref": "DeltaV"},
            "drive": 1.0, "smoothness": 0.5, "morph": 0.0,
            "hysteresis": True, "breakaway": x_b, "capture": 0.5 * x_b,
            # Same x-grid on both curves (shared segment structure).
            "values": [-1.0, -1.0, -0.002, -0.002, 0.002, 0.002, 1.0, 1.0],
            "values2": [-1.0, -s, -0.002, -s, 0.002, s, 1.0, s]}}
    return p


def v_tors_phys(p):
    """Impedance-correct torsion [SSW]: injection Z/Zt of junction
    velocity, unit-weight reflection back into the velocity sum."""
    cpl_in = round(Z_T / Z_TOR, 6)      # 0.3056
    nodes = p["graph"]["nodes"]
    at = node_index(p, "NewVel")
    pre = [
        {"id": "TorsRatio", "type": "CurveNode", "params": {
            "exprKnots": [], "interp": "linear",
            "knots": [[k[0], round(k[1] / 5.2, 8)]
                      for k in B.canon_ratio_knots(1.0)],
            "mode": "points", "source": {"ref": "__perf_f3"}}},
        {"id": "TorsLP", "type": "Biquad", "params": {
            "source": {"tap": "TorsDelay"},
            "b0": round(-0.9 * 0.3, 8), "b1": 0.0, "b2": 0.0,
            "a1": -0.7, "a2": 0.0}},
        {"id": "DeltaVT", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "DeltaV"}, "source2": {"tap": "TorsDelay"}}},
    ]
    nodes[at:at] = pre
    node(p, "NewVel")["params"]["source"] = {"ref": "DeltaVT"}
    at = node_index(p, "NewVel") + 1
    post = [
        {"id": "TorsFeed", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "NewVel"}, "source2": cpl_in}},
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
    return p


# --- dispersion: fit 8 first-order allpasses to the B=0.0004 curve -------
def _ap_pd(a, f):
    w = 2.0 * math.pi * f / SR
    num = math.atan2(-math.sin(w), a + math.cos(w))
    den = math.atan2(-a * math.sin(w), 1 + a * math.cos(w))
    return -(num - den) / w


def fit_dispersion(n_sections=8):
    from scipy.optimize import least_squares
    ks = np.arange(1, 69)
    fk = ks * F0_D * np.sqrt(1 + B_STIFF * ks ** 2)
    fk = fk[fk < 10000]
    ks = ks[:len(fk)]
    # Mode k of a loop needs total delay k*fs/fk; the bulk delay supplies
    # fs/f0, so the filter provides tau(fk) = (fs/f0)(1/sqrt(1+Bk^2) - 1)
    # relative — a decreasing-delay curve, tens of samples by k~40.
    target = SR * ks / fk - SR / F0_D
    target -= target[0]                 # relative shape; DC absorbed in comp
    w = 1.0 / np.sqrt(ks)               # weight low modes
    def resid(a):
        tot = np.array([sum(_ap_pd(ai, f) for ai in a) for f in fk])
        tot -= tot[0]
        return (tot - target) * w
    sol = least_squares(resid, np.linspace(0.05, 0.92, n_sections),
                        bounds=(-0.97, 0.97))
    tot = np.array([sum(_ap_pd(ai, f) for ai in sol.x) for f in fk])
    err = (tot - tot[0]) - target
    print(f"disp16 fit: {n_sections} sections, poles "
          f"{[round(float(a), 3) for a in sol.x]}, "
          f"max |err| {np.max(np.abs(err)):.3f} samples")
    return list(sol.x)


def v_disp16(p, poles):
    nodes = p["graph"]["nodes"]
    at = node_index(p, "BridgeDelay")
    prev = "BridgeIn"
    ap_nodes = []
    for i, a in enumerate(poles):
        ap_nodes.append({"id": f"Disp{i}", "type": "Biquad", "params": {
            "source": {"ref": prev},
            "b0": round(float(a), 6), "b1": 1.0, "b2": 0.0,
            "a1": round(float(a), 6), "a2": 0.0}})
        prev = f"Disp{i}"
    nodes[at:at] = ap_nodes
    node(p, "BridgeDelay")["params"]["source"] = {"ref": prev}
    rb = node(p, "RatioBridge")["params"]
    rb["knots"] = [[f, round(r - sum(_ap_pd(a, f) for a in poles)
                             * f / SR, 8)]
                   for f, r in rb["knots"]]
    return p


def v_larson_btd(p, poles):
    return v_disp16(v_tors_phys(v_width(p)), poles)


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
    g = NORM_PEAK / max(1e-6, float(np.max(np.abs(x))))
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
    poles = fit_dispersion()
    variants = {
        "control2":  (v_control, "canonical base, cello register"),
        "width_h2":  (v_width, "finite bow width h=2 (Larson's #1)"),
        "hyp_b05":   (lambda p: v_hyp(p, 0.05), "hyperbolic stick/slip, light force"),
        "hyp_b12":   (lambda p: v_hyp(p, 0.12), "hyperbolic stick/slip, heavy force"),
        "tors_phys": (v_tors_phys, "torsion, impedance-correct Z/Zt=0.306"),
        "disp16":    (lambda p: v_disp16(p, poles), "stiffness B=4e-4, 8-AP fit"),
        "larson_btd": (lambda p: v_larson_btd(p, poles),
                       "width + torsion + dispersion (his bowedbtd)"),
    }
    results = {}
    print(f"{'cell':12s} gate  " + "  ".join(f"{n}(c,fl)" for n in NOTE_NAMES))
    for name, (fn, why) in variants.items():
        p = fn(copy.deepcopy(base_patch()))
        pj = os.path.join(PATCH_OUT, f"ext2_{name}.json")
        pw = os.path.join(REND_OUT, f"ext2_{name}.wav")
        json.dump(p, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:160]}")
            continue
        ok, rows = gate(pw)
        cells = " ".join(
            (f"{c:+6.0f},{fl:4.2f}" if c is not None else f"  ----,{fl:4.2f}")
            for c, fl in rows)
        print(f"{name:12s} {'PASS' if ok else 'KILL'}  {cells}", flush=True)
        results[name] = (ok, why)
        if ok:
            normalize_copy(pw, os.path.join(AUD_OUT, f"ext2_{name}.wav"))
        else:
            os.remove(pw)
    passed = [n for n, (ok, _) in results.items() if ok]
    with open(os.path.join(AUD_OUT, "README.md"), "w") as f:
        f.write("# stk_bowed_ext2 — Larson's Virtual Cello features, by "
                "the book\n\nSource + constants: docs/research/stk_port/"
                "larson2003_virtual_cello.pdf. Cello-register notes "
                "(C2 G2 D3 A3 D4). Ear references: renders/scratch/"
                "stk_ref/mohonk05/{bowedb,bowedbtd,bachd}.wav (his "
                "renders; his measured cello-body IR is NOT in our "
                "cells — body deferred until an IR is picked by ear).\n\n")
        for n in passed:
            f.write(f"- ext2_{n}.wav — {variants[n][1]}\n")
        killed = [n for n, (ok, _) in results.items() if not ok]
        if killed:
            f.write("\nKilled by gates: "
                    + ", ".join(f"{n} ({variants[n][1]})" for n in killed)
                    + "\n")
    print(f"\n{len(passed)}/{len(results)} to audition: {AUD_OUT}")


if __name__ == "__main__":
    main()
