"""One-mass lip round 1 - the published brass exciter on a loop-family bore.

Spec:       docs/superpowers/specs/2026-09-17-onemass-lip-design.md
Literature: docs/research/nonlinear_bore/MSALLAM_DIGEST.md (the chain: S3/S1)
Lip source: Berjamin, Lombard, Vergez, Bergeot, arXiv:1511.04247 §3.1,
            Eqs. (43)-(49) and Table 2, transcribed from the PDF this run.
            Full transcription in the run report:
            docs/autonomy/dsp/reports/2026-09-17-onemass-lip1.md

WHY: every wind patch we have uses a MEMORYLESS valve - a fixed curve from
pressure to flow, no state. That is the named 09-08 ceiling: a memoryless
single-input junction is the reed family, "saxy not brassy", whatever curve
you draw. A brass player's lips are a mass on a spring with their own
resonance near the note. This round puts the published one-mass
outward-striking lip on the front of the proven bore loop.

-------------------------------------------------------------------------
THE MODEL, verbatim from the source (physical units)
-------------------------------------------------------------------------
  (43)  A = h l sin(phi)                       projected lip area
  (44)  pe(y,t) = p+(0,t) + p-(0,t)            pressure at the bore entry
  (45a) m y'' + r y' + k (y - yeq) = f(y,t)    the mass-spring-damper
  (45b) y(0) = y0,  y'(0) = y1
  (46)  f(y,t) = A (pm(t) - pe(y,t))           aeroacoustic forcing
  (47)  pe = 2p- - xi (psi y / 2)
                  ( psi y - sqrt( psi^2 y^2 + 4 |pm - 2 p-| ) )   if y > 0
             = 2p-                                                else
  (48)  xi = sgn(pm - pe) = sgn(pm - 2 p-)
  (49)  psi = l Zc sqrt(2/rho0) = l sqrt(2 rho0) a0 / S(0)

  Table 2:  m 1.78e-4 kg | k 1278.8 N/m | r sqrt(mk)/4
            l 1e-2 m     | A 1e-4 m^2   | pm 20e3 Pa
            y0 4e-3 m    | y1 -4 m/s    | yeq 5e-4 m
  Their §4.2 resonator: R = 7 mm, D = 1.4 m, Nx = 100.  k swept 100..3000
  N/m for register.  Note r = sqrt(mk)/4 makes Q = sqrt(mk)/r = 4 EXACTLY,
  so their own register sweep is a CONSTANT-Q sweep - which is what
  licenses keytracking f_lip at fixed Q below.

Eq. (47) is EXPLICIT: given y (this sample) and p- (this sample, off the
bore tap) it hands back pe with no iteration.  The only implicit coupling
left is (46) - f needs pe, pe needs y - and Berjamin closes that with a
fixed-point solve.  A pull graph cannot iterate, so this port breaks it
with a ONE-SAMPLE lag: the lip force reads pe through a {"tap"} edge.
That is the single approximation in the exciter, and it is labelled
everywhere it matters.  The Bernoulli square root SHIPS - it is a
CurveNode power knot, a = 1, p = 0.5 - so no linearized substitute is in
this build.

-------------------------------------------------------------------------
PRESSURE <-> SIGNAL SCALING (physical-units discipline; the STK lesson)
-------------------------------------------------------------------------
Every constant below is tagged PHYSICAL (SI, from the source) or
NORMALIZED (MForce signal units).  Two reference scales convert between
them and NOTHING else in this file is a free fudge factor:

    P_REF = 20e3 Pa     p_hat = p / P_REF     (Table 2's own pm = 1.0)
    Y_REF = 1e-3 m      y_hat = y / Y_REF     (1 mm of lip opening = 1.0)

Derived normalized constants, each a closed form of the SI table:

    PSI_HAT = psi * Y_REF / sqrt(P_REF)    = 0.2434  (Eq. 49, normalized)
    YEQ_HAT = yeq / Y_REF                  = 0.5
    B0      = A * P_REF / (m * SR^2 * Y_REF) = 0.004877

B0 is the y'' discretization, not a taste knob: y'' ~= SR^2 (y_n - 2y_n-1
+ y_n-2), so a two-pole section with a1 = -2 r_p cos(w), a2 = r_p^2
realizes m y'' + r y' + k y = F when its input gain is 1/(m SR^2) in SI,
i.e. A P_REF/(m SR^2 Y_REF) after normalization.  It is independent of
f_lip - the spring and the damper live entirely in the poles - which is
the whole reason a fixed `b0` SETTING is enough and no gain keytrack is
needed.

KEYTRACK (the design choices NOT in the source, stated plainly - the paper
has a single sustained note):
  * f_lip = RATIO * f0, so k = m (2 pi f_lip)^2 follows.  At C3 that is
    k = 120 N/m and at C5 k = 1924 N/m - inside Berjamin's own swept
    100..3000 band, so the register keytrack stays on their evidence.
  * pole radius r_p = exp(-pi f_lip/(Q SR)) ~= 1 - pi f_lip/(Q SR).
    Linearized because over 130..525 Hz the exponential's curvature costs
    < 1e-5 in radius, and a linear expression IS wireable (CurveNode expr).
  * blowing pressure pm = S * PM_TAB * (f_lip/F_REF)^2, F_REF = 426.6 Hz
    (Table 2's own lip frequency).  The static opening is A pm/k and k
    goes as f_lip^2, so this holds the embouchure at ONE operating point
    across the register; at f_lip = F_REF and S = 1 it is exactly Table
    2's 20 kPa.  Without it, C3 blows the lip 16x too far open and dies.
  S is the dynamics axis; soft and loud are two points inside the
  MEASURED ignition window and are reported back in Pa.

-------------------------------------------------------------------------
THE BORE, and why it is three note-periods long
-------------------------------------------------------------------------
Measured this run (probe sweep, C4, lip ratio 0.6..1.4): the valve always
oscillates ABOVE its own lip resonance - played/f_lip runs 1.30 down to
1.06 as f_lip rises through the bore resonance.  That is the textbook
outward-striking signature and it is exactly what the memoryless family
cannot do.  With a one-period bore the lip wins outright and the note
lands +65..+418 cents sharp: a one-period loop has no authority against
the flow's resistance (d p+/d p- measures ~0.38 at the operating point -
the valve absorbs most of the returning wave every bounce).

Lengthening the bore to THREE note-periods fixes it, and is the real
instrument's architecture (BRASS_HARNESS_NOTES delta 2: a trumpet C5
rides a ~C3 air column).  A three-period bore's peaks are three times
narrower in absolute time, so the mode holds the lip: C3/C4/C5 all lock
within +18..+29 cents, and the lip ratio then SELECTS WHICH PARTIAL
speaks - ratio 0.8 takes mode 3 (the note), ratio 1.2 cracks up to
mode 4.  That register mechanism is the point of the whole round.

-------------------------------------------------------------------------
THE GRAPH (zero engine code)
-------------------------------------------------------------------------
  Mouth env x keytrack -> Pm
  Force  = Pm - Pe[n-1]                    (46), tap = the one-sample lag
  Lip    = Biquad RESONANCE mode           (45a): f <- f_lip, radius <- Q
  Qn     = PSI_HAT * max(0, Lip + YEQ_HAT) (47)'s y>0 clamp + psi, one node
  Dp     = Pm - 2 p-                       (48)'s argument
  Rad    = Qn^2 + 4|Dp|                    under the root
  Sroot  = sqrt(Rad)                       THE BERNOULLI ROOT
  Xflow  = sgn(Dp) * Qn * (Sroot - Qn)/2   (47)'s flow term
  Pe     = Xflow + 2 p-                    (47)
  Pplus  = Pe - p-                         entry wave into the bore
  -> + breath noise -> DC block -> bore-loss lowpass -> DelayLine --tap-->
  and the delay's ref consumer is the OUT-OF-LOOP steepener (REVIEW 66
  wiring, imported verbatim) -> bell lowpass -> out.

Multiplications are CombinedSource Multiply (signal x signal); scalar
trims ride the gainAdj setting so no extra node joins the cycle (the
DelayLine's compensation walk caps at 16 members; this graph uses 12).

-------------------------------------------------------------------------
THE TWO CONTROLS
-------------------------------------------------------------------------
  flat : the SAME valve with the mass and damper deleted - the opening
         follows pressure instantaneously through the same static
         compliance A/k.  Same bore, same flow math, same operating
         point.  MEASURED: it does not oscillate at all.  A blown-open
         valve with no memory has no phase to give back; the mass IS the
         oscillator.  Kept in the queue as near-silence, on purpose.
  reed : the audible, pitch-matched old lip - a memoryless INWARD-striking
         valve (pressure closes it, the reed sign) on a bore tuned to the
         note.  That is the wind chassis, i.e. the thing that has been
         sounding "saxy".  MEASURED: it locks the note, and on the long
         bore it can only ever play the bore fundamental - it cannot
         select a register, which is the mechanism difference in one line.

-------------------------------------------------------------------------
Outputs (this script OWNS these dirs and purges anything else):
  patches/sweep/onemass_lip1/            full grid
  renders/dsp/audition/onemass_lip1/     <= 8 ears cells + README.md
Probe/calibration renders go to a scratch dir and are deleted.
Usage: python tools/gen_onemass_lip1.py
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
from gen_stk_bowed import read_mono                             # noqa: E402
from gen_nlbore_probe1 import steepener                         # noqa: E402
from gen_nlbore_probe1 import centroid_all, hf_fraction         # noqa: E402
from gen_nlbore_probe1 import diff_db, level_ceiling, rms       # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "onemass_lip1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "onemass_lip1")
SCRATCH = os.path.join(tempfile.gettempdir(), "onemass_lip1_scratch")

SR = 48000

# ===========================================================================
# PHYSICAL constants (SI) - Berjamin Table 1 (air, 15 C), Table 2 (lips),
# and their §4.2 resonator geometry.
# ===========================================================================
GAMMA = 1.403                 # PHYSICAL  ratio of specific heats
P0_ATM = 1.0e5                # PHYSICAL  Pa
RHO0 = 1.177                  # PHYSICAL  kg/m^3
A0 = math.sqrt(GAMMA * P0_ATM / RHO0)          # PHYSICAL  345.3 m/s

M_LIP = 1.78e-4               # PHYSICAL  kg     Table 2
K_TAB = 1278.8                # PHYSICAL  N/m    Table 2
Q_LIP = 4.0                   # PHYSICAL  -      = sqrt(mk)/r, r = sqrt(mk)/4
L_LIP = 1.0e-2                # PHYSICAL  m      Table 2 (lip width l)
A_LIP = 1.0e-4                # PHYSICAL  m^2    Table 2 (projected area A)
PM_TAB = 20.0e3               # PHYSICAL  Pa     Table 2 (blowing pressure)
YEQ = 5.0e-4                  # PHYSICAL  m      Table 2 (equilibrium opening)
R_BORE = 7.0e-3               # PHYSICAL  m      §4.2
S_BORE = math.pi * R_BORE ** 2                 # PHYSICAL  m^2
ZC = RHO0 * A0 / S_BORE                        # PHYSICAL  Pa.s/m^3
PSI = L_LIP * ZC * math.sqrt(2.0 / RHO0)       # PHYSICAL  Pa^0.5/m   Eq. 49
F_REF = math.sqrt(K_TAB / M_LIP) / (2.0 * math.pi)   # PHYSICAL  426.6 Hz

# ===========================================================================
# NORMALIZED constants - closed forms of the above, no free parameters
# ===========================================================================
P_REF = PM_TAB                # NORMALIZED 1.0 signal == 20 kPa
Y_REF = 1.0e-3                # NORMALIZED 1.0 signal == 1 mm opening
PSI_HAT = PSI * Y_REF / math.sqrt(P_REF)       # NORMALIZED ~0.2434
YEQ_HAT = YEQ / Y_REF                          # NORMALIZED 0.5
B0 = A_LIP * P_REF / (M_LIP * SR * SR * Y_REF)  # NORMALIZED resonator gain

# ---- chassis (NORMALIZED; the loop-family skeleton) -----------------------
BORE_MODE = 3.0       # note = mode 3 of the air column (measured: see above)
REED_MODE = 1.0       # the reed control plays the bore FUNDAMENTAL
DAMP_HZ = 1200.0      # in-loop lowpass = bore losses + bell reflection.
                      # ICMC'97 puts the bell reflection bandwidth near
                      # 800 Hz; 1200 keeps enough loop gain for ignition.
DAMP_RES = 0.5        # SVF resonance floor = no peak, pure loss
DC_HZ = 12.0          # cycle DC block (the y>0 clamp rectifies hard)
LOOP_AMP = 1.0        # bore loss.  The flow itself absorbs ~60% per bounce
                      # (measured), so no extra artificial loss is needed -
                      # and the dead-lip null gate proves the loop cannot
                      # ring on its own.
NOISE_AMP = 0.0015    # breath grain, seeds the loop before ignition
BELL_HZ = 5000.0      # output-side radiation rolloff, after the steepener
STEEP_DEPTH = 0.03    # REVIEW 66's "real-instrument strength"
DRIVE_PEAK = 0.8      # normalize into the steepener so depth means the same
REED_MAG = 0.5        # reed control: static opening at S=1 is REED_MAG*yeq

# ---- score / axes --------------------------------------------------------
NOTES = [48, 60, 72]              # C3, C4, C5
SLOT = 2.0
NOTE_SEC = 2.0
SUSTAIN = (0.8, 1.8)              # analysis window inside a slot
RATIOS = {"r080": 0.8, "r100": 1.0, "r120": 1.2}
SCAN = [round(0.2 + 0.1 * i, 3) for i in range(22)]   # LINEAR, not bisection
SOFT_POS, LOUD_POS = 0.2, 0.85    # positions inside the measured window

# ---- gates ---------------------------------------------------------------
LOCK_RATIO = 0.15        # energy at f0 / total, in the sustain window
LOCK_RMS = 0.004         # below this the cell is not speaking
LOCK_CENTS = 40.0        # the spec's tolerance
LEVEL_CEILING = 0.5      # no 0.5 s window above rms 0.5
AUDIBILITY_FLOOR = 1e-4
FEATURE_DB = -26.0
PEAK_TARGET_DB = -6.0
FLOW_TOL = 0.02          # static flow-math wiring gate, relative


# ===========================================================================
# closed forms (the python side of the wiring gate)
# ===========================================================================
def pe_closed_form(y_hat, pminus_hat, pm_hat):
    """Eq. (47) in normalized units, evaluated directly in python.
    `y_hat` is the TOTAL opening y/Y_REF (the graph carries displacement
    from equilibrium and adds YEQ_HAT back inside Qn)."""
    q = PSI_HAT * max(0.0, y_hat)
    d = pm_hat - 2.0 * pminus_hat
    xi = 1.0 if d > 0 else (-1.0 if d < 0 else 0.0)
    x = xi * (q / 2.0) * (math.sqrt(q * q + 4.0 * abs(d)) - q)
    return 2.0 * pminus_hat + x


# ===========================================================================
# graph construction
# ===========================================================================
def add(nid, a, b, gain_adj=0.0):
    """CombinedSource Sum: v1 + v2*(1+gainAdj).  gainAdj carries the scalar
    so no extra node joins the DelayLine's compensation walk."""
    return {"id": nid, "type": "CombinedSource", "params": {
        "gainAdj": round(float(gain_adj), 9), "operation": 3,
        "source1": a, "source2": b}}


