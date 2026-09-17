"""Mesh2D extension round 2 — the REAL Pierce/Van Duyne passive nonlinearity.

Round 1 put Chafe's signal-dependent R on the mesh boundary by wiring a tap
through a CurveNode into the global `edgeR` pin. That is a TIME-VARYING
allpass, which is not passive: three of four dynamic cells ran away until the
drive was normalised to half scale and R was clamped, and Matt's verdict on
the audition was that the "variety range was as narrow as it could be".

This round replaces the stand-in with the filter the literature actually
specifies — Pierce & Van Duyne, JASA 101(2) 1120-1126 (1997), `edgeMode 2`,
one passive nonlinear allpass per edge node, each switching on its OWN state.
Structure, provenance and the measured passivity numbers:
docs/research/stk_port/PIERCE_PASSIVE_NOTES.md. Because it is passive it runs
at FULL drive with no clamp.

AXES, chosen to move audible furniture rather than decimal places:
  geometry   25x6 (Chafe's brass plate) / 12x3 bar / 48x48 big plate / 8x8 small
  strike     centre vs near-corner
  hardness   0.3 / 1 / 4 ms raised-cosine exciter
  decay      0.999 (short) vs 0.9998 (long)
  stiffness  the coefficient pair — symmetric +-0.5, hard +-0.9, and one
             asymmetric-MAGNITUDE pair (0.95, 0.1) where the energy-preserving
             rescale is actually doing work

Seven Pierce configurations, each paired with a matched CONTROL TWIN that
differs only in the edge filter (edgeMode 1, Chafe's static allpass) = 14
cells. Every Pierce cell differs from every other on >= 2 axes.

NEW GATE, aimed straight at round 1's failure: a cell only ships if it is
audibly different from its own control. High-band (>2 kHz) fraction of total
energy must differ by >= 3 percentage points, OR the top-10 spectral peak sets
(>= -40 dB) must differ in >= 3 peaks. Near-duplicates are culled with the
pair, and the cull is logged.

Levels: drive is 1.0 everywhere (the passivity claim under test). Output gain
is a LISTENING TRIM only, calibrated per pair in a first pass so both twins
land at the same sustained loudness — which is what makes the A/B fair. That
calibration is exact because `edgeMode 2` with constant coefficients is
homogeneous (see the README).

Outputs: patches/audition/mesh2d_ext2/ + renders/dsp/audition/mesh2d_ext2/.
Usage: python tools/gen_mesh2d_ext2.py [--only cell]
"""
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_mesh2d import peaks, sine_stage, top_peaks  # noqa: E402
from gen_stk_bandedwg import mul  # noqa: E402
from gen_stk_bowed import read_mono  # noqa: E402
from gen_bwg_perc1 import click_scan  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "audition", "mesh2d_ext2")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "mesh2d_ext2")

SR = 48000
DRIVE = 1.0              # full drive — the passivity claim under test
N_STRIKES = 3
GAP = 4.0
TAIL = 9.0
SECONDS = GAP * (N_STRIKES - 1) + TAIL

CAL_GAIN = 0.2           # first-pass probe gain
CAL_TARGET = 0.25        # target max 0.5 s-window rms (ceiling is 0.5)
GAIN_MIN, GAIN_MAX = 0.02, 6.0

EDGE_FC = 1575.0         # Chafe's published constants for the control twins
EDGE_R = 0.75

GEOM = {"plate25": (25, 6), "bar12": (12, 3),
        "big48": (48, 48), "small8": (8, 8)}
POS = {"ctr": (0.5, 0.5), "cnr": (0.06, 0.06)}
OUT_XY = (1.0, 1.0)      # corner tap everywhere, so the tap is not a confound
HARD = {"h03": 0.0003, "h1": 0.001, "h4": 0.004}   # raised-cosine total, s
DECAY = {"short": 0.999, "long": 0.9998}
STIFF = {"sym5": (0.5, -0.5), "sym9": (0.9, -0.9), "asym": (0.95, 0.1)}

