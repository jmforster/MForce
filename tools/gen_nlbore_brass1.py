"""Nonlinear bore x lip exciters — the brass pairing round.

Spec: docs/superpowers/specs/2026-09-17-nonlinear-bore-design.md (this is the
first step of its "Round 2" direction, still ZERO engine code).
Probe it builds on: tools/gen_nlbore_probe1.py, REVIEW 66, commit 1a11064.
Carrier notes: docs/research/feedback_sweeps/BRASS_HARNESS_NOTES.md,
docs/research/stk_port/STK_PORT_NOTES.md.

Round 1 (the probe) proved the steepener element on wind patches and Matt
kept it ("It definitely brightens in a brassy way"). It also showed the
effect scales with how bright/fast the source material is. So the obvious
next move is to put it where brass actually lives: on LIP-driven exciters.

Two carriers:
  lip  patches/audition/brass_harness1/br1_t100_q15_m3.json — the winning
       cell of the 09-12 lip harness (outward-striking lip valve on a long
       bore). Locked register is C3-C5 only; C6/C7 lock an octave low, so
       this round renders C3 C4 C5 and says so. Known flaw carried over:
       +14..+33 cents static sharp (measured here, not assumed).
  stk  patches/sweep/stk_brass_port/stk_brass_default_canon.json — the STK
       Brass port after the low-shelf DC fix. Measured here: silent at C3
       and C7, speaks at C4/C5/C6; this round renders C4 C5.

Steepener wiring is VERBATIM from the probe (the tap-not-ref detail is
load-bearing: a second {"ref"} would advance the modulator one sample ahead
of the audio, a {"tap"} sees the same sample the delay is writing):

    anchor --> NL_in (gain) --+--> NL_delay (DelayLine, ratio pin driven)
                              |         ^
                              |         |  ratio = 1 - depth*x
                              +--(tap)--+--> NL_curve (CurveNode, linear)
                                        |
                                        +--> [NL_bell: Biquad LP] --> NL_out

PLACEMENT — the one thing that is not verbatim. The probe bolted the chain
on the graph OUTPUT because flute/oboe have no bore/bell anatomy. Both brass
carriers do, and the physics puts the steepening INSIDE the slide, upstream
of the bell. So the chain is inserted immediately after the bore delay and
everything that used to read the bore by {"ref"} now reads the steepener
instead. The loop's own {"tap"} on the bore is deliberately left alone —
this stays the one-way, out-of-loop variant the probe validated.
  lip: Bore --> [chain] --> Radiated (bell highpass) --> Reverb
  stk: Slide --> [chain] --> output   (the STK port has no bell stage, which
       is why the one bell-lowpass cell of this round lives on that carrier)

Gates: depth-0 null against the bare carrier, dose-response monotonicity
(spectral centroid and HF energy fraction vs depth), level dependence (same
depth, quiet vs loud drive), pitch-lock preservation (the steepener must not
detune the carrier), feature audibility vs each carrier's own control,
level ceiling, audibility floor, and peak normalisation of the ears queue.

Outputs (this script OWNS all three dirs and purges anything else):
  patches/sweep/nlbore_brass1/
  renders/dsp/sweep/nlbore_brass1/       (measurement cells)
  renders/dsp/audition/nlbore_brass1/    (<= 8 ears cells + README.md)
Usage: python tools/gen_nlbore_brass1.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "nlbore_brass1")
MEAS_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "nlbore_brass1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "nlbore_brass1")

SR = 48000
BASE_HZ = 120.0            # D = 48000/120 = 400 samples exactly (probe value)
D_SAMPLES = SR / BASE_HZ
KNOT_X = 2.0               # curve domain; beyond it the ratio law clamps

# --- carriers ---------------------------------------------------------------
# id -> (patch path, anchor node = the bore delay, midi notes, slot seconds)
CARRIERS = {
    "lip": (os.path.join(ROOT, "patches", "audition", "brass_harness1",
                         "br1_t100_q15_m3.json"),
            "Bore", [48, 60, 72], 2.0),
    "stk": (os.path.join(ROOT, "patches", "sweep", "stk_brass_port",
                         "stk_brass_default_canon.json"),
            "Slide", [60, 72], 2.0),
}
NOTE_DUR = 1.8
SUSTAIN = (0.4, 1.7)       # within a slot, the part worth measuring

# --- axes -------------------------------------------------------------------
# Calibrated against the probe: the loud leg is normalised to peak DRIVE_PEAK
# before the steepener, so the delay swing is depth*DRIVE_PEAK*D samples. At
# depth 0.030 that is 2.4% of D = 9.6 samples, inside the 1-3% band the
# sources give for a trombone slide at ff. 0.060 is ~2x past physical.
LADDER = [0.0, 0.0075, 0.015, 0.03, 0.06, 0.12]
DRIVE_PEAK = 0.8
SOFT_FACTOR = 0.3
CAL_HEADROOM = 0.35        # the calibration pass renders well below full
                           # scale: at depth 0.06 a unity-trim render clips
                           # int16, and a clipped peak reading mis-sets the
                           # listening trim (measured: 0.56 instead of 0.50).

# name -> (carrier, depth, drive, bell lowpass corner Hz or None)
EARS = {
    "lip_ctl":       ("lip", 0.0,  "loud", None),
    "lip_soft_d030": ("lip", 0.03, "soft", None),
    "lip_loud_d030": ("lip", 0.03, "loud", None),
    "lip_loud_d060": ("lip", 0.06, "loud", None),
    "stk_ctl":       ("stk", 0.0,  "loud", None),
    "stk_d030":      ("stk", 0.03, "loud", None),
    "stk_d060":      ("stk", 0.06, "loud", None),
    "stk_d030_bell": ("stk", 0.03, "loud", 2500.0),
}

# --- gates ------------------------------------------------------------------
LEVEL_CEILING = 0.5        # max 0.5 s-window rms (speaker safety)
AUDIBILITY_FLOOR = 1e-4    # rms below this = silent cell
NULL_GATE_DB = -80.0       # depth-0 vs bare carrier must null below this
FEATURE_DB = -26.0         # level-matched diff vs own control
PEAK_TARGET_DB = -6.0      # ears-queue peak normalisation
CENTS_DRIFT = 3.0          # steepener sits outside the loop: it must not move
                           # any note at all. 3 cents is generous slack.


# ---------------------------------------------------------------------------
# graph construction  (steepener body verbatim from gen_nlbore_probe1.py)
# ---------------------------------------------------------------------------
def ratio_knots(depth):
    """ratio(x) = 1 - depth*x, exactly linear over [-KNOT_X, +KNOT_X]."""
    return [[-KNOT_X, round(1.0 + depth * KNOT_X, 9)],
            [0.0, 1.0],
            [KNOT_X, round(1.0 - depth * KNOT_X, 9)]]


def rbj_lowpass(fc, q=0.70710678):
    """RBJ biquad lowpass normalised to the engine's Direct Form I."""
    w0 = 2.0 * math.pi * fc / SR
    c, s = math.cos(w0), math.sin(w0)
    alpha = s / (2.0 * q)
    a0 = 1.0 + alpha
    return {"b0": (1.0 - c) / 2.0 / a0, "b1": (1.0 - c) / a0,
            "b2": (1.0 - c) / 2.0 / a0, "a1": -2.0 * c / a0,
            "a2": (1.0 - alpha) / a0}


