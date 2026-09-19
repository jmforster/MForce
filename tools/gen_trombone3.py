"""Trombone round 3 - the blowing-harder round (backlog 74a).

Lineage:
  tools/gen_trombone1.py                              the chassis
  tools/gen_trombone2.py                              the measured maps
  docs/autonomy/dsp/reports/2026-09-18-trombone2.md   what they measured
  docs/research/nonlinear_bore/MSALLAM_DIGEST.md      the physics of §2/§6
Report: docs/autonomy/dsp/reports/2026-09-18-trombone3.md

NOTE NAMES ARE THE HOUSE CONVENTION (octave = midi/12), the one the UI
keyboard shows.  Same as attempt 2.  Never going back.

WHY THIS ROUND EXISTS
---------------------
Matt's REVIEW 70 verdict: "Yes, I'd play it. Decent trombone in the lows and
trumpet in the highs... Line beats line_brighter. Nice mellow tone. Further
refinement possible, of course, especially the blowing harder thing."

The blowing-harder thing, measured in attempt 2: spectral centroid 473 Hz at
its soft setting and 477 Hz at its loud setting.  A real trombone opens up
enormously p -> ff; that is 1%.

MEASURED THIS ROUND, AND IT WAS NOT THE PHYSICS.  Two things were sitting on
the effect, and both of them were mine:

  1. THE STEEPENER WAS FED A PEAK-NORMALISED WAVE.  gen_trombone2.calibrate()
     set `in_gain = DRIVE_PEAK / peak(cell)` on the node feeding the
     steepener, and `out_gain = 1/in_gain` after it - PER CELL.  So the soft
     cell and the loud cell drove the nonlinearity to exactly the same peak
     (0.8) and came out with exactly the same distortion.  The one component
     in the whole instrument whose job is "brighter when louder" was being
     handed a signal with the loudness taken out.  Measured, with the
     normalisation removed: the steepener's contribution to the centroid runs
     +6 Hz at pp and +65 Hz at ff (midi 46), i.e. it is a dynamics component
     the moment it is allowed to see dynamics.
  2. THE WHOLE INSTRUMENT WAS PLAYING AT ff.  Attempt 2's soft/loud were 31
     and 50 kPa - a 1.6x window - because ONE global pressure had to keep
     G6 (ignition threshold 72 kPa) alive.  Measured note by note (stage
     `span`), A#3 speaks in tune from 2.7 kPa to 53 kPa, a 20:1 range, and
     attempt 2 sat at 40 kPa - near the top of it.  There was no "softer" to
     go to, for any note, because the top note owned the setting.

So: fixed drive gain (the steepener sees the raw travelling wave), and
per-note threshold-relative dynamics driven by NOTE VELOCITY, so every note
gets its own pp and its own ff and no note is held hostage by another.

THE LIP WAS NOT THE PROBLEM (work order 4, measured, no change made).  The
published mechanism (JASA'96 / MSALLAM_DIGEST §2) is that the lip closing
harder at forte steepens the mouthpiece pressure pulse, and that steepening
is what the bore turns into brass.  Measured here (stage `lip`, all figures
from the graph's own Lip and Pe nodes):

    note   closed at pp   closed at ff   rise rate pp -> ff
    F2         28%            30%          8873 -> 75753  /s   (9x)
    A#3        28%            31%           837 -> 23312  /s  (28x)
    F5         24%            38%           837 -> 44657  /s  (53x)

The valve shuts below -YEQ_HAT = -0.5 mm and it is shut for a quarter of
every cycle even at pp, so "the lip never reaches its clamp" was not the
problem and no lip parameter was touched.  The rise rate - JASA'96's
severity parameter - goes up by up to 53x across the dynamic range.  The
source side was doing its job the whole time; the normalisation downstream
was throwing the result away.

ONE ENGINE TRAP FOUND AND LOGGED, NOT FIXED (no engine code this round):
at ff the lip displacement sits on the engine's global +-8 value clamp
(core/dsp_value_source.h, "clamped to +-8 - so a runaway loop saturates")
for 53-69% of each cycle.  The negative rail is harmless, because the valve
is shut below -0.5 anyway; the POSITIVE one is not, because flow is
proportional to the opening - so the very top of this instrument's dynamic
range is being squared off by a safety clamp rather than by its own physics.

MEASUREMENT TRAP, WORTH KEEPING: a probe that renders an internal node is
multiplied by the engine's voice mix gain (`velocity * (1+boost) * volume`)
exactly like the real output.  Forgetting that reads a soft note's lip as
never closing at all - which is what the first pass of this round concluded
before the factor was checked against the closed form for Pm.

WHAT ELSE CHANGED (work order 1, Matt's standing directive: "if I caused
kludgy level-tweaking to be added due to my 'really soft' comments let's
remove that... we want the sound to be right, can always get bigger
speakers").  Attempt 2's TRIM_MAP flattened the register to EXACTLY 0.0 dB.
That is a studio move, not an instrument.  Decomposed (stage `trim`): of its
14.0 dB, a smooth register ramp accounts for 15.1 dB and note-to-note
scatter for +-4.9 dB, with 5.0 dB jumps between ADJACENT SEMITONES.  The
scatter is unambiguously model artifact - no instrument jumps 5 dB between
two neighbouring notes - and is kept in full.  The register ramp is at least
partly real (a bell radiates high frequencies better than low ones), so
TRIM_SLOPE_KEEP of it is applied and the rest is left in the signal.  The
trim sits where it sat: at the very end, AFTER the steepener and the bell,
and measured (stage `drive`) it does not move the steepener's input by one
part in a million.

The one gain this round ADDS is a velocity compensation, and it is named
rather than hidden: Instrument::prepare_voice_at multiplies a voice by
`velocity * (1 + boost) * volume` (engine/include/mforce/render/instrument.h),
so velocity is already a fader before it is anything else.  Since velocity is
now the breath, that fader would double-count the dynamics - measured, output
rms is exactly proportional to velocity with the patch ignoring it.  Vcomp
divides it back out, at the very output, so the loudness you hear is the
instrument's own.  Unity at velocity 0.8.

Outputs (this script OWNS these dirs and purges anything else):
  patches/sweep/trombone3/               the grid
  patches/audition/trombone1/            THE CANDIDATE (beside attempts 1, 2)
  renders/dsp/audition/trombone3/        <= 8 ears cells + README.md
Usage:
  python tools/gen_trombone3.py             full run (needs the solved maps)
  python tools/gen_trombone3.py span        solve per-note pressure spans
  python tools/gen_trombone3.py trim        decompose attempt 2's level trim
  python tools/gen_trombone3.py drive       work order 2: the steepener's tap
  python tools/gen_trombone3.py lip         work order 4: closure + rise rate
  python tools/gen_trombone3.py bright      THE METRIC: brightness vs dynamics
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_trombone1 as T1                                   # noqa: E402
import gen_trombone2 as T2                                   # noqa: E402
from gen_stk_bowed import read_mono                          # noqa: E402
from gen_nlbore_probe1 import ratio_knots                    # noqa: E402
from gen_nlbore_probe1 import centroid_all, hf_fraction      # noqa: E402
from gen_nlbore_probe1 import diff_db, level_ceiling, rms    # noqa: E402
from gen_onemass_lip1 import (add, expr, flow_nodes, midi_hz,  # noqa: E402
                              mulg, pts, B0, PM_TAB, P_REF, Q_LIP, SR,
                              F_REF, YEQ_HAT)

ROOT = T1.ROOT
CLI = T1.CLI
UI = T2.UI
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "trombone3")
CAND_OUT = os.path.join(ROOT, "patches", "audition", "trombone1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "trombone3")
SCRATCH = os.path.join(tempfile.gettempdir(), "trombone3_scratch")
CAND_NAME = "trombone_attempt3"

note_name = T2.note_name
sounding_rms = T2.sounding_rms
note_rows = T2.note_rows
fmt_table = T2.fmt_table

# ===========================================================================
# THE DRIVE (work order 2)
# ===========================================================================
# The steepener's input gain.  A CONSTANT, and 1.0 at that: NL_in carries the
# bore's own travelling wave at its own amplitude, which is the entire point.
# Attempt 2 put DRIVE_PEAK/peak(cell) here and 1/that after, per cell.
# Measured bore peaks with this gain: 0.04 at the softest playable pp, ~1.4
# at the loudest ff.  The steepener's ratio curve is linear over +-2 and
# clamps outside, so the top of the range stays inside its domain (gated).
DRIVE_GAIN = 1.0
# The steepener's transfer is ratio(x) = 1 - depth*x, written as three knots
# and CLAMPED outside them.  The probe module puts those knots at +-2, which
# was never tested because attempt 2's normalisation meant the drive never
# exceeded 0.8.  With the real travelling wave, F5 at ff reaches 3.2 - so the
# knots go out to +-4 at the IDENTICAL slope.  Inside +-2 the curve is
# unchanged to the digit; outside it, the law now still applies instead of
# flattening.  (1 - 0.03*4 = 0.88, still a positive delay ratio.)
DRIVE_DOMAIN = 4.0


def steep_knots(depth, x=DRIVE_DOMAIN):
    return [[-x, round(1.0 + depth * x, 9)], [0.0, 1.0],
            [x, round(1.0 - depth * x, 9)]]

# ===========================================================================
# LEVELLING (work order 1)
# ===========================================================================
# How much of attempt 2's smooth register ramp survives.  1.0 = attempt 2
# (flat to 0.0 dB), 0.0 = no register correction at all (the 15 dB ramp that
# buries the trombone register).  The note-to-note SCATTER is always kept in
# full - that part is unambiguous artifact.
TRIM_SLOPE_KEEP = 0.65
TRIM_FIT_DEG = 2

# ===========================================================================
# DYNAMICS (work order 3) - per note, relative to that note's own floor
# ===========================================================================
VEL_MF = 0.8               # the score/UI default: EXACTLY attempt 2's point
VEL_PP = 0.25              # velocity at which pp is reached (and held below)
VEL_FF = 1.0
PP_REL = 1.25              # pp sits this far over the note's own floor
FF_REL = 0.85              # ff sits this far under its own stable ceiling
SPAN_CENTS = 15.0          # inside the span a note must still be THIS in tune
SPAN_RMS = 0.0015          # ...and this audible (the trim leaves the low
                           # register a few dB down, so the floor is lower
                           # than T1.LOCK_RMS; lock and cents do the work)
SPAN_LOCK = 0.35           # ...and this periodic (T1.LOCK_RATIO)
# geometric, so the scan resolves the bottom of the range as well as the top
SPAN_SCAN = [round(0.04 * 1.35 ** i, 4) for i in range(19)]     # 0.04 .. 7.9
SPAN_NSEC, SPAN_SLOT = 1.0, 1.3

TUNE_LO, TUNE_HI = T2.TUNE_LO, T2.TUNE_HI        # midi 29..79, F2..G6
MF_SCALE = T2.MF_SCALE                           # 2.0, attempt 2's pressure
EARS_LINE = T2.EARS_LINE
LINE_SEC, LINE_GAP = T2.LINE_SEC, T2.LINE_GAP
HELD_NOTE = T2.HELD_NOTE
# the three notes the round is measured on (the dispatch's F2 / Bb3 / F5)
METRIC_NOTES = [29, 46, 65]

# solved by stage `span` and pasted back - MEASURED, never assumed.
# midi -> the blowing pressure (in the patch's own normalised units, x20 kPa)
# at which that note plays pp / ff.  Read the shape rather than the digits:
# the middle of the range speaks from 3 kPa to 45 kPa, and the two ends do
# not.  F#2 has essentially NO soft (its pp lands at 37 kPa against an mf of
# 40) and G6's pp is 92 kPa, louder than most of the instrument's ff.
# (midi 29's pp is the one that had to be walked: 1.01 fell into a narrow
# pressure hole the 1.35x scan grid stepped over; 1.16 ignited on a 1.0 s
# note and NOT on a 1.2 s one, sitting on the edge of the hole at +14 to
# +31 cents depending on note length; 1.33 is clean at every note length
# tested (0.6/1.0/1.2/1.4 s, +2 to +9 cents).  So F2's soft end is only
# 3.5 dB of breath below its normal - that note has almost no pp.)
PP_MAP = {29: 1.33, 30: 1.8322, 31: 0.434, 32: 0.3215, 33: 0.2243,
    34: 0.2243, 35: 0.2243, 36: 0.2243, 37: 0.2243, 38: 0.2243,
    39: 0.1873, 40: 0.1764, 41: 0.1661, 42: 0.1661, 43: 0.1661,
    44: 0.1661, 45: 0.1661, 46: 0.1661, 47: 0.1661, 48: 0.1661,
    49: 0.1661, 50: 0.1661, 51: 0.1661, 52: 0.1661, 53: 0.1661,
    54: 0.1661, 55: 0.1661, 56: 0.1661, 57: 0.1764, 58: 0.1873,
    59: 0.2243, 60: 0.2243, 61: 0.2243, 62: 0.2243, 63: 0.2243,
    64: 0.2243, 65: 0.2381, 66: 0.2528, 67: 0.3026, 68: 0.3214,
    69: 0.4086, 70: 0.4607, 71: 0.5516, 72: 0.7446, 73: 0.8396,
    74: 1.092, 75: 1.4064, 76: 2.0521, 77: 2.6402, 78: 3.8111,
    79: 4.5806}
FF_MAP = {29: 5.0549, 30: 4.8083, 31: 4.1383, 32: 4.1383, 33: 4.1383,
    34: 3.8972, 35: 3.6702, 36: 3.0654, 37: 3.0654, 38: 3.0654,
    39: 3.0654, 40: 3.0654, 41: 3.0654, 42: 3.0654, 43: 3.0654,
    44: 2.8868, 45: 2.7187, 46: 2.2707, 47: 2.2707, 48: 2.2707,
    49: 2.2707, 50: 2.2707, 51: 2.2707, 52: 2.2707, 53: 2.2707,
    54: 2.7187, 55: 2.8868, 56: 3.0654, 57: 3.0654, 58: 3.0654,
    59: 3.0654, 60: 3.6702, 61: 3.8972, 62: 4.1383, 63: 4.1383,
    64: 4.9548, 65: 5.2612, 66: 5.5867, 67: 5.5867, 68: 5.5867,
    69: 6.6889, 70: 7.1026, 71: 7.542, 72: 7.542, 73: 7.542,
    74: 7.542, 75: 7.542, 76: 8.447, 77: 9.6384, 78: 10.2479,
    79: 10.3458}

LEVEL_CEILING = T2.LEVEL_CEILING
AUDIBILITY_FLOOR = T2.AUDIBILITY_FLOOR
FEATURE_DB = T2.FEATURE_DB
PEAK_CEIL = T2.PEAK_CEIL
TARGET_TOL_DB = T2.TARGET_TOL_DB
LOCK_CENTS_GATE = T2.LOCK_CENTS_GATE
SPEAK_TARGET_C4 = T2.SPEAK_TARGET_C4


# ===========================================================================
# the level trim, rebuilt from attempt 2's measurement
# ===========================================================================
def decompose_trim(trim_map=None, deg=TRIM_FIT_DEG):
    """Split attempt 2's measured trim into a smooth register ramp and
    note-to-note scatter.  Returns (ns, db, smooth_db, scatter_db)."""
    tm = dict(T2.TRIM_MAP if trim_map is None else trim_map)
    ns = sorted(tm)
    db = np.array([20.0 * math.log10(max(tm[n], 1e-9)) for n in ns])
    coef = np.polyfit(np.array(ns, dtype=float), db, deg)
    smooth = np.polyval(coef, np.array(ns, dtype=float))
    return ns, db, smooth, db - smooth


def build_trim(keep=TRIM_SLOPE_KEEP):
    """The trim this round actually applies: all of the scatter, `keep` of
    the register ramp.  Normalised so the largest applied gain is 1.0, which
    only moves a constant into the cell's own output gain."""
    ns, db, smooth, scat = decompose_trim()
    applied = scat + keep * (smooth - smooth.max())
    applied -= applied.max()
    return {n: round(float(10.0 ** (a / 20.0)), 6)
            for n, a in zip(ns, applied)}