# name -> (geom, strike, hardness, decay, stiffness).  Every pair differs on
# >= 2 axes; see the README table.
PIERCE = {
    "plate25_ctr_h1_long":  ("plate25", "ctr", "h1",  "long",  "sym5"),
    "plate25_cnr_h4_short": ("plate25", "cnr", "h4",  "short", "sym5"),
    "bar12_ctr_h03_short":  ("bar12",   "ctr", "h03", "short", "sym9"),
    "big48_ctr_h4_long":    ("big48",   "ctr", "h4",  "long",  "sym5"),
    "big48_cnr_h03_long":   ("big48",   "cnr", "h03", "long",  "sym9"),
    "small8_ctr_h1_short":  ("small8",  "ctr", "h1",  "short", "sym9"),
    "small8_cnr_h1_long":   ("small8",  "cnr", "h1",  "long",  "asym"),
}

# Feature-audibility gate (the round-1 failure)
F_LO, F_MID, F_HI = 200.0, 2000.0, 15000.0
HB_MIN_DELTA = 0.03      # 3 percentage points of high-band fraction
PEAK_N = 10
PEAK_FLOOR_DB = -40.0
PEAK_TOL = 0.01          # 1% in frequency counts as "the same peak"
PEAK_MIN_DIFF = 3
LEVEL_CEILING = 0.5
AUDIBILITY_FLOOR = 1e-4


def flat0(sec):
    return {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def burst_stages(half):
    """N_STRIKES raised cosines of total width 2*half, GAP apart, then the
    ring tail. The Sine ramp is start + range*(cos((1+t)*PI)+1)/2, so 0->1
    then 1->0 over `half` each IS the raised cosine (gen_stk_mesh2d)."""
    st = []
    for k in range(N_STRIKES):
        st.append(sine_stage(0.0, 1.0, half))
        st.append(sine_stage(1.0, 0.0, half))
        st.append(flat0(GAP - 2 * half if k < N_STRIKES - 1 else TAIL))
    return st


def make_patch(cfg, pierce, gain):
    geom, pos, hard, dec, stiff = cfg
    nx, ny = GEOM[geom]
    ix, iy = POS[pos]
    mesh = {"source": {"ref": "Burst"}, "cols": nx, "rows": ny,
            "inX": ix, "inY": iy, "outX": OUT_XY[0], "outY": OUT_XY[1],
            "decay": DECAY[dec], "edgeMode": 2 if pierce else 1}
    if pierce:
        cn, cp = STIFF[stiff]
        mesh["coefNeg"], mesh["coefPos"] = cn, cp
    else:
        mesh["edgeFc"], mesh["edgeR"] = EDGE_FC, EDGE_R
    nodes = [
        {"id": "Burst", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": DRIVE,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": burst_stages(HARD[hard] * 0.5)}},
        {"id": "Mesh", "type": "Mesh2D", "params": mesh},
        mul("Out", {"ref": "Mesh"}, round(gain, 6)),
    ]
    return {
        "sampleRate": SR, "seconds": SECONDS,
        "instrument": {"polyphony": 1, "volume": 1.0},
        "score": [{"note": 60, "time": 0.0, "duration": SECONDS,
                   "velocity": 1.0}],
        "graph": {"output": "Out", "nodes": nodes},
    }


def rms(x):
    return float(np.sqrt((x ** 2).mean())) if len(x) else 0.0


def level_ceiling(x, sr):
    win = int(0.5 * sr)
    nw = len(x) // win
    if nw == 0:
        return rms(x)
    w = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    return float(w.max())


def high_band_fraction(x, sr):
    """Fraction of in-band (200 Hz - 15 kHz) energy above 2 kHz, whole render.
    Gain-invariant, so the listening trim cannot move it."""
    seg = x * np.hanning(len(x))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    hi = sp[(fr > F_MID) & (fr < F_HI)].sum()
    tot = sp[(fr > F_LO) & (fr < F_HI)].sum()
    return float(hi / tot) if tot > 0 else float("nan")


def peak_set(x, sr):
    f, m = peaks(x, sr)
    if len(f) == 0:
        return []
    thr = 10.0 ** (PEAK_FLOOR_DB / 20.0)
    return [pf for pf, pm in top_peaks(f, m, PEAK_N) if pm >= thr]


def peak_mismatch(a, b):
    """How many of a's peaks have no partner within PEAK_TOL in b, taken
    symmetrically (the worse direction). Mode sets that have merely shifted
    slightly count as the same peak; genuinely different mode sets do not."""
    def miss(p, q):
        return sum(1 for f in p
                   if not any(abs(f - g) / f <= PEAK_TOL for g in q))
    return max(miss(a, b), miss(b, a))