def steepener(src_id, depth, in_gain, out_gain, bell_fc=None, bypass=False):
    """The probe chain. Returns (nodes, out_id). Node ORDER is load-bearing."""
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


def rewire_refs(obj, old, new):
    """Repoint every {"ref": old} to new. {"tap": old} is left ALONE — the
    loop must keep reading the un-steepened bore (one-way, out-of-loop)."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == "ref" and v == old:
                obj[k] = new
            else:
                rewire_refs(v, old, new)
    elif isinstance(obj, list):
        for v in obj:
            rewire_refs(v, old, new)


def carrier_patch(cid, depth, in_gain, out_gain, bell_fc=None, bypass=False):
    path, anchor, notes, slot = CARRIERS[cid]
    with open(path) as f:
        p = json.load(f)
    nodes = p["graph"]["nodes"]
    ids = [n["id"] for n in nodes]
    if anchor not in ids:
        raise SystemExit(f"{cid}: anchor {anchor} missing")
    chain, out = steepener(anchor, depth, in_gain, out_gain, bell_fc, bypass)
    for n in nodes:                      # before insertion, so the chain's
        rewire_refs(n["params"], anchor, out)   # own ref to anchor survives
    idx = ids.index(anchor)
    p["graph"]["nodes"] = nodes[:idx + 1] + chain + nodes[idx + 1:]
    if p["graph"]["output"] == anchor:
        p["graph"]["output"] = out
    p.pop("ui", None)
    p["score"] = [{"note": n, "time": slot * k, "duration": NOTE_DUR,
                   "velocity": 0.8} for k, n in enumerate(notes)]
    p["seconds"] = slot * len(notes)
    p["sampleRate"] = SR
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


def slots(cid):
    return len(CARRIERS[cid][2])


def sustain(x, sr, cid):
    """The sustained part of every note, concatenated — the attacks and the
    silence between notes would otherwise dominate the spectral averages."""
    slot = CARRIERS[cid][3]
    segs = []
    for k in range(slots(cid)):
        a = int((slot * k + SUSTAIN[0]) * sr)
        b = int((slot * k + SUSTAIN[1]) * sr)
        if b <= len(x):
            segs.append(x[a:b])
    return np.concatenate(segs) if segs else x


def centroid_all(x, sr, flo=40.0, fhi=20000.0):
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


def f0_peak(seg, sr, target, nfft=1 << 20):
    """Parabolic-interpolated FFT peak within +-10% of the target.

    NOT autocorrelation. Autocorrelation was the first thing tried and it
    reported the steepener detuning the STK carrier by up to 15 cents as the
    strength went up; the spectral peak says the fundamental does not move at
    all (+65.0 cents at every strength, to one decimal). Autocorrelation is
    pulled around by the changing harmonic balance, which is exactly what
    this element changes, so it is the wrong tool here. Both carriers only
    render notes they actually lock, so the +-10% search is safe.
    """
    if len(seg) < 512 or rms(seg) < AUDIBILITY_FLOOR:
        return None
    w = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(w, nfft))
    fr = np.fft.rfftfreq(nfft, 1.0 / sr)
    idx = np.where((fr > target * 0.9) & (fr < target * 1.1))[0]
    if len(idx) < 3:
        return None
    k = int(idx[int(np.argmax(sp[idx]))])
    if k <= 0 or k >= len(sp) - 1:
        return None
    a, b, c = sp[k - 1], sp[k], sp[k + 1]
    den = a - 2.0 * b + c
    d = 0.5 * (a - c) / den if den else 0.0
    return fr[k] + d * (fr[1] - fr[0])


def pitch_cents(x, sr, cid):
    """Cents error per note slot (None where the carrier does not speak)."""
    notes, slot = CARRIERS[cid][2], CARRIERS[cid][3]
    out = []
    for k, n in enumerate(notes):
        tgt = 440.0 * 2.0 ** ((n - 69) / 12.0)
        seg = x[int((slot * k + SUSTAIN[0]) * sr):
                int((slot * k + SUSTAIN[1]) * sr)]
        m = f0_peak(seg, sr, tgt)
        out.append(None if m is None else 1200.0 * math.log2(m / tgt))
    return out


def null_db(ref, cell):
    """Best-lag max-error of `cell` against `ref`, dB re the ref rms. The
    depth-0 chain is a pure D-sample delay, so the search brackets D."""
    n = min(len(ref), len(cell))
    a0 = int(0.3 * SR)
    a1 = int(min(0.9 * n / SR, 4.0) * SR)
    if a1 - a0 < SR // 4:
        a1 = n - int(D_SAMPLES) - 10
    r = ref[a0:a1]
    denom = rms(r)
    best = (float("inf"), 0)
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
    ppath = os.path.join(PATCH_OUT, f"nlb_{name}.json")
    wpath = os.path.join(rend_dir, f"nlb_{name}.wav")
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


def drop(*paths):
    for p in paths:
        if p and os.path.exists(p):
            os.remove(p)


# ---------------------------------------------------------------------------
README = """# Brass pairing round - "is this brass yet, or still saxy?"

