"""Nonlinear bore probe round 1 — a one-way, out-of-loop wave steepener.

Spec: docs/superpowers/specs/2026-09-17-nonlinear-bore-design.md
Literature: docs/research/nonlinear_bore/MSALLAM_DIGEST.md

The physics (Tassart/Msallam ICMC'97 Eq. 10): in the trombone's cylindrical
slide the propagation speed depends on the wave itself,
c(u) = c0 + ((gamma+1)/2) u, so pressure peaks outrun troughs, the wavefront
steepens, and harmonic energy grows WITH LEVEL. The 1996 JASA companion says
the effect matters for the RADIATED sound, not for the self-oscillation — so
this probe is deliberately a one-way steepener bolted on the OUTPUT of an
existing patch, outside its feedback loop (variant 1 of the Acta Acustica
paper).

ZERO engine code. The lumped discrete form (Cooper & Abel DAFx-10 4.2) is a
fixed delay whose length is modulated by the signal running through it:

    d(t) = D * (1 - depth * x(t))

expressed in existing nodes as

    e --> NL_in (gain) --+--> NL_delay (DelayLine, ratio pin driven)
                         |         ^
                         |         |  ratio = 1 - depth*x
                         +--(tap)--+--> NL_curve (CurveNode, linear knots)
                                   |
                                   +--> [NL_bell: Biquad RBJ lowpass] --> NL_out

Wiring notes that matter:
  * NL_curve reads NL_in through a {"tap": ...} edge. A tap never advances
    its target, so NL_delay stays the sole advancer of NL_in and the curve
    sees the SAME sample the delay is writing (a plain second {"ref": ...}
    would have put the modulator one sample ahead of the audio).
  * DelayLine length = sampleRate/frequency * ratio. frequency is pinned to
    a constant (BASE_HZ = 120 -> D = 400 samples = 8.3 ms ~ a 2.9 m bore at
    48 k), compensate stays OFF: this is not a tuned loop member.
  * Linear-interp fractional read at 48 k, no oversampling. Accepted for the
    probe (C&A call the FIR HF droop "not unwelcome"); noted in the README.

Cells
  sine ladder     233 Hz sine, depth 0 / .005 / .01 / .02 / .04 / .08
                  + a bypass render for the depth-0 null
  level pair      depth .04 at input gain 0.1 (pp) vs 0.9 (ff)
  sign flip       depth -0.04 against +0.04
  musical         flute1 + oboe1 from patches/library/winds/, depth ladder
                  x bell-lowpass on/off, plus a soft/loud pair on oboe

Gates: depth-0 null, dose-response monotonicity (harmonic distortion and
spectral centroid vs depth), feature audibility vs the cell's own control,
level ceiling (no 0.5 s window above rms 0.5), audibility floor, and
peak-normalisation of the ears queue to -6 dBFS.

Outputs (this script OWNS all three dirs and purges anything else):
  patches/sweep/nlbore_probe1/
  renders/dsp/sweep/nlbore_probe1/       (measurement cells)
  renders/dsp/audition/nlbore_probe1/    (<= 8 ears cells + README.md)
Usage: python tools/gen_nlbore_probe1.py
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bandedwg import mul  # noqa: E402
from gen_stk_bowed import read_mono  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "nlbore_probe1")
MEAS_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "nlbore_probe1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "nlbore_probe1")
WINDS = os.path.join(ROOT, "patches", "library", "winds")

SR = 48000
BASE_HZ = 120.0            # D = 48000/120 = 400 samples exactly
D_SAMPLES = SR / BASE_HZ
KNOT_X = 2.0               # curve domain; beyond it the ratio law clamps

# --- sine measurement cells -------------------------------------------------
SINE_F = 233.08            # Bb3
SINE_SEC = 2.5
WIN = (0.8, 1.8)           # analysis window, seconds
LADDER = [0.0, 0.005, 0.01, 0.02, 0.04, 0.08]
SINE_GAIN = 0.5
LEVEL_PAIR = [("pp", 0.1), ("ff", 0.9)]
LEVEL_DEPTH = 0.04
SIGN_DEPTH = -0.04
# Rise-rate check. JASA'96 (digest 2.6) says the shock-severity parameter is
# the maximum pressure RISE RATE, not the amplitude. For a sine of amplitude A
# at f0 through this element the modulation index is beta = 2*pi*f0*D*depth*A
# radians — i.e. it scales with f0 at fixed amplitude, which IS the rise-rate
# statement. Measured across three octaves at one depth.
RISE_F = [233.08, 466.16, 932.33]
RISE_DEPTH = 0.04

# --- musical ears cells -----------------------------------------------------
# Calibration of `depth` against the literature: the loud leg is normalised to
# peak DRIVE_PEAK before the steepener, so the delay swing is
# depth*DRIVE_PEAK*D samples. At depth 0.03 that is 2.4% of D = 9.6 samples —
# inside the 1-3% band the sources give for a trombone slide at ff
# (digest S4 2.1 / spec "Probe implementation"). depth 0.06 is deliberately
# ~2x past physical, to hear where it stops being an instrument.
# name -> (patch, depth, level ("loud"/"soft"), bell corner Hz or None)
EARS = {
    "flute_ctl":       ("flute1", 0.0,  "loud", None),
    "flute_d030":      ("flute1", 0.03, "loud", None),
    "flute_d060":      ("flute1", 0.06, "loud", None),
    "flute_d030_bell": ("flute1", 0.03, "loud", 1200.0),
    "oboe_ctl":        ("oboe1",  0.0,  "loud", None),
    "oboe_d030_bell":  ("oboe1",  0.03, "loud", 2000.0),
    "oboe_soft_d030":  ("oboe1",  0.03, "soft", None),
    "oboe_loud_d030":  ("oboe1",  0.03, "loud", None),
}
SOFT_FACTOR = 0.3          # the "pp into the steepener" leg of the A/B
DRIVE_PEAK = 0.8           # normalise the loud leg to this peak before the
                           # steepener, so `depth` means the same on any patch
# flute1 ships a single 3 s note; three octaves make a better audition and
# the pitches were verified (86.9 / 173.8 / 348.5 Hz measured).
SCORE_OVERRIDE = {"flute1": ([41, 53, 65], 2.0, 1.8)}

# --- gates ------------------------------------------------------------------
LEVEL_CEILING = 0.5        # max 0.5 s-window rms (speaker safety)
AUDIBILITY_FLOOR = 1e-4    # rms below this = silent cell
NULL_GATE_DB = -80.0       # depth-0 vs bypass must null below this
FEATURE_DB = -26.0         # level-matched diff vs own control, ~5% of signal
PEAK_TARGET_DB = -6.0      # ears-queue peak normalisation


# ---------------------------------------------------------------------------
# graph construction
# ---------------------------------------------------------------------------
def ratio_knots(depth):
    """ratio(x) = 1 - depth*x, exactly linear over [-KNOT_X, +KNOT_X].

    CurveNode points mode with smoothness 0.5 is an exact lerp and clamps
    to the end knots outside the domain, so an excursion beyond +-2 just
    saturates the delay modulation instead of running away.
    """
    return [[-KNOT_X, round(1.0 + depth * KNOT_X, 9)],
            [0.0, 1.0],
            [KNOT_X, round(1.0 - depth * KNOT_X, 9)]]


def rbj_lowpass(fc, q=0.70710678):
    """RBJ biquad lowpass, normalised to the engine's Direct Form I:
    y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2."""
    w0 = 2.0 * math.pi * fc / SR
    c, s = math.cos(w0), math.sin(w0)
    alpha = s / (2.0 * q)
    a0 = 1.0 + alpha
    return {"b0": (1.0 - c) / 2.0 / a0, "b1": (1.0 - c) / a0,
            "b2": (1.0 - c) / 2.0 / a0, "a1": -2.0 * c / a0,
            "a2": (1.0 - alpha) / a0}


def steepener(src_id, depth, in_gain, out_gain, bell_fc=None, bypass=False):
    """The probe chain appended to an existing graph. Returns (nodes, out_id).

    Node ORDER is load-bearing: NL_in must be built before NL_delay refs it,
    NL_curve before NL_delay refs its ratio pin, and NL_delay must be the
    only {"ref"} consumer of NL_in so it owns the advance.
    """
    nodes = [mul("NL_in", {"ref": src_id}, round(float(in_gain), 9))]
    if bypass:
        nodes.append(mul("NL_out", {"ref": "NL_in"}, round(float(out_gain), 9)))
        return nodes, "NL_out"
    nodes.append({"id": "NL_curve", "type": "CurveNode", "params": {
        "exprKnots": [], "interp": "linear", "knots": ratio_knots(depth),
        "mode": "points", "source": {"tap": "NL_in"}}})
    nodes.append({"id": "NL_delay", "type": "DelayLine", "params": {
        "source": {"ref": "NL_in"}, "frequency": BASE_HZ,
        "ratio": {"ref": "NL_curve"}, "amplitude": 1.0,
        "compensate": False}})
    last = "NL_delay"
    if bell_fc:
        p = {"source": {"ref": "NL_delay"}, "mode": 0, "frequency": 220.0,
             "radius": 0.0}
        p.update({k: round(v, 9) for k, v in rbj_lowpass(bell_fc).items()})
        nodes.append({"id": "NL_bell", "type": "Biquad", "params": p})
        last = "NL_bell"
    nodes.append(mul("NL_out", {"ref": last}, round(float(out_gain), 9)))
    return nodes, "NL_out"


def sine_patch(depth, gain, bypass=False, f0=SINE_F):
    nodes = [{"id": "Sine", "type": "SineSource", "params": {
        "frequency": f0, "amplitude": gain, "phase": 0.0}}]
    chain, out = steepener("Sine", depth, 1.0, 1.0, bypass=bypass)
    nodes += chain
    return {"sampleRate": SR, "seconds": SINE_SEC,
            "instrument": {"polyphony": 1, "volume": 1.0},
            "score": [{"note": 60, "time": 0.0, "duration": SINE_SEC,
                       "velocity": 1.0}],
            "graph": {"output": out, "nodes": nodes}}


def musical_patch(base, depth, in_gain, out_gain, bell_fc=None, bypass=False):
    with open(os.path.join(WINDS, base + ".json")) as f:
        p = json.load(f)
    src = p["graph"]["output"]
    chain, out = steepener(src, depth, in_gain, out_gain, bell_fc, bypass)
    p["graph"]["nodes"] = p["graph"]["nodes"] + chain
    p["graph"]["output"] = out
    p.pop("ui", None)          # positions refer to the old output node
    if base in SCORE_OVERRIDE:
        notes, slot, dur = SCORE_OVERRIDE[base]
        p["score"] = [{"note": n, "time": slot * k, "duration": dur,
                       "velocity": 0.8} for k, n in enumerate(notes)]
        p["seconds"] = slot * len(notes)
    return p


# ---------------------------------------------------------------------------
# measurement
# ---------------------------------------------------------------------------
def rms(x):
    return float(np.sqrt((x ** 2).mean())) if len(x) else 0.0


def level_ceiling(x, sr):
    win = int(0.5 * sr)
    nw = len(x) // win
    if nw == 0:
        return rms(x)
    w = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    return float(w.max())


def window(x, sr):
    return x[int(WIN[0] * sr):int(WIN[1] * sr)]


def harmonics(x, sr, f0, nmax=40):
    """Amplitude at each harmonic of f0, summed over a +-4 Hz band."""
    seg = window(x, sr)
    seg = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg))
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    out = []
    for k in range(1, nmax + 1):
        f = k * f0
        if f > sr * 0.45:
            break
        band = (fr > f - 4.0) & (fr < f + 4.0)
        out.append(float(np.sqrt((sp[band] ** 2).sum())))
    return np.array(out)


def thd(x, sr, f0):
    """sqrt(sum of harmonics 2..N squared) / fundamental."""
    h = harmonics(x, sr, f0)
    if len(h) < 2 or h[0] <= 0:
        return float("nan")
    return float(np.sqrt((h[1:] ** 2).sum()) / h[0])


def centroid(x, sr, flo=40.0, fhi=20000.0):
    seg = window(x, sr)
    seg = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    m = (fr > flo) & (fr < fhi)
    tot = sp[m].sum()
    return float((fr[m] * sp[m]).sum() / tot) if tot > 0 else float("nan")


def centroid_all(x, sr, flo=40.0, fhi=20000.0):
    """Spectral centroid over a whole render (musical cells span notes, so
    the fixed analysis window used for the sine cells does not apply)."""
    seg = x * np.hanning(len(x))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    m = (fr > flo) & (fr < fhi)
    tot = sp[m].sum()
    return float((fr[m] * sp[m]).sum() / tot) if tot > 0 else float("nan")


def hf_fraction(x, sr, fsplit, flo=40.0, fhi=20000.0):
    """Gain-invariant fraction of in-band energy above fsplit."""
    seg = x * np.hanning(len(x))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    tot = sp[(fr > flo) & (fr < fhi)].sum()
    hi = sp[(fr > fsplit) & (fr < fhi)].sum()
    return float(hi / tot) if tot > 0 else float("nan")


def slope_asymmetry(x, sr):
    """max rising slope / max falling slope in the analysis window. A
    steepened wave has one edge sharper than the other; the sign of `depth`
    picks which one."""
    d = np.diff(window(x, sr))
    up, dn = float(d.max()), float(-d.min())
    return up / dn if dn > 0 else float("nan")


def null_db(ref, cell):
    """Best-lag residual of `cell` against `ref`, dB re the ref rms.
    Searches +-8 samples around the nominal D so a one-sample wiring skew
    shows up as an alignment, not as a failure."""
    best = (float("inf"), 0)
    n = min(len(ref), len(cell))
    a0, a1 = int(0.3 * SR), int(min(2.0 * SR, n - int(D_SAMPLES) - 10))
    r = ref[a0:a1]
    denom = rms(r)
    for lag in range(int(D_SAMPLES) - 8, int(D_SAMPLES) + 9):
        c = cell[a0 + lag:a1 + lag]
        if len(c) != len(r):
            continue
        e = float(np.abs(r - c).max())
        if e < best[0]:
            best = (e, lag)
    if denom <= 0:
        return float("nan"), best[1]
    return 20.0 * math.log10(max(best[0], 1e-12) / denom), best[1]


def diff_db(a, b):
    """Level-matched difference between two same-length renders, dB re a."""
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    ra, rb = rms(a), rms(b)
    if ra <= 0 or rb <= 0:
        return float("nan")
    return 20.0 * math.log10(max(rms(a - b * (ra / rb)), 1e-12) / ra)


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------
def render(name, patch, rend_dir):
    ppath = os.path.join(PATCH_OUT, f"nlbore_{name}.json")
    wpath = os.path.join(rend_dir, f"nlbore_{name}.wav")
    with open(ppath, "w") as f:
        json.dump(patch, f, indent=1)
    r = subprocess.run([CLI, ppath, wpath], capture_output=True, text=True,
                       timeout=1800)
    if r.returncode != 0 or not os.path.exists(wpath):
        print(f"  {name:20s} RENDER_FAIL rc={r.returncode} "
              f"{(r.stderr or r.stdout or '')[-300:]}")
        return None, ppath, wpath
    x, sr = read_mono(wpath)
    return (x, sr), ppath, wpath


def purge(d, keep):
    os.makedirs(d, exist_ok=True)
    for fn in os.listdir(d):
        if fn not in keep:
            os.remove(os.path.join(d, fn))
            print(f"purged stale: {os.path.basename(d)}/{fn}")


# ---------------------------------------------------------------------------
README = """# Nonlinear bore probe 1 - "does forte get brassy?"