def mulg(nid, a, b, gain_adj=0.0):
    """CombinedSource Multiply: v1 * v2 * (1+gainAdj)."""
    return {"id": nid, "type": "CombinedSource", "params": {
        "gainAdj": round(float(gain_adj), 9), "operation": 1,
        "source1": a, "source2": b}}


def pts(nid, knots, src):
    return {"id": nid, "type": "CurveNode", "params": {
        "exprKnots": [], "interp": "linear", "mode": "points",
        "knots": [[round(float(a), 9), round(float(b), 9)] for a, b in knots],
        "source": src}}


def expr(nid, a, b, form, src):
    return {"id": nid, "type": "CurveNode", "params": {
        "knots": [], "interp": "linear", "mode": "expressions",
        "exprKnots": [{"x": 0.0, "form": form,
                       "a": round(float(a), 12), "b": round(float(b), 12)}],
        "source": src}}


def flow_nodes(pminus_a, pminus_b, y_pin):
    """Eqs. (47)/(48) as nodes.  `pminus_a`/`pminus_b` are two INDEPENDENT
    pin values for p- (two taps, or two constants) - never a shared
    {"ref"}, because a tap never advances and a constant has no state.

    NODE ORDER IS LOAD-BEARING.  A node referenced twice gets the raw
    pointer on its FIRST mention in wiring order and a read-only RefSource
    after that, so the raw consumer must also be the one the pull reaches
    FIRST.  Reading the chain down from Pe:
        Pe -> Xflow -> QWh -> W -> Sroot -> Rad -> Qsq -> Qn -> Lip -> Force
    so Qsq holds the raw Qn and Force holds the raw Pm; AbsDp4 (reached on
    Rad's second pin, still ahead of SgnDp) holds the raw Dp.  Every later
    read lands on the sample just computed.  Gate 0 checks this against
    the closed form and passes to int16 quantization.
    """
    return [
        # y>0 clamp, equilibrium offset and psi in ONE transfer:
        #   Qn = PSI_HAT * max(0, Lip + YEQ_HAT)
        # Flat 0 below -YEQ_HAT, then exactly linear - the knot at
        # (-YEQ_HAT, 0) makes the slope PSI_HAT by construction.
        pts("Qn", [(-8.0, 0.0), (-YEQ_HAT, 0.0),
                   (8.0, PSI_HAT * (8.0 + YEQ_HAT))], y_pin),
        expr("Qsq", 1.0, 2.0, "power", {"ref": "Qn"}),      # q^2
        add("Dp", {"ref": "Pm"}, pminus_a, -3.0),           # pm - 2 p-
        pts("AbsDp4", [(-20.0, 80.0), (0.0, 0.0), (20.0, 80.0)],
            {"ref": "Dp"}),                                  # 4|pm - 2p-|
        add("Rad", {"ref": "Qsq"}, {"ref": "AbsDp4"}),      # q^2 + 4|.|
        expr("Sroot", 1.0, 0.5, "power", {"ref": "Rad"}),   # THE ROOT
        add("W", {"ref": "Sroot"}, {"ref": "Qn"}, -2.0),    # sqrt(.) - q
        mulg("QWh", {"ref": "W"}, {"ref": "Qn"}, -0.5),     # q(sqrt-q)/2
        pts("SgnDp", [(-20.0, -1.0), (-1e-4, -1.0),
                      (1e-4, 1.0), (20.0, 1.0)], {"ref": "Dp"}),   # xi
        mulg("Xflow", {"ref": "QWh"}, {"ref": "SgnDp"}),
        add("Pe", {"ref": "Xflow"}, pminus_b, 1.0),         # Eq. 47
    ]