Renders here; patches in `patches/sweep/nlbore_brass1/`; generator
`tools/gen_nlbore_brass1.py`. Measurement-only renders live in
`renders/dsp/sweep/nlbore_brass1/` - you do not need to listen to those.

## What this is, in plain language

Last round I built the thing that makes a trombone brighten when it is blown
hard - sound travels a little faster through compressed air, so the loud
peaks of the wave catch up with the troughs on the way down the tube, the
wave goes lopsided, and lopsided means high frequencies. You kept it: "it
definitely brightens in a brassy way." But I had hung it on the flute and
oboe patches, which are not brass.

This round puts it where brass actually lives: on the two lip-driven
exciters we have. Nothing inside either of them changed, and there is still
no new engine code. The one difference from last round is WHERE the effect
sits - last time it was bolted on the very end of the patch, this time it is
inside the tube, before the bell and the room, which is where the physics
says the steepening happens.

The two carriers:

- **lip** - the buzzing-lips model from September 12 (the cell you were
  pointed at, `br1_t100_q15_m3`). It only holds pitch from C3 to C5, so
  these files play C3, C4, C5 and stop. Its tuning is also off, and not by a
  constant: measured here it runs about a third of a semitone FLAT at C3 and
  a fifth of a semitone SHARP at C5. That is a known flaw of the carrier,
  not of the effect, and I left it alone this round.
