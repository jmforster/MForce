"""String harness round 1 - the waveguide bowed string, built native
(spec docs/superpowers/specs/2026-09-11-string-chassis-design.md +
09-12 campaign addendum; Matt's directive 2026-09-12).

Topology (from scratch - NOT an oboe1 derivative; no breath anywhere):

  Bow_sum(Sum: {tap NutDelay} + Bow_bias env)
    -> Junction (hysteresis Shaper: stick/slip curves, ba 0.6,
       capture per cell, drive 1.0, bias 0.36 = round-5 period-1 regime)
    -> BridgeDelay (ratio P = bow position fraction of the loop)
    -> BridgeLP (SVF lowpass keytracked LOSS x f0 - the string's lumped
       bridge loss; res 0.5 floor)
    -> DCBlock (Highpass1P 12 Hz - the terminations cannot sustain DC;
       without it the biased junction's DC recirculates and the "note"
       becomes a ~45 Hz charge/relax cycle that ignores the delays -
       measured 2026-09-12 first light)
    -> NutDelay (ratio 1-P, amplitude <- Ampl_env = loop gain + release,
       compensate ON)  --tap--> Bow_sum.

TUNING, exact by construction: compensate lives on NutDelay ONLY. Its
walk (inputs-only since backlog 66) reaches the closing tap through
BridgeLP -> BridgeDelay -> Junction -> Bow_sum, so it compensates the
tap sample + every filter phase - everything EXCEPT BridgeDelay's bulk
length, which is exactly the ratio P share. Total loop = sr/f0.
The two end-inversions of the physical model multiply to +1 per round
trip and are folded out (standard waveguide simplification).
Bow position note: single-rail loop, so P is the bow's fraction of the
ROUND TRIP - the standard folded simplification of the two-rail model.

Output = transmitted bridge force: DCBlock (post-loss, post-DC) via a
normal ref (the cycle's advancing consumer) -> light Reverb. (First
light taught: tapping BridgeDelay upstream of the DC block put the
bias's standing DC on the output and every critical probe
false-positived at the bisection floor.) Body resonances deferred (spec Q2
lean: bare mechanism first). NutDelay is consumed only by the tap ->
advance list (feedback_loop_design section 3.3, the documented shape).

Axes: P {1/6, 1/8, 1/12} x CAP {0.25, 0.12} x LOSS {8, 12}.
Criticals bisected per (P, LOSS) on Ampl_env maxValue at the shipped
root; cells at 1.2 x critical. No breath = the loop is seeded by the
bias-engagement transient itself.

Outputs: {patches,renders}/{sweep,audition}/string_harness1/.
Usage:  python tools/gen_string_harness1.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "string_harness1")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "string_harness1")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "string_harness1")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "string_harness1")

BA = 0.6
BIAS = 0.36              # 0.6 x ba - round-5 period-1 regime
P = {"p16": 1.0/6.0, "p18": 1.0/8.0, "p112": 1.0/12.0}
CAP = {"c25": 0.25, "c12": 0.12}
LOSS = {"L8": 8.0, "L12": 12.0}
GAIN_OFF = 1.2
ENGAGE = 0.03
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.03
BISECT_LO, BISECT_HI = 0.05, 2.0
NORM_PEAK = 0.7

STICK = [-1.0, -1.0, -0.6, -1.0, -0.15, -0.3, 0.0, 0.0,
         0.15, 0.3, 0.6, 1.0, 1.0, 1.0]
SLIP = [-1.0, -0.25, -0.6, -0.35, -0.15, -1.0, 0.0, 0.0,
        0.15, 1.0, 0.6, 0.35, 1.0, 0.25]


def make_patch(p_frac, cap, loss_mult):
    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f3", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "Bow_bias", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": BIAS,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Expo", "startVal": 0.0, "endVal": 1.0,
                 "percent": ENGAGE, "power": 2.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": 0.0, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
                 "percent": 0.1, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0}]}},
        {"id": "Ampl_env", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": 1.0,   # maxValue = loop gain knob
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Linear", "startVal": 0.0, "endVal": 1.0,
                 "percent": 0.02, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": 0.0, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
                 "percent": 0.25, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0}]}},
        {"id": "Bow_sum", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"tap": "NutDelay"},
            "source2": {"ref": "Bow_bias"}}},
        {"id": "Junction", "type": "Shaper", "params": {
            "source": {"ref": "Bow_sum"}, "drive": 1.0,
            "smoothness": 0.6, "morph": 0.0,
            "hysteresis": True, "breakaway": BA, "capture": cap,
            "values": list(STICK), "values2": list(SLIP)}},
        {"id": "BridgeDelay", "type": "DelayLine", "params": {
            "source": {"ref": "Junction"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": round(p_frac, 6), "amplitude": 1.0,
            "compensate": False}},
        {"id": "Bridge_curve", "type": "CurveNode", "params": {
            "exprKnots": [{"a": loss_mult, "b": 0.0, "form": "linear",
                           "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": {"ref": "__perf_f3"}}},
        {"id": "BridgeLP", "type": "SVFSource", "params": {
            "cutoffFreq": {"ref": "Bridge_curve"}, "mode": 0,
            "normalize": False, "resonance": 0.5,
            "source": {"ref": "BridgeDelay"}}},
        {"id": "DCBlock", "type": "SVFSource", "params": {
            "cutoffFreq": 12.0, "mode": 4, "normalize": False,
            "resonance": 0.7, "source": {"ref": "BridgeLP"}}},
        {"id": "NutDelay", "type": "DelayLine", "params": {
            "source": {"ref": "DCBlock"},
            "frequency": {"ref": "__perf_f2"},
            "ratio": round(1.0 - p_frac, 6),
            "amplitude": {"ref": "Ampl_env"},
            "compensate": True}},
        {"id": "Reverb", "type": "Reverb", "params": {
            "source": {"ref": "DCBlock"},
            "damping": 0.6, "dry": 0.75, "roomSize": 0.25, "wet": 0.15}},
    ]
    return {
        "sampleRate": 48000, "seconds": 10.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": 36 + 12 * k, "time": 2.0 * k, "duration": 2.0,
                   "velocity": 0.8} for k in range(5)],
        "graph": {"output": "Reverb", "nodes": nodes},
        "ui": {"noteFaces": [
            {"fields": {"frequency": "__perf_f1"}, "label": "Note1"},
            {"fields": {"frequency": "__perf_f2"}, "label": "Note2"},
            {"fields": {"frequency": "__perf_f3"}, "label": "Note3"}]},
    }


def set_loopgain(patch, g):
    for n in patch["graph"]["nodes"]:
        if n["id"] == "Ampl_env":
            n["params"]["maxValue"] = round(g, 4)


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


def oscillates(p_frac, cap, loss, gain, scratch):
    probe = make_patch(p_frac, cap, loss)
    set_loopgain(probe, gain)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_str1.json")
    pw = os.path.join(scratch, "probe_str1.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(p_frac, cap, loss, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(p_frac, cap, loss, hi, scratch):
        return None
    if oscillates(p_frac, cap, loss, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(p_frac, cap, loss, mid, scratch):
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

    crits = {}
    for ptag, pf in P.items():
        for ltag, lm in LOSS.items():
            key = f"{ptag}_{ltag}"
            crits[key] = measure_critical(pf, CAP["c25"], lm, scratch)
            print(f"crit {key}: {crits[key]}", flush=True)

    manifest = {"round": "string_harness1 waveguide bowed string",
                "bias": BIAS, "ba": BA, "gain_off": GAIN_OFF,
                "p": P, "cap": CAP, "loss": LOSS, "criticals": crits,
                "variants": []}
    failures = 0

    def render(patch, cell, audition_patch=True):
        pj = os.path.join(PATCH_OUT, cell + ".json")
        pw = os.path.join(SWEEP_OUT, cell + ".wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"FAIL {cell}: {r.stderr.decode(errors='replace')[:160]}",
                  flush=True)
            return None
        if audition_patch:
            shutil.copyfile(pj, os.path.join(PATCH_AUD, cell + ".json"))
        return normalize_copy(pw, os.path.join(AUD_OUT, cell + ".wav"))

    for ptag, pf in P.items():
        for ltag, lm in LOSS.items():
            crit = crits[f"{ptag}_{ltag}"]
            if crit is None:
                manifest["variants"].append(
                    {"id": f"{ptag}_{ltag}", "ok": False,
                     "skip": "no critical"})
                continue
            for ctag, cap in CAP.items():
                cell = f"str1_{ptag}_{ctag}_{ltag}"
                patch = make_patch(pf, cap, lm)
                set_loopgain(patch, GAIN_OFF * crit)
                gn = render(patch, cell)
                if gn is None:
                    failures += 1
                manifest["variants"].append(
                    {"id": cell, "ok": gn is not None,
                     "params": {"p": round(pf, 4), "cap": cap, "loss": lm,
                                "critical": crit,
                                "loop_gain": round(GAIN_OFF * crit, 4)}})

    json.dump(manifest, open(os.path.join(SWEEP_OUT, "manifest.json"), "w"),
              indent=1)
    readme = os.path.join(AUD_OUT, "README.md")
    if os.path.exists(readme):
        shutil.copyfile(readme, os.path.join(PATCH_AUD, "README.md"))
    n = sum(1 for v in manifest["variants"] if v.get("ok"))
    print(f"{n} cells rendered ok, {failures} failures -> {SWEEP_OUT}")
    if failures:
        sys.exit(1)


main()