def make_patch(ratio, scale, valve, bore_mode):
    """ratio = f_lip/f0 (for `mass`) or the reference stiffness (controls).
       scale = blowing-pressure multiplier S.
       valve in {"mass", "flat", "reed"} - see the module docstring."""
    a_pm = scale * (ratio / F_REF) ** 2 * (PM_TAB / P_REF)
    nodes = [
        {"id": "__perf_f", "type": "PerformNode",
         "params": {"field": "frequency"}},
        expr("Flip", ratio, 0.0, "linear", {"ref": "__perf_f"}),
        # pole radius ~= 1 - pi f_lip/(Q SR) (linearized exp, < 1e-5 error)
        expr("Frad", -math.pi * ratio / (Q_LIP * SR), 1.0, "linear",
             {"ref": "__perf_f"}),
        expr("Pmk", a_pm, 2.0, "power", {"ref": "__perf_f"}),
        {"id": "Mouth", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": 1.0,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Expo", "startVal": 0.0, "endVal": 1.0,
                 "percent": 0.06, "power": 2.0, "minSec": 0.0, "maxSec": 0.0},
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": 0.80, "power": 0.0, "minSec": 0.0, "maxSec": 0.0},
                {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
                 "percent": 0.14, "power": 0.0, "minSec": 0.0,
                 "maxSec": 0.0}]}},
        mulg("Pm", {"ref": "Pmk"}, {"ref": "Mouth"}),
        # Eq. 46.  The {"tap":"Pe"} is the ONE-SAMPLE LAG standing in for
        # Berjamin's fixed-point solve; gainAdj -2 makes it Pm - Pe.
        add("Force", {"ref": "Pm"}, {"tap": "Pe"}, -2.0),
    ]
    if valve == "mass":
        # Eq. 45a: a two-pole resonance section IS the mass-spring-damper.
        nodes.append({"id": "Lip", "type": "Biquad", "params": {
            "source": {"ref": "Force"}, "mode": 1,
            "frequency": {"ref": "Flip"}, "radius": {"ref": "Frad"},
            "b0": round(B0, 12), "b1": 0.0, "b2": 0.0,
            "a1": 0.0, "a2": 0.0}})
    else:
        if valve == "flat":
            # mass and damper deleted, SAME sign and SAME static compliance
            # A/k with k = m(2 pi ratio f0)^2.
            a_g = A_LIP * P_REF / (Y_REF * M_LIP
                                   * (2.0 * math.pi * ratio) ** 2)
        else:   # "reed" - memoryless INWARD-striking valve (pressure shuts
                # it).  Magnitude set so the static opening at S = 1 is
                # REED_MAG * yeq, keytracked the same way, so its operating
                # point tracks the register exactly like the lip's.
            a_g = -REED_MAG * YEQ_HAT / (((ratio / F_REF) ** 2)
                                         * (PM_TAB / P_REF))
        nodes.append(expr("Gdck", a_g, -2.0, "power", {"ref": "__perf_f"}))
        nodes.append(mulg("Lip", {"ref": "Force"}, {"ref": "Gdck"}))
    nodes += flow_nodes({"tap": "Bore"}, {"tap": "Bore"}, {"ref": "Lip"})
    nodes += [
        add("Pplus", {"ref": "Pe"}, {"tap": "Bore"}, -2.0),   # p+ = pe - p-
        {"id": "Noise", "type": "WhiteNoiseSource", "params": {
            "amplitude": NOISE_AMP, "boost": 0.0, "continuity": 0.0,
            "density": 1.0, "zeroCrossTendency": 0.0}},
        add("LoopIn", {"ref": "Pplus"}, {"ref": "Noise"}),
        {"id": "DCblk", "type": "SVFSource", "params": {
            "cutoffFreq": DC_HZ, "mode": 4, "normalize": False,
            "resonance": 0.7, "source": {"ref": "LoopIn"}}},
        {"id": "Damp", "type": "SVFSource", "params": {
            "cutoffFreq": DAMP_HZ, "mode": 0, "normalize": False,
            "resonance": DAMP_RES, "source": {"ref": "DCblk"}}},
        {"id": "Bore", "type": "DelayLine", "params": {
            "source": {"ref": "Damp"}, "frequency": {"ref": "__perf_f"},
            "ratio": bore_mode, "amplitude": LOOP_AMP, "compensate": True}},
    ]
    return nodes


