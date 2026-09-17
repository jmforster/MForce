"""Mesh2D extension round 1 — Chafe's edge filters (campaign 70, stage B).

Chafe, "Extensions to the 2D Waveguide Mesh for Modeling Thin Plate
Vibrations" (ICSV26, 2019), on top of the validated STK Mesh2D port. STK
filters only two faces (x = 0, y = 0) with a one-pole; Chafe replaces that
with one 2nd-order ALLPASS per edge node,

    H(z) = (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2)
    a1 = -2 R cos(wc T),  a2 = R^2,  published fc = 1575 Hz, R = 0.75

— unity magnitude, phase only, so it detunes and stretches the mode set
without adding loss ("complex metallic timbres"). The find is the
SIGNAL-DEPENDENT version, a passive nonlinearity for free:
  * pie pan  r(n) = 0.75 + 0.2*x(n)              -> "bashed aluminum pie pan"
  * gong     r(n) = 0.75 - 0.5*x for x <= 0,
             0.75 + 0.003*x for x > 0 (Pierce)   -> modal upwelling

Engine side is one setting + two pins (edgeMode / edgeFc / edgeR); edgeMode 0
is byte-identical to the port, which tools/gen_stk_mesh2d.py proves. The
dynamic cells need no further engine code: a tap of the mesh output runs
through a CurveNode into edgeR. NOTE the approximation this makes — Chafe's
r(n) is per edge NODE, ours is one global R driven from one chosen tap. Same
mechanism, coarser spatial resolution; see the node header.

Geometries: plate 25x6 (Chafe's measured brass plate at 48 kHz, eq. 1) and
bar 12x3. One strike position per patch; score = 3 identical strikes 4 s
apart, so strikes 2 and 3 land on a still-ringing mesh (the dynamic cells
only differ from the static ones when there is signal at the edges).

Outputs: patches/audition/mesh2d_ext1/ + renders/dsp/audition/mesh2d_ext1/.
Usage: python tools/gen_mesh2d_ext1.py [--only cell]
"""
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_mesh2d import peaks, sine_stage  # noqa: E402
from gen_stk_bandedwg import curve, mul  # noqa: E402
from gen_stk_bowed import read_mono  # noqa: E402
from gen_bwg_perc1 import click_scan  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "audition", "mesh2d_ext1")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "mesh2d_ext1")

SR = 48000
BURST_HALF = 0.0005      # seconds per Sine stage; two stages = 1 ms
# Decay 0.99 (STK's default, and the port cases) rings for ~0.5 s: the mesh
# is silent long before the 4 s re-strike and the 2-3 s late window. 0.9998
# puts the plate at ~2e-2 rms at 3 s and still audible at 16 s, which is what
# a struck metal plate wants anyway. In edgeMode 1 this is the ONLY loss in
# the model (the allpass is unity-magnitude, the far faces reflect at unity),
# so the allpass cells ring longer than their controls by construction.
DECAY = 0.9998
OUT_GAIN = 0.8           # audio trim only; the tap into edgeR is pre-gain
N_STRIKES = 3
GAP = 4.0                # seconds between strikes
TAIL = 9.0               # ring headroom after the last strike
SECONDS = GAP * (N_STRIKES - 1) + TAIL

EDGE_FC = 1575.0         # Chafe, published
EDGE_R = 0.75            # Chafe, published
R_LO, R_HI = 0.4, 0.98   # clamp band for the dynamic cells

# geometry -> (cols NX, rows NY). Strike/tap held fixed across all cells so
# the four edge behaviours are the only difference within a geometry.
GEOM = {"plate": (25, 6), "bar": (12, 3)}
IN_XY = (0.3, 0.7)
OUT_XY = (1.0, 1.0)      # corner tap, STK's own listening point

