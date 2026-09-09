"""Bow round 2 - the bow moves: velocity bias into the hysteresis junction.

Round 1's diagnosis (Matt + measurements, 2026-09-08): symmetric input
swings trip breakaway on BOTH polarities (double-slip buzz = "aggro"),
and below breakaway the state machine never engages, so the attack is
the stock oboe breath-bloom (identical criticals proved it). A real bow
drags ONE way at constant velocity - the friction curve is evaluated
around an offset - and the attack is slip-scatter from sample one, not
a bloom through a threshold.

Round 2a lesson (same day): the first wiring fed the bias UPSTREAM of
the Shaper, whose evaluation is curve(drive * x) - so the bias was
scaled by drive, and bisection at high drive slammed the operating
point into the flat clamps (slope 0 = no loop gain): no biased config
found a critical at any drive. Fix, still zero engine code: PIN
drive=1.0 (bias enters curve-x space unscaled; operating point = bias,
independent of everything) and bisect LOOP GAIN on Delay_line's
amplitude (Ampl_env maxValue - the documented read-side KS loss
factor). Bow velocity and string losses become orthogonal axes, as the
physics wants. Cost: Drive_env/Drive_vib are orphaned in these cells
(drive is a scalar 1.0); timbre-vibrato returns later if the round
shows life.

Round 2 adds the bow with existing primitives:

  Bow_bias (Envelope: 0 -> BIAS over ~60 ms, hold, release to 0 - the
            bias source is an Envelope precisely so it can become a
            literal bow STROKE later)
  Bow_sum  (CombinedSource Sum: Drive_inputs + Bow_bias)
  Junction.source <- Bow_sum        (junction operating point = bias)

The bias is NOT circulating (dcblock drains the junction's DC output;
the bias re-enters fresh each pass) - external bow velocity, correctly.
Stick/slip curves, ba=0.6, cap=0 as round 1's center cell.

Axes:
  BIAS_RATIO {0.6, 0.9, 1.2} x breakaway - 1.2 starts the note already
    slipping (bow moving before the string engages).
  GAIN_OFF {lo 1.02, hi 1.1} x critical LOOP GAIN (Ampl_env maxValue;
    drive pinned at 1.0 - see round 2a lesson above; round 1 sat far
    above threshold = double-slip territory).
  BREATH {bs 1.0, bq 0.25} x stock breath amp - bow noise should come
    from friction events, not added hiss (breath-integration lesson).

Controls: x_nobias (bias 0, drive lo, stock breath - round-1 config at
gentle drive: isolates what the bias adds) and x_control (stock oboe1).
Round 1's queue keeps the memoryless kill-test. Pitch_vibrato 0.003
round-wide.

Criticals bisected per BIAS_RATIO at the SHIPPED topology and root
(probe overrides breath to a 0.001 scalar, so the breath axis needs no
extra bisections). corners_per_period reported per cell (spec section 6.5).

Outputs: patches/sweep/hysteresis_bow2/, playable copies ->
patches/audition/hysteresis_bow2/; renders -> renders/dsp/{sweep,
audition}/hysteresis_bow2/ (audition normalized to 0.7). README copied
from renders audition to the patch queue if present.

Usage:  python tools/gen_hysteresis_bow2.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "hysteresis_bow2")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "hysteresis_bow2")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "hysteresis_bow2")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "hysteresis_bow2")

BA = 0.6
CAP = 0.0
BIAS_RATIO = [0.6, 0.9, 1.2]
GAIN_OFF = {"lo": 1.02, "hi": 1.1}
BREATH = {"bs": 1.0, "bq": 0.25}
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


def make_patch(base, bias_ratio, breath_mult=1.0):
    """bias_ratio None = stock control; 0.0 = hysteresis without bias."""
    patch = copy.deepcopy(base)
    nodes_by_id(patch)["Pitch_vibrato"]["params"]["depth"] = 0.003
    if bias_ratio is None:
        return patch
    nodes = nodes_by_id(patch)
    j = nodes["Junction"]["params"]
    j["drive"] = 1.0   # bias must not be drive-scaled (round 2a lesson)
    j["values"] = list(STICK)
    j["values2"] = list(SLIP)
    j["hysteresis"] = True
    j["breakaway"] = BA
    j["capture"] = CAP
    j["morph"] = 0.0
    if breath_mult != 1.0:
        nodes["Breath_curve"]["params"]["exprKnots"][0]["a"] = round(
            nodes["Breath_curve"]["params"]["exprKnots"][0]["a"]
            * breath_mult, 6)
    if bias_ratio == 0.0:
        return patch
    bias = round(bias_ratio * BA, 4)
    arr = patch["graph"]["nodes"]
    new = [
        {"id": "Bow_bias", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": bias,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Expo", "startVal": 0.0, "endVal": 1.0,
                 "percent": 0.03, "power": 2.0,
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
                  if d[i] != 0 and d[i + 1] != 0 and (d[i] > 0) != (d[i + 1] > 0))
    periods = (t1 - t0) * f0
    return round(corners / periods, 2)


def oscillates(base, bias_ratio, drive, scratch):
    probe = make_patch(base, bias_ratio)
    nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
    set_loopgain(probe, drive)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_hyb2.json")
    pw = os.path.join(scratch, "probe_hyb2.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, bias_ratio, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, bias_ratio, hi, scratch):
        return None
    if oscillates(base, bias_ratio, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, bias_ratio, mid, scratch):
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

    manifest = {"round": "hysteresis_bow2 bow-velocity bias",
                "ba": BA, "cap": CAP, "bias_ratio": BIAS_RATIO,
                "gain_off": GAIN_OFF, "breath": BREATH,
                "stick": STICK, "slip": SLIP,
                "probe": {"note": PROBE_NOTE, "rms_win": RMS_WIN,
                          "thresh": RMS_THRESH, "seed": PROBE_SEED,
                          "topology": "shipped, output=Reverb"},
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

    crits = {}
    for br in BIAS_RATIO + [0.0]:
        crits[br] = measure_critical(base, br, scratch)
        print(f"crit bias_ratio {br}: {crits[br]}", flush=True)

    for br in BIAS_RATIO:
        if crits[br] is None:
            manifest["variants"].append(
                {"id": f"hyb2_v{int(br*100):03d}", "ok": False,
                 "skip": f"no critical <= {BISECT_HI}"})
            continue
        for dtag, doff in GAIN_OFF.items():
            for btag, bmult in BREATH.items():
                cell = f"hyb2_v{int(br*100):03d}_{dtag}_{btag}"
                patch = make_patch(base, br, bmult)
                d = doff * crits[br]
                set_loopgain(patch, d)
                gain, cpp = render(patch, cell)
                if gain is None:
                    failures += 1
                manifest["variants"].append(
                    {"id": cell, "patch": cell + ".json",
                     "file": cell + ".wav", "ok": gain is not None,
                     "params": {"bias_ratio": br,
                                "bias": round(br * BA, 4),
                                "gain_off": doff, "breath": bmult,
                                "critical": crits[br], "norm_gain": gain,
                                "corners_per_period": cpp,
                                "loop_gain": round(d, 4)}})

    if crits[0.0] is not None:
        p = make_patch(base, 0.0)
        d = GAIN_OFF["lo"] * crits[0.0]
        set_loopgain(p, d)
        gain, cpp = render(p, "x_nobias")
        if gain is None:
            failures += 1
        manifest["nobias"] = {"critical": crits[0.0], "norm_gain": gain,
                              "corners_per_period": cpp,
                              "loop_gain": round(d, 4)}
    gain, cpp = render(make_patch(base, None), "x_control",
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
