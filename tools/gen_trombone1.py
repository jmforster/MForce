"""Trombone round 1 - the usable-trombone assembly on the one-mass lip.

Spec/lineage:
  docs/superpowers/specs/2026-09-17-onemass-lip-design.md   (the exciter)
  docs/autonomy/dsp/reports/2026-09-17-onemass-lip1.md      (its measurements)
  docs/research/nonlinear_bore/MSALLAM_DIGEST.md            (the chain)
  tools/gen_onemass_lip1.py                                 (the wiring reused)
  tools/bell_smyth.py                                       (the bell, new)
Report: docs/autonomy/dsp/reports/2026-09-18-trombone1.md

WHY THIS ROUND EXISTS
---------------------
REVIEW 68 verdict (Matt, 09-18): "Attack indeed resembles a brass attack,
though it's still pretty far from a trombone patch. Definitely not saxy."
Mechanism confirmed, four named defects, and his directive: "proceed with
intonation + bell + whatever else is known to be lacking in pursuit of a
usable trombone patch."  Mode: KNOWN TARGET, so this is the full assembly,
not another probe.

  A. INTONATION  - every lip cell ran +18..+33 cents sharp, widening with
     register.  Chased it and found the lip was NOT the cause: the residual
     grows with frequency, which is the signature of the delay line's
     linear-interpolated fractional read being slightly short.  So the trim
     lives in the BORE (BORE_MAP, a measured per-note air-column stretch)
     and the lip ratio (TUNE_MAP) is instead chosen for the widest speaking
     window - the margin against cracking, which is the lip's real job.
     Both maps are solved note by note against rendered pitch by stage
     "tune" and pasted back into this file, so the patch carries the
     measurement rather than a rule.
  B. LOW-NOTE ATTACK TIME - attack time was tied to frequency; low notes
     took "forever".  The dominant cause was the BORE (see below), plus two
     levers from the model's own vocabulary:
       1. the paper's initial condition y'(0) = y1 = -4 m/s (Berjamin Table
          2).  A pull graph cannot set filter state, but an impulse of
          momentum at t=0 IS that initial condition: one sample of force
          u_imp such that the two-pole section's velocity jumps by y1.
          Closed form below - it is not a knob.
       2. a blowing-pressure OVERSHOOT at note start (what a player's tongue
          does).  Mouth envelope stage 0 goes above the sustain level and
          stage 1 decays onto it.  Load-bearing: without it C2 will not
          speak at all.
       3. an onset breath burst into the bore (the same injection point as
          the steady grain, so it is coupled to the tonal path and never
          summed onto the output) is WIRED AND OFF: measured at 12x and
          40x the steady grain it changed no speak time on any note.
  C. KEY-GATED ENVELOPE - on QWERTY the note evolved and released on its own
     clock.  Cause found: the round-68 Mouth envelope had NO expand stage
     (percent == 0), so Envelope::gate_release had nothing to hold in and the
     three fractional stages just ran.  FIXED: literal-seconds stages with a
     real expand in the middle - attack / overshoot-decay / HOLD / release.
     Offline that makes sustain scale with note duration while attack and
     release stay put; live (gated_) the expand holds until key-up.  The
     patch is instrument-style with a ui.noteFaces entry so the UI keyboard
     drives __perf_f.
  D. RELEASE SHAPE - the release mirrored the attack ("b-waa-OO").  FIXED:
     REL_SEC of Expo fall, power 3, ~70 ms; the tail you hear afterwards is
     the bore ringing out inside kVoiceTailSec, which is what a real brass
     release is.
  E. BELL - was a plain 5 kHz lowpass on the output and a 1200 Hz lowpass
     standing in for bore losses AND bell reflection inside the loop.  Now
     both come from Smyth & Scott's measured trombone bell (see
     tools/bell_smyth.py for the full provenance and the two labelled
     substitutions): a fitted minimum-phase REFLECTION cascade terminates the
     bore inside the loop, and a fitted TRANSMISSION cascade is the radiated
     path.  The REVIEW-66 steepener stays where it was, between them.

ALSO CHANGED, because measurement said so rather than the queue:
  * THE BORE.  Round 68 built an air column three note-periods long for every
    note - 15.7 m at C2, which is why low notes took as long as a 15 m tube
    to get going.  A real trombone is ONE tube of about 2.7 m for the whole
    range; the player picks which partial speaks with the lip and trims the
    length with the slide.  See F_BORE_NOM / bmode_node below.
  * THE REGISTER PRESSURE EXPONENT.  Round 68's pm ~ f_lip^2 keytrack held
    the static lip opening constant and left C2 as a near-pure sine 30 dB
    below C5.  PM_EXP is the axis and 0.5 ships; see the constant's comment.

Everything else from round 68 is unchanged, including every physical
constant, the Eq.-47 flow wiring and its one-sample lag.

Outputs (this script OWNS these dirs and purges anything else):
  patches/sweep/trombone1/               the grid
  patches/audition/trombone1/            THE CANDIDATE (Matt's patch queue)
  renders/dsp/audition/trombone1/        <= 8 ears cells + README.md
Usage:
  python tools/gen_trombone1.py            full run
  python tools/gen_trombone1.py tune       re-solve the intonation map only
  python tools/gen_trombone1.py probe      chassis smoke test only
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
import bell_smyth                                            # noqa: E402
from gen_stk_bowed import read_mono                          # noqa: E402
from gen_nlbore_probe1 import ratio_knots, rbj_lowpass       # noqa: E402
from gen_nlbore_probe1 import centroid_all, hf_fraction      # noqa: E402
from gen_nlbore_probe1 import diff_db, level_ceiling, rms    # noqa: E402
from gen_onemass_lip1 import (A_LIP, B0, F_REF, M_LIP, PM_TAB, PSI_HAT,
                              P_REF, Q_LIP, SR, YEQ_HAT, Y_REF, add, expr,
                              flow_nodes, midi_hz, mulg, pts)   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "trombone1")
CAND_OUT = os.path.join(ROOT, "patches", "audition", "trombone1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "trombone1")
SCRATCH = os.path.join(tempfile.gettempdir(), "trombone1_scratch")
CAND_NAME = "trombone_attempt1"

# ===========================================================================
# CHASSIS - unchanged from round 68 unless marked NEW
# ===========================================================================
BORE_MODE = 3.0        # the note is mode 3 of the air column (round 68)
# NEW - the real trombone's architecture.  Round 68's bore was THREE note
# periods long for EVERY note, so the air column grew with the note: at C2
# that is a 15.7 m tube.  A real trombone is ONE tube of about 2.7 m for the
# whole range; the player picks a partial with the lip and trims the length
# with the slide.  "tbn" mode does exactly that: the delay's ratio pin
# carries the partial number p = round(f0 / F_BORE_NOM), so the air column
# is f0/p - always within a couple of semitones of a real trombone's
# fundamental - and the lip has to choose partial p.  F_BORE_NOM is Bb1, the
# tenor trombone's own fundamental in first position.
F_BORE_NOM = 58.27     # PHYSICAL  Hz   Bb1, tenor trombone first position
P_MIN, P_MAX = 1, 10
DC_HZ = 12.0           # cycle DC block
LOOP_AMP = 1.0         # bore loss; the flow absorbs ~60% per bounce already
NOISE_AMP = 0.0015     # steady breath grain
STEEP_DEPTH = 0.03     # REVIEW 66's real-instrument strength
DRIVE_PEAK = 0.8       # normalize into the steepener so depth means the same
LEGACY_DAMP_HZ = 1200.0    # round 68's in-loop lowpass (bell-OFF control)
LEGACY_BELL_HZ = 5000.0    # round 68's output lowpass (bell-OFF control)

# --- NEW: the measured bell ------------------------------------------------
# k_refl = 2 -> THREE in-loop sections.  The DelayLine compensation walk caps
# at 16 members and round 68's cycle already holds 12 with one Damp filter in
# it; three sections put the cycle at 14, two under the cap.  A four-section
# reflection fit is 0.13 dB better in the median and was NOT taken, because a
# silently evicted loop member shows up as a pitch error, not an error message.
BELL_K_REFL = 2
BELL_K_TRANS = 4       # out of loop, costs no members
BELL = bell_smyth.design(k_refl=BELL_K_REFL, k_trans=BELL_K_TRANS,
                         trans="room")
BELL_AXIS = bell_smyth.design(k_refl=BELL_K_REFL, k_trans=BELL_K_TRANS,
                              trans="axis")
REFL_SECS = BELL["refl_sections"]
TRANS_SECS = {"room": BELL["trans_sections"],
              "axis": BELL_AXIS["trans_sections"]}
# The transmission cascade is a rising curve normalized to 0 dB at its own
# maximum, i.e. it only attenuates; TRANS_MAKEUP puts the playing band back to
# a workable level before the queue's own peak normalization.
TRANS_MAKEUP = {"room": 0.0, "axis": 12.0}

# ===========================================================================
# ATTACK LEVERS (defect B)
# ===========================================================================
# 1. Berjamin Table 2's initial condition y'(0) = y1 = -4 m/s.  The two-pole
#    section maps a force sample u (normalized) to a displacement increment
#    B0*u in one sample, i.e. y'' = SR^2 * B0 * u * Y_REF, so one sample of it
#    changes y' by SR * B0 * u * Y_REF.  Setting that equal to y1:
#        u_imp = y1 / (SR * B0 * Y_REF)
#    That is a CLOSED FORM of Table 2, not a tuned value.  KICK_SCALE is the
#    axis (0 = round 68, 1 = the paper's own kick).
Y1_MPS = -4.0                                  # PHYSICAL  m/s   Table 2
KICK_UNIT = Y1_MPS / (SR * B0 * Y_REF)         # NORMALIZED force sample
# 2. blowing-pressure overshoot at note start (tonguing)
ATT_SEC = 0.006        # pressure rise
OVER_SEC = 0.050       # overshoot decay onto the sustain level
OVERSHOOT = 1.5        # peak = (1 + OVERSHOOT) x sustain
# 3. onset breath burst into the bore, same injection point as the grain
BURST_SEC = 0.020
BURST_GAIN = 12.0      # multiplies NOISE_AMP during the burst

# ===========================================================================
# RELEASE (defect D)
# ===========================================================================
REL_SEC = 0.070        # fast, non-mirrored; the bore's ring-out is the tail
REL_POWER = 3.0
LEGACY_REL_FRACTION = 0.14   # round 68's mirrored Sine release, for the A/B

# ===========================================================================
# INTONATION (defect A)
# ===========================================================================
LIP_RATIO_BASE = 0.77        # the round-68 sweep's centring estimate

# ===========================================================================
# REGISTER BALANCE (the fifth defect, found by measurement this round)
# ===========================================================================
# Round 68 keytracked the blowing pressure as pm = S PM_TAB (f_lip/F_REF)^2.
# The exponent 2 holds the STATIC LIP OPENING A pm / k constant across the
# register, because k = m (2 pi f_lip)^2.  Measured consequence at C2: the
# absolute pressure falls to ~0.5 kPa, the flow never leaves its near-linear
# region, and C2 comes out as an almost pure sine 30 dB below C5 (centroid
# 81 Hz against C5's 594 Hz).  A real player does not cut mouth pressure 40x
# to play an octave lower; they loosen the embouchure, i.e. the static opening
# GROWS toward the bottom of the range.  That is an exponent below 2, and
# PM_EXP is the axis.  2.0 reproduces round 68 exactly.
PM_EXP = 0.5
# Solved by stage "tune" and pasted back here: MIDI note -> f_lip/f0 ratio.
# Every entry is MEASURED (secant on rendered pitch), never assumed.
TUNE_MAP = {   # MEASURED lip tuning: midi note -> f_lip/f0
    36: 0.706, 37: 0.706, 38: 0.706, 39: 0.706, 40: 0.706, 41: 0.706,
    42: 0.732, 43: 0.732, 44: 0.732, 45: 0.732, 46: 0.792, 47: 0.732,
    48: 0.732, 49: 0.747, 50: 0.773, 51: 0.788, 52: 0.788, 53: 0.788,
    54: 0.788, 55: 0.788, 56: 0.799, 57: 0.799, 58: 0.814, 59: 0.799,
    60: 0.814, 61: 0.825, 62: 0.825, 63: 0.84, 64: 0.851, 65: 0.851, 66:
    0.866, 67: 0.877, 68: 0.877, 69: 0.873, 70: 0.843, 71: 0.858, 72:
    0.884}

BORE_MAP = {   # MEASURED air-column trim: midi note -> length multiplier
    36: 1.0, 37: 1.001616, 38: 1.002491, 39: 1.00341, 40: 1.004385, 41:
    1.005109, 42: 1.006691, 43: 1.00702, 44: 1.007343, 45: 1.007603, 46:
    1.027106, 47: 1.008102, 48: 1.008286, 49: 1.011843, 50: 1.012038,
    51: 1.015415, 52: 1.015257, 53: 1.014966, 54: 1.014792, 55:
    1.014332, 56: 1.013045, 57: 1.013677, 58: 1.01769, 59: 1.013917, 60:
    1.01731, 61: 1.016217, 62: 1.016262, 63: 1.018776, 64: 1.018503, 65:
    1.018422, 66: 1.021144, 67: 1.01948, 68: 1.018538, 69: 1.013071, 70:
    1.010398, 71: 1.011616, 72: 1.014341}
TUNE_LO, TUNE_HI = 36, 72     # C2..C5, every chromatic step
TUNE_TOL_CENTS = 2.0
TUNE_SCALE = 2.0              # blowing pressure the maps are solved at
TUNE_NOTE_SEC, TUNE_SLOT = 1.4, 1.8
LIP_SCAN_STEP = 0.015         # lip-window scan resolution
LIP_SCAN_N = 8                # +-8 steps around the seed = a 0.24-wide scan
LIP_WINDOW_CENTS = 120.0      # inside this the lip is on the RIGHT partial
BORE_ITERS = 5
BORE_SLOPE = -1731.0          # cents per unit air-column multiplier
                              # (1% longer delay = 1200*log2(1/1.01) c)

# ===========================================================================
# score / axes / gates
# ===========================================================================
NOTES = [36, 48, 60, 72]      # C2..C5, the round's speaking-register probe
SLOT = 2.0
NOTE_SEC = 1.6
SUSTAIN = (0.60, 0.95)     # fractions of the note length
SCAN = [round(0.3 + 0.15 * i, 3) for i in range(16)]
SOFT_POS, LOUD_POS = 0.05, 0.95   # the ends of the MEASURED window

# the audition line: C2 up to C5 through a plain C-major skeleton
EARS_LINE = [36, 43, 48, 52, 55, 60, 64, 67, 72]
LINE_SEC, LINE_GAP = 1.1, 0.35
HELD_NOTE = 53           # F3, middle of the register, for the 6 s cell
DURN_NOTE = 53
MF_SCALE = 2.0           # the candidate's blowing pressure
kVOICE_TAIL = 0.4        # engine's kVoiceTailSec (instrument.h)
MATCH_PEAK_CEIL = 0.79   # a loudness-matched cell may not go past this peak
# round 68's chassis, reproduced exactly, as the "before" column
OLD_CHASSIS = {"bore": "modes3", "pm_exp": 2.0, "ratio": 0.8, "tune_map": {},
               "bore_map": {}, "kick": 0.0, "overshoot": False,
               "burst": False, "release": "legacy", "bell": False,
               "scale": 1.0}
# cells that must NOT be independently levelled, and what they inherit
TRIM_TWIN = {"soft": "loud", "line_old_release": "line"}
RMS_TWIN = {"line_bell_off": "line", "line_bell_onaxis": "line"}

LOCK_RATIO = 0.35        # fraction of energy on the note's harmonic series
LOCK_RMS = 0.004
LOCK_CENTS_GATE = 5.0        # the round's own target
LOCK_CENTS_SPEAK = 50.0      # "it spoke on the written note at all"
LEVEL_CEILING = 0.5
AUDIBILITY_FLOOR = 1e-4
FEATURE_DB = -26.0
PEAK_TARGET_DB = -6.0
SPEAK_TARGET_C3 = 0.150      # seconds to 90% of sustain, the prompt's target


# ===========================================================================
# graph
# ===========================================================================
def biquad_chain(prefix, secs, source):
    nodes, src = [], source
    for i, s in enumerate(secs):
        nid = "%s%d" % (prefix, i)
        nodes.append({"id": nid, "type": "Biquad", "params": {
            "source": src, "mode": 0, "frequency": 220.0, "radius": 0.0,
            "b0": round(s[0], 10), "b1": round(s[1], 10),
            "b2": round(s[2], 10), "a1": round(s[3], 10),
            "a2": round(s[4], 10)}})
        src = {"ref": nid}
    return nodes, nodes[-1]["id"]


def mouth_env(gated_release=True, overshoot=None, over_sec=None,
              att_sec=None):
    """Defect C + D.  Literal-seconds stages with a real expand stage:

        0  attack     0 -> 1+OVERSHOOT   ATT_SEC
        1  overshoot  1+OVERSHOOT -> 1   OVER_SEC     (the tongue letting go)
        2  HOLD       1 -> 1             percent 0 = expand
        3  release    1 -> 0             REL_SEC, Expo power 3

    Offline the expand absorbs the note length, so a 0.5 s and a 6 s note get
    the same attack and the same release and differ only in how long they
    hold.  Live, Envelope::gated_ parks on stage 2 until key-up and
    gate_release() jumps to stage 3 - which is exactly what round 68 could not
    do, because it had no percent-0 stage at all.
    """
    overshoot = OVERSHOOT if overshoot is None else overshoot
    over_sec = OVER_SEC if over_sec is None else over_sec
    att_sec = ATT_SEC if att_sec is None else att_sec
    peak = 1.0 + overshoot
    if gated_release:
        rel = {"type": "Expo", "startVal": 1.0, "endVal": 0.0,
               "percent": REL_SEC, "power": REL_POWER,
               "minSec": 0.0, "maxSec": 0.0}
    else:
        # round 68's release, for the A/B: a slow mirrored Sine.  In seconds
        # mode it has to be given a literal length; LEGACY_REL_FRACTION of the
        # 1.6 s note is what it was.
        rel = {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
               "percent": round(LEGACY_REL_FRACTION * NOTE_SEC, 6),
               "power": 0.0, "minSec": 0.0, "maxSec": 0.0}
    return {"id": "Mouth", "type": "Envelope", "params": {
        "minValue": 0.0, "maxValue": 1.0,
        "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
        "timeMode": "seconds", "timeScale": 1.0,
        "stages": [
            {"type": "Expo", "startVal": 0.0, "endVal": peak,
             "percent": att_sec, "power": 0.7, "minSec": 0.0, "maxSec": 0.0},
            {"type": "Expo", "startVal": peak, "endVal": 1.0,
             "percent": over_sec, "power": 2.0, "minSec": 0.0,
             "maxSec": 0.0},
            {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
             "percent": 0.0, "power": 0.0, "minSec": 0.0, "maxSec": 0.0},
            rel]}}


def one_sample_env(nid):
    """A single sample at 1.0, then silence.  Literal-seconds stage of one
    sample rounds to exactly 1 frame; the trailing expand is the silence."""
    return {"id": nid, "type": "Envelope", "params": {
        "minValue": 0.0, "maxValue": 1.0,
        "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
        "timeMode": "seconds", "timeScale": 1.0,
        "stages": [
            {"type": "Hold", "startVal": 1.0, "endVal": 1.0,
             "percent": round(1.0 / SR, 9), "power": 0.0,
             "minSec": 0.0, "maxSec": 0.0},
            {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
             "percent": 0.0, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}]}}


def burst_env(nid, burst_sec=None, burst_gain=None):
    """Onset breath burst as a GAIN on the steady breath grain: starts at
    burst_gain, falls to 1.0 over burst_sec, stays at 1.0 for the rest of the
    note.  Multiplying rather than adding is what keeps the steady grain
    intact - the burst is the same breath, harder, for 20 ms."""
    burst_sec = BURST_SEC if burst_sec is None else burst_sec
    burst_gain = BURST_GAIN if burst_gain is None else burst_gain
    return {"id": nid, "type": "Envelope", "params": {
        "minValue": 1.0, "maxValue": round(float(burst_gain), 6),
        "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
        "timeMode": "seconds", "timeScale": 1.0,
        "stages": [
            {"type": "Expo", "startVal": 1.0, "endVal": 0.0,
             "percent": burst_sec, "power": 2.0,
             "minSec": 0.0, "maxSec": 0.0},
            {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
             "percent": 0.0, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}]}}


def partial_of(f0):
    """Which partial of a trombone-length air column this note is."""
    return max(P_MIN, min(P_MAX, int(round(f0 / F_BORE_NOM))))


def bmode_node(bore_map=None):
    """The delay's ratio pin: how many note periods of air column this note
    rides, i.e. which partial the player is aiming for.

    Without a map it is the bare staircase p = round(f0/F_BORE_NOM), with
    0.02 Hz-wide steps between flat plateaus - the slide jumping position as
    the written note crosses a partial boundary.

    With a map it is p times a MEASURED per-note correction.  That correction
    is where the intonation fix actually lives (defect A).  Round 68 blamed
    the lip and tried to trim it with the lip ratio; measured this run, the
    residual sharpness is a BORE error - the delay line's linear-interpolated
    fractional read is slightly short, and short by more at higher frequency,
    which is exactly the shape of the error (+1 c at C2, +6 c at C3, +14 c at
    C4, +17 c at C5).  Lengthening the delay by that fraction fixes it at the
    source, and leaves the lip ratio free to be set for the thing only it can
    do - picking the right partial with margin on both sides.
    """
    if bore_map:
        knots = sorted((midi_hz(n), partial_of(midi_hz(n)) * m)
                       for n, m in bore_map.items())
        lo_f, lo_y = knots[0]
        hi_f, hi_y = knots[-1]
        knots = [(20.0, lo_y)] + knots + [(4000.0, hi_y)]
        return pts("Bmode", knots, {"ref": "__perf_f"})
    knots = [(20.0, float(P_MIN))]
    for p in range(P_MIN, P_MAX):
        edge = (p + 0.5) * F_BORE_NOM
        knots.append((edge - 0.01, float(p)))
        knots.append((edge + 0.01, float(p + 1)))
    knots.append((4000.0, float(P_MAX)))
    return pts("Bmode", knots, {"ref": "__perf_f"})


def flip_node(ratio, tune_map):
    """f_lip as a function of the played note.

    ratio form   : f_lip = ratio * f0            (round 68)
    tune_map form: a CurveNode points map from f0 in Hz to f_lip in Hz, with
                   one measured knot per tuned note and linear interpolation
                   between them.  This is defect A's fix: the sharpness grew
                   with register, so one number could never centre the whole
                   range - the correction has to be a curve.
    """
    if not tune_map:
        return expr("Flip", ratio, 0.0, "linear", {"ref": "__perf_f"})
    knots = sorted((midi_hz(n), midi_hz(n) * r) for n, r in tune_map.items())
    # extend flat-in-ratio beyond the measured range so notes outside it still
    # speak (the end ratios are held, not the end frequencies)
    lo_f, lo_y = knots[0]
    hi_f, hi_y = knots[-1]
    knots = ([(lo_f * 0.5, lo_y * 0.5)] + knots + [(hi_f * 2.0, hi_y * 2.0)])
    return pts("Flip", knots, {"ref": "__perf_f"})


def lipmap_node(nid, tune_map, fn):
    """A points map from the played note to fn(f0, f_lip).

    NOT a tap on Flip.  The obvious wiring - read the tuned lip frequency
    back off the Flip node through a {"tap"} - is WRONG here and was caught
    by measurement: a tap never advances its target, and nothing else in the
    graph advances Flip on the sample these pins are pulled, so both the pole
    radius and the blowing pressure read a stale value.  Measured cost of
    that bug: the note came out 35 cents flat and 15 dB quiet (C2, +1.2 c and
    rms 0.0128 correct, -33.7 c and rms 0.0023 through the tap).  Since every
    one of these is a function of the played note alone, each gets its own
    points map on __perf_f and the hazard disappears.
    """
    knots = sorted((midi_hz(n), fn(midi_hz(n), midi_hz(n) * r))
                   for n, r in tune_map.items())
    lo_f, lo_y = knots[0]
    hi_f, hi_y = knots[-1]
    return pts(nid, [(20.0, lo_y)] + knots + [(4000.0, hi_y)],
               {"ref": "__perf_f"})


def frad_node(ratio, tune_map):
    """Pole radius r_p = 1 - pi f_lip/(Q SR), Q fixed at Table 2's 4."""
    if not tune_map:
        return expr("Frad", -math.pi * ratio / (Q_LIP * SR), 1.0, "linear",
                    {"ref": "__perf_f"})
    return lipmap_node("Frad", tune_map,
                       lambda f0, fl: 1.0 - math.pi * fl / (Q_LIP * SR))