# Strike amplitude per geometry. Chafe's r(n) = 0.75 + s*x(n) is DIMENSIONAL
# in x — s = 0.2 only swings R by +-0.2 if the mesh signal is order 1, and at
# the port's uniform 0.5 burst the plate peaks at 0.25 while the bar peaks at
# 0.61, so one drive would put the two geometries in different parts of the
# rule. These normalize both to a mesh peak of ~0.5.
#
# WHY 0.5 AND NOT 1.0 — the one real finding of this round. Normalized to a
# peak of 1.0 (the obvious reading of Chafe's constants), THREE of the four
# dynamic cells ran away: flat or rising rms from the first strike, then a
# hard clip at 1.5-2 s and silence. A time-varying allpass is not passive,
# and our GLOBAL edgeR modulates all NX+NY edge allpasses in lockstep from
# one tap — far more coherent energy injection than Chafe's per-node rule.
# At a peak of 0.5 the pie-pan rule (0.75 +- 0.1) runs entirely inside the
# [0.4, 0.98] clamp band, the gong rule only touches the clamp at the signal
# peak, and every cell decays. Drive is therefore the round's real safety
# knob, and "how hard can this boundary be struck" is an open axis.
DRIVE = {"plate": 1.0, "bar": 0.41}

# Analysis bands. NOT the pierce probe's 40-800 / 1500-6000: this mesh has no
# mode below its fundamental (660 Hz on the 25x6 plate, 1323 Hz on the 12x3
# bar), so a sub-800 Hz "low band" measures the excitation shoulder and
# nothing else. The denominator is the WHOLE in-band energy rather than a low
# band, for the same reason the pierce probe needed one at all: on the 12x3
# bar a 200-2000 Hz low band contains exactly one mode, and when that mode
# decays the ratio explodes on noise (measured: 704x on the control, which
# does not upwell). High-band FRACTION, late over early, says the thing the
# gong claim actually says — upper components gaining while the tone decays —
# with a denominator that cannot go to zero while the tone is audible.
# Split at 2 kHz: above the bar's fundamental (1323 Hz) and the plate's
# second mode (1318 Hz), so `hi` is genuinely "the upper modes".
F_LO, F_MID, F_HI = 200.0, 2000.0, 15000.0
# Below this high-band fraction there is no upper band to take a ratio OF —
# the two STK-edge controls sit at 3e-5 and 5e-5 — so the ratio is reported
# n/a rather than as a number made of two near-zero quantities.
UP_FLOOR = 1e-4
EARLY = (0.1, 0.6)       # relative to the FIRST strike
LATE = (2.0, 3.0)
PEAK_FLOOR_DB = -60.0    # mode-count proxy threshold
MIN_SEP = 10.0           # Hz between distinct peaks (gen_stk_mesh2d.top_peaks)


def piepan_knots():
    """r = 0.75 + 0.2*x, clamped to [R_LO, R_HI].

    Points mode clamps at the end knots, so putting the knots exactly where
    the line crosses the clamp band reproduces Chafe's affine rule inside the
    band and holds it flat outside — no extra nodes needed."""
    return [[round((R_LO - EDGE_R) / 0.2, 6), R_LO],
            [0.0, EDGE_R],
            [round((R_HI - EDGE_R) / 0.2, 6), R_HI]]


def gong_knots():
    """Pierce differential stiffness: r = 0.75 - 0.5*x for x <= 0 (i.e.
    0.75 + 0.5*|x|, stiffer on the negative half-cycle), r = 0.75 + 0.003*x
    for x > 0. Negative side hits R_HI at x = -0.46; the positive slope is so
    small that a knot at x = 10 carries it as far as the signal ever goes."""
    return [[round((EDGE_R - R_HI) / 0.5, 6), R_HI],
            [0.0, EDGE_R],
            [10.0, round(EDGE_R + 0.003 * 10.0, 6)]]


# cell suffix -> (edgeMode, dynamic-R knots or None)
EDGES = {
    "ctl":    (0, None),
    "ap":     (1, None),
    "piepan": (1, piepan_knots()),
    "gong":   (1, gong_knots()),
}
CELLS = {f"{g}_{e}": (g, e) for g in GEOM for e in EDGES}


def flat0(sec):
    return {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
            "percent": sec, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}


def burst_stages():
    """N_STRIKES 1 ms raised cosines, GAP apart, then the ring tail. The Sine
    ramp is start + range*(cos((1+t)*PI)+1)/2, so 0->1 then 1->0 over
    0.0005 s each IS the raised cosine (gen_stk_mesh2d's note)."""
    st = []
    for k in range(N_STRIKES):
        st.append(sine_stage(0.0, 1.0, BURST_HALF))
        st.append(sine_stage(1.0, 0.0, BURST_HALF))
        st.append(flat0(GAP - 2 * BURST_HALF if k < N_STRIKES - 1 else TAIL))
    return st


