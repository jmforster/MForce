"""Bow round 3 - the three suspects behind "syn-drum attached to an oboe"
(Matt's round-2 verdict on the on-pitch cells; suspects logged in
renders/dsp/audition/hysteresis_bow2/README.md, greenlit 2026-09-11).

Base config = round 2's on-pitch winner, FIXED: bias 0.9 x breakaway
(0.54), ba 0.6, cap 0, damp keytrack mult 3.0, delay ratio 0.5 (the
period-2 stick/slip cycle lands on the note; peak/f0 0.994, ~2
corners/period). Drive pinned 1.0, loop gain on Ampl_env maxValue -
all round-2 lessons inherited. One loop config => ONE critical
bisection for the whole grid.

Axes = the suspects:
  E (bow engagement, Bow_bias attack stage as fraction of the note):
    03 = ~60 ms of a 2 s note (round 2's step - the suspected click),
    15, 35 = progressively real stroke lengths; the bias envelope IS
    the bow stroke, so this axis is pure envelope shaping.
  G (loop gain over the measured critical): 102 (round 2's slow bloom),
    115, 140. NOTE: round 1's aggro lived at high gain WITHOUT bias;
    with one-sided biased operation this is unexplored - that's the
    point of sweeping it.
  B (breath amp multiplier): 100, 025, 005 - friction noise should
    come from slip events, not additive hiss (breath-integration
    lesson); 005 is nearly bare mechanism.

Controls: x_r2best (round 2's v090_dm3_r05_lo config = E03/G102/B100 -
also doubles as the grid's corner cell rendered under this tool for
byte-comparable bookkeeping) and x_control (stock oboe1). Round 2's
queue keeps the nobias/memoryless comparisons.

Outputs: patches/sweep/hysteresis_bow3/, playable copies ->
patches/audition/hysteresis_bow3/; renders -> renders/dsp/{sweep,
audition}/hysteresis_bow3/ (audition normalized to 0.7). README copied
to the patch queue if present.

Usage:  python tools/gen_hysteresis_bow3.py
"""
import copy
import json
import os
import shutil
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
BASE = os.path.join(ROOT, "patches", "library", "winds", "oboe1.json")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "hysteresis_bow3")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "hysteresis_bow3")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "hysteresis_bow3")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "hysteresis_bow3")

BA = 0.6
BIAS = 0.54            # 0.9 x BA, round-2 winner
DAMP_MULT = 3.0
RATIO = 0.5
ENGAGE = {"e03": 0.03, "e15": 0.15, "e35": 0.35}
GAIN = {"g102": 1.02, "g115": 1.15, "g140": 1.40}
BREATH = {"b100": 1.0, "b025": 0.25, "b005": 0.05}
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.05
BISECT_LO, BISECT_HI = 0.05, 2.0   # Ampl_env maxValue range
PROBE_SEED = 0.001
NORM_PEAK = 0.7
C4 = 261.626

STICK = [-1.0, -1.0, -0.6, -1.0, -0.15, -0.3, 0.0, 0.0,
         0.15, 0.3, 0.6, 1.0, 1.0, 1.0]          # slope 2 through origin
SLIP = [-1.0, -0.25, -0.6, -0.35, -0.15, -1.0, 0.0, 0.0,
        0.15, 1.0, 0.6, 0.35, 1.0, 0.25]         # friction falling flank

NEW_POS = {"Bow_bias": [-1560.0, -560.0], "Bow_sum": [-1400.0, -560.0]}


def nodes_by_id(patch):
    return {n["id"]: n for n in patch["graph"]["nodes"]}


def make_patch(base, engage_pct, breath_mult):
    """engage_pct None = stock oboe1 control."""
    patch = copy.deepcopy(base)
    nodes_by_id(patch)["Pitch_vibrato"]["params"]["depth"] = 0.003
    if engage_pct is None:
        return patch
    nodes = nodes_by_id(patch)
    j = nodes["Junction"]["params"]
    j["drive"] = 1.0
    j["values"] = list(STICK)
    j["values2"] = list(SLIP)
    j["hysteresis"] = True
    j["breakaway"] = BA
    j["capture"] = 0.0
    j["morph"] = 0.0
    nodes["Damp_cutoff_curve"]["params"]["exprKnots"] = [
        {"a": DAMP_MULT, "b": 0.0, "form": "linear", "x": 440.0}]
    nodes["Delay_line"]["params"]["ratio"] = RATIO
    if breath_mult != 1.0:
        nodes["Breath_curve"]["params"]["exprKnots"][0]["a"] = round(
            nodes["Breath_curve"]["params"]["exprKnots"][0]["a"]
            * breath_mult, 6)
    arr = patch["graph"]["nodes"]
    new = [
        {"id": "Bow_bias", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": BIAS,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Expo", "startVal": 0.0, "endVal": 1.0,
                 "percent": engage_pct, "power": 2.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": 0.0, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
                 "percent": 0.1, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0}]}},
        {"id": "Bow_sum", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Drive_inputs"},
            "source2": {"ref": "Bow_bias"}}},
    ]
    at = next(i for i, n in enumerate(arr) if n["id"] == "Junction")
    arr[at:at] = new
    nodes_by_id(patch)["Junction"]["params"]["source"] = {"ref": "Bow_sum"}
    patch["ui"]["positions"].update(NEW_POS)
    for grp in patch.get("groups", []):
        if grp["name"] == "Drive":
            grp["members"] += ["Bow_bias", "Bow_sum"]
    return patch


