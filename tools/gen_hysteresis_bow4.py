"""Bow round 4 - "take the release and reverse it" (Matt 2026-09-11).

Round-3 diagnosis (README there): the scrape Matt wants lives where the
loop crosses the period-1/period-2 boundary SLOWLY - which the release
does by ringing gain down through it. Round 4 mirrors that trajectory
on the attack: bias engages FAST (e03 - no period-1 prelude, R2), and
the slow traversal moves to the GAIN axis - Ampl_env attack rises from
0 (sub-critical) through the boundary to the scrape-rich 1.40x critical
sustain. The stock Sine release (25%) is kept verbatim: attack and
release now cross the same territory in mirror. dcblock cutoff is swept
as the probable flap/scrape-rate clock (C7 locked to 24 Hz at dc24;
Matt's drive-level datum says flap depth tracks boundary proximity with
the dcblock relaxation as the floor).

Fixed: bias 0.54 (0.9 x ba 0.6), cap 0, damp mult 3, delay ratio 0.5,
drive 1.0, engage 0.03, breath 1.0, sustain gain 1.40 x critical.

Axes:
  GA (Ampl_env attack, fraction of note): 15, 35, 60 - how slowly the
     attack traverses the boundary. ga60 of a 2 s note = 1.2 s stroke.
  DC (DC_Block_hpf cutoff Hz): 06, 12, 24 - scrape-rate candidate.

Criticals bisected per DC (the pole is in the loop); probe uses a fast
attack (ga=0.05) so the RMS window reads steady state - attack shape
does not move the threshold.

Controls: x_r3ref = round 3's e35/g140/b100 (the cell whose RELEASE is
the target) and x_control (stock oboe1).

Outputs: patches/sweep/hysteresis_bow4/, playable copies ->
patches/audition/hysteresis_bow4/; renders -> renders/dsp/{sweep,
audition}/hysteresis_bow4/. README copied to the patch queue if present.

Usage:  python tools/gen_hysteresis_bow4.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "hysteresis_bow4")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "hysteresis_bow4")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "hysteresis_bow4")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "hysteresis_bow4")

BA = 0.6
BIAS = 0.54
DAMP_MULT = 3.0
RATIO = 0.5
ENGAGE = 0.03            # bias engages fast (R1/R2)
SUSTAIN_GAIN = 1.40      # x critical - the scrape-rich level
GA = {"ga15": 0.15, "ga35": 0.35, "ga60": 0.60}
DC = {"dc06": 6.0, "dc12": 12.0, "dc24": 24.0}
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.05
BISECT_LO, BISECT_HI = 0.05, 2.0
PROBE_SEED = 0.001
NORM_PEAK = 0.7
C4 = 261.626

STICK = [-1.0, -1.0, -0.6, -1.0, -0.15, -0.3, 0.0, 0.0,
         0.15, 0.3, 0.6, 1.0, 1.0, 1.0]
SLIP = [-1.0, -0.25, -0.6, -0.35, -0.15, -1.0, 0.0, 0.0,
        0.15, 1.0, 0.6, 0.35, 1.0, 0.25]

NEW_POS = {"Bow_bias": [-1560.0, -560.0], "Bow_sum": [-1400.0, -560.0]}


def nodes_by_id(patch):
    return {n["id"]: n for n in patch["graph"]["nodes"]}


def make_patch(base, ga_pct, dc_hz, r3ref=False):
    """ga_pct None = stock oboe1 control. r3ref=True reproduces round 3's
    e35/g140/b100 (slow BIAS engage, stock gain attack) for direct A/B."""
    patch = copy.deepcopy(base)
    nodes_by_id(patch)["Pitch_vibrato"]["params"]["depth"] = 0.003
    if ga_pct is None and not r3ref:
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
    if dc_hz is not None:
        nodes["DC_Block_hpf"]["params"]["cutoffFreq"] = dc_hz
    engage = 0.35 if r3ref else ENGAGE
    arr = patch["graph"]["nodes"]
    new = [
        {"id": "Bow_bias", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": BIAS,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Expo", "startVal": 0.0, "endVal": 1.0,
                 "percent": engage, "power": 2.0,
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
    if ga_pct is not None:
        # R1: the gain traverses the boundary slowly - attack from 0
        # (sub-critical) to maxValue over ga_pct of the note; stock Sine
        # release kept (the target scrape).
        nodes_by_id(patch)["Ampl_env"]["params"]["stages"] = [
            {"type": "Linear", "startVal": 0.0, "endVal": 1.0,
             "percent": ga_pct, "power": 0.0, "minSec": 0.0, "maxSec": 0.0},
            {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
             "percent": 0.0, "power": 0.0, "minSec": 0.0, "maxSec": 0.0},
            {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
             "percent": 0.25, "power": 0.0, "minSec": 0.0, "maxSec": 0.0}]
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


def oscillates(base, dc_hz, gain, scratch):
    probe = make_patch(base, 0.05, dc_hz)
    nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
    set_loopgain(probe, gain)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_hyb4.json")
    pw = os.path.join(scratch, "probe_hyb4.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, dc_hz, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, dc_hz, hi, scratch):
        return None
    if oscillates(base, dc_hz, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, dc_hz, mid, scratch):
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

    crits = {}
    for dtag, dhz in DC.items():
        crits[dtag] = measure_critical(base, dhz, scratch)
        print(f"crit {dtag}: {crits[dtag]}", flush=True)

    manifest = {"round": "hysteresis_bow4 reverse-the-release",
                "base": {"bias": BIAS, "ba": BA, "damp_mult": DAMP_MULT,
                         "ratio": RATIO, "engage": ENGAGE,
                         "sustain_gain": SUSTAIN_GAIN},
                "ga": GA, "dc": DC, "criticals": crits,
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
            return None
        if audition_patch:
            shutil.copyfile(pj, os.path.join(PATCH_AUD, cell + ".json"))
        return normalize_copy(pw, os.path.join(AUD_OUT, cell + ".wav"))

    for gtag, gp in GA.items():
        for dtag, dhz in DC.items():
            cell = f"hyb4_{gtag}_{dtag}"
            crit = crits[dtag]
            if crit is None:
                manifest["variants"].append(
                    {"id": cell, "ok": False, "skip": "no critical"})
                continue
            patch = make_patch(base, gp, dhz)
            set_loopgain(patch, SUSTAIN_GAIN * crit)
            gn = render(patch, cell)
            if gn is None:
                failures += 1
            manifest["variants"].append(
                {"id": cell, "patch": cell + ".json", "file": cell + ".wav",
                 "ok": gn is not None,
                 "params": {"ga": gp, "dc": dhz, "critical": crit,
                            "loop_gain": round(SUSTAIN_GAIN * crit, 4),
                            "norm_gain": gn}})

    p = make_patch(base, None, None, r3ref=True)
    set_loopgain(p, round(SUSTAIN_GAIN * (crits.get("dc12") or 0.93), 4))
    if render(p, "x_r3ref") is None:
        failures += 1
    if render(make_patch(base, None, None), "x_control",
              audition_patch=False) is None:
        failures += 1

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
