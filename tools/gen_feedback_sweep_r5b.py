"""Feedback sweep r5b — reed001a single-patch multi-axis deep-dive
(backlog 37, post-r5a; Matt 09-03: "Go for r5b").

Base = Matt's reed001a save (patches/scratch/loop_sweep_4/, read-only)
plus the r5a-verdicted noise keytrack baked in at exponent -0.85 (mid
sweet spot; flat ramp — ramp curve verdicted "maybe unnecessary"). Matt
hand-tunes the final exponent per patch.

Three axes:
  M (cutoff multiple, keytrack a): 1.5, 2, 3*, 4.5, 6, 9      (* = saved)
  SHAPE (drive path through critical, total ~0.4 s at 2 s notes):
    lin  = saved linear ramp (control)
    blo  = Expo p2.5 slow bloom (lingers near critical, late arrival)
    ovs  = overshoot-then-settle: 60 ms to crit*1.60, 240 ms settle to
           sustain — the tonguing/pressure-spike articulation
    tng  = sharp tongue: 15 ms spike to crit*1.60, 60 ms settle
  J (junction jitter): seed 0 = saved curve verbatim; seeds 1-5 jitter
    every Shaper value multiplicatively +/-15% (exploit around the
    winner, not new curve families)

Critical drive is RE-MEASURED by bisection for every (M, J) combo — the
stale-calibration lesson from reed001a's per-note overblow spread. All
drives are then critical-relative: ramp starts at 0.95*crit, sustain =
crit*1.30 (the operating point of Matt's save, which was an o30 cell).

Renders: full 6x4x6 = 144 grid -> renders/dsp/sweep/feedback_curves5b/;
the 14-cell AXIS TOUR (each axis swept with the others at control) is
COPIED to renders/dsp/pending/feedback_curves5b/ as the listening queue.
Patches -> patches/sweep/feedback_curves5b/.

Usage:  python tools/gen_feedback_sweep_r5b.py
"""
import copy
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
BASE = os.path.join(ROOT, "patches", "scratch", "loop_sweep_4",
                    "reed001a.json")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "feedback_curves5b")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "feedback_curves5b")
PEND_OUT = os.path.join(ROOT, "renders", "dsp", "pending", "feedback_curves5b")

MULTS = [1.5, 2.0, 3.0, 4.5, 6.0, 9.0]
SHAPES = ["lin", "blo", "ovs", "tng"]
JSEEDS = [0, 1, 2, 3, 4, 5]
CONTROL = (3.0, "lin", 0)          # Matt's saved settings
NOISE_EXP = -0.85
SUSTAIN_OFF = 0.30                 # sustain = crit * (1 + this)
OVER_OFF = 0.60                    # overshoot peak for ovs/tng
NOTES = [48, 60, 72, 84, 96]
NOTE_DUR = 2.0