def make_patch(cfg):
    """cfg keys:
         ratio        f_lip/f0 when tune_map is empty
         tune_map     measured {midi: ratio} intonation map ({} = off)
         scale        blowing pressure multiplier S
         bell         True = Smyth reflection+transmission, False = round 68
         kick         initial-lip-velocity impulse scale (0..1)
         overshoot    True = tongued pressure onset, False = plain rise
         burst        True = onset breath burst into the bore
         release      "fast" (defect D fix) or "legacy" (round 68's mirror)
         depth        steepener depth
         in_gain/out_gain
         notes/note_sec/slot
    """
    ratio, tune_map = cfg["ratio"], cfg.get("tune_map") or {}
    # Blowing pressure keytrack pm = S * PM_TAB * (f_lip/F_REF)^2 holds the
    # embouchure at one operating point across the register (round 68 §2).
    # With a tuned map f_lip is not ratio*f0, so the keytrack has to read the
    # tuned lip frequency - it rides a tap on Flip for exactly that reason.
    pexp = cfg.get("pm_exp", PM_EXP)
    a_pm = cfg["scale"] * (PM_TAB / P_REF) / F_REF ** pexp
    nodes = [
        {"id": "__perf_f", "type": "PerformNode",
         "params": {"field": "frequency"}},
        flip_node(ratio, tune_map),
        frad_node(ratio, tune_map),
    ]
    if tune_map:
        nodes.append(lipmap_node("Pmk", tune_map,
                                 lambda f0, fl: a_pm * fl ** pexp))
    else:
        nodes.append(expr("Pmk", a_pm * ratio ** pexp, pexp, "power",
                          {"ref": "__perf_f"}))
    fast = cfg.get("release", "fast") == "fast"
    if cfg.get("overshoot", True):
        nodes.append(mouth_env(fast, cfg.get("over_amt"),
                               cfg.get("over_sec")))
    else:
        # round 68's pressure onset: a plain 6% Expo rise, no tongue
        m = mouth_env(fast, 0.0, 1e-6)
        m["params"]["stages"][0]["percent"] = 0.06 * cfg.get("note_sec",
                                                             NOTE_SEC)
        nodes.append(m)
    nodes.append(mulg("Pm", {"ref": "Pmk"}, {"ref": "Mouth"}))
    if cfg.get("kick", 0.0):
        nodes.append(one_sample_env("Kick"))
        nodes.append(add("PmK", {"ref": "Pm"}, {"ref": "Kick"},
                         cfg["kick"] * KICK_UNIT - 1.0))
        force_src = {"ref": "PmK"}
    else:
        force_src = {"ref": "Pm"}
    # Eq. 46, one-sample lag on pe (round 68's single approximation)
    nodes.append(add("Force", force_src, {"tap": "Pe"}, -2.0))
    # Eq. 45a: the two-pole section IS the mass-spring-damper
    nodes.append({"id": "Lip", "type": "Biquad", "params": {
        "source": {"ref": "Force"}, "mode": 1,
        "frequency": {"ref": "Flip"}, "radius": {"ref": "Frad"},
        "b0": round(B0, 12), "b1": 0.0, "b2": 0.0, "a1": 0.0, "a2": 0.0}})
    nodes += flow_nodes({"tap": "Bore"}, {"tap": "Bore"}, {"ref": "Lip"})
    nodes.append(add("Pplus", {"ref": "Pe"}, {"tap": "Bore"}, -2.0))
    nodes.append({"id": "Noise", "type": "WhiteNoiseSource", "params": {
        "amplitude": NOISE_AMP, "boost": 0.0, "continuity": 0.0,
        "density": 1.0, "zeroCrossTendency": 0.0}})
    if cfg.get("burst", True):
        nodes.append(burst_env("Burst", cfg.get("burst_sec"),
                               cfg.get("burst_gain")))
        # breath COUPLED into the tonal path: it enters the bore, at the same
        # point the steady grain does, so the lip feeds on it.  Never summed
        # onto the output.
        nodes.append(mulg("NoiseM", {"ref": "Noise"}, {"ref": "Burst"}))
        noise_ref = {"ref": "NoiseM"}
    else:
        noise_ref = {"ref": "Noise"}
    nodes.append(add("LoopIn", {"ref": "Pplus"}, noise_ref))
    nodes.append({"id": "DCblk", "type": "SVFSource", "params": {
        "cutoffFreq": DC_HZ, "mode": 4, "normalize": False,
        "resonance": 0.7, "source": {"ref": "LoopIn"}}})
    if cfg.get("bell", True):
        chain, last = biquad_chain("Refl", REFL_SECS, {"ref": "DCblk"})
        nodes += chain
    else:
        nodes.append({"id": "Damp", "type": "SVFSource", "params": {
            "cutoffFreq": LEGACY_DAMP_HZ, "mode": 0, "normalize": False,
            "resonance": 0.5, "source": {"ref": "DCblk"}}})
        last = "Damp"
    if cfg.get("bore", "tbn") == "tbn":
        nodes.append(bmode_node(cfg.get("bore_map")))
        bore_ratio = {"ref": "Bmode"}
    else:
        bore_ratio = BORE_MODE
    nodes.append({"id": "Bore", "type": "DelayLine", "params": {
        "source": {"ref": last}, "frequency": {"ref": "__perf_f"},
        "ratio": bore_ratio, "amplitude": LOOP_AMP, "compensate": True}})

    # --- out of loop: steepener, then the radiated path -------------------
    nodes.append(mulg("NL_in", {"ref": "Bore"},
                      round(float(cfg.get("in_gain", 1.0)), 9)))
    depth = cfg.get("depth", STEEP_DEPTH)
    nodes.append({"id": "NL_curve", "type": "CurveNode", "params": {
        "exprKnots": [], "interp": "linear", "knots": ratio_knots(depth),
        "mode": "points", "source": {"tap": "NL_in"}}})
    nodes.append({"id": "NL_delay", "type": "DelayLine", "params": {
        "source": {"ref": "NL_in"}, "frequency": 120.0,
        "ratio": {"ref": "NL_curve"}, "amplitude": 1.0,
        "compensate": False}})
    if cfg.get("bell", True):
        mode = cfg["bell"] if cfg["bell"] in TRANS_SECS else "room"
        chain, last = biquad_chain("Trans", TRANS_SECS[mode],
                                   {"ref": "NL_delay"})
        nodes += chain
        og = cfg.get("out_gain", 1.0) * 10.0 ** (TRANS_MAKEUP[mode] / 20.0)
    else:
        p = {"source": {"ref": "NL_delay"}, "mode": 0, "frequency": 220.0,
             "radius": 0.0}
        p.update({k: round(v, 9) for k, v in
                  rbj_lowpass(LEGACY_BELL_HZ).items()})
        nodes.append({"id": "NL_bell", "type": "Biquad", "params": p})
        last = "NL_bell"
        og = cfg.get("out_gain", 1.0)
    nodes.append(mulg("NL_out", {"ref": last}, round(float(og), 9)))

    notes = cfg.get("notes", NOTES)
    nsec = cfg.get("note_sec", NOTE_SEC)
    slot = cfg.get("slot", SLOT)
    score = [{"note": n, "time": slot * k, "duration": nsec, "velocity": 0.8}
             for k, n in enumerate(notes)]
    # A simple left-to-right layout so the patch opens as a readable graph
    # in the UI rather than 39 nodes stacked on the origin.
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