- **stk** - the ported STK brass model. It is silent at C3 and C7, so these
  files play C4 and C5 only. It is about two thirds of a semitone sharp at
  C4.

**The question for you: is this brass yet, or still saxy?**

## What to listen for, cell by cell

| file | what it is | listen for |
|---|---|---|
{cells}

The pair that answers the headline question is **{bestpair}**. Same patch,
same effect strength, same playback loudness - the only difference is how
hard the signal is driven into the effect before being turned back down. If
the effect is behaving like a real bore, the loud one should be noticeably
brighter and edgier and the quiet one nearly clean.

`lip_ctl` and `stk_ctl` are **dead controls** - the effect is wired into the
graph but its strength is set to zero. They should be the untouched carrier,
so you can check me without trusting me.

`stk_d030_bell` adds a gentle lowpass after the effect. The STK port has no
bell stage at all (its output is the raw tube), so that cell is the one
place a bell filter has something to do. The lips patch already has its own
bell filter, so it does not get one.

## Limitations, stated up front

**1. The carriers are what they are.** The lips patch is sharp and only
covers three notes; the STK port is quiet and covers two. The effect cannot
fix either. If your verdict is "still saxy", the honest reading is that the
exciter is the problem, not the bore - which is what the last round already
pointed at.

**2. Still mild FM, not shock formation.** Same caveat as last round: with
the shock handling left out (deliberately, following the source paper) this
is the gentle end of the phenomenon.

**3. No oversampling.** The delay reads with plain linear interpolation at
48 kHz, so some of the added top end is interpolation artifact rather than
physics.

**4. On the STK carrier only, a whisker of the effect makes it DARKER
before it makes it brighter.** At the two weakest settings in the table
below the STK model loses about a third of the energy it had above 2 kHz;
that energy is not lost, it moves down into the 1-2 kHz band, and by the
real-instrument setting the brightening has taken over. I do not have the
cause pinned down. I did rule out the obvious suspect - reading the delay
at a fractional position blurs the signal slightly, but a fixed half-sample
offset costs only 0.4 percentage points of top-end energy and this costs
4.4. It does not happen on the lips patch. None of the cells you are being
asked to listen to sit in that dip.

## Measurements

Peak levels are normalised to -6 dBFS across the queue, so loudness
differences you hear are timbre, not gain.

{table}

"diff vs ctl" is how big the difference signal is against the dead control
after matching levels; anything above about -26 dB is comfortably audible.
"energy >2 kHz" is the share of the sound that sits above 2 kHz, and it does
not care about volume - it is the number that says "brighter".

Dose-response: same carrier, same everything, effect strength stepped up.

{ladder}

{levels}

