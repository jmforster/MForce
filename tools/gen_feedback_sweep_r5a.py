"""Feedback sweep r5a — cross-cutting KEYTRACK round (backlog 37, post-r4).

Matt's r4 verdict (2026-09-02, docs/research/feedback_sweeps/R4_VERDICTS.md):
concept proven, keepers saved, but noise level and attack speed are only
right in each patch's home register (reed003 "noise needs dialing back at
higher registers", brass001 swell vanishing up high). Cutoff is keytracked
(37a-a); noise amp and ramp time are not.

r5a therefore: take the verdicted keepers AS SAVED (patches/scratch/
loop_sweep_4/, Matt's files, read-only — transforms preserve every hand
tweak) and add two keytrack curves, 3x3 grid per patch:

  hiss.amplitude <- CurveNode power knot  amp(f) = amp0 * (f/f_home)^p
      p in {0, -0.4, -0.8}                (n00 / n40 / n80)
  driveenv.dynamicPins.timeScale <- power knot  ts(f) = (f/f_home)^q
      q in {0, -0.35, -0.7}               (r00 / r35 / r70)

Both normalize to the patch's saved value at its home note, so the n00_r00
cell is the control (same voice as Matt's save). timeScale is a per-note
setting delivered by push binding at note-on (pin_model_design.md §5);
the hiss curve is an ordinary per-sample param wire.

Render format per Matt 09-02: 5 notes, octaves C3..C7 (MIDI 48..96), 2 s
each — register behavior audible in one WAV, no second Audition pass.

Usage:  python tools/gen_feedback_sweep_r5a.py [noise_exps] [ramp_exps]
        (comma-separated overrides, e.g. "-0.9,-1.05,-1.2" "0" for the
        09-03 noise-only extension; defaults = the original 3x3)
Writes patches -> patches/sweep/feedback_curves5a/
       renders -> renders/dsp/pending/feedback_curves5a/  (the full 54 =
       the listening queue; no novelty filter, these are polish grids)
"""
import copy
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PICKS = os.path.join(ROOT, "patches", "scratch", "loop_sweep_4")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "feedback_curves5a")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "pending", "feedback_curves5a")

KEEPERS = ["flute002", "flute003", "reed001a", "reed003", "reed005",
           "brass001"]
NOISE_EXPS = [0.0, -0.4, -0.8]     # p: hiss amp ~ (f/f_home)^p
RAMP_EXPS = [0.0, -0.35, -0.7]     # q: envelope timeScale ~ (f/f_home)^q
NOTES = [48, 60, 72, 84, 96]       # C3..C7 octaves
NOTE_DUR = 2.0


def midi_to_freq(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def power_curve_node(node_id, a, b):
    """One-knot expressions CurveNode: value = a * freq^b everywhere
    (single knot => edge formula extrapolates, same idiom as the cutoff
    keytrack). x of the knot is cosmetic for a single knot."""
    return {
        "id": node_id,
        "type": "CurveNode",
        "params": {
            "mode": "expressions",
            "interp": "linear",
            "knots": [],
            "exprKnots": [{"x": 440.0, "form": "power", "a": a, "b": b}],
            "source": {"ref": "__perf_freq"},
        },
    }


def make_cell(base, p_noise, q_ramp):
    patch = copy.deepcopy(base)
    nodes = {n["id"]: n for n in patch["graph"]["nodes"]}
    f_home = midi_to_freq(patch["score"][0]["note"])

    # Noise-amp keytrack: amp(f) = amp0 * (f/f_home)^p, exact amp0 at home.
    amp0 = nodes["hiss"]["params"]["amplitude"]
    a = amp0 * f_home ** (-p_noise)
    nodes["hiss"]["params"]["amplitude"] = {"ref": "hisskt"}

    # Ramp-time keytrack: timeScale(f) = (f/f_home)^q, 1.0 at home. Scales
    # the whole driveenv (attack AND release; the percent-0 expand stage
    # absorbs the difference) — delivered once per note-on as a setting.
    nodes["driveenv"]["dynamicPins"] = {"timeScale": {"ref": "rampkt"}}

    # Loader is single-pass in array order (non-tap refs must point to
    # already-built nodes): insert both curves right after __perf_freq.
    arr = patch["graph"]["nodes"]
    at = next(i for i, n in enumerate(arr) if n["id"] == "__perf_freq") + 1
    arr[at:at] = [power_curve_node("hisskt", a, p_noise),
                  power_curve_node("rampkt", f_home ** (-q_ramp), q_ramp)]

    patch["score"] = [
        {"note": n, "time": i * NOTE_DUR, "duration": NOTE_DUR,
         "velocity": 0.8}
        for i, n in enumerate(NOTES)]
    patch["seconds"] = NOTE_DUR * len(NOTES)
    return patch


def main():
    global NOISE_EXPS, RAMP_EXPS
    if len(sys.argv) > 1:
        NOISE_EXPS = [float(x) for x in sys.argv[1].split(",")]
    if len(sys.argv) > 2:
        RAMP_EXPS = [float(x) for x in sys.argv[2].split(",")]
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    mpath = os.path.join(REND_OUT, "manifest.json")
    manifest = (json.load(open(mpath)) if os.path.exists(mpath) else
                {"round": "r5a keytrack (noise amp x ramp time)",
                 "notes": NOTES, "note_dur": NOTE_DUR, "variants": []})
    manifest.setdefault("noise_exps", [])
    manifest.setdefault("ramp_exps", [])
    manifest["noise_exps"] = sorted(set(manifest["noise_exps"] + NOISE_EXPS),
                                    reverse=True)
    manifest["ramp_exps"] = sorted(set(manifest["ramp_exps"] + RAMP_EXPS),
                                   reverse=True)
    failures = 0
    for name in KEEPERS:
        base = json.load(open(os.path.join(PICKS, name + ".json")))
        for p in NOISE_EXPS:
            for q in RAMP_EXPS:
                cell = (f"fb5a_{name}_n{int(-p*100):02d}_r{int(-q*100):02d}")
                patch = make_cell(base, p, q)
                pj = os.path.join(PATCH_OUT, cell + ".json")
                pw = os.path.join(REND_OUT, cell + ".wav")
                json.dump(patch, open(pj, "w"), indent=1)
                r = subprocess.run([CLI, pj, pw], capture_output=True,
                                   timeout=300)
                ok = r.returncode == 0 and os.path.exists(pw)
                if not ok:
                    failures += 1
                    print(f"FAIL {cell}: {r.stderr.decode(errors='replace')[:200]}",
                          flush=True)
                manifest["variants"].append(
                    {"id": cell, "patch": cell + ".json",
                     "file": cell + ".wav", "ok": ok,
                     "params": {"base": name, "noise_exp": p, "ramp_exp": q}})
        print(f"{name}: 9 cells done", flush=True)
    json.dump(manifest, open(os.path.join(REND_OUT, "manifest.json"), "w"),
              indent=1)
    n = len(manifest["variants"])
    print(f"{n - failures}/{n} rendered -> {REND_OUT}")
    if failures:
        sys.exit(1)


main()
