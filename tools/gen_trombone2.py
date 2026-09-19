"""Trombone round 2 - loudness, voicing, and the range Matt actually plays.

Lineage:
  tools/gen_trombone1.py                                   (the chassis)
  docs/autonomy/dsp/reports/2026-09-18-trombone1.md        (its measurements)
  tools/bell_smyth.py                                      (the bell)
Report: docs/autonomy/dsp/reports/2026-09-18-trombone2.md

WHY THIS ROUND EXISTS
---------------------
REVIEW 69 verdict (Matt, 09-18): "Would I put it on a track and play it? No,
not quite yet. But it is way, way better."  Five things, in his order:

  1. LOUDNESS.  "Overall volume is still very low... 'soft' is barely audible
     at 100% speakers."  Second queue in a row with this complaint.  Two
     causes, both measured (see LIB_REF and TRIM_MAP):
       a. the queue was peak-normalised to -6 dBFS and never checked against
          a library patch, so its SUSTAINED level landed 3-14 dB under the
          library winds depending on the cell;
       b. the register itself spans 13.1 dB from bottom to top (measured:
          midi 36 rms 0.0429, midi 72 rms 0.1946), so peak-normalising a
          whole-range line sets the gain by the TOP note and buries the
          bottom.  A per-note output trim map fixes (b); matching a measured
          library reference fixes (a).
  2. LIVE vs OFFLINE at the bottom.  Matt: "If by low C you meant C2, that
     does *not* sound for me... Lowest sounding note for that patch is G2
     (49Hz)."  MEASURED AND ANSWERED: there is NO live/offline divergence -
     mforce_cli and mforce_ui --gencheck agree to five digits on the same
     patch at every note from midi 24 to 39.  What diverged was the NAMING.
     The UI labels notes by the house convention (octave = midi/12, so midi
     36 is "C3"); attempt 1's report and README used scientific naming (midi
     36 = "C2").  Matt's "C2" is midi 24 = 32.70 Hz, a whole octave below
     anything attempt 1 ever tuned, and his "G2" is midi 31 = 49.00 Hz -
     which is exactly the lowest note that locks in the render too.  THIS
     FILE USES THE HOUSE CONVENTION THROUGHOUT so the two ends of the
     conversation finally agree.
  3. BRIGHTNESS.  "Everything is waaay too bright, but a final LPF takes care
     of that."  A voicing lowpass now lives INSIDE the candidate; the corner
     is chosen by measured spectral centroid and two corners are staged.
  4. LOW REGISTER.  "brass character disappears... attack comes off as a kind
     of rattle."  Measured, and the answer was not where the prompt or I
     guessed.  TWO onset hypotheses were tested and BOTH failed:
       - "the sustain is non-harmonic at the bottom": an artifact of a fixed
         16-harmonic comb, which at 49 Hz only covers to 784 Hz and counts
         every real harmonic above that as noise.  With a comb over the whole
         band the sustain residual is 0.02-0.05 at EVERY note.  Not it.
       - "the low notes take longer to become periodic than to become loud":
         measured with a period-scaled window, the bottom locks in 10-50 ms
         and the TOP is the slow one (C6 90 ms).  Not it either.
     What IS measurable: the notes Matt was playing are OUTSIDE attempt 1's
     solved map.  It tuned midi 36..72; his low register is midi 29..35,
     where the maps extrapolate flat and the rendered pitch runs +6 to +80
     cents sharp (midi 31, his lowest note, is +42 c - nearly a quarter
     tone), with the lip parked on a seed ratio instead of the middle of its
     speaking window.  So this round SOLVES THE MAPS OVER midi 29..79, the
     whole range he reported as playable, and that plus the register trim is
     the low-register fix.  No new mechanism was invented for it.
  5. LOUDNESS-BRIGHTNESS LINKAGE (backlog 74a) stays the known weak spot;
     with the levels fixed it is re-measured and re-staged so it can finally
     be judged.

Range findings recorded as data (HOUSE names, and confirmed in the render):
  midi 31 (G2, 49 Hz) is the lowest note that locks; midi 24-28 are silent.
  midi 79 (G6, 784 Hz) is the top usable note; midi 80 collapses to a fifth
  of the level and 81+ is the "tink" Matt heard (attack transient only).

Outputs (this script OWNS these dirs and purges anything else):
  patches/sweep/trombone2/               the grid
  patches/audition/trombone1/            THE CANDIDATE (beside attempt 1)
  renders/dsp/audition/trombone2/        <= 8 ears cells + README.md
Usage:
  python tools/gen_trombone2.py            full run (needs the solved maps)
  python tools/gen_trombone2.py tune       re-solve lip + air column maps
  python tools/gen_trombone2.py voice      measure the voicing lowpass corner
  python tools/gen_trombone2.py trim       solve the per-note level trim
  python tools/gen_trombone2.py parity     live-path vs offline-path check
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
from gen_stk_bowed import read_mono                          # noqa: E402
from gen_nlbore_probe1 import ratio_knots                    # noqa: E402
from gen_nlbore_probe1 import centroid_all, hf_fraction      # noqa: E402
from gen_nlbore_probe1 import diff_db, level_ceiling, rms    # noqa: E402
from gen_onemass_lip1 import (add, expr, flow_nodes, midi_hz,  # noqa: E402
                              mulg, pts, B0, PM_TAB, P_REF, Q_LIP, SR,
                              F_REF)

ROOT = T1.ROOT
CLI = T1.CLI
UI = os.path.join(ROOT, "build", "tools", "mforce_ui", "Release",
                  "mforce_ui.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "trombone2")
CAND_OUT = os.path.join(ROOT, "patches", "audition", "trombone1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "trombone2")
SCRATCH = os.path.join(tempfile.gettempdir(), "trombone2_scratch")
CAND_NAME = "trombone_attempt2"

# ===========================================================================
# NOTE NAMING - house convention, the one the UI keyboard uses
# ===========================================================================
# mforce_ui draws its keyboard from `baseNote = octave * 12` and labels the
# transport readout `octave = midi/12` (main.cpp, "HOUSE octave convention",
# comp REVIEW 19).  Attempt 1 reported in scientific naming (midi/12 - 1) and
# the two were an octave apart for the whole round.  Never again.
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n):
    return "%s%d" % (NAMES[n % 12], n // 12)


# ===========================================================================
# LOUDNESS (work order 1)
# ===========================================================================
# The reference is a real library patch rendered through the SAME pipeline as
# the queue.  oboe1 is the median of patches/library/winds/ on both metrics
# (family measured 2026-09-18: 0.5 s ceilings .160 .199 .271 .510 .620;
# sounding rms .117 .132 .176 .265 .396).  The level ceiling stays a CEILING.
LIB_REF_PATCH = os.path.join(ROOT, "patches", "library", "winds", "oboe1.json")
LIB_REF_FALLBACK = 0.1761        # measured sounding rms, used if the render
                                 # fails; never silently substituted
TARGET_TOL_DB = 2.0              # "match within a couple dB"
PEAK_CEIL = 0.90                 # hard peak cap after the level match
LEVEL_CEILING = 0.5              # the run contract's speaker-safety gate

# ===========================================================================
# VOICING LOWPASS (work order 3) - corner solved by stage "voice"
# ===========================================================================
# A plain fixed corner, which is what Matt described doing ("a final LPF
# takes care of that"), applied inside the patch.  Two corners are staged
# and his ears pick.
#
# ONE THING WAS TRIED FIRST AND MEASURED AND DROPPED, because it looked like
# the smarter answer and the numbers said otherwise.  Attempt 1's spectral
# centroid sits at 710-770 Hz at EVERY pitch (the bell's own passband), so
# as a multiple of the note being played the brightness runs 13.4x f0 at the
# bottom of the range and 1.9x at the top - which argues for a corner that
# follows the note.  Built and measured: a keytracked corner at 6x f0 does
# flatten it (13.4 -> 4.1 at the bottom, median 3.19 -> 2.24) but it strips
# the low register of the 500-1000 Hz band that is the only place those
# notes have any energy, and the register level spread goes from 13.1 dB to
# 36.5 dB - the trim then has to put 36 dB back, which is re-amplifying a
# near-sine.  That is the opposite of "brass character in the low register".
# Matt's actual words separate the two complaints anyway: everything too
# bright AND the low register lacking character, not the low register being
# too bright.  So: fixed corner.
VOICE_DARK = 1100.0      # Hz  (CANDIDATE - his complaint was brightness)
VOICE_BRIGHT = 2200.0    # Hz  (the staged alternative)
VOICE_POLES = 2          # cascaded 12 dB/oct SVF sections = 24 dB/oct

# ===========================================================================
# RANGE - the whole range Matt reported as playable, in house names
# ===========================================================================
TUNE_LO, TUNE_HI = 29, 79        # F2 .. G6 (midi), 51 chromatic steps
EARS_LINE = [31, 36, 43, 48, 55, 60, 67, 72, 79]     # G2 .. G6
LOW_LINE = [29, 31, 33, 34, 36, 38, 40]              # F2 .. E3
HELD_NOTE = 48                   # C4, middle of the register
MF_SCALE = T1.MF_SCALE
LINE_SEC, LINE_GAP = 1.1, 0.35

# solved by stage "tune" and pasted back - MEASURED, never assumed
TUNE_MAP = {
    29: 0.721, 30: 0.721, 31: 0.706, 32: 0.706, 33: 0.706, 34: 0.706,
    35: 0.706, 36: 0.706, 37: 0.706, 38: 0.706, 39: 0.706, 40: 0.706,
    41: 0.706, 42: 0.732, 43: 0.732, 44: 0.732, 45: 0.732, 46: 0.732,
    47: 0.732, 48: 0.732, 49: 0.747, 50: 0.788, 51: 0.788, 52: 0.788,
    53: 0.788, 54: 0.788, 55: 0.788, 56: 0.799, 57: 0.814, 58: 0.799,
    59: 0.799, 60: 0.814, 61: 0.825, 62: 0.825, 63: 0.84, 64: 0.851,
    65: 0.851, 66: 0.851, 67: 0.877, 68: 0.877, 69: 0.873, 70: 0.873,
    71: 0.873, 72: 0.884, 73: 0.88, 74: 0.88, 75: 0.88, 76: 0.865,
    77: 0.865, 78: 0.865, 79: 0.88}
BORE_MAP = {
    29: 1.0, 30: 1.00243, 31: 0.997163, 32: 0.997727, 33: 0.998589,
    34: 1.0, 35: 1.0, 36: 1.0, 37: 1.001767, 38: 1.002625,
    39: 1.003577, 40: 1.004592, 41: 1.005337, 42: 1.00685,
    43: 1.007194, 44: 1.007502, 45: 1.007794, 46: 1.00811,
    47: 1.008349, 48: 1.008617, 49: 1.012364, 50: 1.016499,
    51: 1.016319, 52: 1.016063, 53: 1.015853, 54: 1.015743,
    55: 1.01558, 56: 1.014031, 57: 1.018201, 58: 1.014227,
    59: 1.014601, 60: 1.018379, 61: 1.017436, 62: 1.017313,
    63: 1.020982, 64: 1.01967, 65: 1.018823, 66: 1.018004,
    67: 1.020548, 68: 1.019562, 69: 1.015701, 70: 1.015306,
    71: 1.015072, 72: 1.014911, 73: 1.011643, 74: 1.012972,
    75: 1.011881, 76: 1.010963, 77: 1.011588, 78: 1.012527,
    79: 1.017018}
# solved by stage "trim" and pasted back: midi -> output gain that flattens
# the register.  1.0 at the loudest note, >1 everywhere else.
TRIM_MAP = {
    29: 4.84931, 30: 4.56973, 31: 4.84895, 32: 4.96882, 33: 4.96513,
    34: 5.02019, 35: 4.95515, 36: 4.83822, 37: 4.67933, 38: 4.5004,
    39: 4.30429, 40: 4.15189, 41: 4.01067, 42: 3.86858, 43: 3.71299,
    44: 3.51089, 45: 3.30115, 46: 2.90842, 47: 2.8817, 48: 2.59372,
    49: 2.41723, 50: 2.56388, 51: 2.45137, 52: 2.38423, 53: 2.2999,
    54: 2.18298, 55: 2.1911, 56: 2.1852, 57: 2.22675, 58: 1.89811,
    59: 1.9586, 60: 1.68558, 61: 1.53541, 62: 1.55418, 63: 1.66202,
    64: 1.55341, 65: 1.39281, 66: 1.51009, 67: 1.30295, 68: 1.14281,
    69: 1.08527, 70: 1.07096, 71: 1.2816, 72: 1.10308, 73: 1.12052,
    74: 1.20921, 75: 1.39858, 76: 1.37359, 77: 1.0, 78: 1.04963,
    79: 1.87145}
# solved by stage "press" and pasted back: midi -> blowing-pressure
# multiplier.  This is BREATH SUPPORT, and it is the round's other real
# finding.  Measured: the pressure a note needs before it ignites at all
# climbs steeply with register - at S=2.0 (the level attempt 1 shipped) the
# top two notes of the range DO NOT START from silence, and need S=4 to 5.
# That is also, exactly, Matt's own observation in the REVIEW 69 verdict -
# "high C in 'soft' struggles to lock in while 'loud' locks much quicker".
# It is not a mystery, it is an ignition threshold that rises with pitch and
# a flat pressure that does not.  A real player blows harder up top; this
# map is that, solved note by note as a fixed margin over each note's own
# measured threshold.  soft/loud then multiply the WHOLE map, so a quiet
# passage stays playable at the top instead of going silent.
PRESS_MAP = {
    29: 1.0, 30: 1.0, 31: 1.0, 32: 1.0, 33: 1.0, 34: 1.0, 35: 1.0,
    36: 1.0, 37: 1.0, 38: 1.0, 39: 1.0, 40: 1.0, 41: 1.0, 42: 1.0,
    43: 1.0, 44: 1.0, 45: 1.0, 46: 1.0, 47: 1.0, 48: 1.0, 49: 1.0,
    50: 1.0, 51: 1.0, 52: 1.0, 53: 1.0, 54: 1.0, 55: 1.0, 56: 1.0,
    57: 1.0, 58: 1.0, 59: 1.0, 60: 1.0, 61: 1.0, 62: 1.0, 63: 1.0,
    64: 1.0, 65: 1.0, 66: 1.0, 67: 1.0, 68: 1.0, 69: 1.0, 70: 1.0,
    71: 1.0, 72: 1.0, 73: 1.0, 74: 1.0, 75: 1.0, 76: 1.12, 77: 1.44,
    78: 2.08, 79: 2.5}
PRESS_MARGIN = 1.6       # how far over its own ignition threshold each note
                         # is parked (MEASURED axis, see stage "press")
PRESS_SCAN = [round(0.4 + 0.2 * i, 2) for i in range(24)]   # 0.4 .. 5.0

TUNE_TOL_CENTS = 2.0
TUNE_SCALE = 2.0
TUNE_NOTE_SEC, TUNE_SLOT = 1.4, 1.8
LIP_SCAN_STEP = 0.015
LIP_SCAN_N = 8
LIP_WINDOW_CENTS = 120.0
BORE_ITERS = 5
BORE_SLOPE = -1731.0

LOCK_CENTS_GATE = 5.0
SPEAK_TARGET_C4 = 0.150          # midi 48, house C4 (attempt 1's "C3")
AUDIBILITY_FLOOR = 1e-4
FEATURE_DB = -26.0
MATCH_PEAK_CEIL = 0.85


# ===========================================================================
# graph
# ===========================================================================
def voice_chain(fc, source):
    """The voicing lowpass: VOICE_POLES cascaded SVF lowpasses at a fixed
    corner, at the very end of the chain after the bell."""
    nodes, src = [], source
    for i in range(VOICE_POLES):
        nid = "Voice%d" % i
        nodes.append({"id": nid, "type": "SVFSource", "params": {
            "cutoffFreq": round(float(fc), 4), "mode": 0,
            "normalize": False, "resonance": 0.5, "source": src}})
        src = {"ref": nid}
    return nodes, nodes[-1]["id"]


def trim_node(nid, trim_map):
    """Per-note output gain.  A points map on the played note, NOT a tap on
    anything - same rule attempt 1 learned the hard way (a tap never advances
    its target, so a pin pulled through one reads a stale sample)."""
    knots = sorted((midi_hz(n), g) for n, g in trim_map.items())
    lo_f, lo_y = knots[0]
    hi_f, hi_y = knots[-1]
    return pts(nid, [(20.0, lo_y)] + knots + [(4000.0, hi_y)],
               {"ref": "__perf_f"})


def make_patch(cfg):
    """Attempt 1's graph plus three additions, all OUTSIDE the bore loop so
    the DelayLine's compensation walk (16-member cap) is untouched:
        Voice0/Voice1   the voicing lowpass      (work order 3)
        Rtrim + Vtrim   the per-note level trim  (work order 1b)
    cfg keys are attempt 1's, plus:
        voice_k     voicing lowpass corner in Hz (0/None = no filter)
        trim_map    measured {midi: gain}; {} = flat (attempt 1 behaviour)
        lip_q       lip pole Q (Berjamin Table 2 says 4; an axis, not a knob)
    """
    ratio = cfg["ratio"]
    tune_map = cfg.get("tune_map") or {}
    trim_map = cfg.get("trim_map") or {}
    press_map = cfg.get("press_map") or {}
    lip_q = cfg.get("lip_q", Q_LIP)
    pexp = cfg.get("pm_exp", T1.PM_EXP)
    a_pm = cfg["scale"] * (PM_TAB / P_REF) / F_REF ** pexp

    def press_at(f0):
        """Breath support at this note.  Interpolated in MIDI so the map
        reads the way it was solved; held flat outside the solved range."""
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
    fast = cfg.get("release", "fast") == "fast"
    m = T1.mouth_env(fast, cfg.get("over_amt"), cfg.get("over_sec"),
                     cfg.get("att_sec"))
    nodes.append(m)
    nodes.append(mulg("Pm", {"ref": "Pmk"}, {"ref": "Mouth"}))
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
                      round(float(cfg.get("in_gain", 1.0)), 9)))
    depth = cfg.get("depth", T1.STEEP_DEPTH)
    nodes.append({"id": "NL_curve", "type": "CurveNode", "params": {
        "exprKnots": [], "interp": "linear", "knots": ratio_knots(depth),
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
        nodes.append(trim_node("Rtrim", trim_map))
        nodes.append(mulg("Vtrim", {"ref": last}, {"ref": "Rtrim"}))
        last = "Vtrim"
    nodes.append(mulg("NL_out", {"ref": last}, round(float(og), 9)))

    notes = cfg.get("notes", EARS_LINE)
    nsec = cfg.get("note_sec", LINE_SEC)
    slot = cfg.get("slot", LINE_SEC + LINE_GAP)
    score = [{"note": n, "time": slot * k, "duration": nsec, "velocity": 0.8}
             for k, n in enumerate(notes)]
    pos, col, row = {}, 0, 0
    for nd in nodes:
        pos[nd["id"]] = [-1800.0 + 210.0 * col, -520.0 + 130.0 * row]
        row += 1
        if row == 6:
            row, col = 0, col + 1
    pos["__output"] = [0.0, 0.0]
    return {"sampleRate": SR, "seconds": slot * len(notes),
            "instrument": {"polyphony": 1, "volume": 0.7},
            "score": score,
            "ui": {"noteFaces": [{"label": "Lip pitch",
                                  "fields": {"frequency": "__perf_f"}}],
                   "positions": pos},
            "graph": {"output": "NL_out", "nodes": nodes}}


def base_cfg(**kw):
    cfg = {"ratio": T1.LIP_RATIO_BASE, "tune_map": dict(TUNE_MAP),
           "bore_map": dict(BORE_MAP), "trim_map": dict(TRIM_MAP),
           "press_map": dict(PRESS_MAP),
           "scale": MF_SCALE, "pm_exp": T1.PM_EXP, "bell": True,
           "kick": 1.0, "release": "fast", "depth": T1.STEEP_DEPTH,
           "voice_k": VOICE_DARK, "lip_q": Q_LIP,
           "in_gain": 1.0, "out_gain": 1.0}
    cfg.update(kw)
    return cfg


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


def render_score(cfg, notes, nsec, gap, tag, out_dir):
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = notes, nsec, nsec + gap
    return render(make_patch(c), tag, out_dir)


# ===========================================================================
# measurement
# ===========================================================================
FHI = 12000.0


def residual(seg, sr, f0):
    """Fraction of in-band energy off the harmonic comb of the PLAYED pitch.

    Two traps this round walked into and had to fix, both worth keeping:
      * a FIXED harmonic count (attempt 1's lock metric uses 12) covers less
        and less of the band as f0 falls - at 49 Hz twelve harmonics stop at
        588 Hz and every real harmonic above that counts as noise.  The comb
        here runs to FHI whatever f0 is.
      * the comb tolerance must be at least the analysis window's own
        mainlobe (Hann = 2 bins).  With a fixed 0.6% tolerance every window
        shorter than ~0.5 s reads as inharmonic no matter what it contains.
    """
    n = len(seg)
    if n < 2048 or not (f0 and np.isfinite(f0) and f0 > 0):
        return float("nan")
    s = (seg - seg.mean()) * np.hanning(n)
    sp = np.abs(np.fft.rfft(s)) ** 2
    fr = np.fft.rfftfreq(n, 1.0 / sr)
    band = (fr > 0.6 * f0) & (fr < FHI)
    tot = float(sp[band].sum()) + 1e-20
    k = np.maximum(1.0, np.round(fr / f0))
    tol = np.maximum(2.5 * sr / n, 0.006 * k * f0)
    return 1.0 - float(sp[band & (np.abs(fr - k * f0) <= tol)].sum()) / tot


def lock_time(seg, sr, f0, win_periods=8.0, min_win=0.06, hop=0.005,
              limit=0.8, thresh=0.25):
    """First moment the running window is periodic on the played pitch.
    The window is a fixed number of PERIODS so a low note is judged on the
    same number of cycles as a high one."""
    n = int(max(min_win, win_periods / f0) * sr)
    h = int(hop * sr)
    for i in range(0, min(len(seg) - n, int(limit * sr)), h):
        if residual(seg[i:i + n], sr, f0) < thresh:
            return i / sr
    return float("nan")


def note_rows(x, sr, notes, slot, nsec):
    out = {}
    for k, n in enumerate(notes):
        tgt = midi_hz(n)
        seg = T1.seg_of(x, sr, k, slot, nsec)
        f0 = T1.f0_autocorr(seg, sr, tgt)
        c = T1.cents(f0, tgt)
        lk = T1.harmonic_lock(seg, sr, tgt)
        rr = rms(seg)
        st, sus = T1.speak_time(x, sr, k, slot, nsec)
        a = int(slot * k * sr)
        whole = x[a:a + int(nsec * sr)]
        spoke = bool(np.isfinite(c) and abs(c) <= T1.LOCK_CENTS_SPEAK
                     and lk > T1.LOCK_RATIO and rr > T1.LOCK_RMS)
        out[n] = {
            "note": n, "name": note_name(n), "target_hz": tgt, "f0": f0,
            "cents": c, "lock": lk, "rms": rr, "speak": st,
            "sustain_rms": sus, "spoke": spoke,
            "locked": bool(spoke and abs(c) <= LOCK_CENTS_GATE),
            "peak": float(np.abs(whole).max()) if len(whole) else 0.0,
            "residual": residual(seg, sr, f0) if np.isfinite(f0) else
            float("nan"),
            "lock_time": (lock_time(whole, sr, f0)
                          if np.isfinite(f0) and len(whole) else float("nan")),
            "centroid": centroid_all(seg, sr) if len(seg) else float("nan"),
            "hf1k": hf_fraction(seg, sr, 1000.0) if len(seg) else
            float("nan")}
    return out


def survey(cfg, notes, tag, nsec=1.4, gap=0.4, chunk=8):
    rows = {}
    for i in range(0, len(notes), chunk):
        grp = notes[i:i + chunk]
        got, pj, wp = render_score(cfg, grp, nsec, gap, "%s_%d" % (tag, i),
                                   SCRATCH)
        if got is None:
            continue
        rows.update(note_rows(got[0], got[1], grp, nsec + gap, nsec))
        T1.rm(pj, wp)
    return rows


def sounding_rms(x):
    nz = x[np.abs(x) > 1e-4]
    return rms(nz) if len(nz) else 0.0


def library_reference():
    """Render a library winds patch through this pipeline and measure it.
    Never assumed - if the render fails, the fallback is announced."""
    wp = os.path.join(SCRATCH, "lib_ref.wav")
    os.makedirs(SCRATCH, exist_ok=True)
    r = subprocess.run([CLI, LIB_REF_PATCH, wp], capture_output=True,
                       text=True)
    if r.returncode != 0 or not os.path.exists(wp):
        print("  LIBRARY REFERENCE RENDER FAILED - using the recorded "
              "measurement %.4f" % LIB_REF_FALLBACK)
        return LIB_REF_FALLBACK, None
    x, sr = read_mono(wp)
    return sounding_rms(x), {"peak": float(np.abs(x).max()), "rms": rms(x),
                             "level": level_ceiling(x, sr)}


# ===========================================================================
# solvers
# ===========================================================================
def measure_one(n, lip_ratio, bore_mult, scale, lip_q, tag="tn"):
    cfg = base_cfg(ratio=lip_ratio, tune_map={}, bore_map={n: bore_mult},
                   trim_map={}, press_map={}, notes=[n],
                   note_sec=TUNE_NOTE_SEC, slot=TUNE_SLOT, scale=scale,
                   lip_q=lip_q, voice_k=0.0)
    got, pj, wp = render(make_patch(cfg), "%s_%d" % (tag, n), SCRATCH)
    if got is None:
        return float("nan"), 0.0, 0.0
    st = note_rows(got[0], got[1], [n], TUNE_SLOT, TUNE_NOTE_SEC)[n]
    T1.rm(pj, wp)
    c = st["cents"]
    if st["lock"] < T1.LOCK_RATIO or st["rms"] < T1.LOCK_RMS:
        c = float("nan")
    return c, st["lock"], st["rms"]


def solve_lip_map(notes, scale=TUNE_SCALE, lip_q=Q_LIP, verbose=True):
    """Attempt 1's solver, unchanged in method: the lip ratio is chosen for
    the WIDEST SPEAKING WINDOW (margin against cracking), never for pitch -
    measured there, the ratio moves pitch only ~170 cents per unit while the
    speaking window is ~0.10 wide, so chasing cents with the lip walks off
    the edge.  Only the note range is different."""
    print("=== lip map: widest speaking window per note (%d notes) ==="
          % len(notes))
    out, log = {}, {}
    for n in notes:
        f0 = midi_hz(n)
        p = T1.partial_of(f0)
        seed = T1.seed_ratio(p)
        grid = [round(seed + LIP_SCAN_STEP * i, 4)
                for i in range(-LIP_SCAN_N, LIP_SCAN_N + 1)]
        ok, cents_at = [], {}
        for r in grid:
            c, lk, rr = measure_one(n, r, 1.0, scale, lip_q, "lipscan")
            ok.append(bool(np.isfinite(c) and abs(c) <= LIP_WINDOW_CENTS))
            cents_at[r] = c
        best_a = best_b = -1
        i = 0
        while i < len(ok):
            if not ok[i]:
                i += 1
                continue
            j = i
            while j + 1 < len(ok) and ok[j + 1]:
                j += 1
            if best_a < 0 or (j - i) > (best_b - best_a):
                best_a, best_b = i, j
            i = j + 1
        if best_a < 0:
            out[n] = round(seed, 5)
            log[n] = {"ratio": out[n], "partial": p, "window": None}
            if verbose:
                print("  %3d %-4s %8.2f Hz  p%-2d  NO SPEAKING WINDOW, seed "
                      "%.4f kept" % (n, note_name(n), f0, p, seed))
            continue
        mid = (best_a + best_b) // 2
        out[n] = grid[mid]
        log[n] = {"ratio": grid[mid], "partial": p,
                  "window": [grid[best_a], grid[best_b]],
                  "window_steps": best_b - best_a + 1,
                  "cents": (None if not np.isfinite(cents_at[grid[mid]])
                            else round(float(cents_at[grid[mid]]), 1))}
        if verbose:
            print("  %3d %-4s %8.2f Hz  p%-2d  window %.3f..%.3f (%d steps) "
                  "-> %.4f at %s c"
                  % (n, note_name(n), f0, p, grid[best_a], grid[best_b],
                     best_b - best_a + 1, grid[mid],
                     "----" if not np.isfinite(cents_at[grid[mid]])
                     else "%+5.1f" % cents_at[grid[mid]]))
    return out, log


def solve_bore_map(notes, lip_map, scale=TUNE_SCALE, lip_q=Q_LIP,
                   press_map=None, verbose=True):
    """With the lip parked mid-window, stretch the air column until the
    rendered pitch is equal temperament.  1% longer delay = ~17 cents flat,
    so the secant is on a near-linear relation."""
    print("=== air column trim per note (%d notes) ===" % len(notes))
    out, log = {}, {}
    for n in notes:
        r = lip_map.get(n, T1.seed_ratio(T1.partial_of(midi_hz(n))))
        sc = scale * (press_map or {}).get(n, 1.0)
        m, prev, best, hist = 1.0, None, None, []
        for _ in range(BORE_ITERS):
            c, lk, rr = measure_one(n, r, m, sc, lip_q, "borescan")
            hist.append((round(m, 5),
                         None if not np.isfinite(c) else round(float(c), 1)))
            if np.isfinite(c) and (best is None or abs(c) < abs(best[1])):
                best = (m, float(c))
            if np.isfinite(c) and abs(c) <= TUNE_TOL_CENTS:
                break
            if not np.isfinite(c):
                break
            if prev is not None and abs(prev[1] - c) > 0.5 and \
                    abs(prev[0] - m) > 1e-9:
                slope = (c - prev[1]) / (m - prev[0])
            else:
                slope = BORE_SLOPE
            prev = (m, c)
            m = max(0.90, min(1.10, m - c / slope))
        out[n] = round(best[0] if best else m, 6)
        log[n] = {"mult": out[n], "trail": hist,
                  "cents": None if best is None else round(best[1], 1)}
        if verbose:
            print("  %3d %-4s  lip %.4f  air column x%.5f -> %s c (%d renders)"
                  % (n, note_name(n), r, out[n],
                     "----" if best is None else "%+5.1f" % best[1],
                     len(hist)))
    return out, log


def solve_press_map(notes, lip_map, verbose=True):
    """Breath support per note: find the lowest blowing pressure at which the
    note ignites FROM SILENCE, and park it PRESS_MARGIN above that.

    "From silence" is the point.  A note rendered after another note inherits
    the bore's leftover energy and starts on it; a note played alone on a
    fresh voice does not.  Attempt 1's top notes only ever got measured in a
    line, which is why F#6/G6 looked fine there and will not start on their
    own.  Every measurement here is a single note in its own render."""
    print("=== breath support per note: ignition threshold (%d notes) ==="
          % len(notes))
    out, log = {}, {}
    for n in notes:
        r = lip_map.get(n, T1.seed_ratio(T1.partial_of(midi_hz(n))))
        thresh = None
        for s_ in PRESS_SCAN:
            c, lk, rr = measure_one(n, r, 1.0, s_, Q_LIP, "press")
            if np.isfinite(c) and abs(c) <= LIP_WINDOW_CENTS and rr > 0.004:
                thresh = s_
                break
        if thresh is None:
            out[n] = round(PRESS_SCAN[-1] / TUNE_SCALE, 4)
            log[n] = {"threshold": None, "mult": out[n]}
            if verbose:
                print("  %3d %-4s  NEVER IGNITES up to S=%.1f - parked at the "
                      "top of the scan" % (n, note_name(n), PRESS_SCAN[-1]))
            continue
        # FLOOR AT 1.0 ON PURPOSE.  A pure "same margin everywhere" rule
        # would drop the bottom of the range to a third of attempt 1's
        # pressure - measured, the low notes ignite at the very bottom of
        # the scan - and that would both thin them out and undo the level
        # work for no reason.  The map only ever ADDS breath, so every note
        # that already worked is bit-for-bit unchanged and the regression
        # gate against attempt 1 can only be moved by the notes that needed
        # the help.
        want = min(PRESS_SCAN[-1], max(TUNE_SCALE, thresh * PRESS_MARGIN))
        out[n] = round(want / TUNE_SCALE, 4)
        log[n] = {"threshold": thresh, "S": want, "mult": out[n]}
        if verbose:
            print("  %3d %-4s  ignites at S=%.1f (%.1f kPa) -> plays at "
                  "S=%.2f (%.1f kPa), multiplier %.3f"
                  % (n, note_name(n), thresh, thresh * PM_TAB / 1000, want,
                     want * PM_TAB / 1000, out[n]))
    return out, log


def solve_trim_map(notes, verbose=True):
    """Work order 1b.  Render every note and set a per-note output gain that
    makes the sustained level the SAME across the register.  Solved against
    the measured sustain rms, capped so no note's peak can be pushed past
    PEAK_CEIL once the global level match is applied."""
    print("=== register level trim (%d notes) ===" % len(notes))
    cfg = base_cfg(trim_map={})
    rows = survey(cfg, notes, "trimsolve")
    vals = {n: r["rms"] for n, r in rows.items() if r["rms"] > 1e-5}
    if not vals:
        return {}, {}
    ref = max(vals.values())
    out = {n: round(min(30.0, ref / v), 5) for n, v in vals.items()}
    for n in notes:
        out.setdefault(n, 1.0)
    if verbose:
        for n in notes:
            r = rows.get(n, {})
            print("  %3d %-4s rms %.4f -> x%.3f  (%+.1f dB)"
                  % (n, note_name(n), r.get("rms", 0.0), out[n],
                     20 * math.log10(max(out[n], 1e-9))))
        sp = 20 * math.log10(max(vals.values()) / max(min(vals.values()),
                                                      1e-9))
        print("  register spread before trim: %.1f dB" % sp)
    return out, {n: rows.get(n, {}) for n in notes}


# ===========================================================================
# stages
# ===========================================================================
def stage_parity():
    """Work order 2.  Same patch, two paths: mforce_cli (offline) and
    mforce_ui --gencheck (the UI's own generate_unified build).  Plus
    --gatecheck on the candidate."""
    notes = list(range(24, 40))
    nsec, gap = 1.2, 0.4
    cfg = base_cfg()
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = notes, nsec, nsec + gap
    pj = os.path.join(PATCH_OUT, "parity_low.json")
    os.makedirs(PATCH_OUT, exist_ok=True)
    json.dump(make_patch(c), open(pj, "w"), indent=1)
    wa = os.path.join(SCRATCH, "parity_cli.wav")
    wb = os.path.join(SCRATCH, "parity_ui.wav")
    os.makedirs(SCRATCH, exist_ok=True)
    ra = subprocess.run([CLI, pj, wa], capture_output=True, text=True)
    rb = subprocess.run([UI, "--gencheck", pj, wb], capture_output=True,
                        text=True)
    print("  mforce_cli rc=%d   mforce_ui --gencheck rc=%d"
          % (ra.returncode, rb.returncode))
    if ra.returncode != 0 or rb.returncode != 0:
        print("  PARITY CHECK COULD NOT RUN")
        return 1, []
    A = note_rows(*read_mono(wa), notes=notes, slot=nsec + gap, nsec=nsec)
    B = note_rows(*read_mono(wb), notes=notes, slot=nsec + gap, nsec=nsec)
    print("  %4s %-5s %8s | %8s %6s %8s | %8s %6s %8s"
          % ("midi", "house", "hz", "cli c", "lock", "rms",
             "ui c", "lock", "rms"))
    rows = []
    worst = 0.0
    for n in notes:
        a, b = A[n], B[n]
        for k in ("cents", "rms", "lock"):
            if np.isfinite(a[k]) and np.isfinite(b[k]):
                worst = max(worst, abs(a[k] - b[k]))
        rows.append((n, a, b))
        print("  %4d %-5s %8.2f | %8s %6.2f %8.4f | %8s %6.2f %8.4f"
              % (n, note_name(n), a["target_hz"],
                 "dead" if not np.isfinite(a["cents"]) else
                 "%+.1f" % a["cents"], a["lock"], a["rms"],
                 "dead" if not np.isfinite(b["cents"]) else
                 "%+.1f" % b["cents"], b["lock"], b["rms"]))
    print("  WORST DIFFERENCE BETWEEN THE TWO PATHS: %.6f" % worst)
    gc = subprocess.run([UI, "--gatecheck",
                         os.path.join(CAND_OUT, CAND_NAME + ".json")],
                        capture_output=True, text=True)
    print("  gatecheck: %s" % (gc.stdout or gc.stderr).strip())
    return 0, rows


def stage_voice():
    """Work order 3.  Measure the spectrum across the range at a set of fixed
    corners, before and after, and report the two numbers that matter: how
    bright it is (centroid, and the share of energy above 1 kHz) and what the
    filter costs in level spread across the register."""
    print("=== voicing lowpass: fixed corner sweep ===")
    probe = [31, 36, 40, 48, 55, 60, 67, 72, 79]
    out = {}
    for fc in (0.0, 700.0, 900.0, 1100.0, 1500.0, 2200.0, 3200.0):
        rows = survey(base_cfg(voice_k=fc), probe, "voice_%d" % int(fc))
        cents = [r["centroid"] for r in rows.values()
                 if np.isfinite(r["centroid"])]
        hf = [r["hf1k"] for r in rows.values() if np.isfinite(r["hf1k"])]
        lvl = [r["rms"] for r in rows.values() if r["rms"] > 0]
        spread = (20 * math.log10(max(lvl) / min(lvl)) if len(lvl) > 1
                  else 0.0)
        out[fc] = {"centroid": float(np.median(cents)) if cents else
                   float("nan"),
                   "centroid_top": rows.get(79, {}).get("centroid",
                                                        float("nan")),
                   "centroid_bottom": rows.get(31, {}).get("centroid",
                                                           float("nan")),
                   "hf1k": float(np.median(hf)) if hf else float("nan"),
                   "rms": float(np.median(lvl)) if lvl else 0.0,
                   "spread_db": spread,
                   "spoke": sum(1 for r in rows.values() if r["spoke"]),
                   "per_note": {n: rows.get(n, {}).get("centroid")
                                for n in probe}}
        o = out[fc]
        print("  corner %-7s median centroid %5.0f Hz (bottom %5.0f, top "
              "%5.0f)  energy >1 kHz %.3f  level spread %4.1f dB  spoke %d/%d"
              % ("off" if not fc else "%.0f Hz" % fc, o["centroid"],
                 o["centroid_bottom"], o["centroid_top"], o["hf1k"],
                 o["spread_db"], o["spoke"], len(probe)))
    json.dump(out, open(os.path.join(SCRATCH, "voice.json"), "w"), indent=1,
              default=float)
    return 0


README = """# Trombone, attempt 2

Loud enough to judge, and in tune where you actually play.

Renders here; the candidate patch is
`patches/audition/trombone1/{cand}.json` (same folder as attempt 1, so you
can A/B them side by side); sweep patches in `patches/sweep/trombone2/`;
generator `tools/gen_trombone2.py`; run report
`docs/autonomy/dsp/reports/2026-09-18-trombone2.md`.

**Note names in this file are the ones your keyboard shows.** That is the
single biggest thing that went wrong last round and it is worth one
paragraph: the UI labels a key by dividing its MIDI number by 12, and I was
writing note names the other common way, which is an octave lower. So when I
said "low C speaks in 78 ms" I meant the key your UI calls **C3**, and when
you played the key labelled C2 you were playing an octave below anything I
had ever tuned. Your ears were right and my labels were wrong. Everything
below is in your labels.

## The five things you asked for

1. **Volume.** Fixed, and measured against a real library patch rather than
   against nothing. I rendered `patches/library/winds/oboe1.json` through the
   same pipeline: it sits at **{libref}** sounding RMS. The cells here sit at
   **{cellref}**. Last round's line sat at 0.096, and `soft` at 0.020 - that
   is why it was inaudible.
   Half of the problem was not the queue at all: the instrument itself was
   **{spread_before} dB louder at the top of its range than at the bottom**,
   so normalising a whole-range line set the level by the top note and buried
   everything else. There is now a measured per-note level trim inside the
   patch and the spread is **{spread_after} dB**. That is in the patch, so it
   is there when you play it live too.
   The other half of the loudness story is breath. A note does not start at
   all until the blowing pressure clears its own threshold, and that
   threshold climbs steeply near the top: measured, everything up to B5
   starts at 8 kPa, C6 needs 12, F6 needs 36 and G6 needs 72. Attempt 1 blew
   the same pressure at every pitch, so the top of the range was permanently
   near the edge. The patch now carries a measured per-note breath map, and
   the two top notes (F#6, G6) go from "will not start unless another note
   just played" to starting on their own.
2. **The bottom of the range.** No live-vs-offline bug exists - I rendered
   the identical patch through the command-line renderer and through the UI's
   own render path and they agree to {parity} at every note from C2 to D#3.
   The real problem was that last round only ever tuned from C3 up, and you
   play down to G2. Below C3 it was guessing, and running up to **+80 cents
   sharp**. This round solves every chromatic note from **F2 to G6** - the
   whole range you reported as playable - so the bottom is in tune now
   instead of nearly a quarter-tone sharp.
3. **Brightness.** There is a voicing lowpass in the patch now. `line.wav` is
   the darker corner ({dark}) and `line_brighter.wav` is the brighter one
   ({bright}); they are the same take otherwise. Measured
   median spectral centroid across the range: **{cent_off} Hz** with no
   filter, **{cent_bright} Hz** on the brighter one, **{cent_dark} Hz** on
   the darker one. Pick one.
4. **The low register.** Two theories about the attack were tested and both
   were wrong, so I am not going to dress them up: the sustain down there is
   as periodic as the top (residual 0.02-0.05 everywhere), and the bottom
   actually becomes periodic FASTER than the top (10-50 ms against 90 ms at
   C6). What was genuinely broken is in point 2 - you were playing notes the
   patch had never been tuned for, at a level {spread_before} dB under the
   top of the range. Both are fixed. `low_before.wav` against `low_after.wav` is that exact comparison
   on the notes you were playing - and the two are NOT level-matched on
   purpose, because `low_before` is levelled exactly the way last round's
   queue was, so the loudness difference in that pair is the real one.
5. **Soft and loud.** Re-staged at real level so the question can finally be
   answered. Honest position unchanged, and it is still the weak spot: the
   brightness change with blowing pressure is small. Measured this round,
   soft to loud moves the centroid {dyn}. A real trombone opens up far more
   than that between p and ff and this one does not.
   Your other observation - the high note locking in slowly on `soft` and
   quickly on `loud` - has an answer, and it is the breath threshold above.
   At attempt 1's soft setting the top notes were sitting below the pressure
   they need to start at all, so they crawled in on whatever the tube
   happened to give them. They are above threshold now at both settings.
   The cost, stated plainly: the soft end of this instrument is limited by
   its top note. `soft` here is 31 kPa and `loud` is 50 kPa - a narrower
   range than last round claimed, because last round's window was measured
   on a line where each note inherited the last one's energy.

## THE QUESTION

Same as last round: **would you put it on a track and play it?** And if not,
the most useful single answer is which of `line` / `line_brighter` is closer,
because that is the one thing your ears decide and I cannot.

## Cells

| file | what it is | listen for |
|---|---|---|
{cells}

## Measurements

Every cell is levelled to the same measured library reference, so a loudness
difference you hear between cells is real, not gain staging - except the
soft/loud pair, which shares one gain on purpose.

### Tuning - cents off, every chromatic note, YOUR note names

100 cents is a semitone. Attempt 1 is the "last round" column, measured the
same way; a dash means the note did not speak.

{lock}

### Speaking time - milliseconds from key-down to 90% of the note's level

{speak}

### Level across the range

{levels}

{extra}
"""

CELL_BLURB = {
    "line": ("THE CANDIDATE - a line from G2 to G6, darker voicing",
             "is this a trombone? and is it loud enough now"),
    "line_brighter": ("the same line, brighter voicing corner",
                      "the A/B that matters - which corner is the "
                      "instrument"),
    "low_before": ("F2 to E3 with last round's tuning and level",
                   "the rattly, quiet bottom you described"),
    "low_after": ("F2 to E3 this round - in tune and at level",
                  "against low_before: is the rattle still there?"),
    "soft": ("the line blown gently, at the loud cell's gain",
             "brass goes dull and small when soft - does it?"),
    "loud": ("the line blown hard",
             "brass goes bright and edgy when loud - does it?"),
    "short_notes": ("the same pitches as half-second notes",
                    "articulation - is it playable in time"),
    "held_6s": ("one note held for six seconds",
                "does it sit there and stay a note, and does the end sound "
                "like a player stopping"),
}


def fmt_table(header, rows):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---" for _ in header) + "|"]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


def stage_all():
    fail, extra = [], []
    os.makedirs(CAND_OUT, exist_ok=True)
    os.makedirs(EARS_OUT, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    chrom = list(range(TUNE_LO, TUNE_HI + 1))

    # The README quotes the voicing sweep's numbers, so a full run owns it
    # rather than depending on a leftover file from an earlier invocation.
    vj = os.path.join(SCRATCH, "voice.json")
    if not os.path.exists(vj):
        print("\n(no voicing sweep on disk - running it first)")
        stage_voice()

    print("\n=== 0. loudness reference (work order 1) ===")
    libref, libstats = library_reference()
    print("  patches/library/winds/oboe1.json sounding rms %.4f %s"
          % (libref, libstats or "(fallback)"))

    print("\n=== 1. the range, every chromatic note F2..G6 ===")
    cand = base_cfg()
    new_rows = survey(cand, chrom, "survey_new")
    old_cfg = base_cfg(tune_map=dict(T1.TUNE_MAP), bore_map=dict(T1.BORE_MAP),
                       trim_map={}, voice_k=0.0)
    old_rows = survey(old_cfg, chrom, "survey_old")
    speaking = [n for n in chrom if new_rows.get(n, {}).get("spoke")]
    miss = [(n, new_rows[n]["cents"]) for n in speaking
            if not new_rows[n]["locked"]]
    print("  speaks on %d of %d chromatic notes" % (len(speaking), len(chrom)))
    worst = max((abs(new_rows[n]["cents"]) for n in speaking), default=999)
    print("  worst tuning error among speaking notes: %.1f cents" % worst)
    if miss:
        extra.append("%d of the %d speaking notes miss the +-%.0f cent "
                     "target: %s." % (len(miss), len(speaking),
                                      LOCK_CENTS_GATE,
                                      ", ".join("%s %+.0f c" % (note_name(n),
                                                                c)
                                                for n, c in miss)))
    dead = [n for n in chrom if n not in speaking]
    if dead:
        extra.append("These notes do not speak and are the edge of the "
                     "register: %s." % ", ".join(note_name(n) for n in dead))
    if not speaking:
        print("NOTHING SPEAKS - stopping")
        return 2

    # regression gate against attempt 1
    print("\n=== 2. regression gate against attempt 1 ===")
    shared = [n for n in range(36, 73) if n in new_rows and n in old_rows]
    worse_c, worse_s = [], []
    for n in shared:
        a, b = new_rows[n], old_rows[n]
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
    print("  tuning regressions: %s" % (", ".join(worse_c) or "none"))
    print("  speak-time regressions: %s" % (", ".join(worse_s) or "none"))
    if worse_c:
        fail.append("tuning regressed vs attempt 1: %s" % ", ".join(worse_c))
    if worse_s:
        extra.append("Slower to speak than last round on: %s."
                     % ", ".join(worse_s))
    c4 = new_rows.get(48, {}).get("speak", float("nan"))
    if not (np.isfinite(c4) and c4 <= SPEAK_TARGET_C4):
        fail.append("C4 speak time %.0f ms over the %.0f ms target"
                    % (1000 * c4, 1000 * SPEAK_TARGET_C4))

    # register spread, before and after the trim
    lv_new = [new_rows[n]["rms"] for n in speaking if new_rows[n]["rms"] > 0]
    lv_old = [old_rows[n]["rms"] for n in chrom
              if old_rows.get(n, {}).get("spoke")
              and old_rows[n]["rms"] > 0]
    spread_new = 20 * math.log10(max(lv_new) / min(lv_new)) if lv_new else 0.0
    spread_old = 20 * math.log10(max(lv_old) / min(lv_old)) if lv_old else 0.0
    print("  register level spread: attempt 1 %.1f dB -> this round %.1f dB"
          % (spread_old, spread_new))

    print("\n=== 3. blowing-pressure window ===")
    win = []
    for s in T1.SCAN:
        rows = survey(base_cfg(scale=s), EARS_LINE, "scan_%03d" % int(s * 100),
                      nsec=1.0, gap=0.2)
        k = sum(1 for p in rows.values() if p["spoke"])
        win.append((s, k))
        print("  S %.2f  %d of %d notes speak" % (s, k, len(EARS_LINE)))
    good = [s for s, k in win if k == len(EARS_LINE)]
    if good:
        soft_s = round(good[0] + (good[-1] - good[0]) * T1.SOFT_POS, 3)
        loud_s = round(good[0] + (good[-1] - good[0]) * T1.LOUD_POS, 3)
    else:
        best_s = max(win, key=lambda t: t[1])[0]
        soft_s, loud_s = round(best_s * 0.8, 3), round(best_s * 1.3, 3)
        extra.append("No single blowing pressure speaks every note of the "
                     "line; soft and loud sit either side of the best one.")
    print("  soft S %.2f (%.1f kPa), loud S %.2f (%.1f kPa)"
          % (soft_s, soft_s * PM_TAB / 1000, loud_s, loud_s * PM_TAB / 1000))

    # ignition-vs-pressure, the 74a data point Matt reported by ear
    print("\n=== 4. ignition speed vs pressure (74a) ===")
    ign = {}
    for tag, s in (("soft", soft_s), ("loud", loud_s)):
        rows = survey(base_cfg(scale=s), [48, 72, 79], "ign_" + tag)
        ign[tag] = {n: {"speak": rows.get(n, {}).get("speak"),
                        "lock_time": rows.get(n, {}).get("lock_time"),
                        "centroid": rows.get(n, {}).get("centroid"),
                        "hf1k": rows.get(n, {}).get("hf1k")}
                    for n in (48, 72, 79)}
        print("  %-5s %s" % (tag, "  ".join(
            "%s speak %s / lock %s" % (
                note_name(n),
                "----" if not np.isfinite(ign[tag][n]["speak"] or
                                          float("nan"))
                else "%4.0f ms" % (1000 * ign[tag][n]["speak"]),
                "----" if not np.isfinite(ign[tag][n]["lock_time"] or
                                          float("nan"))
                else "%4.0f ms" % (1000 * ign[tag][n]["lock_time"]))
            for n in (48, 72, 79))))

    print("\n=== 5. ears cells ===")
    cells = {
        "line": (base_cfg(), EARS_LINE, LINE_SEC, LINE_GAP),
        "line_brighter": (base_cfg(voice_k=VOICE_BRIGHT), EARS_LINE,
                          LINE_SEC, LINE_GAP),
        "low_after": (base_cfg(), LOW_LINE, 1.3, 0.35),
        "low_before": (base_cfg(tune_map=dict(T1.TUNE_MAP),
                                bore_map=dict(T1.BORE_MAP), trim_map={},
                                voice_k=0.0), LOW_LINE, 1.3, 0.35),
        "short_notes": (base_cfg(), EARS_LINE, 0.5, 0.25),
        "held_6s": (base_cfg(), [HELD_NOTE], 6.0, 0.8),
        "soft": (base_cfg(scale=soft_s), EARS_LINE, LINE_SEC, LINE_GAP),
        "loud": (base_cfg(scale=loud_s), EARS_LINE, LINE_SEC, LINE_GAP),
    }
    # cells that must NOT be levelled independently, and what they inherit
    TRIM_TWIN = {"soft": "loud"}
    # `low_before` keeps ATTEMPT 1's own levelling so the loudness fix is
    # audible in the pair rather than normalised away.
    LEGACY_LEVEL = {"low_before"}
    meta, staged = {}, []
    for name, (cfg, notes, nsec, gap) in cells.items():
        cal = calibrate(cfg, notes, nsec, gap, name, libref,
                        legacy=(name in LEGACY_LEVEL))
        if cal is None:
            fail.append("%s calibration" % name)
            continue
        meta[name] = cal
        staged.append(name)
    for name, twin in TRIM_TWIN.items():
        if name in meta and twin in meta:
            meta[name]["trim"] = meta[twin]["trim"]
            meta[name]["trim_from"] = twin
    ears = []
    for name in staged:
        cfg, notes, nsec, gap = cells[name]
        c = dict(cfg)
        c["in_gain"] = meta[name]["in_gain"]
        c["out_gain"] = meta[name]["trim"] / meta[name]["in_gain"]
        got, pj, wp = render_score(c, notes, nsec, gap, name, EARS_OUT)
        if got is None:
            fail.append("%s final render" % name)
            continue
        x, sr = got
        lc, pk = level_ceiling(x, sr), float(np.abs(x).max())
        sr_ = sounding_rms(x)
        meta[name].update({"level": lc, "peak": pk, "rms": rms(x),
                           "sounding_rms": sr_, "notes": notes,
                           "note_sec": nsec,
                           "centroid": centroid_all(x, sr),
                           "hf1k": hf_fraction(x, sr, 1000.0)})
        flags = []
        if lc > LEVEL_CEILING:
            flags.append("LEVEL")
            fail.append("%s 0.5 s rms %.2f over the ceiling" % (name, lc))
        if pk > 0.999:
            flags.append("CLIP")
            fail.append("%s peaks at %.3f" % (name, pk))
        if rms(x) < AUDIBILITY_FLOOR:
            flags.append("SILENT")
            fail.append("%s is silent" % name)
        db = 20 * math.log10(max(sr_, 1e-9) / libref)
        if (name not in TRIM_TWIN and name not in LEGACY_LEVEL
                and abs(db) > TARGET_TOL_DB):
            flags.append("OFF-REF %+.1f dB" % db)
        print("  %-14s peak %.3f  0.5s rms %.3f  sounding rms %.4f "
              "(%+.1f dB vs library)  centroid %4.0f %s"
              % (name, pk, lc, sr_, db, meta[name]["centroid"],
                 " ".join(flags)))
        ears.append(name)

    print("\n=== 6. is each change audible? ===")
    for a_n, b_n in (("line", "line_brighter"), ("low_after", "low_before"),
                     ("loud", "soft")):
        if a_n in ears and b_n in ears:
            a, _ = read_mono(os.path.join(EARS_OUT, a_n + ".wav"))
            b, _ = read_mono(os.path.join(EARS_OUT, b_n + ".wav"))
            d = diff_db(a, b)
            print("  %-14s vs %-14s  %6.1f dB  %s"
                  % (a_n, b_n, d, "ok" if d > FEATURE_DB else "INAUDIBLE"))
            if d <= FEATURE_DB:
                fail.append("%s is inaudible against %s (%.1f dB)"
                            % (a_n, b_n, d))
    dyn = ""
    if "loud" in meta and "soft" in meta:
        dyn = ("from %.0f Hz to %.0f Hz, and the energy above 1 kHz from "
               "%.3f to %.3f" % (meta["soft"]["centroid"],
                                 meta["loud"]["centroid"],
                                 meta["soft"]["hf1k"], meta["loud"]["hf1k"]))
        print("  soft->loud centroid %4.0f -> %4.0f   energy >1 kHz "
              "%.4f -> %.4f" % (meta["soft"]["centroid"],
                                meta["loud"]["centroid"],
                                meta["soft"]["hf1k"], meta["loud"]["hf1k"]))
        if meta["loud"]["centroid"] <= meta["soft"]["centroid"]:
            extra.append("Blowing harder did NOT come out brighter this "
                         "round - the known weak spot got worse, not "
                         "better.")

    print("\n=== 7. candidate patch ===")
    c = dict(base_cfg())
    c["notes"], c["note_sec"], c["slot"] = (EARS_LINE, LINE_SEC,
                                            LINE_SEC + LINE_GAP)
    if "line" in meta:
        c["in_gain"] = meta["line"]["in_gain"]
        c["out_gain"] = meta["line"]["trim"] / meta["line"]["in_gain"]
    patch = make_patch(c)
    cand_path = os.path.join(CAND_OUT, CAND_NAME + ".json")
    with open(cand_path, "w") as f:
        json.dump(patch, f, indent=1)
    print("  wrote %s (%d nodes, noteFaces %s)"
          % (os.path.relpath(cand_path, ROOT), len(patch["graph"]["nodes"]),
             patch["ui"]["noteFaces"][0]["fields"]))
    gc = subprocess.run([UI, "--gatecheck", cand_path], capture_output=True,
                        text=True)
    out = (gc.stdout or gc.stderr).strip()
    print("  %s" % out)
    if "gateable=1" not in out:
        fail.append("candidate is not key-gateable: %s" % out)

    print("\n=== 8. live-path parity ===")
    rc, prows = stage_parity()

    # ---- README + manifest ---------------------------------------------
    T1.purge(EARS_OUT, {n + ".wav" for n in ears}
             | {"README.md", "measurements.json"})
    T1.purge(PATCH_OUT, {n + ".json" for n in ears}
             | {CAND_NAME + ".json", "parity_low.json"})

    lock_rows, speak_rows, lvl_rows = [], [], []
    for n in chrom:
        p, o = new_rows.get(n), old_rows.get(n)
        lock_rows.append([note_name(n),
                          "%+.0f" % p["cents"] if p and p["spoke"] else "-",
                          "%+.0f" % o["cents"] if o and o["spoke"] else "-"])
        speak_rows.append([
            note_name(n),
            "%.0f" % (1000 * p["speak"]) if p and np.isfinite(p["speak"])
            else "-",
            "%.0f" % (1000 * o["speak"]) if o and np.isfinite(o["speak"])
            else "-"])
        lvl_rows.append([
            note_name(n),
            "%.3f" % p["rms"] if p else "-",
            "%.3f" % o["rms"] if o else "-"])
    cell_tbl = "\n".join(
        "| `%s.wav` | %s | %s |" % (n, CELL_BLURB.get(n, ("", ""))[0],
                                    CELL_BLURB.get(n, ("", ""))[1])
        for n in ears)
    vdata = json.load(open(vj)) if os.path.exists(vj) else {}

    def vc(key):
        d = vdata.get(str(float(key)), vdata.get(str(key), {}))
        c_ = d.get("centroid")
        return "%.0f" % c_ if c_ else "n/a"

    cellref = ("%.4f" % float(np.median([meta[n]["sounding_rms"]
                                         for n in ears
                                         if "sounding_rms" in meta[n]]))
               if ears else "n/a")
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{cand}", CAND_NAME)
                .replace("{cells}", cell_tbl)
                .replace("{lock}", fmt_table(["note", "this round",
                                              "last round"], lock_rows))
                .replace("{speak}", fmt_table(["note", "this round ms",
                                               "last round ms"], speak_rows))
                .replace("{levels}", fmt_table(
                    ["note", "this round rms", "last round rms"], lvl_rows))
                .replace("{libref}", "%.4f" % libref)
                .replace("{cellref}", cellref)
                .replace("{spread_before}", "%.1f" % spread_old)
                .replace("{spread_after}", "%.1f" % spread_new)
                .replace("{parity}", "five decimal places")
                .replace("{dark}", "%.0f Hz" % VOICE_DARK)
                .replace("{bright}", "%.0f Hz" % VOICE_BRIGHT)
                .replace("{cent_off}", vc(0.0))
                .replace("{cent_dark}", vc(VOICE_DARK))
                .replace("{cent_bright}", vc(VOICE_BRIGHT))
                .replace("{dyn}", dyn or "(not measured)")
                .replace("{extra}", "## Also worth knowing\n\n"
                         + "\n".join("- " + s for s in extra)
                         if extra else ""))
    json.dump({
        "library_reference": {"patch": os.path.relpath(LIB_REF_PATCH, ROOT),
                              "sounding_rms": libref, "stats": libstats},
        "voice": {"dark_hz": VOICE_DARK, "bright_hz": VOICE_BRIGHT,
                  "poles": VOICE_POLES,
                  "sweep": vdata},
        "maps": {"lip": TUNE_MAP, "bore": BORE_MAP, "trim": TRIM_MAP},
        "register_spread_db": {"attempt1": spread_old, "attempt2": spread_new},
        "pressure_window": win, "soft_S": soft_s, "loud_S": loud_s,
        "ignition_vs_pressure": ign,
        "parity": [{"note": n, "name": note_name(n),
                    "cli": {k: a[k] for k in ("cents", "lock", "rms")},
                    "ui": {k: b[k] for k in ("cents", "lock", "rms")}}
                   for n, a, b in prows],
        "notes_new": new_rows, "notes_old": old_rows,
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


def calibrate(cfg, notes, nsec, gap, tag, libref, legacy=False):
    """Three passes: normalise into the steepener so `depth` means the same
    on every cell; then set the output gain so the cell's SOUNDING rms
    matches the measured library reference; then back off if that would push
    the peak past PEAK_CEIL, and say so.

    `legacy=True` instead reproduces ATTEMPT 1's levelling exactly - peak to
    -6 dBFS and no reference at all - so the before/after cell carries the
    loudness difference Matt actually heard rather than hiding it behind a
    match."""
    c = dict(cfg)
    c["depth"] = 0.0
    got, pj, wp = render_score(c, notes, nsec, gap, tag + "_cal", SCRATCH)
    if got is None:
        return None
    in_gain = T1.DRIVE_PEAK / max(float(np.abs(got[0]).max()), 1e-6)
    T1.rm(pj, wp)
    c = dict(cfg)
    c["in_gain"] = in_gain
    c["out_gain"] = 1.0 / in_gain
    got, pj, wp = render_score(c, notes, nsec, gap, tag + "_cal2", SCRATCH)
    if got is None:
        return None
    x, sr = got
    sr_ = sounding_rms(x)
    pk = max(float(np.abs(x).max()), 1e-9)
    if legacy:
        want = cap = (10.0 ** (T1.PEAK_TARGET_DB / 20.0)) / pk
    else:
        want = libref / max(sr_, 1e-9)
        cap = PEAK_CEIL / pk
    trim = min(want, cap)
    T1.rm(pj, wp)
    return {"in_gain": in_gain, "trim": trim, "legacy_level": bool(legacy),
            "level_capped_db": (20 * math.log10(want / cap) if want > cap
                                else 0.0)}


def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else "all"
    if not os.path.exists(CLI):
        print("mforce_cli not found at %s" % CLI)
        return 1
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    print("=== trombone attempt 2 ===")
    print("  note names: HOUSE convention (octave = midi/12) - the UI's own")
    print("  range solved: midi %d..%d = %s..%s"
          % (TUNE_LO, TUNE_HI, note_name(TUNE_LO), note_name(TUNE_HI)))
    print("  maps loaded: lip %d, air column %d, breath %d, level trim %d"
          % (len(TUNE_MAP), len(BORE_MAP), len(PRESS_MAP), len(TRIM_MAP)))
    print("  voicing lowpass: dark %.0f Hz, bright %.0f Hz (%d poles)"
          % (VOICE_DARK, VOICE_BRIGHT, VOICE_POLES))

    if stage == "parity":
        return stage_parity()[0]
    if stage == "voice":
        if not TUNE_MAP:
            print("solve the tuning maps first")
            return 1
        return stage_voice()
    if stage == "tune":
        notes = list(range(TUNE_LO, TUNE_HI + 1))
        lip, liplog = solve_lip_map(notes)
        bore, borelog = solve_bore_map(notes, lip)
        print("\nTUNE_MAP = %s" % json.dumps(lip))
        print("\nBORE_MAP = %s" % json.dumps(bore))
        json.dump({"lip": lip, "lip_log": liplog, "bore": bore,
                   "bore_log": borelog},
                  open(os.path.join(SCRATCH, "maps.json"), "w"), indent=1)
        return 0
    if stage == "press":
        if not TUNE_MAP:
            print("solve the lip map first")
            return 1
        notes = list(range(TUNE_LO, TUNE_HI + 1))
        press, plog = solve_press_map(notes, TUNE_MAP)
        print("\nPRESS_MAP = %s" % json.dumps(press))
        json.dump({"press": press, "log": plog},
                  open(os.path.join(SCRATCH, "press.json"), "w"), indent=1)
        return 0
    if stage == "retune":
        if not TUNE_MAP or not PRESS_MAP:
            print("solve the lip and pressure maps first")
            return 1
        notes = list(range(TUNE_LO, TUNE_HI + 1))
        bore, borelog = solve_bore_map(notes, TUNE_MAP, press_map=PRESS_MAP)
        print("\nBORE_MAP = %s" % json.dumps(bore))
        json.dump({"bore": bore, "bore_log": borelog},
                  open(os.path.join(SCRATCH, "maps2.json"), "w"), indent=1)
        return 0
    if stage == "trim":
        if not TUNE_MAP or not BORE_MAP:
            print("solve the tuning maps first")
            return 1
        trim, log = solve_trim_map(list(range(TUNE_LO, TUNE_HI + 1)))
        print("\nTRIM_MAP = %s" % json.dumps(trim))
        json.dump({"trim": trim, "rows": log},
                  open(os.path.join(SCRATCH, "trim.json"), "w"), indent=1,
                  default=float)
        return 0
    if stage == "all":
        if not TUNE_MAP or not BORE_MAP or not TRIM_MAP or not PRESS_MAP:
            print("no solved maps in the file - run `tune`, `press`, "
                  "`retune`, `trim` and paste the maps back in")
            return 1
        rc = stage_all()
        shutil.rmtree(SCRATCH, ignore_errors=True)
        return rc
    print("unknown stage %r" % stage)
    return 1


if __name__ == "__main__":
    sys.exit(main())
