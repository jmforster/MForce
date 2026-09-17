"""BandedWG percussion round 2 — characteristic decay (REVIEW 60 verdict:
"almost all loooong ambient sounds... We need characteristic envelopes,
or the physics equivalent (ultra-rapid decay) thereof").

Same mode sets as round 1, but the per-mode loop gain becomes a
CurveNode on note frequency targeting a CONSTANT T60 per cell:
    g(f) = exp(ln(1e-3) / (f * mode_k * T60_k)),  T60_k = T60 / (1+0.6k)
so decay time no longer balloons with period (round 1's gong-everything
failure: fixed per-loop gains give T60 proportional to 1/f). Upper
modes die faster, as real bars do.

Outputs: patches/audition/bwg_perc2/ + renders/dsp/audition/bwg_perc2/.
Usage: python tools/gen_bwg_perc2.py [--only cell]
"""
import json
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bandedwg as g
import gen_bwg_perc1 as p1

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "audition", "bwg_perc2")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "bwg_perc2")

# (modes, excitation, T60_base seconds) — mode ratios as round 1;
# T60s at characteristic instrument values, constant across the
# keyboard (the whole point of this round).
CELLS = {
    "marimba":    (p1.CELLS["marimba"][0],    p1.CELLS["marimba"][2],    0.8),
    "xylophone":  (p1.CELLS["xylophone"][0],  p1.CELLS["xylophone"][2],  0.35),
    "vibraphone": (p1.CELLS["vibraphone"][0], p1.CELLS["vibraphone"][2], 6.0),
    "chime":      (p1.CELLS["chime"][0],      p1.CELLS["chime"][2],      6.0),
    "tomdrum":    (p1.CELLS["tomdrum"][0],    p1.CELLS["tomdrum"][2],    0.35),
    "woodblock":  (p1.CELLS["woodblock"][0],  p1.CELLS["woodblock"][2],  0.08),
    "glass_long": (p1.CELLS["glass_long"][0], p1.CELLS["glass_long"][2], 2.5),
}
LN_60DB = math.log(1e-3)


# Measured calibration (this round, all 7 cells): the composite loop
# (delay + resonant biquad energy storage + multi-mode tail) decays
# 3-5x slower than the bare recirculation model exp(ln(1e-3)/(f*m*T60)).
# Feeding the formula T60/4 lands measured T60 within ~0.8-1.3x of
# target across the board.
T60_CAL = 4.0


def t60_gain_knots(mode, t60_k):
    """[f, g] at semitone frequencies for constant-T60 per-loop gain."""
    ks = []
    for f in g.semis():
        gain = math.exp(LN_60DB / (f * mode * (t60_k / T60_CAL)))
        ks.append([round(f, 4), round(min(max(gain, 0.2), 0.99995), 8)])
    return ks


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv \
        else None
    p1.retune_module()
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    results = []
    for name, (modes, excitation, t60) in CELLS.items():
        if only and name != only:
            continue
        # basegains are placeholders — replaced by T60 curves below.
        g.PRESETS[name] = (modes, [0.9] * len(modes), excitation)
        patch, slot = g.make_patch(name, bowed=False)
        nodes = patch["graph"]["nodes"]
        prepend = []
        for k, m in enumerate(modes):
            t60_k = t60 / (1.0 + 0.6 * k)
            prepend.append({"id": f"__perf_t{k}", "type": "PerformNode",
                            "params": {"field": "frequency"}})
            prepend.append(g.curve(f"TG{k}", t60_gain_knots(m, t60_k),
                                   {"ref": f"__perf_t{k}"}))
            fb = next(n for n in nodes if n["id"] == f"Fb{k}")
            fb["params"]["source2"] = {"ref": f"TG{k}"}
        # producers before consumers — the loader resolves refs in order
        nodes[:0] = prepend
        # the new perform nodes need note faces like the rest
        patch["ui"]["noteFaces"] = [
            {"fields": {"frequency": n["id"]}, "label": f"Note{i + 1}"}
            for i, n in enumerate(nn for nn in nodes
                                  if nn["type"] == "PerformNode")]
        patch["seconds"] = slot * len(g.NOTES) + max(2.0, t60 + 1.0)
        ppath = os.path.join(PATCH_OUT, f"bwg2_{name}.json")
        wpath = os.path.join(REND_OUT, f"bwg2_{name}.wav")
        with open(ppath, "w") as f:
            json.dump(patch, f, indent=1)
        r = subprocess.run([CLI, ppath, wpath], capture_output=True,
                           text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            results.append((name, "RENDER_FAIL"))
            print(f"{name:12s} RENDER_FAIL {(r.stderr or '')[-120:]}")
            continue
        x, sr = g.read_mono(wpath)
        clicks = p1.click_scan(x, sr)
        cents = [p1.mode1_cents(x, sr, slot * k, t)
                 for k, t in enumerate(g.TARGETS)]
        t60s = [p1.t60_estimate(x, sr, slot * k, slot)
                for k in range(len(g.NOTES))]
        worst_c = max((abs(c) for c in cents if not math.isnan(c)),
                      default=float("nan"))
        # decay-accuracy gate: measured T60 at C4 within 3x of target
        # (coarse — the estimator is rough on short windows)
        meas = t60s[1]
        t60_ok = meas != float("inf") and t60 / 3.0 < meas < t60 * 3.0
        gate, why = "PASS", []
        if clicks:
            gate, why = "REJECT", why + [f"clicks {clicks}"]
        if math.isnan(worst_c) or worst_c > 40.0:
            gate, why = "REJECT", why + [f"mode1 off {worst_c:.0f}c"]
        if not t60_ok:
            gate, why = "REJECT", why + [f"T60 {meas:.2f}s vs target {t60}"]
        results.append((name, gate))
        print(f"{name:12s} {gate}  mode1 {worst_c:5.1f}c  "
              f"T60@C4 {meas:6.2f}s (target {t60:4.2f})  {'; '.join(why)}")
        if gate != "PASS":
            os.remove(ppath)
            os.remove(wpath)
    n_pass = sum(1 for r in results if r[1] == "PASS")
    print(f"\n{n_pass}/{len(results)} cells pass")


if __name__ == "__main__":
    main()