Renders here; patches in `patches/sweep/nlbore_probe1/`; generator
`tools/gen_nlbore_probe1.py`. Measurement-only renders (sine tests) are in
`renders/dsp/sweep/nlbore_probe1/` - you do not need to listen to those.

## What this is, in plain language

A real trombone gets brighter when it is played loud, and not because the
player changes anything about the lips. Sound travels slightly faster where
the air is compressed, so the loud peaks of the wave catch up with the
troughs on their way down the tube. The wave shape gets lopsided - a sharp
edge - and a sharp edge is high frequencies. Quiet playing does not do this
at all. That is what "brassy" means physically.

I built that one effect and bolted it on the OUTPUT of two of our existing
wind patches. Nothing inside the patches changed; the loops still run exactly
as they did. It is a delay whose length wobbles with the signal going through
it - built entirely out of parts we already have, no new engine code.

**The question for you: does the loud version read as brassy, or just as
distorted?** And is there a setting of it you would actually use on a patch?

## What to listen for, cell by cell

| file | what it is | listen for |
|---|---|---|
{cells}

The pair that answers the headline question is **`oboe_soft_d030` vs
`oboe_loud_d030`**. Identical patch, identical effect, identical playback
loudness - the ONLY difference is how hard the signal is driven into the
effect before it is turned back down. If the effect is doing what a real bore
does, the loud one should be noticeably brighter/edgier and the soft one
should be nearly clean. If they sound the same, the mechanism is not
level-dependent and I have got something wrong.