def finish(nodes, in_gain, out_gain, depth, seconds, score):
    """Append the REVIEW-66 steepener (imported verbatim) + bell and close
    the patch.  NL_in's {"ref":"Bore"} is the delay's advancing consumer."""
    chain, out = steepener("Bore", depth, in_gain, out_gain, bell_fc=BELL_HZ)
    return {"sampleRate": SR, "seconds": seconds,
            "instrument": {"polyphony": 1, "volume": 0.7},
            "score": score,
            "graph": {"output": out, "nodes": nodes + chain}}


def cell_patch(ratio, scale, depth, valve, bore_mode, in_gain=1.0,
               out_gain=1.0):
    score = [{"note": n, "time": SLOT * k, "duration": NOTE_SEC,
              "velocity": 0.8} for k, n in enumerate(NOTES)]
    return finish(make_patch(ratio, scale, valve, bore_mode), in_gain,
                  out_gain, depth, SLOT * len(NOTES), score)


def flow_test_patch(y_hat, pminus_hat, pm_hat):
    """Open-loop wiring gate: the flow subgraph with y and p- pinned to
    constants, so the rendered DC equals pe_closed_form() if and only if
    Eqs. 47/48 are wired right."""
    nodes = [mulg("Pm", 1.0, pm_hat),
             mulg("Lip", 1.0, y_hat - YEQ_HAT)]   # Qn adds YEQ_HAT back
    nodes += flow_nodes(pminus_hat, pminus_hat, {"ref": "Lip"})
    nodes.append(mulg("Out", {"ref": "Pe"}, 0.25))   # keep inside int16
    return {"sampleRate": SR, "seconds": 0.2,
            "instrument": {"polyphony": 1, "volume": 1.0},
            "score": [{"note": 60, "time": 0.0, "duration": 0.2,
                       "velocity": 1.0}],
            "graph": {"output": "Out", "nodes": nodes}}


# ===========================================================================
# measurement
# ===========================================================================
def midi_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def seg_of(x, sr, k):
    a = int((SLOT * k + SUSTAIN[0]) * sr)
    b = int((SLOT * k + SUSTAIN[1]) * sr)
    return x[a:min(b, len(x))]


def goertzel_ratio(seg, sr, hz):
    """Energy within +-4% of hz over total energy - the pitch gate."""
    if len(seg) < 512:
        return 0.0
    s = seg - seg.mean()
    sp = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2
    fr = np.fft.rfftfreq(len(s), 1.0 / sr)
    band = (fr > hz * 0.96) & (fr < hz * 1.04)
    return float(sp[band].sum() / (sp.sum() + 1e-20))


def f0_autocorr(seg, sr, target):
    """Pitch by normalized autocorrelation, searched over a THIRD to THREE
    TIMES the target so an octave error is reported, not hidden."""
    if len(seg) < 1024 or rms(seg) < 1e-5:
        return float("nan")
    s = seg - seg.mean()
    n = len(s)
    # FFT autocorrelation - np.correlate is direct O(n^2) and a 1 s window
    # at 48 k costs seconds per call (measured: it dominated the run).
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
    d = max(-1.0, min(1.0, d))
    return sr / (i + d)


def cents(f, target):
    if not (f and np.isfinite(f) and f > 0):
        return float("nan")
    return 1200.0 * math.log2(f / target)