def midi_to_freq(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def stage(sv, ev, typ="Linear", pct=0.0, power=0.0, max_sec=0.0):
    return {"startVal": sv, "endVal": ev, "type": typ, "percent": pct,
            "power": power, "holdPct": 0.0, "minSec": 0.0, "maxSec": max_sec}


def drive_stages(shape, crit):
    """Returns (minValue, maxValue, stages). Fractions of the 2 s note."""
    lo, sus, over = 0.95 * crit, crit * (1 + SUSTAIN_OFF), crit * (1 + OVER_OFF)
    if shape in ("lin", "blo"):
        typ = "Linear" if shape == "lin" else "Expo"
        pw = 0.0 if shape == "lin" else 2.5
        return lo, sus, [
            stage(0.0, 1.0, typ, 0.2, pw, max_sec=1.0),
            stage(1.0, 1.0),                              # expand hold
            stage(1.0, 0.0, "Sine", 0.2)]
    s_norm = (sus - lo) / (over - lo)
    p1, p2 = (0.03, 0.12) if shape == "ovs" else (0.0075, 0.03)
    return lo, over, [
        stage(0.0, 1.0, "Linear", p1, max_sec=0.5),
        stage(1.0, s_norm, "Linear", p2, max_sec=0.5),
        stage(s_norm, s_norm),                            # expand hold
        stage(s_norm, 0.0, "Sine", 0.2)]


def jitter_values(values, seed):
    if seed == 0:
        return list(values)
    rng = random.Random(883100 + seed)
    return [max(-1.0, min(1.0, v * (1.0 + rng.uniform(-0.15, 0.15))))
            for v in values]


def make_patch(base, mult, shape, jseed, crit, score):
    patch = copy.deepcopy(base)
    nodes = {n["id"]: n for n in patch["graph"]["nodes"]}
    nodes["keytrack"]["params"]["exprKnots"][0]["a"] = mult
    nodes["junction"]["params"]["values"] = jitter_values(
        nodes["junction"]["params"]["values"], jseed)
    mn, mx, stages = drive_stages(shape, crit)
    nodes["driveenv"]["params"]["minValue"] = round(mn, 4)
    nodes["driveenv"]["params"]["maxValue"] = round(mx, 4)
    nodes["driveenv"]["params"]["stages"] = stages
    # r5a-verdicted noise keytrack, exponent -0.85 normalized at home note.
    f_home = midi_to_freq(base["score"][0]["note"])
    amp0 = nodes["hiss"]["params"]["amplitude"]
    arr = patch["graph"]["nodes"]
    at = next(i for i, n in enumerate(arr) if n["id"] == "__perf_freq") + 1
    arr.insert(at, {
        "id": "hisskt", "type": "CurveNode",
        "params": {"mode": "expressions", "interp": "linear", "knots": [],
                   "exprKnots": [{"x": 440.0, "form": "power",
                                  "a": amp0 * f_home ** (-NOISE_EXP),
                                  "b": NOISE_EXP}],
                   "source": {"ref": "__perf_freq"}}})
    nodes["hiss"]["params"]["amplitude"] = {"ref": "hisskt"}
    patch["score"] = score
    patch["seconds"] = sum(e["duration"] for e in score) if score else 10.0
    if score and len(score) > 1:
        patch["seconds"] = score[-1]["time"] + score[-1]["duration"]
    return patch


# ------------------------------------------------------------ measurement ---
def tail_rms(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    tail = raw[-int(0.3 * 48000) * ch:]
    return (sum(s * s for s in tail) / len(tail)) ** 0.5 / 32767.0


def oscillates(base, mult, jseed, drive, scratch):
    """Flat-drive probe at the home note (min == max == drive)."""
    probe = make_patch(base, mult, "lin", jseed, drive,
                       [{"note": base["score"][0]["note"], "time": 0.0,
                         "duration": 0.9, "velocity": 0.8}])
    for n in probe["graph"]["nodes"]:
        if n["id"] == "driveenv":
            n["params"]["minValue"] = drive
            n["params"]["maxValue"] = drive
    pj, pw = os.path.join(scratch, "probe5b.json"), \
        os.path.join(scratch, "probe5b.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and tail_rms(pw) > 0.02


def measure_critical(base, mult, jseed, scratch):
    lo, hi = 0.05, 6.0
    if not oscillates(base, mult, jseed, hi, scratch):
        return None
    if oscillates(base, mult, jseed, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, mult, jseed, mid, scratch):
            hi = mid
        else:
            lo = mid
    return round(hi, 4)


# ------------------------------------------------------------------- main ---
def main():
    for d in (PATCH_OUT, SWEEP_OUT, PEND_OUT):
        os.makedirs(d, exist_ok=True)
    scratch = os.path.join(ROOT, "renders", "scratch")
    os.makedirs(scratch, exist_ok=True)
    base = json.load(open(BASE))
    score = [{"note": n, "time": i * NOTE_DUR, "duration": NOTE_DUR,
              "velocity": 0.8} for i, n in enumerate(NOTES)]

    crits = {}
    for mult in MULTS:
        for jseed in JSEEDS:
            c = measure_critical(base, mult, jseed, scratch)
            crits[(mult, jseed)] = c
            print(f"crit m={mult} j={jseed}: {c}", flush=True)

    manifest = {"round": "r5b reed001a deep-dive", "mults": MULTS,
                "shapes": SHAPES, "jseeds": JSEEDS, "control": CONTROL,
                "noise_exp": NOISE_EXP, "sustain_off": SUSTAIN_OFF,
                "over_off": OVER_OFF,
                "criticals": {f"m{m}_j{j}": c for (m, j), c in crits.items()},
                "variants": []}
    tour, failures = [], 0
    for mult in MULTS:
        for shape in SHAPES:
            for jseed in JSEEDS:
                crit = crits[(mult, jseed)]
                cell = (f"fb5b_m{str(mult).replace('.', 'p')}"
                        f"_{shape}_j{jseed}")
                if crit is None:
                    manifest["variants"].append(
                        {"id": cell, "ok": False, "skip": "no critical"})
                    continue
                patch = make_patch(base, mult, shape, jseed, crit, score)
                pj = os.path.join(PATCH_OUT, cell + ".json")
                pw = os.path.join(SWEEP_OUT, cell + ".wav")
                json.dump(patch, open(pj, "w"), indent=1)
                r = subprocess.run([CLI, pj, pw], capture_output=True,
                                   timeout=300)
                ok = r.returncode == 0 and os.path.exists(pw)
                if not ok:
                    failures += 1
                    print(f"FAIL {cell}:"
                          f" {r.stderr.decode(errors='replace')[:160]}",
                          flush=True)
                manifest["variants"].append(
                    {"id": cell, "patch": cell + ".json",
                     "file": cell + ".wav", "ok": ok,
                     "params": {"mult": mult, "shape": shape,
                                "jseed": jseed, "critical": crit}})
                # Axis tour: vary one axis, others at control.
                on_axis = sum([(mult, ) == CONTROL[:1],
                               (shape, ) == CONTROL[1:2],
                               (jseed, ) == CONTROL[2:]]) >= 2
                if ok and on_axis:
                    shutil.copy2(pw, os.path.join(PEND_OUT, cell + ".wav"))
                    tour.append(cell)
        print(f"m={mult} done", flush=True)

    json.dump(manifest, open(os.path.join(SWEEP_OUT, "manifest.json"), "w"),
              indent=1)
    n = sum(1 for v in manifest["variants"] if "params" in v)
    print(f"{n - failures}/{n} rendered -> {SWEEP_OUT}")
    print(f"axis tour {len(tour)} cells -> {PEND_OUT}")
    if failures:
        sys.exit(1)


main()