TRIM_MAP = build_trim()


# ===========================================================================
# graph
# ===========================================================================
def note_map_node(nid, m, fn=None):
    """A CurveNode points map from the played note (Hz) to a value.  NOT a
    tap on anything - attempt 1's wiring trap: a tap never advances its
    target, so a pin pulled through one reads a stale sample."""
    knots = sorted((midi_hz(n), (fn(n, v) if fn else v)) for n, v in m.items())
    return pts(nid, [(20.0, knots[0][1])] + knots + [(4000.0, knots[-1][1])],
               {"ref": "__perf_f"})


def dyn_nodes(pp_map, ff_map, press_map, scale):
    """Per-note, velocity-driven blowing pressure (work order 3).

        P(note, v) = P_mf(note) * (1 + Vdn(v)*Wdn(note) + Vup(v)*Wup(note))

    Vdn runs -1 at velocity VEL_PP to 0 at VEL_MF; Vup runs 0 at VEL_MF to
    +1 at VEL_FF.  Wdn/Wup are per-note maps solved from that note's OWN
    measured floor and ceiling, so the range's top note no longer sets the
    whole instrument's pp (attempt 2's cost, stated in its own report).
    At velocity VEL_MF both terms are zero and the pressure is exactly
    attempt 2's - which is why the tuning maps still hold and the regression
    gate can be met by construction rather than by luck.

    Two velocity curves rather than one because the two ends are independent:
    a note's floor and its ceiling are not the same distance from mf, and one
    global multiplier would again be pinned by the worst note at each end.
    """
    def mf_of(n):
        return scale * press_map.get(n, 1.0)
    wdn = {n: max(0.0, min(0.999, 1.0 - pp_map[n] / mf_of(n)))
           for n in pp_map if mf_of(n) > 0}
    wup = {n: max(0.0, ff_map.get(n, mf_of(n)) / mf_of(n) - 1.0)
           for n in pp_map if mf_of(n) > 0}
    return [
        {"id": "__perf_v", "type": "PerformNode",
         "params": {"field": "velocity"}},
        pts("Vdn", [(0.0, -1.0), (VEL_PP, -1.0), (VEL_MF, 0.0), (1.0, 0.0)],
            {"ref": "__perf_v"}),
        pts("Vup", [(0.0, 0.0), (VEL_MF, 0.0), (VEL_FF, 1.0)],
            {"ref": "__perf_v"}),
        note_map_node("Wdn", wdn),
        note_map_node("Wup", wup),
        mulg("Mdn", {"ref": "Vdn"}, {"ref": "Wdn"}),
        mulg("Mup", {"ref": "Vup"}, {"ref": "Wup"}),
        add("Dsum", {"ref": "Mdn"}, {"ref": "Mup"}),
        expr("DynF", 1.0, 1.0, "linear", {"ref": "Dsum"}),
    ], (wdn, wup)


def velcomp_node(nid):
    """Undo the engine's own velocity fader (instrument.h: a voice is mixed
    at `velocity * (1+boost) * volume`).  Velocity is the breath now, and the
    breath already changes the level through the physics; leaving the fader
    in would count the same dynamic twice.  Unity at VEL_MF, held flat below
    VEL_PP so a near-zero velocity still fades out."""
    vs = [0.0, VEL_PP] + [round(VEL_PP + 0.05 * i, 3)
                          for i in range(1, int((1.0 - VEL_PP) / 0.05) + 1)]
    knots = [(v, VEL_MF / max(v, VEL_PP)) for v in sorted(set(vs))]
    return pts(nid, knots, {"ref": "__perf_v"})


def voice_chain(fc, source):
    """Matt picked the 1100 Hz corner ("Line beats line_brighter"), so the
    voicing lowpass is unchanged from attempt 2: two cascaded SVF lowpasses
    at the very end of the chain, after the bell."""
    nodes, src = [], source
    for i in range(T2.VOICE_POLES):
        nid = "Voice%d" % i
        nodes.append({"id": nid, "type": "SVFSource", "params": {
            "cutoffFreq": round(float(fc), 4), "mode": 0,
            "normalize": False, "resonance": 0.5, "source": src}})
        src = {"ref": nid}
    return nodes, nodes[-1]["id"]