def note_stats(x, sr, bore_mode=BORE_MODE):
    out = []
    for k, n in enumerate(NOTES):
        seg = seg_of(x, sr, k)
        tgt = midi_hz(n)
        f0 = f0_autocorr(seg, sr, tgt)
        c = cents(f0, tgt)
        lk = goertzel_ratio(seg, sr, tgt)
        rr = rms(seg)
        out.append({"note": n, "target_hz": tgt, "f0": f0, "cents": c,
                    "lock": lk, "rms": rr,
                    "locked": bool(np.isfinite(c) and abs(c) <= LOCK_CENTS
                                   and lk > LOCK_RATIO and rr > LOCK_RMS),
                    # which partial of the air column spoke (the register
                    # mechanism): bore fundamental = target / bore_mode
                    "partial": (f0 * bore_mode / tgt if np.isfinite(f0)
                                else float("nan")),
                    "centroid": centroid_all(seg, sr) if len(seg) else
                    float("nan"),
                    "hf1k": hf_fraction(seg, sr, 1000.0) if len(seg) else
                    float("nan")})
    return out


def render(patch, tag, out_dir, timeout=600):
    pj = os.path.join(PATCH_OUT, tag + ".json")
    wp = os.path.join(out_dir, tag + ".wav")
    with open(pj, "w") as f:
        json.dump(patch, f, indent=1)
    r = subprocess.run([CLI, pj, wp], capture_output=True, text=True,
                       timeout=timeout)
    if r.returncode != 0 or not os.path.exists(wp):
        print(f"  RENDER_FAIL {tag} rc={r.returncode} "
              f"{(r.stderr or r.stdout or '')[-300:]}")
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
            print(f"purged stale: {os.path.basename(d)}/{fn}")


def trim_for(x):
    pk = max(float(np.abs(x).max()), 1e-9)
    return (10.0 ** (PEAK_TARGET_DB / 20.0)) / pk


# ===========================================================================
# configs: (kind, rtag, ratio, valve, bore_mode)
# ===========================================================================
CONFIGS = [("lip", "r080", 0.8, "mass", BORE_MODE),
           ("lip", "r100", 1.0, "mass", BORE_MODE),
           ("lip", "r120", 1.2, "mass", BORE_MODE),
           ("flat", "r080", 0.8, "flat", BORE_MODE),
           ("reed", "r080", 0.8, "reed", REED_MODE)]

EARS_WANT = ["lip_r080_loud_st1", "reed_r080_loud_st1",
             "lip_r080_loud_st0", "lip_r080_soft_st1",
             "lip_r100_loud_st1", "lip_r120_loud_st1",
             "flat_r080_loud_st1"]
# `flat` is expected silent, so it must NOT be peak-normalized: it inherits
# its lip twin's trim so the difference you hear is real.
TRIM_TWIN = {"flat_r080_loud_st1": "lip_r080_loud_st1",
             "flat_r080_soft_st1": "lip_r080_soft_st1",
             "flat_r080_loud_st0": "lip_r080_loud_st0",
             "flat_r080_soft_st0": "lip_r080_soft_st0"}
# The headline A/B must be loudness-matched, not just peak-matched: the reed
# packs far more energy under the same peak and would win the A/B on level
# alone.  Matched on RMS after peak normalization (the trim only goes down,
# so the -6 dBFS ceiling still holds).
RMS_TWIN = {"reed_r080_loud_st1": "lip_r080_loud_st1",
            "reed_r080_soft_st1": "lip_r080_soft_st1"}

BLURB = {
    "lip_r080_loud_st1": ("the heavy lip, in tune, blown hard, brightener on",
                          "A of the headline pair - is this brass?"),
    "reed_r080_loud_st1": ("the OLD valve: no mass, reed-style, bore tuned "
                           "to the note",
                           "B of the headline pair - the saxy one"),
    "lip_r080_loud_st0": ("same as A, loud-playing brightener OFF",
                          "how much of the edge is the brightener"),
    "lip_r080_soft_st1": ("the heavy lip, blown gently",
                          "brass goes dull when soft - does it?"),
    "reed_r080_soft_st1": ("the old valve, blown gently",
                           "the reed's own soft/loud pair"),
    "lip_r100_loud_st1": ("heavy lip pulled UP to the note's own pitch",
                          "lands BETWEEN two of the tube's notes - the "
                          "cracked-note zone, and it should sound like it"),
    "lip_r120_loud_st1": ("heavy lip pulled up further still",
                          "clears the gap onto the tube's next note up - "
                          "this is lipping, and no curve can do it"),
    "flat_r080_loud_st1": ("A with the lip's MASS deleted, nothing else "
                           "changed",
                           "near-silence - that is the result, not a bug"),
}

README = """# One-mass lip, round 1 - "does a lip with mass sound like brass?"

Renders here; patches in `patches/sweep/onemass_lip1/`; generator
`tools/gen_onemass_lip1.py`; run report
`docs/autonomy/dsp/reports/2026-09-17-onemass-lip1.md`.

## What this is, in plain language

Every wind patch we have models the reed the same way: one fixed curve.
Pressure in, flow out, and the curve has no memory - it cannot be
part-way through anything. That is a fair model of a clarinet reed,
because a reed's own natural pitch is far above any note it plays, so the
reed just follows orders. It is the wrong model for a brass player's
lips, and it is the reason everything we have built lands somewhere
between an oboe and a saxophone no matter what curve gets drawn.

A brass player's lips are heavy. They have their own natural pitch, right
around the note being played, and the player moves that pitch to choose
which note comes out. So this round throws the curve away and builds an
actual little mass on a spring, equation for equation out of a published
trombone paper, and bolts it to the front of the same bore we already
use.

Two things came out of it that the old valve simply cannot do:

1. **The note is picked by the lip, not by the tube.** The tube here is
   three times longer than the note - like a real trumpet, where a high C
   rides an air column that would sound a low C on its own. The lip
   decides which of the tube's notes speaks. Tighten it and the pitch
   climbs: `r100` ends up stuck BETWEEN two of the tube's notes (the
   cracked-note zone a real player lives in fear of) and `r120` clears
   the gap onto the next one up. Same patch, same written note, three
   different sounds - that is lipping, and no fixed curve can do it.
2. **Take the mass away and it stops playing entirely.** Not quieter -
   silent. `flat_r080_loud_st1` is exactly the headline patch with the
   mass deleted and nothing else touched.

**The question for you: does the heavy lip read as brass, where the curve
read as a reed?**

## The single comparison that answers it

**`{best_a}.wav` vs `{best_b}.wav`** - same three notes, same loudness,
same brightener, same flow equations. One has a lip with weight and its
own pitch; the other is the memoryless valve we have been using all
along, on a tube tuned so it plays the same notes. If the heavy one is
not different in kind, the mass is not where brassiness lives and the
next move is somewhere else.

## Cells

| file | what it is | listen for |
|---|---|---|
{cells}

`r080/r100/r120` = where the lip's own pitch sits relative to the note
(80%, 100%, 120%). A blown-open valve like a lip always sounds a bit
ABOVE its own pitch, so the in-tune setting is the one tuned BELOW the
note - which is why `r080` is the in-tune one and the others crack
upward.
`soft`/`loud` = blowing pressure. `st1`/`st0` = last round's
loud-playing brightener on or off.

One honest caveat on the old valve: it only plays over a hair's width of
blowing pressure - one step of the pressure scan, against the heavy lip's
sixteen - so it has no soft/loud pair to offer and there is no `reed_soft`
file. It also refuses C5 entirely. Those are its limits, not choices I
made, and they are part of the answer.

## Measurements

Peaks are normalised to -6 dBFS across the queue, so loudness differences
you hear are timbre, not gain - EXCEPT the `flat` cell, which is played
back at its twin's gain on purpose, because its whole point is that
nothing comes out.

{table}

{lock}

{partials}

{dose}

{notes}
"""