# ===========================================================================
# measurement
# ===========================================================================
def seg_of(x, sr, k, slot=SLOT, nsec=NOTE_SEC):
    """The steady part of note k.  The window is a FRACTION of the note, not
    a fixed pair of seconds: a fixed window measured half a second of silence
    after the end of every 1.0 s note in this round's pressure scan and
    reported the whole instrument as dead."""
    a = int((slot * k + SUSTAIN[0] * nsec) * sr)
    b = int((slot * k + SUSTAIN[1] * nsec) * sr)
    return x[a:min(b, len(x))]


def harmonic_lock(seg, sr, hz, nharm=12):
    """Fraction of the segment's energy sitting on the harmonic series of hz.

    Round 68 measured energy at f0 ALONE.  That stops working the moment a
    real bell is in the path: the measured transmission is ~40 dB down at
    100 Hz, so a perfectly in-tune low note carries almost no energy AT its
    fundamental and the old gate called it dead (measured this run - every
    note below D3 failed the f0-only gate while its pitch was right).  The
    harmonic sum is the honest test of "is it playing this note".
    """
    if len(seg) < 512:
        return 0.0
    s = seg - seg.mean()
    sp = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2
    fr = np.fft.rfftfreq(len(s), 1.0 / sr)
    tot = float(sp.sum()) + 1e-20
    acc = 0.0
    for n in range(1, nharm + 1):
        f = hz * n
        if f > sr * 0.45:
            break
        acc += float(sp[(fr > f * 0.97) & (fr < f * 1.03)].sum())
    return acc / tot