def render(name, patch, quiet=False):
    ppath = os.path.join(PATCH_OUT, f"mesh2d_ext2_{name}.json")
    wpath = os.path.join(REND_OUT, f"mesh2d_ext2_{name}.wav")
    with open(ppath, "w") as f:
        json.dump(patch, f, indent=1)
    r = subprocess.run([CLI, ppath, wpath], capture_output=True, text=True,
                       timeout=1800)
    if r.returncode != 0 or not os.path.exists(wpath):
        if not quiet:
            print(f"{name:22s} RENDER_FAIL {(r.stderr or '')[-200:]}")
        return None, ppath, wpath
    x, sr = read_mono(wpath)
    return (x, sr), ppath, wpath


README = """# Mesh2D round 2 — the real Pierce/Van Duyne passive nonlinearity

Renders: `renders/dsp/audition/mesh2d_ext2/`; patches alongside in
`patches/audition/mesh2d_ext2/`. Generator: `tools/gen_mesh2d_ext2.py`.

## What changed since round 1

Round 1's "gong" cells were a **stand-in**: Chafe's signal-dependent reflection
coefficient, wired as one global `edgeR` pin driven from one tap through a
CurveNode. A time-varying allpass is not passive — three of four dynamic cells
ran away and had to be tamed with half drive and a clamp band — and the whole
round came back "variety range as narrow as it could be".

This round ships the filter the literature actually specifies. Pierce &
Van Duyne, *"A passive nonlinear digital filter design which facilitates
physics-based sound synthesis of highly nonlinear musical instruments"*,
JASA **101**(2) 1120-1126 (1997), as `Mesh2D` **`edgeMode 2`**: a first-order
allpass per edge node whose coefficient is a **spring stiffness** switched by
the sign of that node's own internal state —

    u(n)  = f_r(n) - a u(n-1)
    f_l(n) = a u(n) + u(n-1)
    a     = coefNeg  while u < 0        (the patent's a_1)
            coefPos  while u >= 0       (the patent's a_2)

Two consequences that matter for listening:

* **It is per-node.** Each edge node switches on its own displacement. Round
  1's global-R approximation is gone.
* **It is passive.** Measured worst cumulative output/input energy
  **{passivity}** over 48 open-loop probes (white-noise bursts, sweeps,
  full-scale square, impulse, DC; eight coefficient pairs) — gate `<= 1+1e-6`,
  run by `tools/engine_tests`. So there is no drive ceiling: every cell here is
  struck at **drive 1.0**, with no clamp, and none violates the level gate.

Full derivation and provenance, including the fact that the literal recurrence
printed in the patent is *not* passive in discrete time (it reaches 2.43) and
what had to be tightened: `docs/research/stk_port/PIERCE_PASSIVE_NOTES.md`.

## One property worth knowing before you listen

With constant coefficients this filter is **exactly homogeneous**: scaling the
excitation scales the output by the same factor and changes nothing else.
Measured, not just derived — `small8_cnr_h1_long` rendered at drive x0.25 and
x4.0 with the output gain compensated is **bit-identical** (max sample
difference 0.000e+00) to the drive x1.0 render. A
piecewise-linear spring has no amplitude scale, so *striking harder does not
change the timbre in `edgeMode 2`* — only the level. That is the real physics
of the paper, not a limitation of the port, and it is the opposite of Chafe's
`r = 0.75 + s*x` rule, which is dimensional in `x` and so was a drive-sensitive
safety problem in round 1. It is also why output gain here is purely a
listening trim: it is calibrated per pair so the two twins land at the same
sustained loudness, which makes the A/B honest.

If amplitude-dependent brightness is wanted, it has to come from somewhere
else — the exciter, or a coefficient pin driven by an envelope. That is an open
question, not a gap in this filter.

## Cells

Seven Pierce configurations, each with a **matched control twin** that differs
only in the edge filter (`edgeMode 1`, Chafe's static allpass at fc 1575 Hz,
R 0.75). Strike, tap, exciter, decay and drive are identical within a pair, so
the edge filter is the only variable. Score is three identical raised-cosine
strikes 4 s apart (strikes 2 and 3 land on a still-ringing mesh) plus 9 s of
ring; tap is the far corner throughout.

| cell | geometry | strike | exciter | decay | stiffness pair |
|------|----------|--------|---------|-------|----------------|
{cells}

`25x6` at 48 kHz is Chafe's measured brass plate; `12x3` the short bar from the
port cases; `48x48` is the mesh cap, a large plate; `8x8` a small one. `ctr` =
centre, `cnr` = near-corner. Stiffness pairs: `sym5` = (0.5, -0.5) — the
symmetric pair Faust ships as its own example; `sym9` = (0.9, -0.9), a much
harder switch; `asym` = (0.95, 0.1), asymmetric in *magnitude* as well as sign,
which is the case where the energy-preserving state carry is actually doing
work.

## Metrics and the audibility gate

{table}

`hbFrac` = fraction of in-band (200 Hz - 15 kHz) energy above 2 kHz, whole
render, gain-invariant. `dHB` = the Pierce cell's `hbFrac` minus its control's,
in percentage points. `dPeaks` = how many of the two cells' top-10 spectral
peaks (>= -40 dB) have no partner within 1% in the other, worse direction.
`modes` = size of that top-10 set. `winRms` = worst 0.5 s window rms (ceiling
0.5). `gain` = the per-pair listening trim.

**The gate this round adds**, because round 1 shipped near-duplicates: a Pierce
cell only survives if `|dHB| >= 3` percentage points **or** `dPeaks >= 3`.
Anything that fails is a near-duplicate of its own control and is culled along
with its twin.

{cull}

## THE QUESTION

The engine question is settled — the filter is passive and the mesh takes a
full-strength strike without a clamp. What is open is musical:

1. **Does the passive nonlinearity read as a material, or as a filter?** The
   control twin is the same mesh with a linear allpass boundary. Playing each
   pair back to back: is the Pierce cell a *different instrument*, or the same
   instrument with a tone control moved? That is the question round 1 could not
   answer because the variety was too narrow to hear.
2. **Which axis carries the variety?** Geometry, strike position, exciter
   hardness, decay and stiffness pair all move independently here. Which one
   actually changes what the thing *is*, and which are decimal places? That
   answer decides what round 3 sweeps.
"""


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv \
        else None
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    names = [n for k in PIERCE for n in (k, k + "_ctl")]
    keep = {f"mesh2d_ext2_{n}.json" for n in names} | \
           {f"mesh2d_ext2_{n}.wav" for n in names} | {"README.md"}
    for d in (PATCH_OUT, REND_OUT):
        for fn in os.listdir(d):
            if fn not in keep:
                os.remove(os.path.join(d, fn))
                print(f"purged stale: {fn}")

    print("%-22s %8s %8s %6s %7s %6s %7s %7s  %s"
          % ("cell", "hbFrac", "dHB(pp)", "dPeak", "modes", "gain", "winRms",
             "rms", "verdict"))
    rows, culls = [], []
    for base, cfg in PIERCE.items():
        if only and only not in (base, base + "_ctl"):
            continue
        ctl = base + "_ctl"

        # Pass 1: calibrate the listening trim. edgeMode 2 is homogeneous and
        # edgeMode 1 static is linear, so scaling the render is exact.
        cal = {}
        for nm, is_p in ((base, True), (ctl, False)):
            got, _, _ = render(nm, make_patch(cfg, is_p, CAL_GAIN), quiet=True)
            if got is None:
                break
            cal[nm] = level_ceiling(got[0], got[1])
        if len(cal) < 2:
            print(f"{base:22s} RENDER_FAIL in calibration pass")
            continue
        loudest = max(cal.values())
        gain = float(np.clip(CAL_GAIN * CAL_TARGET / max(loudest, 1e-9),
                             GAIN_MIN, GAIN_MAX))

        # Pass 2: the real renders, both twins at the same trim.
        out = {}
        for nm, is_p in ((base, True), (ctl, False)):
            got, pp, wp = render(nm, make_patch(cfg, is_p, gain))
            if got is None:
                break
            out[nm] = (got[0], got[1], pp, wp)
        if len(out) < 2:
            for _, _, pp, wp in out.values():
                for p in (pp, wp):
                    if os.path.exists(p):
                        os.remove(p)
            continue

        xp, sr = out[base][0], out[base][1]
        xc = out[ctl][0]
        hb_p, hb_c = high_band_fraction(xp, sr), high_band_fraction(xc, sr)
        pk_p, pk_c = peak_set(xp, sr), peak_set(xc, sr)
        d_hb = (hb_p - hb_c) * 100.0
        d_pk = peak_mismatch(pk_p, pk_c)

        bad = []
        for nm in (base, ctl):
            x = out[nm][0]
            wr = level_ceiling(x, sr)
            if wr > LEVEL_CEILING:
                bad.append(f"LEVEL {nm} {wr:.2f}")
            if rms(x) < AUDIBILITY_FLOOR:
                bad.append(f"SILENT {nm}")
            cl = click_scan(x, sr)
            if cl:
                bad.append(f"clicks {nm} at {cl[:3]}")
        if not bad and abs(d_hb) < HB_MIN_DELTA * 100.0 \
                and d_pk < PEAK_MIN_DIFF:
            bad.append(f"NEAR-DUPLICATE of control (dHB {d_hb:+.1f} pp, "
                       f"dPeaks {d_pk})")

        if bad:
            reason = ", ".join(bad)
            culls.append((base, reason))
            for nm in (base, ctl):
                for p in out[nm][2:4]:
                    if os.path.exists(p):
                        os.remove(p)
            print("%-22s %8.4f %8s %6d %7d %6.3f %7s %7s  CULLED: %s"
                  % (base, hb_p, "%+.1f" % d_hb, d_pk, len(pk_p), gain, "-",
                     "-", reason), flush=True)
            continue

        for nm, hb, pk in ((base, hb_p, pk_p), (ctl, hb_c, pk_c)):
            x = out[nm][0]
            rows.append((nm, cfg, hb, d_hb if nm == base else 0.0,
                         d_pk if nm == base else 0, len(pk), gain,
                         level_ceiling(x, sr), rms(x)))
            print("%-22s %8.4f %8s %6s %7d %6.3f %7.3f %7.4f  ok"
                  % (nm, hb, "%+.1f" % d_hb if nm == base else "",
                     str(d_pk) if nm == base else "", len(pk), gain,
                     level_ceiling(x, sr), rms(x)), flush=True)

    if only:
        return

    cells = []
    for base, (g, p, h, d, s) in PIERCE.items():
        if any(r[0] == base for r in rows):
            cells.append("| `%s` + `%s_ctl` | %dx%d | %s | %s | %s (%.4f) | "
                         "%s |" % (base, base, *GEOM[g],
                                   "centre" if p == "ctr" else "near-corner",
                                   {"h03": "0.3 ms", "h1": "1 ms",
                                    "h4": "4 ms"}[h],
                                   d, DECAY[d], "%s = (%.2f, %.2f)"
                                   % (s, *STIFF[s])))
    tbl = ["| cell | hbFrac | dHB (pp) | dPeaks | modes | gain | winRms | rms |",
           "|------|--------|----------|--------|-------|------|--------|-----|"]
    for r in rows:
        tbl.append("| `%s` | %.4f | %s | %s | %d | %.3f | %.3f | %.4f |"
                   % (r[0], r[2],
                      "**%+.1f**" % r[3] if r[0] in PIERCE else "",
                      "**%d**" % r[4] if r[0] in PIERCE else "",
                      r[5], r[6], r[7], r[8]))
    if culls:
        cull = ("**Culled this round** (near-duplicates and gate failures, "
                "removed from both dirs):\n\n"
                + "\n".join("* `%s` + twin — %s" % c for c in culls))
    else:
        cull = ("**Nothing was culled**: every Pierce cell cleared the "
                "audibility gate against its own control.")
    passivity = "1.000000019"
    with open(os.path.join(REND_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{table}", "\n".join(tbl))
                      .replace("{cells}", "\n".join(cells))
                      .replace("{cull}", cull)
                      .replace("{passivity}", passivity))
    print(f"\n{len(rows)} cells shipped, {len(culls)} pairs culled")
    print(f"README -> {os.path.join(REND_OUT, 'README.md')}")


if __name__ == "__main__":
    main()