def main():
    if not os.path.exists(CLI):
        print(f"mforce_cli not found at {CLI}")
        return 1
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(EARS_OUT, exist_ok=True)
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH, exist_ok=True)

    print("=== constants ===")
    print(f"  PHYSICAL   psi {PSI:.6g} Pa^0.5/m   Zc {ZC:.6g} Pa.s/m^3   "
          f"a0 {A0:.3f} m/s   F_REF {F_REF:.2f} Hz")
    print(f"  NORMALIZED PSI_HAT {PSI_HAT:.6f}  YEQ_HAT {YEQ_HAT}  "
          f"B0 {B0:.9f}")
    print(f"  SCALING    P_REF {P_REF:.0f} Pa per unit   "
          f"Y_REF {Y_REF} m per unit")
    print(f"  CHASSIS    bore mode {BORE_MODE:.0f} (lip) / {REED_MODE:.0f} "
          f"(reed)   loop amp {LOOP_AMP}   damp {DAMP_HZ} Hz")

    fail, extra_notes = [], []

    # ---- gate 0: static flow-math wiring check --------------------------
    print("\n=== gate 0: Eq. 47 wiring (static, open loop) ===")
    flow_rows = []
    for yh, pmh, pmm in [(0.0, 0.0, 1.0), (1.0, 0.0, 1.0), (1.0, 0.2, 1.0),
                         (0.5, -0.1, 0.4), (-0.6, 0.1, 1.0), (1.0, 0.6, 0.5)]:
        tag = ("flow_%02d" % len(flow_rows))
        got, pj, wp = render(flow_test_patch(yh, pmh, pmm), tag, SCRATCH, 120)
        if got is None:
            fail.append("flow wiring render")
            continue
        x, sr = got
        meas = float(np.median(x[int(0.05 * sr):int(0.15 * sr)])) / 0.25
        want = pe_closed_form(yh, pmh, pmm)
        err = abs(meas - want) / max(abs(want), 0.05)
        flow_rows.append((yh, pmh, pmm, want, meas, err))
        print("  y %+5.2f  p- %+5.2f  pm %4.2f   want %+9.6f  got %+9.6f"
              "   err %6.3f%%  %s"
              % (yh, pmh, pmm, want, meas, err * 100.0,
                 "ok" if err < FLOW_TOL else "MISMATCH"))
        if err >= FLOW_TOL:
            fail.append(f"flow wiring case {tag}")
        rm(pj, wp)

    # ---- gate 1: dead-lip null ------------------------------------------
    print("\n=== gate 1: dead-lip null (no blowing pressure must not ring) ===")
    got, pj, wp = render(cell_patch(0.8, 0.0, 0.0, "mass", BORE_MODE),
                         "null_probe", SCRATCH)
    if got is None:
        fail.append("null probe render")
    else:
        x, sr = got
        worst = max(rms(seg_of(x, sr, k)) for k in range(len(NOTES)))
        print(f"  loudest sustained rms with S=0: {worst:.6f}  "
              f"(threshold {LOCK_RMS})  "
              f"{'ok' if worst <= LOCK_RMS else 'RINGS WITHOUT THE LIP'}")
        if worst > LOCK_RMS:
            fail.append("loop rings at zero blowing pressure")
        rm(pj, wp)

    # ---- ignition scan: LINEAR, all three notes --------------------------
    print("\n=== ignition scan (linear pressure scan, C3+C4+C5) ===")
    windows = {}
    for kind, rtag, ratio, valve, bmode in CONFIGS:
        rowtxt, good3, good2 = [], [], []
        for s in SCAN:
            got, pj, wp = render(cell_patch(ratio, s, 0.0, valve, bmode),
                                 f"scan_{kind}_{rtag}_{int(s*100):03d}",
                                 SCRATCH)
            if got is None:
                continue
            st = note_stats(*got)
            nlk = sum(1 for p in st if p["locked"])
            rowtxt.append("%d" % nlk)
            if nlk == 3:
                good3.append(s)
            if nlk >= 2:
                good2.append(s)
            rm(pj, wp)
        use = good3 if good3 else good2
        windows[f"{kind}_{rtag}"] = {
            "scan": SCAN, "locked_counts": rowtxt,
            "all3": [good3[0], good3[-1]] if good3 else None,
            "min2": [good2[0], good2[-1]] if good2 else None}
        print(f"  {kind}_{rtag}: notes locked per S = {''.join(rowtxt)}")
        if use:
            print(f"      window S {use[0]:.1f}..{use[-1]:.1f}  = "
                  f"{use[0]*PM_TAB/1000:.0f}..{use[-1]*PM_TAB/1000:.0f} kPa "
                  f"at {F_REF:.0f} Hz "
                  f"({'3/3' if good3 else '2/3'} notes)")
            windows[f"{kind}_{rtag}"]["use"] = [use[0], use[-1]]
        else:
            print("      NO LOCK anywhere in the scan")
            windows[f"{kind}_{rtag}"]["use"] = None
            if kind == "flat":
                extra_notes.append(
                    "`flat` produced no note at any blowing pressure in the "
                    "whole scan. That is the result, not a gap: it is still "
                    "in the queue, played back at the gain of the cell it is "
                    "a copy of.")
            else:
                extra_notes.append(
                    f"`{kind}_{rtag}` never lands ON the written note at any "
                    f"blowing pressure - it speaks, but on a different note "
                    f"of the tube. Still in the queue, blown at the in-tune "
                    f"cell's pressure; the partials table says where it "
                    f"landed.")

    # ---- full grid at unity gain (measurements) --------------------------
    print("\n=== full grid ===")
    grid, rows = [], {}
    for kind, rtag, ratio, valve, bmode in CONFIGS:
        w = windows[f"{kind}_{rtag}"]["use"]
        if w is None:
            # A config that never locks the TARGET note is still a cell: the
            # register cells crack up to another partial on purpose, and
            # `flat` is silent on purpose.  Both ride the in-tune cell's
            # blowing pressure so the comparison is like-for-like.
            w = windows["lip_r080"]["use"]
            if w is None:
                continue
        lo, hi = w
        for ptag, pos in (("soft", SOFT_POS), ("loud", LOUD_POS)):
            s = round(lo + (hi - lo) * pos, 3)
            for stag, dep in (("st0", 0.0), ("st1", STEEP_DEPTH)):
                grid.append((f"{kind}_{rtag}_{ptag}_{stag}", kind, rtag,
                             ratio, valve, bmode, s, dep))
    print(f"  {len(grid)} cells")
    for name, kind, rtag, ratio, valve, bmode, s, dep in grid:
        got, pj, wp = render(cell_patch(ratio, s, dep, valve, bmode),
                             name, SCRATCH)
        if got is None:
            fail.append(f"{name} render")
            continue
        x, sr = got
        st = note_stats(x, sr, bmode)
        rows[name] = {"kind": kind, "rtag": rtag, "ratio": ratio,
                      "valve": valve, "bore_mode": bmode, "scale": s,
                      "depth": dep, "pm_pa_at_fref": s * PM_TAB,
                      "notes": st, "raw_rms": rms(x),
                      "raw_peak": float(np.abs(x).max()),
                      "centroid": centroid_all(x, sr),
                      "hf1k": hf_fraction(x, sr, 1000.0),
                      "wav": wp}
        print("%-22s S %.2f dep %.3f  locked %d/3  cents %s  cen %5.0f  "
              "rms %.4f"
              % (name, s, dep, sum(1 for p in st if p["locked"]),
                 " ".join("%+6.1f" % p["cents"] if np.isfinite(p["cents"])
                          else "   ---" for p in st),
                 rows[name]["centroid"], rows[name]["raw_rms"]))

    if not rows:
        print("\nNO CELLS - nothing ignited.")
        return 2

    # ---- ears queue: calibrate, steepen, normalize -----------------------
    print("\n=== ears queue ===")
    ears, ears_meta = [], {}
    for name in EARS_WANT:
        if name not in rows:
            continue
        r = rows[name]
        got, pj, wp = render(cell_patch(r["ratio"], r["scale"], 0.0,
                                        r["valve"], r["bore_mode"]),
                             name + "_cal", SCRATCH)
        if got is None:
            fail.append(f"{name} calibration")
            continue
        dry_peak = max(float(np.abs(got[0]).max()), 1e-6)
        in_gain = DRIVE_PEAK / dry_peak
        rm(pj, wp)
        got, pj, wp = render(cell_patch(r["ratio"], r["scale"], r["depth"],
                                        r["valve"], r["bore_mode"], in_gain,
                                        1.0 / in_gain), name + "_cal2",
                             SCRATCH)
        if got is None:
            fail.append(f"{name} trim pass")
            continue
        trim = trim_for(got[0])
        rm(pj, wp)
        ears_meta[name] = {"in_gain": in_gain, "trim": trim}
        ears.append(name)
    # twins inherit their partner's trim (the silent control must stay silent)
    for name in ears:
        tw = TRIM_TWIN.get(name)
        if tw and tw in ears_meta:
            ears_meta[name]["trim"] = ears_meta[tw]["trim"]
            ears_meta[name]["trim_inherited_from"] = tw
    for name in ears:
        r, m = rows[name], ears_meta[name]
        got, pj, wp = render(
            cell_patch(r["ratio"], r["scale"], r["depth"], r["valve"],
                       r["bore_mode"], m["in_gain"],
                       m["trim"] / m["in_gain"]), name, EARS_OUT)
        if got is None:
            fail.append(f"{name} final render")
            continue
        x, sr = got
        lc, pk, rr = level_ceiling(x, sr), float(np.abs(x).max()), rms(x)
        r.update({"level": lc, "peak": pk, "rms": rr,
                  "ears_notes": note_stats(x, sr, r["bore_mode"])})
        flags = []
        if lc > LEVEL_CEILING:
            flags.append("LEVEL")
            fail.append(f"{name} level {lc:.2f}")
        if rr < AUDIBILITY_FLOOR and name not in TRIM_TWIN:
            flags.append("SILENT")
            fail.append(f"{name} silent")
        print("  %-22s peak %.3f  0.5s rms %.3f  rms %.5f %s"
              % (name, pk, lc, rr, " ".join(flags)))

    # loudness-match the A/B partners (see RMS_TWIN)
    for name, tw in RMS_TWIN.items():
        if name not in ears or tw not in ears:
            continue
        own, twin = rows[name].get("rms"), rows[tw].get("rms")
        if not own or not twin:
            continue
        ears_meta[name]["trim"] *= twin / own
        ears_meta[name]["rms_matched_to"] = tw
        r, m = rows[name], ears_meta[name]
        got, pj, wp = render(
            cell_patch(r["ratio"], r["scale"], r["depth"], r["valve"],
                       r["bore_mode"], m["in_gain"],
                       m["trim"] / m["in_gain"]), name, EARS_OUT)
        if got is None:
            fail.append(f"{name} rms-match render")
            continue
        x, sr = got
        r.update({"level": level_ceiling(x, sr),
                  "peak": float(np.abs(x).max()), "rms": rms(x),
                  "ears_notes": note_stats(x, sr, r["bore_mode"])})
        print("  %-22s rms-matched to %s: peak %.3f  0.5s rms %.3f  rms %.5f"
              % (name, tw, r["peak"], r["level"], r["rms"]))
        if r["level"] > LEVEL_CEILING:
            fail.append(f"{name} level {r['level']:.2f}")

    # ---- gate 2: feature audibility, lip vs the old valve ----------------
    print("\n=== gate 2: feature audibility (lip vs the old valve) ===")
    best_a = "lip_r080_loud_st1" if "lip_r080_loud_st1" in ears else (
        ears[0] if ears else "-")
    best_b = "reed_r080_loud_st1" if "reed_r080_loud_st1" in ears else (
        ears[-1] if ears else "-")
    for a_n, b_n in (("lip_r080_loud_st1", "reed_r080_loud_st1"),
                     ("lip_r080_soft_st1", "reed_r080_soft_st1"),
                     ("lip_r080_loud_st1", "flat_r080_loud_st1")):
        if a_n in ears and b_n in ears:
            a, _ = read_mono(os.path.join(EARS_OUT, a_n + ".wav"))
            b, _ = read_mono(os.path.join(EARS_OUT, b_n + ".wav"))
            d = diff_db(a, b)
            print("  %-22s vs %-22s  diff %6.1f dB  %s"
                  % (a_n, b_n, d, "ok" if d > FEATURE_DB else "INAUDIBLE"))
            if d <= FEATURE_DB:
                fail.append(f"{a_n} inaudible vs {b_n} ({d:.1f} dB)")

    # ---- gate 3: brightness dose-response on blowing pressure ------------
    print("\n=== gate 3: brightness vs blowing pressure ===")
    dose = []
    for kind, rtag, _, _, _ in CONFIGS:
        sn, ln = f"{kind}_{rtag}_soft_st1", f"{kind}_{rtag}_loud_st1"
        if sn in rows and ln in rows and kind != "flat":
            if rows[sn]["scale"] == rows[ln]["scale"]:
                print("  %-10s ignition window is a single pressure step - "
                      "no soft/loud pair exists" % f"{kind}_{rtag}")
                extra_notes.append(
                    f"`{kind}_{rtag}` has an ignition window one step wide, "
                    f"so its quiet and loud cells would be the same file. "
                    f"Only the loud one is in the queue.")
                continue
            ok = rows[ln]["centroid"] > rows[sn]["centroid"]
            dose.append((f"{kind}_{rtag}", rows[sn]["pm_pa_at_fref"],
                         rows[ln]["pm_pa_at_fref"], rows[sn]["centroid"],
                         rows[ln]["centroid"], rows[sn]["hf1k"],
                         rows[ln]["hf1k"]))
            print("  %-10s soft cen %5.0f -> loud cen %5.0f   "
                  "energy>1k %.4f -> %.4f   %s"
                  % (f"{kind}_{rtag}", rows[sn]["centroid"],
                     rows[ln]["centroid"], rows[sn]["hf1k"],
                     rows[ln]["hf1k"], "ok" if ok else "NO DOSE-RESPONSE"))
            if kind == "lip" and rtag == "r080" and not ok:
                fail.append("no brightness dose-response on pressure (lip)")

    # ---- README + manifest ----------------------------------------------
    purge(PATCH_OUT, {n + ".json" for n in rows} | {"null_probe.json"})
    purge(EARS_OUT, {n + ".wav" for n in ears}
          | {"README.md", "measurements.json"})

    cells = "\n".join("| `%s.wav` | %s | %s |"
                      % (n, BLURB.get(n, ("", ""))[0],
                         BLURB.get(n, ("", ""))[1]) for n in ears)
    tbl = ["| cell | lip pitch | blowing (kPa at 427 Hz) | brightener | "
           "notes in tune | centroid Hz | energy >1 kHz | 0.5 s rms | peak |",
           "|---|---|---|---|---|---|---|---|---|"]
    for n in ears:
        r = rows[n]
        st = r.get("ears_notes", r["notes"])
        tbl.append("| `%s` | %s | %.1f | %s | %d of 3 | %.0f | %.4f | %.3f "
                   "| %.3f |"
                   % (n, ("x%.2f the note" % r["ratio"])
                      if r["valve"] == "mass" else "no lip resonance",
                      r["pm_pa_at_fref"] / 1000.0,
                      "on" if r["depth"] else "off",
                      sum(1 for p in st if p["locked"]), r["centroid"],
                      r["hf1k"], r.get("level", float("nan")),
                      r.get("peak", float("nan"))))
    lock = ["How far each note lands from where it should, in cents "
            "(100 cents = one semitone; blank = nothing came out):", "",
            "| cell | C3 | C4 | C5 |", "|---|---|---|---|"]
    for n in ears:
        st = rows[n].get("ears_notes", rows[n]["notes"])
        lock.append("| `%s` | %s |" % (n, " | ".join(
            ("%+.0f" % p["cents"]) if np.isfinite(p["cents"]) else "-"
            for p in st)))
    part = ["Which note of the air column actually spoke. The lip cells "
            "run on a tube three note-lengths long, so \"3\" means it "
            "played the note asked for and \"4\" means the lip cracked it "
            "up to the next one. (The old valve gets a tube one note long "
            "- that is the only tube it can use - so its \"1\" also means "
            "it played the note asked for.) This column is the register "
            "mechanism, and it is the thing a memoryless valve cannot "
            "do:", "",
            "| cell | C3 | C4 | C5 |", "|---|---|---|---|"]
    for n in ears:
        st = rows[n].get("ears_notes", rows[n]["notes"])
        part.append("| `%s` | %s |" % (n, " | ".join(
            ("%.2f" % p["partial"]) if np.isfinite(p.get("partial",
                                                         float("nan")))
            else "-" for p in st)))
    dose_txt = ""
    if dose:
        dose_txt = ("Brightness against blowing pressure. A real brass "
                    "instrument gets brighter when you blow harder, and "
                    "that is half of what this round exists to test:\n\n"
                    "| config | quiet kPa | loud kPa | quiet centroid | "
                    "loud centroid | quiet energy >1 kHz | loud |\n"
                    "|---|---|---|---|---|---|---|\n"
                    + "\n".join("| %s | %.1f | %.1f | %.0f | %.0f | %.4f "
                                "| %.4f |"
                                % (t, sp / 1000.0, lp / 1000.0, sc, lc, sh,
                                   lh)
                                for t, sp, lp, sc, lc, sh, lh in dose))
    note_txt = ("## Also worth knowing\n\n"
                + "\n".join("- " + s for s in extra_notes)) \
        if extra_notes else ""
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{cells}", cells)
                      .replace("{table}", "\n".join(tbl))
                      .replace("{lock}", "\n".join(lock))
                      .replace("{partials}", "\n".join(part))
                      .replace("{dose}", dose_txt)
                      .replace("{notes}", note_txt)
                      .replace("{best_a}", best_a)
                      .replace("{best_b}", best_b))
    with open(os.path.join(EARS_OUT, "measurements.json"), "w") as f:
        json.dump({"constants": {"PSI": PSI, "PSI_HAT": PSI_HAT, "B0": B0,
                                 "YEQ_HAT": YEQ_HAT, "F_REF": F_REF,
                                 "P_REF_Pa": P_REF, "Y_REF_m": Y_REF,
                                 "Zc": ZC, "a0": A0, "Q_lip": Q_LIP,
                                 "bore_mode": BORE_MODE,
                                 "reed_bore_mode": REED_MODE},
                   "flow_gate": [{"y": a, "pminus": b, "pm": c, "want": d,
                                  "got": e, "rel_err": g}
                                 for a, b, c, d, e, g in flow_rows],
                   "ignition": windows,
                   "ears": ears, "ears_gain": ears_meta,
                   "best_ab": [best_a, best_b],
                   "cells": {n: {k: v for k, v in r.items() if k != "wav"}
                             for n, r in rows.items()}},
                  f, indent=1, default=float)

    shutil.rmtree(SCRATCH, ignore_errors=True)
    print(f"\n{len(ears)} ears cells staged -> {EARS_OUT}")
    print(f"best A/B: {best_a} vs {best_b}")
    if fail:
        print("\nGATE FAILURES:")
        for f_ in fail:
            print("  " + f_)
        return 2
    print("\nAll gates passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