def set_loopgain(patch, g):
    nodes_by_id(patch)["Ampl_env"]["params"]["maxValue"] = round(g, 4)


# ------------------------------------------------------------ measurement ---
def window_rms(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    a, b = int(RMS_WIN[0] * sr) * ch, int(RMS_WIN[1] * sr) * ch
    seg = raw[a:b]
    if not seg:
        return 0.0
    return (sum(s * s for s in seg) / len(seg)) ** 0.5 / 32767.0


def corners_per_period(path, t0=2.6, t1=3.8, f0=C4):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    mono = [sum(raw[i * ch:(i + 1) * ch]) / ch
            for i in range(int(t0 * sr), int(t1 * sr))]
    if len(mono) < 8:
        return None
    d = [mono[i + 1] - mono[i] for i in range(len(mono) - 1)]
    corners = sum(1 for i in range(len(d) - 1)
                  if d[i] != 0 and d[i + 1] != 0
                  and (d[i] > 0) != (d[i + 1] > 0))
    return round(corners / ((t1 - t0) * f0), 2)


def oscillates(base, gain, scratch):
    """Shipped topology and root; E/B don't move the threshold (probe
    breath is a seed scalar anyway), so one bisection serves the grid."""
    probe = make_patch(base, ENGAGE["e03"], 1.0)
    nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
    set_loopgain(probe, gain)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_hyb3.json")
    pw = os.path.join(scratch, "probe_hyb3.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, hi, scratch):
        return None
    if oscillates(base, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, mid, scratch):
            hi = mid
        else:
            lo = mid
    return round(hi, 4)


def normalize_copy(src, dst):
    w = wave.open(src)
    params, n, ch = w.getparams(), w.getnframes(), w.getnchannels()
    raw = list(struct.unpack("<%dh" % (n * ch), w.readframes(n)))
    w.close()
    peak = max(1, max(abs(s) for s in raw))
    g = NORM_PEAK * 32767.0 / peak
    out = wave.open(dst, "wb")
    out.setparams(params)
    out.writeframes(struct.pack(
        "<%dh" % len(raw),
        *[max(-32768, min(32767, int(s * g))) for s in raw]))
    out.close()
    return round(g, 3)


# ------------------------------------------------------------------- main ---
def main():
    for d in (PATCH_OUT, PATCH_AUD, SWEEP_OUT, AUD_OUT):
        os.makedirs(d, exist_ok=True)
    scratch = os.path.join(ROOT, "renders", "scratch")
    os.makedirs(scratch, exist_ok=True)
    for d in (SWEEP_OUT, AUD_OUT):
        for f in os.listdir(d):
            if f.endswith(".wav"):
                os.remove(os.path.join(d, f))
    for f in os.listdir(PATCH_AUD):
        if f.endswith(".json"):
            os.remove(os.path.join(PATCH_AUD, f))
    base = json.load(open(BASE))

    crit = measure_critical(base, scratch)
    if crit is None:
        sys.exit("bow3 base config never oscillated in probe - aborting")
    print(f"critical (one config, whole grid): {crit}", flush=True)

    manifest = {"round": "hysteresis_bow3 suspects: engage x gain x breath",
                "base": {"bias": BIAS, "ba": BA, "damp_mult": DAMP_MULT,
                         "ratio": RATIO}, "critical": crit,
                "engage": ENGAGE, "gain": GAIN, "breath": BREATH,
                "norm_peak": NORM_PEAK, "variants": []}
    failures = 0

    def render(patch, cell, audition_patch=True):
        pj = os.path.join(PATCH_OUT, cell + ".json")
        pw = os.path.join(SWEEP_OUT, cell + ".wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"FAIL {cell}:"
                  f" {r.stderr.decode(errors='replace')[:160]}", flush=True)
            return None, None
        if audition_patch:
            shutil.copyfile(pj, os.path.join(PATCH_AUD, cell + ".json"))
        return (normalize_copy(pw, os.path.join(AUD_OUT, cell + ".wav")),
                corners_per_period(pw))

    for etag, ep in ENGAGE.items():
        for gtag, goff in GAIN.items():
            for btag, bm in BREATH.items():
                cell = f"hyb3_{etag}_{gtag}_{btag}"
                patch = make_patch(base, ep, bm)
                set_loopgain(patch, goff * crit)
                gain, cpp = render(patch, cell)
                if gain is None:
                    failures += 1
                manifest["variants"].append(
                    {"id": cell, "patch": cell + ".json",
                     "file": cell + ".wav", "ok": gain is not None,
                     "params": {"engage_pct": ep, "gain_off": goff,
                                "breath": bm, "loop_gain":
                                round(goff * crit, 4),
                                "norm_gain": gain,
                                "corners_per_period": cpp}})

    gain, cpp = render(make_patch(base, None, None), "x_control",
                      audition_patch=False)
    if gain is None:
        failures += 1
    manifest["control"] = {"norm_gain": gain, "corners_per_period": cpp}

    json.dump(manifest, open(os.path.join(SWEEP_OUT, "manifest.json"), "w"),
              indent=1)
    readme = os.path.join(AUD_OUT, "README.md")
    if os.path.exists(readme):
        shutil.copyfile(readme, os.path.join(PATCH_AUD, "README.md"))
    n = sum(1 for v in manifest["variants"] if v.get("ok"))
    print(f"{n} cells rendered ok, {failures} failures -> {SWEEP_OUT}")
    print(f"render queue -> {AUD_OUT}")
    print(f"patch queue  -> {PATCH_AUD}")
    if failures:
        sys.exit(1)


main()