def make_patch(cfg):
    """Attempt 2's graph with three changes, all measured:
        NL_in gain        a CONSTANT (work order 2), not a per-cell normaliser
        Vdn..DynF         per-note velocity dynamics (work order 3)
        Rtrim             partial register trim (work order 1), still at the
                          very output, still after the steepener and the bell
    cfg keys are attempt 2's, plus:
        pp_map/ff_map   measured per-note pressure span ({} = no dynamics)
        drive_gain      fixed steepener input gain
        vel_comp        undo the engine's velocity fader
        probe           (node_id, scale) - render an INTERNAL node instead of
                        the output, for measurement only
    """
    ratio = cfg["ratio"]
    tune_map = cfg.get("tune_map") or {}
    trim_map = cfg.get("trim_map") or {}
    press_map = cfg.get("press_map") or {}
    pp_map = cfg.get("pp_map") or {}
    ff_map = cfg.get("ff_map") or {}
    lip_q = cfg.get("lip_q", Q_LIP)
    pexp = cfg.get("pm_exp", T1.PM_EXP)
    scale = cfg["scale"]
    a_pm = scale * (PM_TAB / P_REF) / F_REF ** pexp

    def press_at(f0):
        if not press_map:
            return 1.0
        ks = sorted(press_map)
        m = 12.0 * math.log2(max(f0, 1e-6) / 440.0) + 69.0
        if m <= ks[0]:
            return press_map[ks[0]]
        if m >= ks[-1]:
            return press_map[ks[-1]]
        for a, b in zip(ks[:-1], ks[1:]):
            if a <= m <= b:
                t = (m - a) / float(b - a)
                return press_map[a] * (1 - t) + press_map[b] * t
        return 1.0

    nodes = [
        {"id": "__perf_f", "type": "PerformNode",
         "params": {"field": "frequency"}},
        T1.flip_node(ratio, tune_map),
    ]
    if tune_map:
        nodes.append(T1.lipmap_node(
            "Frad", tune_map,
            lambda f0, fl: 1.0 - math.pi * fl / (lip_q * SR)))
        nodes.append(T1.lipmap_node(
            "Pmk", tune_map,
            lambda f0, fl: a_pm * press_at(f0) * fl ** pexp))
    else:
        nodes.append(expr("Frad", -math.pi * ratio / (lip_q * SR), 1.0,
                          "linear", {"ref": "__perf_f"}))
        nodes.append(expr("Pmk", a_pm * ratio ** pexp, pexp, "power",
                          {"ref": "__perf_f"}))
    weights = None
    if pp_map:
        dn, weights = dyn_nodes(pp_map, ff_map, press_map, scale)
        nodes += dn
        nodes.append(mulg("Pmd", {"ref": "Pmk"}, {"ref": "DynF"}))
        pm_src = {"ref": "Pmd"}
    else:
        pm_src = {"ref": "Pmk"}
    fast = cfg.get("release", "fast") == "fast"
    nodes.append(T1.mouth_env(fast, cfg.get("over_amt"), cfg.get("over_sec"),
                              cfg.get("att_sec")))
    nodes.append(mulg("Pm", pm_src, {"ref": "Mouth"}))
    if cfg.get("kick", 0.0):
        nodes.append(T1.one_sample_env("Kick"))
        nodes.append(add("PmK", {"ref": "Pm"}, {"ref": "Kick"},
                         cfg["kick"] * T1.KICK_UNIT - 1.0))
        force_src = {"ref": "PmK"}
    else:
        force_src = {"ref": "Pm"}
    nodes.append(add("Force", force_src, {"tap": "Pe"}, -2.0))
    nodes.append({"id": "Lip", "type": "Biquad", "params": {
        "source": {"ref": "Force"}, "mode": 1,
        "frequency": {"ref": "Flip"}, "radius": {"ref": "Frad"},
        "b0": round(B0, 12), "b1": 0.0, "b2": 0.0, "a1": 0.0, "a2": 0.0}})
    nodes += flow_nodes({"tap": "Bore"}, {"tap": "Bore"}, {"ref": "Lip"})
    nodes.append(add("Pplus", {"ref": "Pe"}, {"tap": "Bore"}, -2.0))
    nodes.append({"id": "Noise", "type": "WhiteNoiseSource", "params": {
        "amplitude": T1.NOISE_AMP, "boost": 0.0, "continuity": 0.0,
        "density": 1.0, "zeroCrossTendency": 0.0}})
    nodes.append(add("LoopIn", {"ref": "Pplus"}, {"ref": "Noise"}))
    nodes.append({"id": "DCblk", "type": "SVFSource", "params": {
        "cutoffFreq": T1.DC_HZ, "mode": 4, "normalize": False,
        "resonance": 0.7, "source": {"ref": "LoopIn"}}})
    if cfg.get("bell", True):
        chain, last = T1.biquad_chain("Refl", T1.REFL_SECS, {"ref": "DCblk"})
        nodes += chain
    else:
        nodes.append({"id": "Damp", "type": "SVFSource", "params": {
            "cutoffFreq": T1.LEGACY_DAMP_HZ, "mode": 0, "normalize": False,
            "resonance": 0.5, "source": {"ref": "DCblk"}}})
        last = "Damp"
    nodes.append(T1.bmode_node(cfg.get("bore_map")))
    nodes.append({"id": "Bore", "type": "DelayLine", "params": {
        "source": {"ref": last}, "frequency": {"ref": "__perf_f"},
        "ratio": {"ref": "Bmode"}, "amplitude": T1.LOOP_AMP,
        "compensate": True}})

    # --- out of loop: steepener, bell transmission, VOICING, TRIM, gain ----
    nodes.append(mulg("NL_in", {"ref": "Bore"},
                      round(float(cfg.get("drive_gain", DRIVE_GAIN)), 9)))
    depth = cfg.get("depth", T1.STEEP_DEPTH)
    nodes.append({"id": "NL_curve", "type": "CurveNode", "params": {
        "exprKnots": [], "interp": "linear",
        "knots": steep_knots(depth, cfg.get("steep_domain", DRIVE_DOMAIN)),
        "mode": "points", "source": {"tap": "NL_in"}}})
    nodes.append({"id": "NL_delay", "type": "DelayLine", "params": {
        "source": {"ref": "NL_in"}, "frequency": 120.0,
        "ratio": {"ref": "NL_curve"}, "amplitude": 1.0,
        "compensate": False}})
    mode = cfg.get("bell") if cfg.get("bell") in T1.TRANS_SECS else "room"
    chain, last = T1.biquad_chain("Trans", T1.TRANS_SECS[mode],
                                  {"ref": "NL_delay"})
    nodes += chain
    og = cfg.get("out_gain", 1.0) * 10.0 ** (T1.TRANS_MAKEUP[mode] / 20.0)

    if cfg.get("voice_k"):
        chain, last = voice_chain(cfg["voice_k"], {"ref": last})
        nodes += chain
    if trim_map:
        nodes.append(note_map_node("Rtrim", trim_map))
        nodes.append(mulg("Vtrim", {"ref": last}, {"ref": "Rtrim"}))
        last = "Vtrim"
    if cfg.get("vel_comp") and pp_map:
        nodes.append(velcomp_node("Vcomp"))
        nodes.append(mulg("Vcmul", {"ref": last}, {"ref": "Vcomp"}))
        last = "Vcmul"
    nodes.append(mulg("NL_out", {"ref": last}, round(float(og), 9)))
    out = "NL_out"

    probe = cfg.get("probe")
    if probe:
        # Render an internal node without breaking the pull that advances the
        # graph: Zero still consumes NL_out (so every node runs exactly as it
        # does in the real patch, verified bit-identical), and the tap reads
        # the sample just computed.  `scale` keeps the probe inside the
        # renderer's 16-bit range; the caller divides it back out.
        pid, pscale = probe
        nodes.append(mulg("Zero", {"ref": "NL_out"}, 0.0))
        nodes.append(add("Praw", {"ref": "Zero"}, {"tap": pid}, 0.0))
        nodes.append(mulg("Probe", {"ref": "Praw"}, round(float(pscale), 9)))
        out = "Probe"

    notes = cfg.get("notes", EARS_LINE)
    nsec = cfg.get("note_sec", LINE_SEC)
    slot = cfg.get("slot", LINE_SEC + LINE_GAP)
    vels = cfg.get("velocities")
    vel = cfg.get("velocity", VEL_MF)
    score = [{"note": n, "time": slot * k, "duration": nsec,
              "velocity": float(vels[k] if vels else vel)}
             for k, n in enumerate(notes)]
    pos, col, row = {}, 0, 0
    for nd in nodes:
        pos[nd["id"]] = [-1800.0 + 210.0 * col, -520.0 + 130.0 * row]
        row += 1
        if row == 6:
            row, col = 0, col + 1
    pos["__output"] = [0.0, 0.0]
    patch = {"sampleRate": SR, "seconds": slot * len(notes),
             "instrument": {"polyphony": 1, "volume": 0.7},
             "score": score,
             "ui": {"noteFaces": [{"label": "Lip pitch",
                                   "fields": {"frequency": "__perf_f"}}],
                    "positions": pos},
             "graph": {"output": out, "nodes": nodes}}
    if pp_map:
        patch["ui"]["noteFaces"][0]["fields"]["velocity"] = "__perf_v"
    if weights:
        patch["_weights"] = None
        del patch["_weights"]
    return patch


def base_cfg(**kw):
    cfg = {"ratio": T1.LIP_RATIO_BASE, "tune_map": dict(T2.TUNE_MAP),
           "bore_map": dict(T2.BORE_MAP), "trim_map": dict(TRIM_MAP),
           "press_map": dict(T2.PRESS_MAP),
           "pp_map": dict(PP_MAP), "ff_map": dict(FF_MAP),
           "scale": MF_SCALE, "pm_exp": T1.PM_EXP, "bell": True,
           "kick": 1.0, "release": "fast", "depth": T1.STEEP_DEPTH,
           "voice_k": T2.VOICE_DARK, "lip_q": Q_LIP,
           "drive_gain": DRIVE_GAIN, "vel_comp": True,
           "velocity": VEL_MF, "out_gain": 1.0}
    cfg.update(kw)
    return cfg


def a2_cfg(**kw):
    """Attempt 2's configuration, for the A/B and the before column: its
    full flattening trim, no velocity dynamics, and (set by the caller) its
    per-cell normalised drive."""
    cfg = base_cfg(trim_map=dict(T2.TRIM_MAP), pp_map={}, ff_map={},
                   vel_comp=False, steep_domain=2.0)
    cfg.update(kw)
    return cfg