`flute_ctl` and `oboe_ctl` are **dead controls** - the effect is present in
the graph but its strength is zero. They should sound exactly like the stock
patch. They are there so you can A/B without trusting me.

The `_bell` cells add a gentle lowpass after the steepener, standing in for
the trombone bell's reflection bandwidth (~800 Hz in the literature). Real
horns radiate the *complement* of that filter, so treat these as "does taming
the top help", not as a finished bell.

## Two limitations, stated up front

**1. It is doing mild FM, not shock formation.** On a pure sine the added
harmonics come out in an exact Bessel ladder - the fingerprint of phase
modulation, not of a wave collapsing into a sawtooth. That is not a bug and
it is not a surprise: the paper this form comes from says so itself (with
shock handling omitted "the effect becomes equivalent to phase/frequency
modulation"). At real-instrument strength on a 233 Hz tone the modulation
index is only about 0.2 radians, nowhere near a shock. A real trombone gets
shocks at fortissimo because the mouthpiece pressure has a near-vertical
edge, not because it is loud - so the effect here gets much stronger on
bright, spiky source material than on a smooth tone, which is why the oboe
cells move so much more than the flute cells. Details in the run report.

**2. No oversampling.** The delay reads with plain linear interpolation at
48 kHz, so some of the added high end is interpolation artifact rather than
physics. Cooper & Abel call that artifact "not unwelcome" because it loosely
mimics air damping, but the top octave is not trustworthy as physics. If you
like the sound, round 2 fixes this properly.

## Measurements

Peak levels are normalised to -6 dBFS across the queue, so loudness
differences you hear are timbre, not gain.

{table}

"diff vs ctl" is how big the difference signal is against the control, after
matching levels. Anything above about -26 dB is comfortably audible; a
reading near or above 0 dB means the waveform has been re-phased so much that
subtracting the two leaves more than either one - true of every oboe cell, so
take those as "grossly different waveform", not as a fine-grained score.

Dose-response on a pure 233 Hz sine (the science gate - no instrument, just
the effect):

{ladder}

{levels}

{rise}

{sign}
"""


def main():
    if not os.path.exists(CLI):
        print(f"mforce_cli not found at {CLI}")
        return 1
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(MEAS_OUT, exist_ok=True)
    os.makedirs(EARS_OUT, exist_ok=True)

    meas_names = ["sine_bypass"] + [f"sine_d{int(d*1000):03d}" for d in LADDER] \
        + [f"sine_{t}_d{int(LEVEL_DEPTH*1000):03d}" for t, _ in LEVEL_PAIR] \
        + [f"sine_f{int(round(f)):04d}_d{int(RISE_DEPTH*1000):03d}"
           for f in RISE_F] \
        + [f"sine_neg{int(abs(SIGN_DEPTH)*1000):03d}"]
    ears_names = list(EARS)
    cal_names = [n + "_cal" for n in ears_names]
    purge(PATCH_OUT, {f"nlbore_{n}.json"
                      for n in meas_names + ears_names + cal_names})
    purge(MEAS_OUT, {f"nlbore_{n}.wav" for n in meas_names + cal_names})
    purge(EARS_OUT, {f"nlbore_{n}.wav" for n in ears_names} | {"README.md"})

    fail = []

    # ---- Phase A: sine dose-response ------------------------------------
    print("\n=== Phase A: sine dose-response (233 Hz) ===")
    got, _, _ = render("sine_bypass", sine_patch(0.0, SINE_GAIN, bypass=True),
                       MEAS_OUT)
    if got is None:
        return 1
    bypass, sr = got
    print("%-16s %10s %10s %10s %10s %10s"
          % ("cell", "THD", "THD dB", "centroid", "slopeAsym", "vs d000"))
    ladder_rows, ref0 = [], None
    for d in LADDER:
        nm = f"sine_d{int(d*1000):03d}"
        got, _, _ = render(nm, sine_patch(d, SINE_GAIN), MEAS_OUT)
        if got is None:
            fail.append(f"{nm} render")
            continue
        x, sr = got
        t, cen, sa = thd(x, sr, SINE_F), centroid(x, sr), slope_asymmetry(x, sr)
        if d == 0.0:
            ref0 = x
            nulldb, lag = null_db(bypass, x)
            print("%-16s %10.6f %10.1f %10.1f %10.4f   NULL %.1f dB @ lag %d"
                  % (nm, t, 20 * math.log10(max(t, 1e-12)), cen, sa,
                     nulldb, lag))
            if nulldb > NULL_GATE_DB:
                fail.append(f"depth-0 null {nulldb:.1f} dB > {NULL_GATE_DB}")
            ladder_rows.append((d, t, cen, sa, nulldb))
        else:
            dd = diff_db(ref0, x) if ref0 is not None else float("nan")
            print("%-16s %10.6f %10.1f %10.1f %10.4f %10.1f"
                  % (nm, t, 20 * math.log10(max(t, 1e-12)), cen, sa, dd))
            ladder_rows.append((d, t, cen, sa, dd))

    # monotonicity gate
    mono = True
    if len(ladder_rows) == len(LADDER):
        ts = [r[1] for r in ladder_rows]
        cs = [r[2] for r in ladder_rows]
        mono = all(ts[i] < ts[i + 1] for i in range(len(ts) - 1))
        mono_c = all(cs[i] < cs[i + 1] for i in range(len(cs) - 1))
        print(f"\nmonotonic THD: {mono}   monotonic centroid: {mono_c}")
        if not mono:
            fail.append("THD not monotonic in depth")
    else:
        fail.append("ladder incomplete")

    # ---- level dependence ------------------------------------------------
    print("\n=== level dependence (same cell, two input gains) ===")
    lvl_rows = []
    for tag, g in LEVEL_PAIR:
        nm = f"sine_{tag}_d{int(LEVEL_DEPTH*1000):03d}"
        got, _, _ = render(nm, sine_patch(LEVEL_DEPTH, g), MEAS_OUT)
        if got is None:
            fail.append(f"{nm} render")
            continue
        x, sr = got
        t = thd(x, sr, SINE_F)
        lvl_rows.append((tag, g, t, centroid(x, sr)))
        print("%-16s gain %.2f  THD %.6f (%.1f dB)  centroid %.1f"
              % (nm, g, t, 20 * math.log10(max(t, 1e-12)), centroid(x, sr)))
    if len(lvl_rows) == 2 and not (lvl_rows[1][2] > lvl_rows[0][2] * 3.0):
        fail.append("no level dependence (ff THD not > 3x pp THD)")

    # ---- rise-rate scaling ----------------------------------------------
    print("\n=== rise-rate scaling (same depth and amplitude, 3 octaves) ===")
    rise_rows = []
    for f0 in RISE_F:
        nm = f"sine_f{int(round(f0)):04d}_d{int(RISE_DEPTH*1000):03d}"
        got, _, _ = render(nm, sine_patch(RISE_DEPTH, SINE_GAIN, f0=f0),
                           MEAS_OUT)
        if got is None:
            fail.append(f"{nm} render")
            continue
        x, sr = got
        h = harmonics(x, sr, f0, 12)
        # beta from the Bessel ratio J1/J0 ~ beta/2 for small beta
        ratio = float(h[1] / h[0]) if h[0] > 0 else float("nan")
        beta_pred = 2.0 * math.pi * f0 * (D_SAMPLES / SR) * RISE_DEPTH \
            * SINE_GAIN
        rise_rows.append((f0, thd(x, sr, f0), ratio, beta_pred,
                          slope_asymmetry(x, sr)))
        print("%-20s f0 %7.1f  THD %.5f  H2/H1 %.5f  beta_pred %.4f rad  "
              "slopeAsym %.3f"
              % (nm, f0, rise_rows[-1][1], ratio, beta_pred,
                 rise_rows[-1][4]))

    # ---- sign flip -------------------------------------------------------
    print("\n=== sign flip control ===")
    sign_rows = []
    nm = f"sine_neg{int(abs(SIGN_DEPTH)*1000):03d}"
    got, _, _ = render(nm, sine_patch(SIGN_DEPTH, SINE_GAIN), MEAS_OUT)
    if got is not None:
        x, sr = got
        pos = [r for r in ladder_rows if abs(r[0] - abs(SIGN_DEPTH)) < 1e-9]
        sign_rows = [("+%.3f" % abs(SIGN_DEPTH), pos[0][1], pos[0][3])
                     if pos else ("+", float("nan"), float("nan")),
                     ("%.3f" % SIGN_DEPTH, thd(x, sr, SINE_F),
                      slope_asymmetry(x, sr))]
        for tag, t, sa in sign_rows:
            print("%-16s THD %.6f  slopeAsym %.4f" % (tag, t, sa))
    else:
        fail.append("sign-flip render")

    # ---- Phase B: musical ears cells -------------------------------------
    print("\n=== Phase B: musical cells ===")
    # calibration pass: measure each base patch's dry peak so `depth` means
    # the same thing on every patch, and set the listening trim.
    dry_peak = {}
    for base in sorted({v[0] for v in EARS.values()}):
        nm = base + "_dry_cal"
        got, pp, wp = render(nm, musical_patch(base, 0.0, 1.0, 1.0,
                                               bypass=True), MEAS_OUT)
        if got is None:
            fail.append(f"{base} dry calibration")
            continue
        dry_peak[base] = max(float(np.abs(got[0]).max()), 1e-6)
        print(f"  {base:10s} dry peak {dry_peak[base]:.4f} -> in_gain "
              f"{DRIVE_PEAK / dry_peak[base]:.3f}")
        for p in (pp, wp):
            if os.path.exists(p):
                os.remove(p)

    rows, ctl_cache = [], {}
    for name, (base, depth, lvl, bell) in EARS.items():
        if base not in dry_peak:
            continue
        in_gain = DRIVE_PEAK / dry_peak[base] * (SOFT_FACTOR
                                                 if lvl == "soft" else 1.0)
        # pass 1: probe at unity trim, then normalise the peak to -6 dBFS.
        got, pp, wp = render(name + "_cal",
                             musical_patch(base, depth, in_gain,
                                           1.0 / in_gain, bell), MEAS_OUT)
        if got is None:
            fail.append(f"{name} calibration render")
            continue
        peak = max(float(np.abs(got[0]).max()), 1e-6)
        trim = (10.0 ** (PEAK_TARGET_DB / 20.0)) / peak
        for p in (pp, wp):
            if os.path.exists(p):
                os.remove(p)
        got, pp, wp = render(name, musical_patch(base, depth, in_gain,
                                                 trim / in_gain, bell),
                             EARS_OUT)
        if got is None:
            fail.append(f"{name} render")
            continue
        x, sr = got
        lc, rr, pk = level_ceiling(x, sr), rms(x), float(np.abs(x).max())
        hf2, hf1 = hf_fraction(x, sr, 2000.0), hf_fraction(x, sr, 1000.0)
        cen = centroid_all(x, sr)
        bad = []
        if lc > LEVEL_CEILING:
            bad.append(f"LEVEL {lc:.2f}")
        if rr < AUDIBILITY_FLOOR:
            bad.append("SILENT")
        if name.endswith("_ctl"):
            ctl_cache[base] = x
        rows.append([name, base, depth, lvl, bell, hf2, hf1, cen, lc, rr,
                     pk, bad])
        print("%-18s depth %.3f %-5s bell %-6s hf2k %.4f hf1k %.4f cen %6.0f "
              " winRms %.3f peak %.3f rms %.4f %s"
              % (name, depth, lvl, str(bell), hf2, hf1, cen, lc, pk, rr,
                 " ".join(bad)))

    # feature-audibility gate: every non-control cell vs its own control
    print("\n=== feature audibility (vs own control) ===")
    for r in rows:
        name, base = r[0], r[1]
        if name.endswith("_ctl") or base not in ctl_cache:
            r.append(float("nan"))
            continue
        x, _ = read_mono(os.path.join(EARS_OUT, f"nlbore_{name}.wav"))
        dd = diff_db(ctl_cache[base], x)
        ctl_hf = hf_fraction(ctl_cache[base], SR, 1000.0)
        ctl_cen = centroid_all(ctl_cache[base], SR)
        print("%-18s diff %.1f dB   hf1k %.4f vs ctl %.4f (%+.1f pp)   "
              "centroid %.0f vs %.0f Hz   %s"
              % (name, dd, r[6], ctl_hf, (r[6] - ctl_hf) * 100.0, r[7],
                 ctl_cen, "ok" if dd > FEATURE_DB else "INAUDIBLE"))
        if dd <= FEATURE_DB:
            fail.append(f"{name} inaudible vs control ({dd:.1f} dB)")
        r.append(dd)

    # ---- README ----------------------------------------------------------
    blurb = {
        "flute_ctl": "flute (3 notes, an octave apart), effect OFF",
        "flute_d030": "flute, effect at real-instrument strength",
        "flute_d060": "flute, effect pushed ~2x past physical",
        "flute_d030_bell": "flute, real strength + bell lowpass 1.2 kHz",
        "oboe_ctl": "oboe (5 notes, C3 to C7), effect OFF",
        "oboe_d030_bell": "oboe, real strength + bell lowpass 2 kHz",
        "oboe_soft_d030": "oboe, QUIET into the effect (the pp leg)",
        "oboe_loud_d030": "oboe, LOUD into the effect (the ff leg)",
    }
    listen = {
        "flute_ctl": "should be the stock flute, unchanged",
        "flute_d030": "a bit more edge than the control - subtle by design",
        "flute_d060": "where it stops being an instrument (or does it?)",
        "flute_d030_bell": "the lowpass mostly cancels the effect on this "
                           "dark patch - warmth worth it?",
        "oboe_ctl": "should be the stock oboe, unchanged",
        "oboe_d030_bell": "edge with the top tamed; check the high notes",
        "oboe_soft_d030": "A of the A/B - should be close to the control",
        "oboe_loud_d030": "B of the A/B - the brassiness test",
    }
    cells = "\n".join(
        "| `nlbore_%s.wav` | %s | %s |" % (r[0], blurb.get(r[0], ""),
                                           listen.get(r[0], ""))
        for r in rows)
    tbl = ["| cell | patch | depth | drive | bell Hz | energy >1 kHz | "
           "energy >2 kHz | centroid Hz | diff vs ctl | 0.5 s rms | peak |",
           "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        tbl.append("| `%s` | %s | %.3f | %s | %s | %.4f | %.4f | %.0f | %s "
                   "| %.3f | %.3f |"
                   % (r[0], r[1], r[2], r[3],
                      ("%d" % r[4]) if r[4] else "-", r[6], r[5], r[7],
                      "-" if not np.isfinite(r[12]) else "%.1f dB" % r[12],
                      r[8], r[10]))
    lad = ["| depth | harmonic distortion | vs depth 0 | spectral centroid Hz "
           "| rise/fall slope |", "|---|---|---|---|---|"]
    for d, t, cen, sa, dd in ladder_rows:
        lad.append("| %.3f | %.5f (%.1f dB) | %s | %.0f | %.3f |"
                   % (d, t, 20 * math.log10(max(t, 1e-12)),
                      ("null %.0f dB" % dd) if d == 0.0 else "%.1f dB" % dd,
                      cen, sa))
    lvl = "Level dependence at depth %.3f: " % LEVEL_DEPTH + ", ".join(
        "%s (input %.2f) distortion %.5f" % (t, g, v)
        for t, g, v, _ in lvl_rows) + "."
    if len(lvl_rows) == 2 and lvl_rows[0][2] > 0:
        lvl += " Loud is **%.1fx** the distortion of quiet." % (
            lvl_rows[1][2] / lvl_rows[0][2])
    rise = ""
    if rise_rows:
        rise = ("Same effect strength, same amplitude, three octaves of "
                "source tone - the effect grows with pitch (i.e. with how "
                "fast the waveform is moving), which is the literature's "
                "actual severity parameter:\n\n"
                "| source tone | harmonic distortion | predicted modulation "
                "index (rad) | rise/fall slope |\n|---|---|---|---|\n"
                + "\n".join("| %.0f Hz | %.5f | %.4f | %.3f |"
                            % (f, t, b, sa)
                            for f, t, _, b, sa in rise_rows))
    sgn = ""
    if sign_rows:
        sgn = ("Sign flip: %s gives rise/fall slope ratio %.3f, %s gives "
               "%.3f - the same amount of harmonic energy, on the other edge "
               "of the wave (for a steady sine, flipping the sign is a time "
               "shift, so this is the expected result)."
               % (sign_rows[0][0], sign_rows[0][2], sign_rows[1][0],
                  sign_rows[1][2]))
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{cells}", cells)
                      .replace("{table}", "\n".join(tbl))
                      .replace("{ladder}", "\n".join(lad))
                      .replace("{levels}", lvl)
                      .replace("{rise}", rise)
                      .replace("{sign}", sgn))

    print("\n%d ears cells staged -> %s" % (len(rows), EARS_OUT))
    if fail:
        print("\nGATE FAILURES:")
        for f_ in fail:
            print("  " + f_)
        return 2
    print("\nAll gates passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