def f0_autocorr(seg, sr, target):
    if len(seg) < 1024 or rms(seg) < 1e-5:
        return float("nan")
    s = seg - seg.mean()
    n = len(s)
    nf = 1 << int(math.ceil(math.log2(2 * n)))
    sp = np.fft.rfft(s, nf)
    ac = np.fft.irfft(sp * np.conj(sp), nf)[:n]
    ac = ac / (ac[0] + 1e-20)
    lo = max(2, int(sr / (target * 3.0)))
    hi = min(n - 2, int(sr / (target / 3.0)))
    if hi <= lo + 3:
        return float("nan")
    i = int(np.argmax(ac[lo:hi])) + lo
    if ac[i] < 0.2 or i <= lo or i >= hi - 1:
        return float("nan")
    y0, y1, y2 = ac[i - 1], ac[i], ac[i + 1]
    den = y0 - 2.0 * y1 + y2
    d = 0.5 * (y0 - y2) / den if abs(den) > 1e-12 else 0.0
    return sr / (i + max(-1.0, min(1.0, d)))


def cents(f, target):
    if not (f and np.isfinite(f) and f > 0):
        return float("nan")
    return 1200.0 * math.log2(f / target)


def env_rms(x, sr, hop_ms=2.0, win_ms=8.0):
    hop = max(1, int(sr * hop_ms / 1000.0))
    win = max(hop, int(sr * win_ms / 1000.0))
    n = max(0, (len(x) - win) // hop + 1)
    out = np.empty(n)
    for i in range(n):
        s = x[i * hop: i * hop + win]
        out[i] = math.sqrt(float(np.mean(s * s)) + 1e-30)
    return out, hop / sr


def speak_time(x, sr, k, slot=SLOT, nsec=NOTE_SEC):
    """Seconds from note-on to 90% of the note's own sustained level."""
    a = int(slot * k * sr)
    b = int((slot * k + nsec) * sr)
    seg = x[a:min(b, len(x))]
    if len(seg) < 1000:
        return float("nan"), 0.0
    e, dt = env_rms(seg, sr)
    if len(e) < 10:
        return float("nan"), 0.0
    tail = e[int(0.55 * len(e)):int(0.95 * len(e))]
    sus = float(np.median(tail)) if len(tail) else 0.0
    if sus < LOCK_RMS * 0.25:
        return float("nan"), sus
    idx = np.where(e >= 0.9 * sus)[0]
    return (float(idx[0] * dt) if len(idx) else float("nan")), sus


def note_stats(x, sr, notes=None, slot=SLOT, nsec=NOTE_SEC,
               bore_mode=BORE_MODE):
    notes = notes or NOTES
    out = []
    for k, n in enumerate(notes):
        seg = seg_of(x, sr, k, slot, nsec)
        tgt = midi_hz(n)
        f0 = f0_autocorr(seg, sr, tgt)
        c = cents(f0, tgt)
        lk = harmonic_lock(seg, sr, tgt)
        rr = rms(seg)
        st, sus = speak_time(x, sr, k, slot, nsec)
        out.append({
            "note": n, "target_hz": tgt, "f0": f0, "cents": c, "lock": lk,
            "rms": rr, "speak": st, "sustain_rms": sus,
            "spoke": bool(np.isfinite(c) and abs(c) <= LOCK_CENTS_SPEAK
                          and lk > LOCK_RATIO and rr > LOCK_RMS),
            "locked": bool(np.isfinite(c) and abs(c) <= LOCK_CENTS_GATE
                           and lk > LOCK_RATIO and rr > LOCK_RMS),
            "partial": (f0 * bore_mode / tgt if np.isfinite(f0)
                        else float("nan")),
            "centroid": centroid_all(seg, sr) if len(seg) else float("nan"),
            "hf1k": hf_fraction(seg, sr, 1000.0) if len(seg) else
            float("nan")})
    return out


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


def rm(*paths):
    for p in paths:
        if p and os.path.exists(p):
            os.remove(p)


def purge(d, keep):
    os.makedirs(d, exist_ok=True)
    for fn in os.listdir(d):
        if fn not in keep:
            os.remove(os.path.join(d, fn))
            print("purged stale: %s/%s" % (os.path.basename(d), fn))


def trim_for(x):
    pk = max(float(np.abs(x).max()), 1e-9)
    return (10.0 ** (PEAK_TARGET_DB / 20.0)) / pk


def base_cfg(**kw):
    cfg = {"ratio": LIP_RATIO_BASE, "tune_map": dict(TUNE_MAP),
           "bore_map": dict(BORE_MAP), "scale": 1.0,
           "bore": "tbn", "pm_exp": PM_EXP,
           "bell": True, "kick": 1.0, "overshoot": True, "burst": False,
           "release": "fast", "depth": STEEP_DEPTH, "in_gain": 1.0,
           "out_gain": 1.0}
    cfg.update(kw)
    return cfg


# ===========================================================================
# stages
# ===========================================================================
def stage_probe():
    """Chassis smoke test: does the measured bell keep the thing alive?"""
    print("=== probe: bell on/off across the pressure scan ===")
    for bell in (False, True):
        row = []
        for s in (0.5, 1.0, 1.5, 2.0, 3.0):
            got, pj, wp = render(make_patch(base_cfg(scale=s, bell=bell,
                                                     tune_map={})),
                                 "probe_%d_%03d" % (bell, int(s * 100)),
                                 SCRATCH)
            if got is None:
                row.append("x")
                continue
            st = note_stats(*got)
            row.append("%d" % sum(1 for p in st if p["spoke"]))
            print("   bell=%s S=%.2f  %s" % (
                bell, s, " ".join(
                    "%s%+.0fc/%.0fms" % (
                        "C" + str(p["note"] // 12 - 1),
                        p["cents"] if np.isfinite(p["cents"]) else 0,
                        1000 * p["speak"] if np.isfinite(p["speak"]) else -1)
                    for p in st)))
            rm(pj, wp)
        print("  bell=%s spoke-counts %s" % (bell, "".join(row)))
    return 0


def seed_ratio(p):
    """Starting lip ratio for partial p, from this round's own probe:
    p=1 -> 0.70, p=2 -> 0.70, p=4 -> 0.80, p=9 -> 0.90.  The trend is
    physics, not curve fitting: the higher the partial, the closer together
    the air column's modes sit in cents, so the lip - which always sounds
    ABOVE its own resonance - has to be tuned nearer the note to pick the
    right mode out of a denser set."""
    return 0.68 + 0.026 * p


def measure_one(n, lip_ratio, bore_mult, scale, tag="tn"):
    """Render note n alone and report (cents, lock, rms)."""
    cfg = base_cfg(ratio=lip_ratio, tune_map={}, bore_map={n: bore_mult},
                   notes=[n], note_sec=TUNE_NOTE_SEC, slot=TUNE_SLOT,
                   scale=scale)
    got, pj, wp = render(make_patch(cfg), "%s_%d" % (tag, n), SCRATCH)
    if got is None:
        return float("nan"), 0.0, 0.0
    st = note_stats(got[0], got[1], [n], TUNE_SLOT, TUNE_NOTE_SEC)[0]
    rm(pj, wp)
    c = st["cents"]
    if st["lock"] < LOCK_RATIO or st["rms"] < LOCK_RMS:
        c = float("nan")
    return c, st["lock"], st["rms"]


def solve_lip_map(notes, scale=None, verbose=True):
    """Choose the lip tuning per note for the widest MARGIN, not for pitch.

    Why margin and not pitch: measured this run, the lip ratio moves the
    played pitch by only ~170 cents per unit near the operating point, while
    the window in which the note speaks at all is about 0.10 wide - so
    chasing the last 15 cents with the lip walks straight off the edge of
    the window and the note stops speaking (measured: the round's first
    solver oscillated between +16 c and silence).  The lip's real job is
    picking the right partial; the cents belong to the bore (solve_bore_map).

    So: scan the ratio across the window, keep every setting that speaks the
    written note, and take the middle of the widest unbroken run.
    """
    notes = notes or list(range(TUNE_LO, TUNE_HI + 1))
    print("=== lip map: widest speaking window per note (%d notes) ==="
          % len(notes))
    out, log = {}, {}
    for n in notes:
        f0 = midi_hz(n)
        p = partial_of(f0)
        seed = seed_ratio(p)
        grid = [round(seed + LIP_SCAN_STEP * i, 4)
                for i in range(-LIP_SCAN_N, LIP_SCAN_N + 1)]
        ok, cents_at = [], {}
        for r in grid:
            c, lk, rr = measure_one(n, r, 1.0, scale if scale is not None
                                    else TUNE_SCALE, "lipscan")
            good = bool(np.isfinite(c) and abs(c) <= LIP_WINDOW_CENTS)
            ok.append(good)
            cents_at[r] = c
        # widest contiguous run of speaking settings
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
            log[n] = {"ratio": out[n], "partial": p, "window": None,
                      "cents": None}
            if verbose:
                print("  %3d %7.2f Hz  partial %2d   NO SPEAKING WINDOW, "
                      "seed %.4f kept" % (n, f0, p, seed))
            continue
        mid = (best_a + best_b) // 2
        r = grid[mid]
        out[n] = r
        log[n] = {"ratio": r, "partial": p,
                  "window": [grid[best_a], grid[best_b]],
                  "window_steps": best_b - best_a + 1,
                  "cents": (None if not np.isfinite(cents_at[r])
                            else round(float(cents_at[r]), 1))}
        if verbose:
            print("  %3d %7.2f Hz  partial %2d  window %.3f..%.3f (%d steps)"
                  "  -> ratio %.4f at %s cents"
                  % (n, f0, p, grid[best_a], grid[best_b],
                     best_b - best_a + 1, r,
                     "----" if not np.isfinite(cents_at[r])
                     else "%+5.1f" % cents_at[r]))
    return out, log


def solve_bore_map(notes, lip_map, scale=None, verbose=True):
    """Defect A proper.  With the lip parked in the middle of its window,
    stretch the air column until the rendered pitch is equal temperament.

    A delay 1% longer flattens the note by ~17 cents, so the secant here is
    on a near-exact linear relation and converges in two or three renders.
    """
    notes = notes or list(range(TUNE_LO, TUNE_HI + 1))
    print("=== bore map: air-column trim per note (%d notes) ===" % len(notes))
    out, log = {}, {}
    for n in notes:
        r = lip_map.get(n, seed_ratio(partial_of(midi_hz(n))))
        m, prev, best, hist = 1.0, None, None, []
        for _ in range(BORE_ITERS):
            c, lk, rr = measure_one(n, r, m, scale if scale is not None
                                    else TUNE_SCALE, "borescan")
            hist.append((round(m, 5), None if not np.isfinite(c)
                         else round(float(c), 1)))
            if np.isfinite(c) and (best is None or abs(c) < abs(best[1])):
                best = (m, float(c))
            if np.isfinite(c) and abs(c) <= TUNE_TOL_CENTS:
                break
            if not np.isfinite(c):
                break                       # the lip lost it; keep the best
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
            print("  %3d %7.2f Hz  lip %.4f  air column x%.5f  -> %s cents"
                  " (%d renders)"
                  % (n, midi_hz(n), r, out[n],
                     "----" if best is None else "%+5.1f" % best[1],
                     len(hist)))
    return out, log


# ===========================================================================
README = """# Trombone, attempt 1 - "is this a usable trombone?"

Renders here; the candidate patch is
`patches/audition/trombone1/{cand}.json`; sweep patches in
`patches/sweep/trombone1/`; generator `tools/gen_trombone1.py` (the bell
lives in `tools/bell_smyth.py`); run report
`docs/autonomy/dsp/reports/2026-09-18-trombone1.md`.

## What changed since the round you just heard

You said the attack was a brass attack but it was still a long way from a
trombone, and you listed four things wrong. All four are fixed, the bell is
in, and one more thing turned up on the way that mattered more than any of
them.

1. **It was sharp** - 18 to 33 cents, worse the higher it went. Chased it and
   found the sharpness was not the lip at all: the delay line that carries
   the air column reads back very slightly short, and shorter at higher
   frequencies, which is exactly the shape of the error. So the fix is a
   measured per-note stretch of the tube. Every note from C2 to C5 was solved
   against its own rendered pitch.
2. **Low notes took forever to speak.** The real cause was the tube: the old
   patch built a tube three note-lengths long for EVERY note, so a low C rode
   a 15 metre air column and took as long as one to get going. A real
   trombone is one tube of about 2.7 metres for the whole range - the player
   picks which of the tube's notes speaks with the lips and trims the length
   with the slide. This patch does that. On top of it, the note starts with
   a brief pressure overshoot (what tonguing is) and a flick of the lip at
   note-on that comes straight out of the published model's own starting
   conditions.
3. **The note ignored the key.** The blowing envelope had no hold stage at
   all, so it always ran its own fixed schedule regardless of the key. It now
   holds while the key is down and releases when you let go, and the patch is
   a proper instrument patch with a keyboard face, so the UI plays it.
4. **The release mirrored the attack** ("b-waa-OO"). The pressure now drops in
   about 70 ms and what you hear after that is the tube ringing out, which is
   what a brass release actually is.
5. **The bell.** It used to be a plain lowpass. It is now two filters worked
   out from a paper that MEASURED a real trombone bell (Smyth & Scott, 2011):
   one for what the bell reflects back down the tube, one for what it
   radiates into the room. The honest caveats are in the report - the paper's
   own curves are pictures with no numbers behind them, so what shipped is
   the paper's model solved on the paper's measured bell shape, not traced
   off a graph.

**THE QUESTION: is this a usable trombone?** Not "is it better than last
time" - would you put it on a track and play it.

## The single comparison that matters

**`{best_a}.wav` vs `{best_b}.wav`** - {best_why}

## Cells

| file | what it is | listen for |
|---|---|---|
{cells}

## Measurements

Peaks are normalised to -6 dBFS across the queue, so loudness differences you
hear are timbre and not gain - except the soft cell, which is played at the
loud cell's gain on purpose so the dynamic difference is real.

### Tuning - how far each note lands from where it should be, in cents

100 cents = one semitone. The target this round was +-5 on every note that
speaks.

{lock}

### Speaking time - milliseconds from key-down to 90% of the note's own level

The old round's chassis is in the same table, measured the same way.

{speak}

### Note length

The same note rendered at three different lengths, to prove the hold stage
works: the attack and the release should not move, only the middle.

{durn}

{extra}
"""

CELL_BLURB = {
    "line": ("the candidate, a line across its whole range at mf",
             "is this a trombone? tuning, attack, and whether the bottom "
             "and the top sound like the same instrument"),
    "line_bell_off": ("the same line with the measured bell replaced by the "
                      "plain lowpass it used to have",
                      "what the real bell is worth - B of the headline pair"),
    "line_old_release": ("the same line with the OLD release back on",
                         "the 'b-waa-OO' you flagged, against the fast one"),
    "held_6s": ("one note held for six seconds",
                "does it just sit there and stay a note, and does the end "
                "sound like a player stopping"),
    "short_notes": ("the same pitches as half-second notes",
                    "articulation - does it speak in time to be playable"),
    "line_bell_onaxis": ("the same line with the bell read the way the paper "
                         "measured it - a microphone right in the bell",
                         "the same instrument with your ear inside the bell: "
                         "brighter, and probably too bright"),
    "soft": ("the line blown gently, at the loud cell's playback gain",
             "brass goes dull and small when soft"),
    "loud": ("the line blown hard",
             "brass goes bright and edgy when loud"),
}


def render_score(cfg, notes, nsec, gap, tag, out_dir):
    c = dict(cfg)
    c["notes"] = notes
    c["note_sec"] = nsec
    c["slot"] = nsec + gap
    return render(make_patch(c), tag, out_dir)


def survey(cfg, notes, tag, nsec=1.4, gap=0.4, chunk=8):
    """Measure every note in `notes` (cents / speak time / level), a few per
    render so the cost stays sane."""
    rows = {}
    for i in range(0, len(notes), chunk):
        grp = notes[i:i + chunk]
        got, pj, wp = render_score(cfg, grp, nsec, gap, "%s_%d" % (tag, i),
                                   SCRATCH)
        if got is None:
            continue
        st = note_stats(got[0], got[1], grp, nsec + gap, nsec)
        for p in st:
            rows[p["note"]] = p
        rm(pj, wp)
    return rows


def calibrate(cfg, notes, nsec, gap, tag):
    """Two passes: normalize the loop's output into the steepener so `depth`
    means the same thing on every cell, then peak-normalize the result."""
    c = dict(cfg)
    c["depth"] = 0.0
    got, pj, wp = render_score(c, notes, nsec, gap, tag + "_cal", SCRATCH)
    if got is None:
        return None
    in_gain = DRIVE_PEAK / max(float(np.abs(got[0]).max()), 1e-6)
    rm(pj, wp)
    c = dict(cfg)
    c["in_gain"] = in_gain
    c["out_gain"] = 1.0 / in_gain
    got, pj, wp = render_score(c, notes, nsec, gap, tag + "_cal2", SCRATCH)
    if got is None:
        return None
    trim = trim_for(got[0])
    rm(pj, wp)
    return {"in_gain": in_gain, "trim": trim}


def fmt_table(header, rows):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---" for _ in header) + "|"]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n):
    return "%s%d" % (NOTE_NAMES[n % 12], n // 12 - 1)


def stage_all():
    fail, extra = [], []
    os.makedirs(CAND_OUT, exist_ok=True)
    os.makedirs(EARS_OUT, exist_ok=True)
    cand = base_cfg(scale=MF_SCALE)
    chrom = list(range(TUNE_LO, TUNE_HI + 1))

    # ---- 1. does the whole chromatic range speak, and in tune? -----------
    print("\n=== 1. speaking register, every chromatic note C2..C5 ===")
    new_rows = survey(cand, chrom, "survey_new")
    old_cfg = base_cfg(**OLD_CHASSIS)
    old_rows = survey(old_cfg, chrom, "survey_old")
    speaking, miss = [], []
    for n in chrom:
        p = new_rows.get(n)
        if not p:
            continue
        if p["spoke"]:
            speaking.append(n)
            if not p["locked"]:
                miss.append((n, p["cents"]))
    print("  speaks on %d of %d chromatic notes" % (len(speaking), len(chrom)))
    worst = max((abs(p["cents"]) for p in new_rows.values()
                 if p["spoke"] and np.isfinite(p["cents"])), default=999)
    print("  worst tuning error among speaking notes: %.1f cents" % worst)
    if miss:
        print("  outside +-%.0f c: %s" % (LOCK_CENTS_GATE, ", ".join(
            "%s %+.0f" % (note_name(n), c) for n, c in miss)))
        extra.append("%d of the %d speaking notes miss the +-%.0f cent "
                     "target: %s." % (len(miss), len(speaking),
                                      LOCK_CENTS_GATE,
                                      ", ".join("%s %+.0f c" % (note_name(n),
                                                                c)
                                                for n, c in miss)))
    if not speaking:
        print("NOTHING SPEAKS - stopping")
        return 2
    dead = [n for n in chrom if n not in speaking]
    if dead:
        extra.append("These notes do not speak at all and are the edge of "
                     "the register: %s." % ", ".join(note_name(n)
                                                     for n in dead))

    # speak-time comparison, old chassis vs new
    sp_new = [new_rows[n]["speak"] for n in speaking
              if np.isfinite(new_rows[n]["speak"])]
    print("  speak time (new): median %.0f ms, worst %.0f ms"
          % (1000 * np.median(sp_new), 1000 * max(sp_new)))
    c3 = new_rows.get(48, {}).get("speak", float("nan"))
    if not (np.isfinite(c3) and c3 <= SPEAK_TARGET_C3):
        fail.append("C3 speak time %.0f ms over the %.0f ms target"
                    % (1000 * c3, 1000 * SPEAK_TARGET_C3))

    # ---- 2. ignition window / dynamics ----------------------------------
    print("\n=== 2. blowing-pressure window ===")
    win = []
    for s in SCAN:
        rows = survey(base_cfg(scale=s), EARS_LINE, "scan_%03d" % int(s * 100),
                      nsec=1.0, gap=0.2)
        k = sum(1 for p in rows.values() if p["spoke"])
        win.append((s, k))
        print("  S %.2f  %d of %d notes speak" % (s, k, len(EARS_LINE)))
    good = [s for s, k in win if k == len(EARS_LINE)]
    if good:
        soft_s = round(good[0] + (good[-1] - good[0]) * SOFT_POS, 3)
        loud_s = round(good[0] + (good[-1] - good[0]) * LOUD_POS, 3)
        print("  full-range window S %.2f..%.2f = %.1f..%.1f kPa"
              % (good[0], good[-1], good[0] * PM_TAB / 1000,
                 good[-1] * PM_TAB / 1000))
    else:
        best_s = max(win, key=lambda t: t[1])[0]
        soft_s, loud_s = round(best_s * 0.8, 3), round(best_s * 1.3, 3)
        extra.append("No single blowing pressure speaks every note of the "
                     "audition line; the soft and loud cells sit either side "
                     "of the best one.")
    print("  soft S %.2f (%.1f kPa), loud S %.2f (%.1f kPa)"
          % (soft_s, soft_s * PM_TAB / 1000, loud_s, loud_s * PM_TAB / 1000))

    # ---- 3. note length scaling (defect C, offline proof) ---------------
    print("\n=== 3. does the note scale with its length? (defect C) ===")
    durn = []
    for d in (0.5, 2.0, 6.0):
        got, pj, wp = render_score(cand, [DURN_NOTE], d, 0.6,
                                   "durn_%d" % int(d * 10), SCRATCH)
        if got is None:
            fail.append("duration render %.1f s" % d)
            continue
        x, sr = got
        e, dt = env_rms(x, sr)
        pk = float(e.max()) + 1e-12
        onx = np.where(e > 0.5 * pk)[0]
        t_on = float(onx[0] * dt) if len(onx) else float("nan")
        t_off = float(onx[-1] * dt) if len(onx) else float("nan")
        tail = np.where(e > 0.05 * pk)[0]
        t_end = float(tail[-1] * dt) if len(tail) else float("nan")
        durn.append((d, t_on, t_off, t_end, t_off - t_on))
        print("  %.1f s note: above half level %.3f..%.3f s (%.3f s), "
              "audible until %.3f s" % (d, t_on, t_off, t_off - t_on, t_end))
        rm(pj, wp)
    if len(durn) == 3:
        holds = [r[4] for r in durn]
        if not (holds[0] < holds[1] < holds[2]):
            fail.append("the note does not get longer when the note gets "
                        "longer: %s" % [round(h, 3) for h in holds])
        late = [r[3] - r[0] for r in durn]
        if max(late) > kVOICE_TAIL + 0.25:
            fail.append("sound runs %.2f s past the end of the note"
                        % max(late))

    # ---- 4. ears cells ---------------------------------------------------
    print("\n=== 4. ears cells ===")
    cells = {
        "line": (base_cfg(scale=MF_SCALE), EARS_LINE, LINE_SEC, LINE_GAP),
        "line_bell_off": (base_cfg(scale=MF_SCALE, bell=False), EARS_LINE,
                          LINE_SEC, LINE_GAP),
        "line_old_release": (base_cfg(scale=MF_SCALE, release="legacy"),
                             EARS_LINE, LINE_SEC, LINE_GAP),
        "held_6s": (base_cfg(scale=MF_SCALE), [HELD_NOTE], 6.0, 0.8),
        "short_notes": (base_cfg(scale=MF_SCALE), EARS_LINE, 0.5, 0.25),
        "soft": (base_cfg(scale=soft_s), EARS_LINE, LINE_SEC, LINE_GAP),
        "loud": (base_cfg(scale=loud_s), EARS_LINE, LINE_SEC, LINE_GAP),
        "line_bell_onaxis": (base_cfg(scale=MF_SCALE, bell="axis"),
                             EARS_LINE, LINE_SEC, LINE_GAP),
    }
    meta, staged = {}, []
    for name, (cfg, notes, nsec, gap) in cells.items():
        cal = calibrate(cfg, notes, nsec, gap, name)
        if cal is None:
            fail.append("%s calibration" % name)
            continue
        meta[name] = cal
        staged.append(name)
    # twins: the soft cell keeps the loud cell's gain so the dynamic is real;
    # the release A/B and the bell A/B keep the candidate's gain so the only
    # difference is the thing being tested.
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
        lc, pk, rr = level_ceiling(x, sr), float(np.abs(x).max()), rms(x)
        meta[name].update({"level": lc, "peak": pk, "rms": rr,
                           "notes": notes, "note_sec": nsec,
                           "centroid": centroid_all(x, sr),
                           "hf1k": hf_fraction(x, sr, 1000.0)})
        flags = []
        if lc > LEVEL_CEILING:
            flags.append("LEVEL")
            fail.append("%s 0.5 s rms %.2f over the ceiling" % (name, lc))
        if rr < AUDIBILITY_FLOOR:
            flags.append("SILENT")
            fail.append("%s is silent" % name)
        print("  %-18s peak %.3f  0.5 s rms %.3f  rms %.5f  centroid %4.0f %s"
              % (name, pk, lc, rr, meta[name]["centroid"], " ".join(flags)))
        ears.append(name)

    # the bell A/B must be LOUDNESS-matched, not just peak-matched: the
    # bell-off leg packs far more energy under the same peak and would win
    # the comparison on level alone.
    for name, twin in RMS_TWIN.items():
        if name not in ears or twin not in ears:
            continue
        own, tw = meta[name].get("rms"), meta[twin].get("rms")
        if not own or not tw:
            continue
        # never let the match push a cell near full scale: a loudness match
        # that boosts is bounded by MATCH_PEAK_CEIL, and if the bound bites
        # the residual mismatch is reported instead of hidden.
        want = meta[name]["trim"] * tw / own
        cap = meta[name]["trim"] * MATCH_PEAK_CEIL / max(meta[name]["peak"],
                                                         1e-9)
        meta[name]["trim"] = min(want, cap)
        meta[name]["rms_matched_to"] = twin
        if want > cap:
            extra.append("`%s` could not be brought all the way up to "
                         "`%s`'s loudness without running out of headroom - "
                         "it is %.1f dB quieter than a true match."
                         % (name, twin, 20 * math.log10(want / cap)))
        cfg, notes, nsec, gap = cells[name]
        c = dict(cfg)
        c["in_gain"] = meta[name]["in_gain"]
        c["out_gain"] = meta[name]["trim"] / meta[name]["in_gain"]
        got, pj, wp = render_score(c, notes, nsec, gap, name, EARS_OUT)
        if got is None:
            fail.append("%s rms-match render" % name)
            continue
        x, sr = got
        meta[name].update({"level": level_ceiling(x, sr),
                           "peak": float(np.abs(x).max()), "rms": rms(x),
                           "centroid": centroid_all(x, sr),
                           "hf1k": hf_fraction(x, sr, 1000.0)})
        print("  %-18s loudness-matched to %s: peak %.3f  0.5 s rms %.3f"
              % (name, twin, meta[name]["peak"], meta[name]["level"]))
        if meta[name]["level"] > LEVEL_CEILING:
            fail.append("%s 0.5 s rms %.2f over the ceiling"
                        % (name, meta[name]["level"]))

    # ---- 5. feature audibility ------------------------------------------
    print("\n=== 5. is each change audible? ===")
    for a_n, b_n in (("line", "line_bell_off"), ("line", "line_old_release"),
                     ("loud", "soft")):
        if a_n in ears and b_n in ears:
            a, _ = read_mono(os.path.join(EARS_OUT, a_n + ".wav"))
            b, _ = read_mono(os.path.join(EARS_OUT, b_n + ".wav"))
            d = diff_db(a, b)
            print("  %-18s vs %-18s  %6.1f dB  %s"
                  % (a_n, b_n, d, "ok" if d > FEATURE_DB else "INAUDIBLE"))
            if d <= FEATURE_DB:
                fail.append("%s is inaudible against %s (%.1f dB)"
                            % (a_n, b_n, d))
    if "loud" in meta and "soft" in meta:
        print("  soft centroid %4.0f -> loud centroid %4.0f   energy >1 kHz "
              "%.4f -> %.4f"
              % (meta["soft"]["centroid"], meta["loud"]["centroid"],
                 meta["soft"]["hf1k"], meta["loud"]["hf1k"]))
        if meta["loud"]["centroid"] <= meta["soft"]["centroid"]:
            fail.append("blowing harder does not make it brighter")

    # ---- 6. the candidate patch -----------------------------------------
    print("\n=== 6. candidate patch ===")
    c = dict(base_cfg(scale=MF_SCALE))
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

    # ---- 7. README + manifest -------------------------------------------
    # this script OWNS its three directories: anything not in the current
    # cell set goes, so a renamed cell can never leave a stale WAV in the
    # listening queue (the 2026-09-16 speakers incident).
    purge(EARS_OUT, {n + ".wav" for n in ears}
          | {"README.md", "measurements.json"})
    purge(CAND_OUT, {CAND_NAME + ".json"})
    purge(PATCH_OUT, {n + ".json" for n in ears} | {CAND_NAME + ".json"})

    lock_rows = []
    for n in chrom:
        p, o = new_rows.get(n), old_rows.get(n)
        lock_rows.append([
            note_name(n),
            "%+.0f" % p["cents"] if p and p["spoke"] and
            np.isfinite(p["cents"]) else "-",
            "%+.0f" % o["cents"] if o and o["spoke"] and
            np.isfinite(o["cents"]) else "-"])
    lock_tbl = fmt_table(["note", "this round", "last round"], lock_rows)
    speak_rows = []
    for n in chrom:
        p, o = new_rows.get(n), old_rows.get(n)
        speak_rows.append([
            note_name(n),
            "%.0f" % (1000 * p["speak"]) if p and np.isfinite(p["speak"])
            else "-",
            "%.0f" % (1000 * o["speak"]) if o and np.isfinite(o["speak"])
            else "-"])
    speak_tbl = fmt_table(["note", "this round ms", "last round ms"],
                          speak_rows)
    durn_tbl = fmt_table(
        ["note length", "sound starts", "sound ends", "time held",
         "last audible"],
        [["%.1f s" % d, "%.3f s" % a, "%.3f s" % b, "%.3f s" % h,
          "%.3f s" % e] for d, a, b, e, h in durn])
    cell_tbl = "\n".join(
        "| `%s.wav` | %s | %s |" % (n, CELL_BLURB.get(n, ("", ""))[0],
                                    CELL_BLURB.get(n, ("", ""))[1])
        for n in ears)
    best_a, best_b = "line", "line_bell_off"
    best_why = ("the same notes at the same loudness, once through the "
                "measured trombone bell and once through the plain lowpass "
                "it replaced. Everything else is identical. This is the "
                "round's biggest single change and the one most likely to "
                "be wrong.")
    if "line_bell_off" not in ears:
        best_a, best_b = "line", "line_old_release"
        best_why = "the new release against the old one."
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{cand}", CAND_NAME)
                .replace("{cells}", cell_tbl)
                .replace("{lock}", lock_tbl)
                .replace("{speak}", speak_tbl)
                .replace("{durn}", durn_tbl)
                .replace("{best_a}", best_a).replace("{best_b}", best_b)
                .replace("{best_why}", best_why)
                .replace("{extra}", "## Also worth knowing\n\n"
                         + "\n".join("- " + s for s in extra)
                         if extra else ""))
    json.dump({
        "constants": {
            "PHYSICAL": {"y1_mps": Y1_MPS, "F_REF_hz": F_REF,
                         "m_kg": M_LIP, "k_tab_N_per_m": 1278.8,
                         "A_m2": A_LIP, "pm_tab_Pa": PM_TAB,
                         "Q_lip": Q_LIP, "f_bore_nominal_hz": F_BORE_NOM,
                         "bell_len_m": bell_smyth.BELL_LEN,
                         "bell_mouth_r_m": bell_smyth.BELL_MOUTH_R,
                         "bell_gamma": bell_smyth.BELL_GAMMA},
            "NORMALIZED": {"B0": B0, "PSI_HAT": PSI_HAT, "YEQ_HAT": YEQ_HAT,
                           "KICK_UNIT": KICK_UNIT, "P_REF_Pa": P_REF,
                           "Y_REF_m": Y_REF, "PM_EXP": PM_EXP,
                           "LOOP_AMP": LOOP_AMP, "STEEP_DEPTH": STEEP_DEPTH,
                           "TRANS_MAKEUP_dB": TRANS_MAKEUP,
                           "ATT_SEC": ATT_SEC, "OVER_SEC": OVER_SEC,
                           "OVERSHOOT": OVERSHOOT, "REL_SEC": REL_SEC},
        },
        "bell": {"refl_sections": REFL_SECS, "trans_sections": TRANS_SECS,
                 "refl_fit_err_db": float(np.abs(BELL["refl_err"]).max()),
                 "trans_fit_err_db": float(np.abs(BELL["trans_err"]).max())},
        "lip_map": TUNE_MAP, "bore_map": BORE_MAP,
        "pressure_window": win, "soft_S": soft_s, "loud_S": loud_s,
        "duration_scaling": durn,
        "notes_new": {n: {k: v for k, v in p.items()}
                      for n, p in new_rows.items()},
        "notes_old": {n: {k: v for k, v in p.items()}
                      for n, p in old_rows.items()},
        "ears": {n: {k: v for k, v in meta[n].items()} for n in ears},
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

    print("=== constants ===")
    print("  PHYSICAL   y1 %.1f m/s (Table 2)   F_REF %.2f Hz   "
          "m %.3e kg   A %.1e m^2" % (Y1_MPS, F_REF, M_LIP, A_LIP))
    print("  PHYSICAL   bore nominal %.2f Hz (Bb1)   bell %.3f m long, "
          "mouth radius %.3f m, flare %.1f (Smyth Table 1)"
          % (F_BORE_NOM, bell_smyth.BELL_LEN, bell_smyth.BELL_MOUTH_R,
             bell_smyth.BELL_GAMMA))
    print("  NORMALIZED B0 %.9f   PSI_HAT %.6f   YEQ_HAT %.3f   "
          "KICK_UNIT %.4f   PM_EXP %.2f"
          % (B0, PSI_HAT, YEQ_HAT, KICK_UNIT, PM_EXP))
    print("  BELL       refl %d sections (fit err med %.2f dB, max %.2f dB), "
          "trans %d sections (med %.2f dB, max %.2f dB)"
          % (len(REFL_SECS), np.median(np.abs(BELL["refl_err"])),
             np.abs(BELL["refl_err"]).max(), len(TRANS_SECS["room"]),
             np.median(np.abs(BELL["trans_err"])),
             np.abs(BELL["trans_err"]).max()))
    print("  ENVELOPE   attack %.0f ms, overshoot +%.0f%% over %.0f ms, "
          "release %.0f ms (Expo p%.0f)"
          % (ATT_SEC * 1000, OVERSHOOT * 100, OVER_SEC * 1000,
             REL_SEC * 1000, REL_POWER))
    print("  MAPS       lip %d notes, air column %d notes"
          % (len(TUNE_MAP), len(BORE_MAP)))

    if stage == "probe":
        return stage_probe()
    if stage == "tune":
        lip, liplog = solve_lip_map(None)
        bore, borelog = solve_bore_map(None, lip)
        print("\nTUNE_MAP = %s" % json.dumps(lip))
        print("\nBORE_MAP = %s" % json.dumps(bore))
        json.dump({"lip": lip, "lip_log": liplog,
                   "bore": bore, "bore_log": borelog},
                  open(os.path.join(SCRATCH, "maps.json"), "w"), indent=1)
        return 0
    if stage == "all":
        if not TUNE_MAP or not BORE_MAP:
            print("no solved maps in the file - run `tune` first and paste "
                  "TUNE_MAP / BORE_MAP back in")
            return 1
        rc = stage_all()
        shutil.rmtree(SCRATCH, ignore_errors=True)
        return rc
    print("unknown stage %r" % stage)
    return 1


if __name__ == "__main__":
    sys.exit(main())