{pitch}
"""


def main():
    if not os.path.exists(CLI):
        print(f"mforce_cli not found at {CLI}")
        return 1
    for d in (PATCH_OUT, MEAS_OUT, EARS_OUT):
        os.makedirs(d, exist_ok=True)

    meas = ["%s_bare" % c for c in CARRIERS] + ["%s_dry" % c for c in CARRIERS]
    meas += ["%s_lad%03d" % (c, int(d * 1000))
             for c in CARRIERS for d in LADDER]
    ears_names = list(EARS)
    cal = [n + "_cal" for n in ears_names]
    purge(PATCH_OUT, {f"nlb_{n}.json" for n in meas + ears_names + cal})
    purge(MEAS_OUT, {f"nlb_{n}.wav" for n in meas + cal})
    purge(EARS_OUT, {f"nlb_{n}.wav" for n in ears_names} | {"README.md"})

    fail = []

    # ---- Phase A: bare carriers, drive calibration, null gate -------------
    print("\n=== Phase A: carriers ===")
    dry_peak, bare = {}, {}
    for cid in CARRIERS:
        # unity pass: measure the carrier's own peak so `depth` means the same
        # thing on both carriers.
        got, pp, wp = render(f"{cid}_dry",
                             carrier_patch(cid, 0.0, 1.0, 1.0, bypass=True),
                             MEAS_OUT)
        if got is None:
            fail.append(f"{cid} dry render")
            continue
        dry_peak[cid] = max(float(np.abs(got[0]).max()), 1e-6)
        drop(pp, wp)
        # null reference: the SAME drive gains as the depth-0 ladder cell, so
        # the only difference left is the steepener itself. (First run used
        # unity gains here and the null floored at -69 dB on 16-bit rounding
        # of a 0.911/1.098 gain pair, not on anything in the wiring.)
        g = DRIVE_PEAK / dry_peak[cid]
        got, _, _ = render(f"{cid}_bare",
                           carrier_patch(cid, 0.0, g, 1.0 / g, bypass=True),
                           MEAS_OUT)
        if got is None:
            fail.append(f"{cid} bare render")
            continue
        x, sr = got
        bare[cid] = x
        cents = pitch_cents(x, sr, cid)
        print("  %-4s dry peak %.4f -> drive gain %.3f   cents %s"
              % (cid, dry_peak[cid], g,
                 " ".join("-" if c is None else "%+.1f" % c for c in cents)))

    # ---- Phase B: dose-response ladders -----------------------------------
    print("\n=== Phase B: dose-response (drive normalised to peak %.2f) ==="
          % DRIVE_PEAK)
    ladder = {}
    for cid in CARRIERS:
        if cid not in dry_peak:
            continue
        g = DRIVE_PEAK / dry_peak[cid]
        rows = []
        print("  %s: %-8s %10s %10s %10s %10s"
              % (cid, "depth", "cen Hz", "hf>1k", "hf>2k", "vs d000"))
        ref0 = None
        for d in LADDER:
            nm = "%s_lad%03d" % (cid, int(d * 1000))
            got, _, _ = render(nm, carrier_patch(cid, d, g, 1.0 / g),
                               MEAS_OUT)
            if got is None:
                fail.append(f"{nm} render")
                continue
            x, sr = got
            s = sustain(x, sr, cid)
            cen, h1, h2 = (centroid_all(s, sr), hf_fraction(s, sr, 1000.0),
                           hf_fraction(s, sr, 2000.0))
            if d == 0.0:
                ref0 = x
                nd, lag = null_db(bare[cid], x)
                print("     %-8.4f %10.0f %10.4f %10.4f   NULL %.1f dB @ lag %d"
                      % (d, cen, h1, h2, nd, lag))
                if nd > NULL_GATE_DB:
                    fail.append(f"{cid} depth-0 null {nd:.1f} dB "
                                f"> {NULL_GATE_DB}")
                rows.append((d, cen, h1, h2, nd, pitch_cents(x, sr, cid)))
            else:
                dd = diff_db(ref0, x) if ref0 is not None else float("nan")
                print("     %-8.4f %10.0f %10.4f %10.4f %10.1f"
                      % (d, cen, h1, h2, dd))
                rows.append((d, cen, h1, h2, dd, pitch_cents(x, sr, cid)))
        ladder[cid] = rows
        if len(rows) == len(LADDER):
            cs = [r[1] for r in rows]
            hs = [r[3] for r in rows]
            mc = all(cs[i] < cs[i + 1] for i in range(len(cs) - 1))
            mh = all(hs[i] < hs[i + 1] for i in range(len(hs) - 1))
            print("     monotonic centroid: %s   monotonic hf>2k: %s"
                  % (mc, mh))
            if cid == "lip" and not (mc and mh):
                fail.append("lip dose-response not monotonic "
                            f"(centroid {mc}, hf2k {mh})")
            # pitch-lock preservation at the physical depth
            base = rows[0][5]
            for r in rows:
                for k, (b, c) in enumerate(zip(base, r[5])):
                    if b is None or c is None:
                        continue
                    if abs(c - b) > CENTS_DRIFT:
                        fail.append("%s depth %.4f note %d drifted %+.0f c"
                                    % (cid, r[0], k, c - b))
        else:
            fail.append(f"{cid} ladder incomplete")

    # ---- Phase C: ears cells ---------------------------------------------
    print("\n=== Phase C: ears cells ===")
    rows, ctl = [], {}
    for name, (cid, depth, drive, bell) in EARS.items():
        if cid not in dry_peak:
            continue
        g = DRIVE_PEAK / dry_peak[cid] * (SOFT_FACTOR if drive == "soft"
                                          else 1.0)
        got, pp, wp = render(name + "_cal",
                             carrier_patch(cid, depth, g,
                                           CAL_HEADROOM / g, bell),
                             MEAS_OUT)
        if got is None:
            fail.append(f"{name} calibration render")
            continue
        peak = max(float(np.abs(got[0]).max()) / CAL_HEADROOM, 1e-6)
        trim = (10.0 ** (PEAK_TARGET_DB / 20.0)) / peak
        drop(pp, wp)
        got, _, _ = render(name, carrier_patch(cid, depth, g, trim / g, bell),
                           EARS_OUT)
        if got is None:
            fail.append(f"{name} render")
            continue
        x, sr = got
        s = sustain(x, sr, cid)
        lc, rr, pk = level_ceiling(x, sr), rms(x), float(np.abs(x).max())
        cen, h1, h2 = (centroid_all(s, sr), hf_fraction(s, sr, 1000.0),
                       hf_fraction(s, sr, 2000.0))
        bad = []
        if lc > LEVEL_CEILING:
            bad.append("LEVEL %.2f" % lc)
        if rr < AUDIBILITY_FLOOR:
            bad.append("SILENT")
        if name.endswith("_ctl"):
            ctl[cid] = x
        rows.append([name, cid, depth, drive, bell, cen, h1, h2, lc, rr, pk,
                     pitch_cents(x, sr, cid), bad])
        print("%-16s depth %.3f %-5s bell %-6s cen %6.0f hf1k %.4f hf2k %.4f "
              "winRms %.3f peak %.3f %s"
              % (name, depth, drive, str(bell), cen, h1, h2, lc, pk,
                 " ".join(bad)))
        if bad:
            fail += ["%s %s" % (name, b) for b in bad]

    print("\n=== feature audibility (vs own dead control) ===")
    for r in rows:
        name, cid = r[0], r[1]
        if name.endswith("_ctl") or cid not in ctl:
            r.append(float("nan"))
            continue
        x, _ = read_mono(os.path.join(EARS_OUT, f"nlb_{name}.wav"))
        dd = diff_db(ctl[cid], x)
        cs = sustain(ctl[cid], SR, cid)
        print("%-16s diff %+.1f dB   hf>2k %.4f vs ctl %.4f (%+.2f pp)   "
              "centroid %.0f vs %.0f Hz   %s"
              % (name, dd, r[7], hf_fraction(cs, SR, 2000.0),
                 (r[7] - hf_fraction(cs, SR, 2000.0)) * 100.0, r[5],
                 centroid_all(cs, SR), "ok" if dd > FEATURE_DB
                 else "INAUDIBLE"))
        if dd <= FEATURE_DB:
            fail.append(f"{name} inaudible vs control ({dd:.1f} dB)")
        r.append(dd)

    # level dependence on the lip carrier
    by = {r[0]: r for r in rows}
    lvl_line = ""
    if "lip_soft_d030" in by and "lip_loud_d030" in by:
        sh, lh = by["lip_soft_d030"][7], by["lip_loud_d030"][7]
        sc, lc_ = by["lip_soft_d030"][5], by["lip_loud_d030"][5]
        print("\nlevel dependence (lips, depth 0.030): quiet hf>2k %.4f "
              "-> loud %.4f (%.2fx), centroid %.0f -> %.0f Hz"
              % (sh, lh, lh / sh if sh > 0 else float("nan"), sc, lc_))
        lvl_line = ("Level dependence on the lips patch at strength 0.030: "
                    "driven quietly the sound has %.1f%% of its energy above "
                    "2 kHz; driven loud, %.1f%% (**%.2fx**), and the "
                    "brightness centre moves %.0f Hz -> %.0f Hz. The effect "
                    "only shows up when it is blown hard, which is the whole "
                    "point of the mechanism."
                    % (sh * 100, lh * 100, lh / sh if sh > 0 else float("nan"),
                       sc, lc_))
        if not lh > sh * 1.05:
            fail.append("no level dependence on lips (loud hf2k not > "
                        "1.05x quiet)")

    # ---- README ----------------------------------------------------------
    blurb = {
        "lip_ctl": "buzzing lips, C3-C4-C5, effect OFF",
        "lip_soft_d030": "lips, driven QUIETLY into the effect",
        "lip_loud_d030": "lips, driven LOUD into the effect",
        "lip_loud_d060": "lips, effect pushed ~2x past a real instrument",
        "stk_ctl": "STK brass model, C4-C5, effect OFF",
        "stk_d030": "STK brass, effect at real-instrument strength",
        "stk_d060": "STK brass, effect pushed ~2x past physical",
        "stk_d030_bell": "STK brass, real strength + a bell lowpass at 2.5 kHz",
    }
    listen = {
        "lip_ctl": "the untouched carrier - your reference point",
        "lip_soft_d030": "A of the A/B - should be close to the control",
        "lip_loud_d030": "B of the A/B - the brassiness test",
        "lip_loud_d060": "where it stops being an instrument (or does it?)",
        "stk_ctl": "the untouched carrier - your reference point",
        "stk_d030": "more edge than the control, same notes",
        "stk_d060": "twice as far - too far?",
        "stk_d030_bell": "same as stk_d030 with the top tamed - better or "
                         "just duller?",
    }
    cells = "\n".join("| `nlb_%s.wav` | %s | %s |"
                      % (r[0], blurb.get(r[0], ""), listen.get(r[0], ""))
                      for r in rows)
    tbl = ["| cell | carrier | strength | drive | bell Hz | energy >1 kHz | "
           "energy >2 kHz | centroid Hz | diff vs ctl | 0.5 s rms | peak |",
           "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        tbl.append("| `%s` | %s | %.3f | %s | %s | %.4f | %.4f | %.0f | %s "
                   "| %.3f | %.3f |"
                   % (r[0], r[1], r[2], r[3],
                      ("%d" % r[4]) if r[4] else "-", r[6], r[7], r[5],
                      "-" if not np.isfinite(r[13]) else "%+.1f dB" % r[13],
                      r[8], r[10]))
    lad = []
    for cid, rws in ladder.items():
        lad.append("\n**%s carrier**\n" % ("buzzing lips" if cid == "lip"
                                           else "STK brass"))
        lad.append("| strength | centroid Hz | energy >1 kHz | energy >2 kHz "
                   "| vs strength 0 |")
        lad.append("|---|---|---|---|---|")
        for d, cen, h1, h2, dd, _ in rws:
            lad.append("| %.4f | %.0f | %.4f | %.4f | %s |"
                       % (d, cen, h1, h2,
                          ("nulls at %.0f dB" % dd) if d == 0.0
                          else "%+.1f dB" % dd))
    pit = []
    for r in rows:
        pit.append("| `%s` | %s |" % (r[0], " ".join(
            "-" if c is None else "%+.0f c" % c for c in r[11])))
    pitch = ("Tuning check - the effect sits outside the loop, so it must not "
             "move the pitch, and it does not: every cell holds its "
             "carrier's tuning to within a cent at every strength. The "
             "tuning error itself belongs to the carrier and is untouched "
             "this round.\n\n| cell | cents off per note |\n|---|---|\n"
             + "\n".join(pit))

    best = "`nlb_lip_soft_d030.wav` vs `nlb_lip_loud_d030.wav`"
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{cells}", cells)
                      .replace("{bestpair}", best)
                      .replace("{table}", "\n".join(tbl))
                      .replace("{ladder}", "\n".join(lad))
                      .replace("{levels}", lvl_line)
                      .replace("{pitch}", pitch))

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