def make_patch(geom, edge):
    nx, ny = GEOM[geom]
    mode, knots = EDGES[edge]
    mesh = {"source": {"ref": "Burst"}, "cols": nx, "rows": ny,
            "inX": IN_XY[0], "inY": IN_XY[1],
            "outX": OUT_XY[0], "outY": OUT_XY[1],
            "decay": DECAY, "edgeMode": mode}
    if mode == 1:
        mesh["edgeFc"] = EDGE_FC
        mesh["edgeR"] = {"ref": "EdgeR"} if knots else EDGE_R
    nodes = [
        {"id": "Burst", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": DRIVE[geom],
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": burst_stages()}},
    ]
    if knots:
        # Chafe's signal-dependent R. The tap is a guarded read of the mesh
        # output one sample back, so the curve closes the loop without
        # advancing the mesh twice.
        nodes.append(curve("EdgeR", knots, {"tap": "Mesh"}))
    nodes.append({"id": "Mesh", "type": "Mesh2D", "params": mesh})
    nodes.append(mul("Out", {"ref": "Mesh"}, OUT_GAIN))
    return {
        "sampleRate": SR, "seconds": SECONDS,
        "instrument": {"polyphony": 1, "volume": 1.0},
        "score": [{"note": 60, "time": 0.0, "duration": SECONDS,
                   "velocity": 1.0}],
        "graph": {"output": "Out", "nodes": nodes},
    }


def band_ratio(x, sr, a_s, b_s):
    """Fraction of in-band energy above F_MID in one window."""
    seg = x[int(a_s * sr):int(b_s * sr)]
    if len(seg) < 16:
        return float("nan")
    seg = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    hi = sp[(fr > F_MID) & (fr < F_HI)].sum()
    tot = sp[(fr > F_LO) & (fr < F_HI)].sum()
    return hi / tot if tot > 0 else float("nan")


def mode_count(x, sr):
    """Distinct spectral peaks above PEAK_FLOOR_DB over the first 2 s. Uses
    gen_stk_mesh2d.peaks (parabolic, normalized to the strongest in-band
    peak) with the same MIN_SEP dedupe — a mesh mode is a lobe several bins
    wide, so raw local maxima count one mode a dozen times."""
    f, m = peaks(x[:int(2.0 * sr)], sr)
    if len(f) == 0:
        return 0
    thr = 10.0 ** (PEAK_FLOOR_DB / 20.0)
    kept = []
    for i in np.argsort(m)[::-1]:
        if m[i] < thr:
            break
        if any(abs(f[i] - g) < MIN_SEP for g in kept):
            continue
        kept.append(f[i])
    return len(kept)


def fmt_up(v):
    return "n/a" if v != v else "%.2f" % v


def rms(x):
    return float(np.sqrt((x ** 2).mean())) if len(x) else 0.0


def win_rms(x, sr, a_s, b_s):
    return rms(x[int(a_s * sr):int(b_s * sr)])


def level_ceiling(x, sr):
    """Sustained loudness is the speaker hazard, not momentary peak: no 0.5 s
    window may exceed rms 0.5 (run contract)."""
    win = int(0.5 * sr)
    nw = len(x) // win
    if nw == 0:
        return rms(x)
    w = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    return float(w.max())


README = """# Mesh2D extension round 1 — Chafe edge filters

Renders: `renders/dsp/audition/mesh2d_ext1/` (patches alongside in
`patches/audition/mesh2d_ext1/`). Generator: `tools/gen_mesh2d_ext1.py`.

The STK Mesh2D port (commit 74c81c3) filters only the x=0 and y=0 faces, with
a one-pole; the far faces reflect at unity. Chafe (ICSV26 2019) replaces that
one-pole with one 2nd-order **allpass** per edge node:

    H(z) = (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2)
    a1 = -2 R cos(wc T)      a2 = R^2      fc = 1575 Hz      R = 0.75

Unity magnitude, phase only — it detunes and stretches the mode set instead
of damping it. The published dynamic variants make R follow the signal:

| variant | rule | Chafe's word for it |
|---------|------|---------------------|
| pie pan | r(n) = 0.75 + 0.2 x(n) | "bashed aluminum pie pan" |
| gong (Pierce) | r(n) = 0.75 - 0.5 x for x <= 0, 0.75 + 0.003 x for x > 0 | gong-like modal upwelling |

Engine: `Mesh2D` gained a setting `edgeMode` (0 = STK one-pole, **default**,
byte-identical to the validated port; 1 = Chafe allpass) and two pins,
`edgeFc` (1575) and `edgeR` (0.75). The dynamic cells need no engine code —
a tap of the mesh output runs through a `CurveNode` into `edgeR`.

**Approximation to know about while listening:** Chafe's r(n) is per edge
NODE; ours is one global R driven from ONE tap (the output junction). Same
mechanism, coarser spatially. R is clamped to [0.4, 0.98] in the curves.

**Two knobs are ours, not Chafe's, and both mattered.** `decay` 0.99 (STK's
default) rings for half a second — nothing survives to the second strike — so
these use 0.9998; in edgeMode 1 that is the model's *only* loss, since the
allpass is unity-magnitude and the far faces reflect at unity. And strike
amplitude is normalised per geometry to a mesh peak of ~0.5 rather than the
port's fixed 0.5 burst, because Chafe's rule is dimensional in x: at the
port's drive the plate would see a quarter of the R swing the bar sees. At a
mesh peak of 1.0 — the obvious reading — three of the four dynamic cells ran
away and clipped, a global-R artefact (all edge allpasses modulating in
lockstep inject far more coherent energy than a per-node rule). Drive is the
safety knob here, and "how hard can this boundary be struck" is open.

## Cells

Two geometries, four edge behaviours each. Strike and tap are identical
within a geometry, so the edge filter is the only difference. Score is three
identical 1 ms raised-cosine strikes 4 s apart (strikes 2 and 3 land on a
still-ringing mesh — the dynamic cells only act where there is signal at the
edges), then 9 s of ring.

| cell | geometry | edge | what it tests |
|------|----------|------|---------------|
| `plate_ctl` / `bar_ctl` | 25x6 / 12x3 | STK one-pole | the reference the port validated |
| `plate_ap` / `bar_ap` | " | allpass, fc 1575, R 0.75 static | Chafe's published constants — mode stretching only |
| `plate_piepan` / `bar_piepan` | " | R = 0.75 + 0.2 x | signal-dependent detune: wobble, parallel time-varying mode tracks |
| `plate_gong` / `bar_gong` | " | Pierce sign rule | modal upwelling — components GROWING while the tone decays |

25x6 at 48 kHz is Chafe's own measured brass plate (68.5 x 17.8 x 0.4 cm,
c = 1333.5 m/s) via N = SR*len/c; 12x3 is the short bar from the port cases.

## Metrics

{table}

`early` / `late` = fraction of in-band (200 Hz-15 kHz) energy above 2 kHz, in
windows relative to the FIRST strike (0.1-0.6 s and 2.0-3.0 s). `upwell` =
late / early: above 1 means the upper modes are gaining on the fundamental
region while the tone decays, which is the gong claim. Both controls sit at
3e-5 to 5e-5 early — the STK one-pole leaves essentially nothing above 2 kHz
to take a ratio of — so their `upwell` is reported n/a rather than as a
number made of two near-zero quantities. `modes` = distinct spectral peaks
above -60 dB in 200 Hz-15 kHz over the first 2 s. `dec` = tail rms (15-16 s)
over post-last-strike rms (8.1-9.1 s); below 1 = decaying.

**What the numbers already say, before anyone listens.** The mode count is
the loud result: the STK edge leaves the bar with 2 modes within 60 dB (1323
Hz and one partner at -50 dB) and the plate with 4; the static allpass takes
those to 11 and 23, the dynamic rules further. The allpass also MOVES the
mode set down — the bar's strongest peak goes 1323 -> 853 Hz, the plate's 660
-> 518 Hz — because the allpass's phase delay lengthens the effective
boundary. That is Chafe's "detuning and ratio stretching" showing up exactly
as advertised, and it means the control and the extension cells are not the
same instrument at a different setting; they are different instruments.

## THE QUESTION

Two things to listen for, and they are separate:

1. **Which edge behaviours sound alive?** Static allpass vs pie-pan wobble vs
   gong upwelling — does the signal-dependent R read as a real material doing
   something, or as modulation sitting on top of a mesh? The gong cells in
   particular: does the upwelling the metric reports actually register as a
   gong swelling, or just as a bright tail?
2. **Plate or bar?** Which geometry direction deserves the first attempt at a
   real instrument out of this family.
"""


def main():
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    # The output dirs belong to THIS cell set: purge anything else (the
    # 2026-09-16 pierce-probe incident — stale runaway WAVs left sitting in
    # the audition queue next to the quiet new ones).
    keep = {f"mesh2d_ext1_{n}.json" for n in CELLS} | \
           {f"mesh2d_ext1_{n}.wav" for n in CELLS} | {"README.md"}
    for d in (PATCH_OUT, REND_OUT):
        for fn in os.listdir(d):
            if fn not in keep:
                os.remove(os.path.join(d, fn))
                print(f"purged stale: {fn}")

    hdr = ("%-14s %8s %8s %6s %7s %6s %6s %6s %6s  %s" %
           ("cell", "early", "late", "upwell", "modes", "dec",
            "rms", "peak", "winRms", "verdict"))
    print(hdr)
    rows = []
    for name, (geom, edge) in CELLS.items():
        if only and name != only:
            continue
        patch = make_patch(geom, edge)
        ppath = os.path.join(PATCH_OUT, f"mesh2d_ext1_{name}.json")
        wpath = os.path.join(REND_OUT, f"mesh2d_ext1_{name}.wav")
        with open(ppath, "w") as f:
            json.dump(patch, f, indent=1)
        r = subprocess.run([CLI, ppath, wpath], capture_output=True,
                           text=True, timeout=900)
        if r.returncode != 0 or not os.path.exists(wpath):
            print(f"{name:14s} RENDER_FAIL {(r.stderr or '')[-200:]}")
            if os.path.exists(ppath):
                os.remove(ppath)
            continue
        x, sr = read_mono(wpath)

        early = band_ratio(x, sr, *EARLY)
        late = band_ratio(x, sr, *LATE)
        up = late / early if early >= UP_FLOOR else float("nan")
        nmodes = mode_count(x, sr)
        post = win_rms(x, sr, GAP * (N_STRIKES - 1) + 0.1,
                       GAP * (N_STRIKES - 1) + 1.1)
        tail = win_rms(x, sr, SECONDS - 2.0, SECONDS - 1.0)
        dec = tail / post if post > 0 else float("nan")
        total = rms(x)
        peak = float(np.abs(x).max())
        wrms = level_ceiling(x, sr)
        clicks = click_scan(x, sr)

        bad = []
        if wrms > 0.5:
            bad.append(f"LEVEL {wrms:.2f}")
        if total < 1e-4:
            bad.append(f"SILENT {total:.2e}")
        if clicks:
            bad.append(f"clicks at {clicks[:3]}")
        verdict = "ok" if not bad else "CULLED: " + ", ".join(bad)
        if bad:
            os.remove(ppath)
            os.remove(wpath)
        else:
            rows.append((name, early, late, up, nmodes, dec, total, peak,
                         wrms))
        print("%-14s %8.2e %8.2e %6s %7d %6.3f %6.4f %6.3f %6.3f  %s"
              % (name, early, late, fmt_up(up), nmodes, dec, total, peak,
                 wrms, verdict), flush=True)

    if only:
        return
    tbl = ["| cell | early | late | upwell | modes | dec | rms | peak | winRms |",
           "|------|-------|------|--------|-------|-----|-----|------|--------|"]
    for r in rows:
        tbl.append("| `%s` | %.2e | %.2e | **%s** | %d | %.3f | %.4f | "
                   "%.3f | %.3f |" %
                   (r[0], r[1], r[2], fmt_up(r[3]), r[4], r[5], r[6], r[7],
                    r[8]))
    with open(os.path.join(REND_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{table}", "\n".join(tbl)))
    print(f"\nREADME -> {os.path.join(REND_OUT, 'README.md')}")


if __name__ == "__main__":
    main()