# ===========================================================================
# render + measure
# ===========================================================================
def render(patch, tag, out_dir, timeout=900):
    pj = os.path.join(PATCH_OUT, tag + ".json")
    wp = os.path.join(out_dir, tag + ".wav")
    os.makedirs(os.path.dirname(pj), exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    with open(pj, "w") as f:
        json.dump(patch, f, indent=1)
    r = subprocess.run([CLI, pj, wp], capture_output=True, text=True,
                       timeout=timeout)
    if r.returncode != 0 or not os.path.exists(wp):
        print("  RENDER_FAIL %s rc=%s %s"
              % (tag, r.returncode, (r.stderr or r.stdout or "")[-300:]))
        return None, pj, wp
    return read_mono(wp), pj, wp


def render_score(cfg, notes, nsec, gap, tag, out_dir, **kw):
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = notes, nsec, nsec + gap
    c.update(kw)
    return render(make_patch(c), tag, out_dir)


def probe_signal(cfg, node, scale, tag="probe"):
    """One internal node's waveform, as a numpy array in ITS OWN units.

    Two gains have to come back out.  `scale` is the one the probe itself
    applies to stay inside the renderer's 16-bit range.  The other is the
    ENGINE's: Instrument::prepare_voice_at mixes a voice at
    `velocity * (1+boost) * volume`, and that multiplies the probe exactly as
    it multiplies the real output.  Verified against the closed form for Pm
    (= S * sqrt(f_lip/F_REF)): measured/closed-form is 0.1746 at velocity
    0.25, 0.5599 at 0.80 and 0.7000 at 1.00, i.e. velocity x volume to four
    digits.  Forgetting this reads a soft note's lip as never closing.
    """
    c = dict(cfg)
    c["probe"] = (node, scale)
    got, pj, wp = render(make_patch(c), tag, SCRATCH)
    if got is None:
        return None, None
    T1.rm(pj, wp)
    vol = float(c.get("volume", 0.7))
    vel = float(c.get("velocity", VEL_MF))
    return got[0] / (float(scale) * max(vel * vol, 1e-9)), got[1]


def survey(cfg, notes, tag, nsec=1.4, gap=0.4, chunk=8, **kw):
    rows = {}
    for i in range(0, len(notes), chunk):
        grp = notes[i:i + chunk]
        got, pj, wp = render_score(cfg, grp, nsec, gap, "%s_%d" % (tag, i),
                                   SCRATCH, **kw)
        if got is None:
            continue
        rows.update(note_rows(got[0], got[1], grp, nsec + gap, nsec))
        T1.rm(pj, wp)
    return rows


def one_note(cfg, n, tag="one", nsec=SPAN_NSEC, slot=SPAN_SLOT, **kw):
    """A single note in its own render - the only honest way to measure a
    threshold, because a note rendered after another one inherits the bore's
    leftover energy and starts on it (attempt 2 §2)."""
    got, pj, wp = render_score(cfg, [n], nsec, slot - nsec, tag, SCRATCH, **kw)
    if got is None:
        return None
    x, sr = got
    row = note_rows(x, sr, [n], slot, nsec)[n]
    T1.rm(pj, wp)
    return row


def speaks(row):
    return bool(row and np.isfinite(row["cents"])
                and abs(row["cents"]) <= SPAN_CENTS
                and row["lock"] > SPAN_LOCK and row["rms"] > SPAN_RMS)


# ===========================================================================
# stage: trim decomposition (work order 1)
# ===========================================================================
def stage_trim():
    ns, db, smooth, scat = decompose_trim()
    print("=== attempt 2's level trim, decomposed ===")
    print("  applied range                  %5.1f dB" % (db.max() - db.min()))
    print("  smooth register ramp           %5.1f dB" % (smooth.max()
                                                         - smooth.min()))
    print("  note-to-note scatter           +-%.1f dB (rms %.2f)"
          % (np.abs(scat).max(), scat.std()))
    print("  biggest jump between ADJACENT semitones:")
    print("    in the applied trim          %5.1f dB"
          % np.abs(np.diff(db)).max())
    print("    in the smooth ramp alone     %5.2f dB"
          % np.abs(np.diff(smooth)).max())
    print("  keeping scatter in full and %.2f of the ramp -> residual "
          "register spread %.1f dB"
          % (TRIM_SLOPE_KEEP,
             (1.0 - TRIM_SLOPE_KEEP) * (smooth.max() - smooth.min())))
    print("\nTRIM_MAP (this round) = %s" % json.dumps(TRIM_MAP))
    return 0


# ===========================================================================
# stage: the steepener's tap point (work order 2)
# ===========================================================================
def stage_drive():
    print("=== work order 2: what the steepener actually sees ===")
    print("\n  (a) does the output trim touch the steepener's input?")
    rows = []
    for tm, lbl in ((dict(TRIM_MAP), "trim on"), ({}, "trim off")):
        cfg = base_cfg(scale=MF_SCALE, notes=[46], note_sec=1.4, slot=1.8,
                       trim_map=tm)
        x, sr = probe_signal(cfg, "NL_in", 0.2, "d_trim")
        seg = T1.seg_of(x, sr, 0, 1.8, 1.4)
        rows.append((lbl, float(np.abs(seg).max()), float(rms(seg))))
        print("      %-9s NL_in peak %.6f  rms %.6f" % rows[-1])
    same = (abs(rows[0][1] - rows[1][1]) < 1e-6
            and abs(rows[0][2] - rows[1][2]) < 1e-6)
    print("      -> the trim is downstream of the nonlinearity: %s"
          % ("CONFIRMED" if same else "NO - IT IS UPSTREAM, MOVE IT"))

    print("\n  (b) steepener drive vs blowing pressure, attempt 2's "
          "per-cell normalisation vs this round's fixed gain")
    print("      %6s | %12s %12s | %12s" % ("S", "raw bore pk", "a2 NL_in pk",
                                            "a3 NL_in pk"))
    drive = []
    for S in (0.2, 0.5, 1.0, 2.0, 3.0):
        cfg = base_cfg(scale=S, notes=[46], note_sec=1.4, slot=1.8,
                       pp_map={}, ff_map={}, vel_comp=False, drive_gain=1.0)
        x, sr = probe_signal(cfg, "NL_in", 0.2, "d_raw")
        raw = float(np.abs(T1.seg_of(x, sr, 0, 1.8, 1.4)).max())
        a2 = T1.DRIVE_PEAK          # attempt 2 normalised EVERY cell to this
        drive.append({"S": S, "raw_peak": raw, "a2_peak": a2, "a3_peak": raw})
        print("      %6.2f | %12.4f %12.4f | %12.4f" % (S, raw, a2, raw))
    lo, hi = drive[0]["raw_peak"], drive[-1]["raw_peak"]
    print("      -> attempt 2 drove the steepener at a CONSTANT %.2f peak at "
          "every dynamic;\n         this round's drive spans %.1f dB over the "
          "same pressures." % (T1.DRIVE_PEAK, 20 * math.log10(hi / max(lo,
                                                                       1e-9))))

    print("\n  (c) what the steepener contributes, with and without drive")
    print("      %6s | %9s %9s %7s | %8s %8s" % ("S", "cent off", "cent on",
                                                 "delta", "hf off", "hf on"))
    contrib = []
    for S in (0.2, 0.5, 1.0, 2.0, 3.0):
        vals = []
        for d in (0.0, T1.STEEP_DEPTH):
            cfg = base_cfg(scale=S, notes=[46], note_sec=1.4, slot=1.8,
                           depth=d, pp_map={}, ff_map={}, vel_comp=False)
            got, pj, wp = render(make_patch(cfg), "d_abl", SCRATCH)
            if got is None:
                vals.append((float("nan"), float("nan")))
                continue
            seg = T1.seg_of(got[0], got[1], 0, 1.8, 1.4)
            vals.append((centroid_all(seg, got[1]),
                         hf_fraction(seg, got[1], 1000.0)))
            T1.rm(pj, wp)
        contrib.append({"S": S, "off": vals[0], "on": vals[1]})
        print("      %6.2f | %9.0f %9.0f %+7.0f | %8.4f %8.4f"
              % (S, vals[0][0], vals[1][0], vals[1][0] - vals[0][0],
                 vals[0][1], vals[1][1]))
    json.dump({"trim_invariance": rows, "drive": drive, "steepener": contrib},
              open(os.path.join(SCRATCH, "drive.json"), "w"), indent=1,
              default=float)
    return 0


# ===========================================================================
# stage: lip closure (work order 4)
# ===========================================================================
def lip_stats(cfg, n, nsec=1.4, slot=1.8):
    """Closed fraction and peak rise rate at the source, from the graph's own
    internal nodes.  `Lip` is displacement from equilibrium in units of
    Y_REF = 1 mm, and the valve shuts when Lip + YEQ_HAT <= 0 (the Qn knot).
    `Pe` is the mouthpiece pressure in units of P_REF = 20 kPa."""
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = [n], nsec, slot
    lip, sr = probe_signal(c, "Lip", 0.05, "l_lip")
    pe, _ = probe_signal(c, "Pe", 0.05, "l_pe")
    if lip is None or pe is None:
        return None
    L = T1.seg_of(lip, sr, 0, slot, nsec)
    P = T1.seg_of(pe, sr, 0, slot, nsec)
    opening = L + YEQ_HAT
    return {"closed_frac": float((opening <= 0.0).mean()),
            "lip_min": float(L.min()), "open_max": float(opening.max()),
            # the ENGINE saturates every dsp value at +-8 (core/
            # dsp_value_source.h: "clamped to +-8 - so a runaway loop
            # saturates").  The negative rail is harmless here because the
            # valve is shut below -YEQ_HAT anyway; the POSITIVE one is not,
            # because flow is proportional to the opening.
            "rail_open": float((L >= 7.999).mean()),
            "rail_shut": float((L <= -7.999).mean()),
            "rise_rate": float(np.abs(np.diff(P)).max() * sr),
            "pe_pp": float(P.max() - P.min())}


def stage_lip():
    if not PP_MAP:
        print("solve the pressure spans first (stage `span`)")
        return 1
    print("=== work order 4: does the lip close harder at forte? ===")
    print("  Lip is displacement in mm from equilibrium; the valve shuts at")
    print("  -%.2f mm.  Rise rate is JASA'96's severity parameter (digest "
          "§2)." % YEQ_HAT)
    print("  %4s %-5s %-3s %7s | %7s %8s %8s %11s %8s"
          % ("midi", "note", "dyn", "kPa", "closed%", "open max", "Pe p-p",
             "rise rate/s", "rail%"))
    out = {}
    for n in METRIC_NOTES:
        row = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            cfg = base_cfg(velocity=v)
            st = lip_stats(cfg, n)
            if st is None:
                continue
            st["kPa"] = dyn_pressure(n, v) * PM_TAB / 1000.0
            row[tag] = st
            print("  %4d %-5s %-3s %7.1f | %7.1f %8.3f %8.3f %11.0f %8.1f"
                  % (n, note_name(n), tag, st["kPa"], 100 * st["closed_frac"],
                     st["open_max"], st["pe_pp"], st["rise_rate"],
                     100 * st["rail_open"]))
        out[n] = row
    json.dump(out, open(os.path.join(SCRATCH, "lip.json"), "w"), indent=1,
              default=float)
    return 0


def dyn_pressure(n, vel):
    """The blowing pressure S the patch will use for this note at this
    velocity - the python twin of the Vdn/Vup/Wdn/Wup subgraph."""
    mf = MF_SCALE * T2.PRESS_MAP.get(n, 1.0)
    if not PP_MAP or n not in PP_MAP:
        return mf
    wdn = max(0.0, min(0.999, 1.0 - PP_MAP[n] / mf))
    wup = max(0.0, FF_MAP.get(n, mf) / mf - 1.0)
    if vel <= VEL_PP:
        vdn, vup = -1.0, 0.0
    elif vel <= VEL_MF:
        vdn, vup = -(VEL_MF - vel) / (VEL_MF - VEL_PP), 0.0
    else:
        vdn, vup = 0.0, min(1.0, (vel - VEL_MF) / (VEL_FF - VEL_MF))
    return mf * (1.0 + vdn * wdn + vup * wup)


# ===========================================================================
# stage: the per-note pressure span (work order 3)
# ===========================================================================
def solve_span(notes, verbose=True):
    """For every note: scan blowing pressure from silence, find the
    CONTIGUOUS run of pressures containing its mf point where it speaks in
    tune, and take that run's ends as the note's own floor and ceiling.

    Contiguity matters and is not a detail: midi 29 speaks at S 0.08-0.7,
    goes silent at 1.0, and speaks again from 1.4 up.  A plain min/max would
    hand it a pp two octaves of pressure below a hole it falls into.
    """
    print("=== per-note pressure span, each note alone from silence (%d) ==="
          % len(notes))
    pp, ff, log = {}, {}, {}
    for n in notes:
        # EVERYTHING here is in ACTUAL pressure units (what the lip sees):
        # the patch's cfg["scale"] is multiplied by the note's own breath-
        # support entry, so a scan point S means S * PRESS_MAP[n].
        sup = T2.PRESS_MAP.get(n, 1.0)
        mf = MF_SCALE * sup
        ok, rows = {}, {}
        for S in SPAN_SCAN:
            r = one_note(base_cfg(scale=S, pp_map={}, ff_map={},
                                  vel_comp=False), n, "span")
            ok[S] = speaks(r)
            rows[S] = ({"cents": r["cents"], "rms": r["rms"],
                        "lock": r["lock"]} if r else None)
        # the contiguous run containing the mf pressure, in actual units
        act = [S * sup for S in SPAN_SCAN]
        idx = [i for i, S in enumerate(SPAN_SCAN) if ok[S]]
        run = None
        if idx:
            runs, a = [], idx[0]
            for i, j in zip(idx[:-1], idx[1:]):
                if j != i + 1:
                    runs.append((a, i))
                    a = j
            runs.append((a, idx[-1]))
            best = None
            for lo, hi in runs:
                if act[lo] <= mf <= act[hi]:
                    best = (lo, hi)
                    break
            if best is None:                 # mf sits in a hole: widest run
                best = max(runs, key=lambda t: t[1] - t[0])
            run = best
        if run is None:
            pp[n], ff[n] = round(mf, 4), round(mf, 4)
            log[n] = {"floor": None, "ceiling": None, "mf": mf, "rows": rows}
            if verbose:
                print("  %3d %-4s  NEVER SPEAKS in the scan - pinned at mf "
                      "%.2f" % (n, note_name(n), mf))
            continue
        floor, ceil = act[run[0]], act[run[1]]
        pp[n] = round(min(mf, max(floor, floor * PP_REL)), 4)
        ff[n] = round(max(mf, ceil * FF_REL), 4)
        log[n] = {"floor": floor, "ceiling": ceil, "mf": mf,
                  "pp": pp[n], "ff": ff[n], "rows": rows}
        if verbose:
            print("  %3d %-4s  speaks %6.2f..%6.2f (%5.1f..%5.1f kPa, %4.1f "
                  "dB of span)  mf %5.2f -> pp %5.2f ff %5.2f"
                  % (n, note_name(n), floor, ceil, floor * PM_TAB / 1000,
                     ceil * PM_TAB / 1000, 20 * math.log10(ceil / floor),
                     mf, pp[n], ff[n]))
    return pp, ff, log


def smooth_spans(pp, ff, log, win=5):
    """The scan grid steps by 1.35x - 2.6 dB - so a measured ceiling is only
    known to within one step, and neighbouring notes can land on different
    steps for no physical reason.  A running geometric mean over `win` notes
    takes that quantisation out; the result is then clamped BACK inside each
    note's own measured floor/ceiling so smoothing can never push a note
    past a limit that was actually measured on it.
    """
    ns = sorted(pp)
    out_pp, out_ff = {}, {}
    for i, n in enumerate(ns):
        lo, hi = max(0, i - win // 2), min(len(ns), i + win // 2 + 1)
        grp = ns[lo:hi]
        gm_pp = math.exp(sum(math.log(max(pp[m], 1e-6)) for m in grp)
                         / len(grp))
        gm_ff = math.exp(sum(math.log(max(ff[m], 1e-6)) for m in grp)
                         / len(grp))
        L = log.get(n, {})
        mf = L.get("mf", MF_SCALE * T2.PRESS_MAP.get(n, 1.0))
        fl = L.get("floor") or pp[n]
        cl = L.get("ceiling") or ff[n]
        out_pp[n] = round(min(mf, max(fl * PP_REL, gm_pp)), 4)
        out_ff[n] = round(max(mf, min(cl * FF_REL, gm_ff)), 4)
    return out_pp, out_ff


def validate_spans(pp, ff, verbose=True):
    """Render every note AT its solved pp and ff and check it actually
    speaks there.  The scan grid is coarse (1.35x a step) and this model has
    narrow holes in pressure - midi 29 speaks at S 0.80 and at 1.09 and is
    DEAD at 1.00, which is exactly where a floor x PP_REL landed.  A derived
    value that was never rendered is a guess, so each end is walked toward mf
    until it plays.
    """
    fixed_pp, fixed_ff, moved = dict(pp), dict(ff), []
    for n in sorted(pp):
        mf = MF_SCALE * T2.PRESS_MAP.get(n, 1.0)
        for k in range(9):
            if speaks(one_note(base_cfg(scale=fixed_pp[n] /
                                        T2.PRESS_MAP.get(n, 1.0),
                                        pp_map={}, ff_map={},
                                        vel_comp=False), n, "vpp")):
                break
            nxt = min(mf, fixed_pp[n] * 1.15)
            if nxt <= fixed_pp[n]:
                break
            fixed_pp[n] = round(nxt, 4)
        for k in range(9):
            if speaks(one_note(base_cfg(scale=fixed_ff[n] /
                                        T2.PRESS_MAP.get(n, 1.0),
                                        pp_map={}, ff_map={},
                                        vel_comp=False), n, "vff")):
                break
            nxt = max(mf, fixed_ff[n] * 0.9)
            if nxt >= fixed_ff[n]:
                break
            fixed_ff[n] = round(nxt, 4)
        if fixed_pp[n] != pp[n] or fixed_ff[n] != ff[n]:
            moved.append((n, pp[n], fixed_pp[n], ff[n], fixed_ff[n]))
            if verbose:
                print("    %3d %-4s  pp %6.2f -> %6.2f   ff %6.2f -> %6.2f"
                      % (n, note_name(n), pp[n], fixed_pp[n], ff[n],
                         fixed_ff[n]))
    if verbose and not moved:
        print("    every solved pp and ff speaks as rendered - nothing moved")
    return fixed_pp, fixed_ff, moved


def stage_span():
    notes = list(range(TUNE_LO, TUNE_HI + 1))
    raw_pp, raw_ff, log = solve_span(notes)
    pp, ff = smooth_spans(raw_pp, raw_ff, log)
    print("\n  after smoothing (clamped back inside each note's own "
          "measured limits):")
    for n in notes:
        print("    %3d %-4s  pp %6.2f -> %6.2f   ff %6.2f -> %6.2f"
              % (n, note_name(n), raw_pp[n], pp[n], raw_ff[n], ff[n]))
    print("\n  validating every solved end by rendering it:")
    pp, ff, moved = validate_spans(pp, ff)
    print("\nPP_MAP = %s" % json.dumps(pp))
    print("\nFF_MAP = %s" % json.dumps(ff))
    json.dump({"pp": pp, "ff": ff, "raw_pp": raw_pp, "raw_ff": raw_ff,
               "log": log},
              open(os.path.join(SCRATCH, "span.json"), "w"), indent=1,
              default=float)
    return 0


# ===========================================================================
# stage: THE METRIC (brightness vs dynamics)
# ===========================================================================
def bright_row(cfg, n, nsec=1.4, slot=1.8, norm_drive=False, **kw):
    """Centroid and >1 kHz share for one note, optionally with attempt 2's
    per-cell peak normalisation into the steepener reproduced exactly."""
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = [n], nsec, slot
    c.update(kw)
    if norm_drive:
        probe = dict(c)
        probe["drive_gain"] = 1.0
        x, sr = probe_signal(probe, "NL_in", 0.2, "b_norm")
        if x is None:
            return None
        pk = float(np.abs(T1.seg_of(x, sr, 0, slot, nsec)).max())
        c["drive_gain"] = T1.DRIVE_PEAK / max(pk, 1e-9)
        c["out_gain"] = c.get("out_gain", 1.0) * max(pk, 1e-9) / T1.DRIVE_PEAK
    got, pj, wp = render(make_patch(c), "bright", SCRATCH)
    if got is None:
        return None
    x, sr = got
    seg = T1.seg_of(x, sr, 0, slot, nsec)
    row = note_rows(x, sr, [n], slot, nsec)[n]
    T1.rm(pj, wp)
    return {"centroid": centroid_all(seg, sr),
            "hf1k": hf_fraction(seg, sr, 1000.0),
            "rms": float(rms(seg)), "cents": row["cents"],
            "lock": row["lock"]}


def stage_bright(quiet=False):
    """THE round's metric.  Three configurations on the same three notes:
      a2   attempt 2 as shipped - one global soft/loud pair, per-cell peak
           normalisation into the steepener
      a3n  this round's per-note span, but WITH attempt 2's normalisation
           (isolates the span fix)
      a3   this round as shipped - per-note span AND a fixed drive gain
    """
    if not PP_MAP:
        print("solve the pressure spans first (stage `span`)")
        return 1, {}
    print("=== THE METRIC: brightness vs dynamics ===")
    a2_soft, a2_loud = 1.55, 2.5       # attempt 2's own 31 / 50 kPa
    out = {}
    for n in METRIC_NOTES:
        rows = {}
        rows["a2_pp"] = bright_row(a2_cfg(scale=a2_soft), n, norm_drive=True)
        rows["a2_mf"] = bright_row(a2_cfg(scale=MF_SCALE), n, norm_drive=True)
        rows["a2_ff"] = bright_row(a2_cfg(scale=a2_loud), n, norm_drive=True)
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            rows["a3n_" + tag] = bright_row(base_cfg(velocity=v), n,
                                            norm_drive=True)
            rows["a3_" + tag] = bright_row(base_cfg(velocity=v), n)
        out[n] = rows
        if quiet:
            continue
        print("\n  midi %d (%s, %.1f Hz)" % (n, note_name(n), midi_hz(n)))
        print("    %-28s %7s %7s %7s %8s" % ("", "pp", "mf", "ff",
                                             "pp->ff"))
        for pre, lbl in (("a2", "attempt 2 as shipped"),
                         ("a3n", "a3 span, a2 normalisation"),
                         ("a3", "attempt 3 as shipped")):
            c = [rows.get(pre + "_" + t) for t in ("pp", "mf", "ff")]
            if any(r is None for r in c):
                print("    %-28s  render failed" % lbl)
                continue
            print("    %-28s %7.0f %7.0f %7.0f %+7.1f%%"
                  % (lbl + " centroid", c[0]["centroid"], c[1]["centroid"],
                     c[2]["centroid"],
                     100 * (c[2]["centroid"] / c[0]["centroid"] - 1)))
            print("    %-28s %7.4f %7.4f %7.4f %7.2fx"
                  % (lbl + " >1 kHz share", c[0]["hf1k"], c[1]["hf1k"],
                     c[2]["hf1k"], c[2]["hf1k"] / max(c[0]["hf1k"], 1e-9)))
            print("    %-28s %7.1f %7.1f %7.1f %+7.1f dB"
                  % (lbl + " level (dB re mf)",
                     20 * math.log10(max(c[0]["rms"], 1e-9) / c[1]["rms"]),
                     0.0,
                     20 * math.log10(max(c[2]["rms"], 1e-9) / c[1]["rms"]),
                     20 * math.log10(max(c[2]["rms"], 1e-9)
                                     / max(c[0]["rms"], 1e-9))))
    json.dump(out, open(os.path.join(SCRATCH, "bright.json"), "w"), indent=1,
              default=float)
    return 0, out


# ===========================================================================
# ears cells
# ===========================================================================
LADDER_GAP = 0.45
LADDER_SEC = 1.3


def ladder_cells():
    """pp / mf / ff on one pitch, three times, so the dynamic is the only
    thing that changes."""
    return {"ladder_low": 29, "ladder_mid": 46, "ladder_high": 65}


CELL_BLURB = {
    "line": ("THE CANDIDATE - G2 to G6 at normal playing strength",
             "is it still the trombone you said you'd play?"),
    "line_soft": ("the same line played softly (velocity 0.25)",
                  "the register at pp - does the whole line still speak?"),
    "ladder_low": ("F2 three times: soft, normal, hard",
                   "does the low note open up when blown harder?"),
    "ladder_mid": ("A#3 three times: soft, normal, hard",
                   "THE ONE THAT MATTERS - this is where the model has its "
                   "widest breath range"),
    "ladder_high": ("F5 three times: soft, normal, hard",
                    "same question at the top of the useful range"),
    "swell": ("one note (A#3) played seven times, softest to hardest",
              "the whole dynamic range in one go - does the tone change or "
              "just the volume?"),
    "ab_attempt2_ff": ("attempt 2 blown as hard as it could be",
                       "the A/B: this is what you have now"),
    "ab_attempt3_ff": ("attempt 3 blown as hard as it can be, SAME loudness",
                       "the A/B: level-matched on purpose, so any difference "
                       "you hear is tone, not volume"),
}


def calibrate(cfg, notes, nsec, gap, tag, target, peak_ceil=PEAK_CEIL,
              **kw):
    """ONE pass, at the very output.  Render the cell as it is, then set a
    single output gain so its sounding rms matches the presentation target,
    backing off if that would push the peak past the ceiling.

    This is the whole of attempt 2's three-pass calibrate() that survives:
    the pass that normalised INTO the steepener is gone (work order 2), and
    the gain that remains multiplies the finished signal.  It changes how
    loud the WAV is and nothing else about it.
    """
    got, pj, wp = render_score(cfg, notes, nsec, gap, tag + "_cal", SCRATCH,
                               **kw)
    if got is None:
        return None
    x, sr = got
    sr_ = sounding_rms(x)
    pk = max(float(np.abs(x).max()), 1e-9)
    want = target / max(sr_, 1e-9)
    cap = peak_ceil / pk
    T1.rm(pj, wp)
    return {"gain": min(want, cap), "want": want, "cap": cap,
            "capped_db": (20 * math.log10(want / cap) if want > cap else 0.0)}


README = """# Trombone, attempt 3 - the blowing-harder round

## THE QUESTION

**Does it open up when you blow harder now?**

Everything else in this round exists to make that question answerable. Your
verdict on attempt 2 was "yes, I'd play it... further refinement possible, of
course, especially the blowing harder thing", and the blowing harder thing
measured 473 Hz at soft against 477 Hz at loud. One percent. This round it
measures {headline}.

## How to blow harder

**Velocity is the breath now.** Play a key harder (MIDI) or move the velocity
slider (QWERTY) and you are changing how hard the instrument is blown, not how
loud it is turned up. The default 0.8 is exactly attempt 2's setting, so the
patch you already know is still there in the middle; below it the instrument
gets genuinely soft, above it genuinely hard.

Each note has its own soft and its own loud, measured from silence one note at
a time. That is the fix for the thing attempt 2's report admitted: its soft
setting was dragged up by the top note of the range, because one pressure had
to keep G6 alive, so nothing could ever be quiet.

## What was wrong, and it was not the physics

Two of my own gain stages were sitting on the effect.

1. **The nonlinearity was being fed a level-corrected signal.** The component
   whose entire job is "brighter when louder" had the loudness taken out
   before it ever saw the wave - every cell in the last two queues was
   normalised to the same peak going in, and un-normalised coming out. Soft
   and loud therefore got identical distortion by construction. That gain is
   gone; the nonlinearity now sees the air column's own travelling wave at
   whatever size it happens to be, which between the softest and hardest
   blowing spans {drivespan} dB.
2. **The whole instrument was already playing at forte.** Measured note by
   note, A#3 speaks in tune over a {spanfold}:1 range of breath, and attempt
   2 sat near the top of it. There was no softer to go to - for any note,
   because one setting had to keep the top note of the range alive.

The lip itself was fine and I changed nothing about it: measured inside the
graph, it is already slamming shut for {closedpp} of every cycle when played
softly and {closedff} when played hard, and the steepness of the pressure
pulse it makes - the quantity the published brass literature says controls
brightness - goes up {riserate}x between the two. The source was doing its
job the whole time.

## Levelling - what came out

You said: *"if I caused kludgy level-tweaking to be added due to my 'really
soft' comments let's remove that... we want the sound to be right, can always
get bigger speakers."*

Attempt 2 flattened the instrument's register to **exactly 0.0 dB** bottom to
top. That is a studio move, not an instrument, and it is gone. What I kept is
the part that is unambiguously a model artifact: the raw instrument jumps up
to **5 dB between two NEIGHBOURING semitones**, which no real instrument does.
That scatter is still corrected in full. The smooth part - the instrument
getting stronger as it climbs, which is partly real, because a bell radiates
high frequencies better than low ones - is now only {slopekeep} corrected, so
**{residual} dB of register slope is left in the sound**. Nothing that remains
touches the signal before the nonlinearity; it is a gain on the finished
sound, and that is measured, not asserted.

The WAVs in this folder are still loudness-matched to a real library patch for
listening, except where a cell says otherwise - a queue you cannot hear is a
queue you cannot judge.

## Cells

| file | what it is | listen for |
|---|---|---|
{cells}

The three `ladder_` files are the round. Same pitch three times - soft, normal,
hard - with the loudness difference left in, because that difference is the
instrument. `ab_attempt2_ff` against `ab_attempt3_ff` is the before-and-after
at full strength, and that pair IS level-matched so you are only judging tone.

## The numbers

Spectral centroid is roughly "where the sound's weight sits" in Hz; higher is
brighter. The >1 kHz share is the fraction of the energy above 1 kHz.

{metric}

{ladder}

## What did not change

Tuning, range and attack are attempt 2's, deliberately and by gate: at the
default velocity the instrument is at attempt 2's exact working point.
{gates}

## Still true, still not fixed

- The top of the range (F#6, G6) barely plays at all: those notes need
  enormous breath to start and have almost no dynamic range once they do.
- The low register has the narrowest breath range of the three - measured, F2
  brightens {lowgain} between soft and hard against {midgain} for A#3.
- The slide is not a slide, the air column is a lumped loop rather than a
  waveguide, and nothing anywhere is fitted to a recording of a real trombone.
"""


def a2_loud(notes, nsec, gap, tag):
    """Attempt 2's loud cell, reproduced exactly: its global 50 kPa, its full
    flattening trim, and its per-cell peak normalisation into the steepener
    (drive_gain = DRIVE_PEAK/peak, undone after).  This is the `before` side
    of the A/B and it is built from attempt 2's own recipe, not remembered."""
    cfg = a2_cfg(scale=2.5)
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = notes, nsec, nsec + gap
    c["drive_gain"] = 1.0
    x, sr = probe_signal(c, "NL_in", 0.2, tag + "_np")
    if x is None:
        return None
    pk = max(float(np.abs(x).max()), 1e-9)
    cfg["drive_gain"] = T1.DRIVE_PEAK / pk
    cfg["out_gain"] = pk / T1.DRIVE_PEAK
    return cfg


def stage_all():
    fail, extra = [], []
    os.makedirs(CAND_OUT, exist_ok=True)
    os.makedirs(EARS_OUT, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    chrom = list(range(TUNE_LO, TUNE_HI + 1))

    print("\n=== 0. loudness reference ===")
    libref, libstats = T2.library_reference()
    print("  patches/library/winds/oboe1.json sounding rms %.4f" % libref)

    print("\n=== 1. the gate table: every chromatic note at the default "
          "velocity ===")
    new_rows = survey(base_cfg(), chrom, "survey_new")
    a2_rows = survey(a2_cfg(), chrom, "survey_a2")
    speaking = [n for n in chrom if new_rows.get(n, {}).get("spoke")]
    worst = max((abs(new_rows[n]["cents"]) for n in speaking), default=999)
    print("  speaks on %d of %d notes; worst tuning error %.1f cents"
          % (len(speaking), len(chrom), worst))
    worse_c, worse_s = [], []
    for n in chrom:
        a, b = new_rows.get(n), a2_rows.get(n)
        if not a or not b:
            continue
        if b["spoke"] and not a["spoke"]:
            worse_c.append("%s stopped speaking" % note_name(n))
            continue
        if a["spoke"] and b["spoke"]:
            if abs(a["cents"]) > max(LOCK_CENTS_GATE, abs(b["cents"]) + 1.0):
                worse_c.append("%s %+.0f c (was %+.0f)"
                               % (note_name(n), a["cents"], b["cents"]))
            if (np.isfinite(a["speak"]) and np.isfinite(b["speak"])
                    and a["speak"] > b["speak"] * 1.5 + 0.03):
                worse_s.append("%s %.0f ms (was %.0f)"
                               % (note_name(n), 1000 * a["speak"],
                                  1000 * b["speak"]))
    print("  tuning regressions vs attempt 2:      %s"
          % (", ".join(worse_c) or "none"))
    print("  speak-time regressions vs attempt 2:  %s"
          % (", ".join(worse_s) or "none"))
    if worse_c:
        fail.append("tuning regressed vs attempt 2: %s" % ", ".join(worse_c))
    if worse_s:
        fail.append("speak time regressed vs attempt 2: %s"
                    % ", ".join(worse_s))
    c4 = new_rows.get(48, {}).get("speak", float("nan"))
    if not (np.isfinite(c4) and c4 <= SPEAK_TARGET_C4):
        fail.append("C4 speak time %.0f ms over the %.0f ms target"
                    % (1000 * c4, 1000 * SPEAK_TARGET_C4))
    lv_new = [new_rows[n]["rms"] for n in speaking if new_rows[n]["rms"] > 0]
    lv_a2 = [a2_rows[n]["rms"] for n in chrom
             if a2_rows.get(n, {}).get("spoke") and a2_rows[n]["rms"] > 0]
    spread_new = (20 * math.log10(max(lv_new) / min(lv_new)) if lv_new
                  else 0.0)
    spread_a2 = 20 * math.log10(max(lv_a2) / min(lv_a2)) if lv_a2 else 0.0
    print("  register level spread: attempt 2 %.1f dB -> this round %.1f dB"
          % (spread_a2, spread_new))

    print("\n=== 2. ignition from silence, one note at a time ===")
    # "starts on its own" = ignites and locks on the written pitch from
    # silence.  It is NOT a loudness test: at pp a mid-register note sits
    # around rms 0.002, which is under T1.LOCK_RMS and is supposed to be -
    # that is what pp means.  speaks() keeps the pitch and periodicity
    # conditions and drops the loudness floor to a true silence floor.
    ign, ignlvl = {}, {}
    for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
        dead, lv = [], []
        for n in chrom:
            r = one_note(base_cfg(velocity=v), n, "ign", nsec=1.2, slot=1.5)
            if not speaks(r):
                dead.append(note_name(n))
            elif r:
                lv.append(r["rms"])
        ign[tag] = dead
        ignlvl[tag] = [float(min(lv)), float(max(lv))] if lv else [0.0, 0.0]
        print("  %-3s  %d of %d notes start on their own (rms %.4f..%.4f)%s"
              % (tag, len(chrom) - len(dead), len(chrom), ignlvl[tag][0],
                 ignlvl[tag][1],
                 "" if not dead else "   SILENT: " + ", ".join(dead)))
    if ign["mf"]:
        fail.append("notes that no longer ignite from silence at the default "
                    "velocity: %s" % ", ".join(ign["mf"]))
    if ign["pp"]:
        extra.append("These notes will not start at the softest setting: %s. "
                     "They need more breath than pp gives them."
                     % ", ".join(ign["pp"]))
    if ign["ff"]:
        extra.append("These notes will not start at the hardest setting: %s."
                     % ", ".join(ign["ff"]))

    print("\n=== 3. the steepener's drive stays inside its domain ===")
    peaks, peaks_pp = {}, {}
    for n in (29, 46, 65, 79):
        for tag, v, d in (("pp", VEL_PP, peaks_pp), ("ff", VEL_FF, peaks)):
            x, sr = probe_signal(base_cfg(velocity=v, notes=[n],
                                          note_sec=1.4, slot=1.8),
                                 "NL_in", 0.1, "dom")
            d[n] = float(np.abs(T1.seg_of(x, sr, 0, 1.8, 1.4)).max())
        print("  %-4s drive peak pp %.3f -> ff %.3f (%.0f dB; domain +-%.1f)"
              % (note_name(n), peaks_pp[n], peaks[n],
                 20 * math.log10(peaks[n] / max(peaks_pp[n], 1e-9)),
                 DRIVE_DOMAIN))
    if max(peaks.values()) > DRIVE_DOMAIN:
        fail.append("steepener drive %.2f runs outside its curve domain"
                    % max(peaks.values()))

    print("\n=== 4. the metric ===")
    _, metric = stage_bright(quiet=False)

    print("\n=== 5. ears cells ===")
    lad = ladder_cells()
    cells, twins = {}, {}
    cells["line"] = (base_cfg(), EARS_LINE, LINE_SEC, LINE_GAP)
    cells["line_soft"] = (base_cfg(velocity=VEL_PP), EARS_LINE, LINE_SEC,
                          LINE_GAP)
    for name, n in lad.items():
        c = base_cfg()
        c["velocities"] = [VEL_PP, VEL_MF, VEL_FF]
        cells[name] = (c, [n, n, n], LADDER_SEC, LADDER_GAP)
    sw = base_cfg()
    sw["velocities"] = [0.25, 0.35, 0.5, 0.65, 0.8, 0.9, 1.0]
    cells["swell"] = (sw, [46] * 7, 1.0, 0.3)
    a2c = a2_loud(EARS_LINE, LINE_SEC, LINE_GAP, "ab2")
    if a2c is not None:
        cells["ab_attempt2_ff"] = (a2c, EARS_LINE, LINE_SEC, LINE_GAP)
        cells["ab_attempt3_ff"] = (base_cfg(velocity=VEL_FF), EARS_LINE,
                                   LINE_SEC, LINE_GAP)
        twins["ab_attempt3_ff"] = "ab_attempt2_ff"
    meta, staged = {}, []
    for name, (cfg, notes, nsec, gap) in cells.items():
        cal = calibrate(cfg, notes, nsec, gap, name, libref)
        if cal is None:
            fail.append("%s calibration" % name)
            continue
        meta[name] = cal
        staged.append(name)
    ears = []
    for name in staged:
        cfg, notes, nsec, gap = cells[name]
        c = dict(cfg)
        c["out_gain"] = c.get("out_gain", 1.0) * meta[name]["gain"]
        got, pj, wp = render_score(c, notes, nsec, gap, name, EARS_OUT)
        if got is None:
            fail.append("%s final render" % name)
            continue
        x, sr = got
        meta[name].update({
            "level": level_ceiling(x, sr), "peak": float(np.abs(x).max()),
            "rms": float(rms(x)), "sounding_rms": sounding_rms(x),
            "notes": notes, "note_sec": nsec,
            "velocities": cfg.get("velocities") or [cfg.get("velocity",
                                                            VEL_MF)],
            "centroid": centroid_all(x, sr),
            "hf1k": hf_fraction(x, sr, 1000.0)})
        ears.append(name)
    # the A/B pair shares ONE gain so only the tone differs
    for a_n, b_n in twins.items():
        if a_n in meta and b_n in meta:
            g = meta[b_n]["sounding_rms"] / max(meta[a_n]["sounding_rms"],
                                                1e-9)
            cfg, notes, nsec, gap = cells[a_n]
            c = dict(cfg)
            c["out_gain"] = c.get("out_gain", 1.0) * meta[a_n]["gain"] * g
            got, pj, wp = render_score(c, notes, nsec, gap, a_n, EARS_OUT)
            if got is not None:
                x, sr = got
                meta[a_n].update({"level": level_ceiling(x, sr),
                                  "peak": float(np.abs(x).max()),
                                  "rms": float(rms(x)),
                                  "sounding_rms": sounding_rms(x),
                                  "matched_to": b_n,
                                  "centroid": centroid_all(x, sr),
                                  "hf1k": hf_fraction(x, sr, 1000.0)})
    for name in ears:
        m = meta[name]
        db = 20 * math.log10(max(m["sounding_rms"], 1e-9) / libref)
        flags = []
        if m["level"] > LEVEL_CEILING:
            flags.append("LEVEL")
            fail.append("%s 0.5 s rms %.2f over the ceiling"
                        % (name, m["level"]))
        if m["peak"] > 0.999:
            flags.append("CLIP")
            fail.append("%s peaks at %.3f" % (name, m["peak"]))
        if m["rms"] < AUDIBILITY_FLOOR:
            flags.append("SILENT")
            fail.append("%s is silent" % name)
        print("  %-16s peak %.3f  0.5s rms %.3f  sounding rms %.4f "
              "(%+.1f dB vs library)  centroid %4.0f %s"
              % (name, m["peak"], m["level"], m["sounding_rms"], db,
                 m["centroid"], " ".join(flags)))

    print("\n=== 6. is the A/B audible? ===")
    for a_n, b_n in (("ab_attempt3_ff", "ab_attempt2_ff"),
                     ("line", "line_soft")):
        if a_n in ears and b_n in ears:
            a, _ = read_mono(os.path.join(EARS_OUT, a_n + ".wav"))
            b, _ = read_mono(os.path.join(EARS_OUT, b_n + ".wav"))
            d = diff_db(a, b)
            print("  %-16s vs %-16s %6.1f dB  %s"
                  % (a_n, b_n, d, "ok" if d > FEATURE_DB else "INAUDIBLE"))
            if d <= FEATURE_DB:
                fail.append("%s is inaudible against %s" % (a_n, b_n))

    print("\n=== 7. candidate patch ===")
    c = dict(base_cfg())
    c["notes"], c["note_sec"], c["slot"] = (EARS_LINE, LINE_SEC,
                                            LINE_SEC + LINE_GAP)
    if "line" in meta:
        c["out_gain"] = meta["line"]["gain"]
    patch = make_patch(c)
    cand_path = os.path.join(CAND_OUT, CAND_NAME + ".json")
    with open(cand_path, "w") as f:
        json.dump(patch, f, indent=1)
    print("  wrote %s (%d nodes)"
          % (os.path.relpath(cand_path, ROOT), len(patch["graph"]["nodes"])))
    gc = subprocess.run([UI, "--gatecheck", cand_path], capture_output=True,
                        text=True)
    out = (gc.stdout or gc.stderr).strip()
    print("  %s" % out)
    if "gateable=1" not in out:
        fail.append("candidate is not key-gateable: %s" % out)

    print("\n=== 8. lip closure (work order 4) ===")
    lip = {}
    for n in METRIC_NOTES:
        lip[n] = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            st = lip_stats(base_cfg(velocity=v), n)
            if st:
                st["kPa"] = dyn_pressure(n, v) * PM_TAB / 1000.0
                lip[n][tag] = st
        r = lip[n]
        print("  %-4s closed %.0f%% -> %.0f%%   rise rate %.0f -> %.0f /s "
              "(%.0fx)"
              % (note_name(n), 100 * r["pp"]["closed_frac"],
                 100 * r["ff"]["closed_frac"], r["pp"]["rise_rate"],
                 r["ff"]["rise_rate"],
                 r["ff"]["rise_rate"] / max(r["pp"]["rise_rate"], 1e-9)))
    rail = max((lip[n][t]["rail_open"] for n in lip for t in lip[n]),
               default=0.0)
    if rail > 0.01:
        extra.append("At the hardest setting the lip runs into the engine's "
                     "own +-8 value limit for up to %.0f%% of each cycle "
                     "(core/dsp_value_source.h), so the very top of the "
                     "dynamic range is being squared off by a safety clamp "
                     "rather than by the instrument. Logged, not fixed - no "
                     "engine code this round." % (100 * rail))

    # ---- README + manifest ---------------------------------------------
    T1.purge(EARS_OUT, {n + ".wav" for n in ears}
             | {"README.md", "measurements.json"})
    T1.purge(PATCH_OUT, {n + ".json" for n in ears}
             | {CAND_NAME + ".json"})

    def pct(n, pre="a3"):
        r = metric.get(n, {})
        a, b = r.get(pre + "_pp"), r.get(pre + "_ff")
        if not a or not b:
            return "n/a"
        return "%+.0f%%" % (100 * (b["centroid"] / a["centroid"] - 1))

    mrows = []
    for n in METRIC_NOTES:
        r = metric.get(n, {})
        for pre, lbl in (("a2", "attempt 2"), ("a3", "attempt 3")):
            cc = [r.get(pre + "_" + t) for t in ("pp", "mf", "ff")]
            if any(x is None for x in cc):
                continue
            mrows.append([note_name(n), lbl,
                          "%.0f" % cc[0]["centroid"], "%.0f" % cc[1]["centroid"],
                          "%.0f" % cc[2]["centroid"],
                          "%+.0f%%" % (100 * (cc[2]["centroid"]
                                              / cc[0]["centroid"] - 1)),
                          "%.1fx" % (cc[2]["hf1k"] / max(cc[0]["hf1k"], 1e-9)),
                          "%+.0f dB" % (20 * math.log10(
                              max(cc[2]["rms"], 1e-9)
                              / max(cc[0]["rms"], 1e-9)))])
    lrows = []
    for n in METRIC_NOTES:
        r = lip.get(n, {})
        if not r:
            continue
        lrows.append([note_name(n),
                      "%.0f%%" % (100 * r["pp"]["closed_frac"]),
                      "%.0f%%" % (100 * r["ff"]["closed_frac"]),
                      "%.0f" % r["pp"]["rise_rate"],
                      "%.0f" % r["ff"]["rise_rate"],
                      "%.0fx" % (r["ff"]["rise_rate"]
                                 / max(r["pp"]["rise_rate"], 1e-9))])
    cell_tbl = "\n".join(
        "| `%s.wav` | %s | %s |" % (n, CELL_BLURB.get(n, ("", ""))[0],
                                    CELL_BLURB.get(n, ("", ""))[1])
        for n in ears)
    ns, db, smooth, scat = decompose_trim()
    gates = ("All 51 notes from F2 to G6 still speak, the worst is %.1f cents "
             "off, and nothing got slower to start. The register spread went "
             "from attempt 2's %.1f dB to %.1f dB, on purpose."
             % (worst, spread_a2, spread_new))
    headline = "%s on A#3 and %s on F5" % (pct(46), pct(65))
    drive_lo = min(peaks.values())
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{cells}", cell_tbl)
                .replace("{headline}", headline)
                .replace("{drivespan}", "%.0f" % (20 * math.log10(
                    peaks[46] / max(peaks_pp[46], 1e-9))))
                .replace("{spanfold}", "%.0f" % (
                    FF_MAP[46] / FF_REL / (PP_MAP[46] / PP_REL)))
                .replace("{closedpp}", "%.0f%%"
                         % (100 * lip[46]["pp"]["closed_frac"]))
                .replace("{closedff}", "%.0f%%"
                         % (100 * lip[46]["ff"]["closed_frac"]))
                .replace("{riserate}", "%.0f" % (
                    lip[46]["ff"]["rise_rate"]
                    / max(lip[46]["pp"]["rise_rate"], 1e-9)))
                .replace("{slopekeep}", "%d%%" % int(100 * TRIM_SLOPE_KEEP))
                .replace("{residual}", "%.0f" % ((1.0 - TRIM_SLOPE_KEEP)
                                                 * (smooth.max()
                                                    - smooth.min())))
                .replace("{metric}", fmt_table(
                    ["note", "version", "soft", "normal", "hard",
                     "centroid change", ">1 kHz change", "level change"],
                    mrows))
                .replace("{ladder}", "### The lip, measured inside the graph\n"
                         "\n" + fmt_table(
                             ["note", "closed at pp", "closed at ff",
                              "rise rate pp", "rise rate ff", "change"],
                             lrows))
                .replace("{gates}", gates)
                .replace("{lowgain}", pct(29))
                .replace("{midgain}", pct(46))
                + ("\n\n## Also worth knowing\n\n"
                   + "\n".join("- " + s for s in extra) if extra else ""))
    json.dump({
        "library_reference": {"patch": os.path.relpath(T2.LIB_REF_PATCH,
                                                       ROOT),
                              "sounding_rms": libref, "stats": libstats},
        "drive_gain": DRIVE_GAIN, "drive_peaks_ff": peaks,
        "drive_peaks_pp": peaks_pp,
        "trim": {"slope_keep": TRIM_SLOPE_KEEP, "map": TRIM_MAP,
                 "a2_map": T2.TRIM_MAP,
                 "smooth_db": float(smooth.max() - smooth.min()),
                 "scatter_db": float(np.abs(scat).max())},
        "spans": {"pp": PP_MAP, "ff": FF_MAP, "vel_pp": VEL_PP,
                  "vel_mf": VEL_MF, "vel_ff": VEL_FF},
        "register_spread_db": {"attempt2": spread_a2, "attempt3": spread_new},
        "metric": metric, "lip": lip, "ignition": ign,
        "ignition_rms": ignlvl,
        "notes_new": new_rows, "notes_a2": a2_rows,
        "ears": {n: meta[n] for n in ears},
    }, open(os.path.join(EARS_OUT, "measurements.json"), "w"), indent=1,
        default=float)

    print("\n%d ears cells staged -> %s" % (len(ears), EARS_OUT))
    if fail:
        print("\nGATE FAILURES:")
        for f_ in fail:
            print("  " + f_)
        return 2
    print("\nAll gates passed.")
    return 0


def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else "all"
    if not os.path.exists(CLI):
        print("mforce_cli not found at %s" % CLI)
        return 1
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    print("=== trombone attempt 3 - the blowing-harder round ===")
    print("  note names: HOUSE convention (octave = midi/12) - the UI's own")
    print("  range: midi %d..%d = %s..%s"
          % (TUNE_LO, TUNE_HI, note_name(TUNE_LO), note_name(TUNE_HI)))
    print("  drive into the steepener: FIXED at %.3f (attempt 2: per-cell "
          "peak normalisation)" % DRIVE_GAIN)
    print("  register trim: scatter in full, %.2f of the ramp" %
          TRIM_SLOPE_KEEP)
    print("  dynamics: velocity %.2f = pp, %.2f = mf (attempt 2's point), "
          "%.2f = ff" % (VEL_PP, VEL_MF, VEL_FF))
    print("  pressure spans loaded: %d notes" % len(PP_MAP))
    if stage == "trim":
        return stage_trim()
    if stage == "drive":
        return stage_drive()
    if stage == "span":
        return stage_span()
    if stage == "lip":
        return stage_lip()
    if stage == "bright":
        return stage_bright()[0]
    if stage == "all":
        if not PP_MAP or not FF_MAP:
            print("no solved spans in the file - run `span` and paste back")
            return 1
        rc = stage_all()
        shutil.rmtree(SCRATCH, ignore_errors=True)
        return rc
    print("unknown stage %r" % stage)
    return 1


if __name__ == "__main__":
    sys.exit(main())
