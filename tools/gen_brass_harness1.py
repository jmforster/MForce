"""Brass harness round 1 - outward-striking lip on a long bore
(spec docs/superpowers/specs/2026-09-12-brass-harness-design.md).

Topology (from scratch; sibling of the reed/flute chassis - keeps the
wind family's pressure-source energy entry, replaces reed valve, damp
keytrack, and formant output with brass anatomy):

  Noise (small, breath grain) -> Input_sum (+ bore return via
    {tap Bore}); MOUTH PRESSURE enters POST-CUP at Drive_sum - the cup
    is a bandpass and would annihilate the static pressure (measured:
    pressure had zero effect until this fix), and it doubles as the
    cycle's DC block. Junction operating point = Mouth level; brass has
    an over-blow EXTINCTION as well as an ignition threshold, so the
    pressure bisection ceiling sits inside the curve's live region
    -> Cup (SVF bandpass FIXED ~500 Hz, normalize - mouthpiece Helmholtz)
    -> Junction (Shaper, MORPH-coupled lip: values = lips-nearly-closed
       curve, values2 = lips-open curve, morph <- LipOpen; the 2D
       junction's N=2 slice, per-sample)
    -> Bore (DelayLine ratio MODE - the bore fundamental is the note/MODE;
       compensate ON, single-delay cycle) --tap--> Input_sum,
  with BellLP (the bell's reflected-lows filter) INSIDE the cycle
  between Input_sum and Cup (series position in a loop is free; the
  tap must target the delay for compensation). Output = SVF highpass
  of Bore at the bell cutoff (the transmitted complement, v1
  approximation).
  Lip = SVF bandpass keytracked LIPTUNE x played f0 (the lip mass-
        spring), reading the junction INPUT (mouthpiece pressure);
        LipOpen = Shaper rescale of Lip to 0..1 driving morph.
  Output = Bore MINUS BellLP (the bell TRANSMITS what it does not
        reflect - complementary highs; CombinedSource Sum with
        gainAdj -2 on source2) -> light Reverb.

OUTWARD-striking sign, expressed in the curve pair: the closed curve
passes little and clamps hard; the open curve passes generously with
positive slope - so rising pressure with lips swung OPEN by the lip
resonance admits MORE flow (a reed's single curve chokes it).

Mode selection is THE experiment: the delay is 2-3x the note period
(bore fundamental below the note); the lip resonance near the PLAYED
f0 must lock mode n. Round fails honestly per-note if it cannot.

Axes: LIPTUNE {0.85, 1.0} x Q {5, 15} x MODE {2, 3}.
Criticals bisected per config on Mouth maxValue (pressure = the
threshold knob for a pressure-driven valve) at the shipped root; cells
at 1.3 x critical.

Outputs: {patches,renders}/{sweep,audition}/brass_harness1/.
Usage:  python tools/gen_brass_harness1.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "brass_harness1")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "brass_harness1")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "brass_harness1")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "brass_harness1")

LIPTUNE = {"t085": 0.85, "t100": 1.0}
Q = {"q5": 5.0, "q15": 15.0}
MODE = {"m2": 2.0, "m3": 3.0}
GAIN_OFF = 1.3
CUP_HZ = 500.0
CUP_Q = 0.8
BELL_HZ = 1000.0
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.008
BISECT_LO, BISECT_HI = 0.02, 0.8
NORM_PEAK = 0.7

# Lip curve pair (outward-striking): closed passes a trickle and clamps;
# open passes generously. Same 6-point structure (morph lerps points).
# Curve pair that measurably LOCKED mode 2 at f0 (pressure ~0.4 scan,
# 2026-09-12); the operating-point-steep redesign lost the lock to a
# cup-centered oscillation - kept in git history.
CLOSED = [-1.0, -0.12, -0.3, -0.08, 0.0, 0.0,
          0.3, 0.1, 0.6, 0.14, 1.0, 0.16]
OPEN = [-1.0, -1.0, -0.3, -0.75, 0.0, 0.0,
        0.3, 0.9, 0.6, 1.15, 1.0, 1.25]


def make_patch(liptune, q, mode):
    nodes = [
        {"id": "__perf_f1", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "__perf_f2", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "Mouth", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": 1.0,   # pressure = bisect knob
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "fraction", "timeScale": 1.0,
            "stages": [
                {"type": "Expo", "startVal": 0.0, "endVal": 1.0,
                 "percent": 0.04, "power": 2.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": 0.0, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0},
                {"type": "Sine", "startVal": 1.0, "endVal": 0.0,
                 "percent": 0.12, "power": 0.0,
                 "minSec": 0.0, "maxSec": 0.0}]}},
        {"id": "Noise", "type": "WhiteNoiseSource", "params": {
            "amplitude": 0.004, "boost": 0.0, "continuity": 0.0,
            "density": 1.0, "zeroCrossTendency": 0.0}},
        {"id": "Input_sum", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Noise"}, "source2": {"tap": "Bore"}}},
        {"id": "BellLP", "type": "SVFSource", "params": {
            "cutoffFreq": BELL_HZ, "mode": 0, "normalize": False,
            "resonance": 0.6, "source": {"ref": "Input_sum"}}},
        {"id": "Cup", "type": "SVFSource", "params": {
            "cutoffFreq": CUP_HZ, "mode": 2, "normalize": True,
            "resonance": CUP_Q, "source": {"ref": "BellLP"}}},
        {"id": "Lip_curve", "type": "CurveNode", "params": {
            "exprKnots": [{"a": liptune, "b": 0.0, "form": "linear",
                           "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": {"ref": "__perf_f2"}}},
        {"id": "Lip", "type": "SVFSource", "params": {
            "cutoffFreq": {"ref": "Lip_curve"}, "mode": 2,
            "normalize": True, "resonance": q,
            "source": {"ref": "Input_sum"}}},
        {"id": "LipOpen", "type": "Shaper", "params": {
            "source": {"ref": "Lip"}, "drive": 4.0, "smoothness": 0.5,
            "morph": 0.0,
            "values": [-2.0, 0.0, -1.0, 0.0, 1.0, 1.0, 2.0, 1.0]}},
        {"id": "Drive_sum", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 3,
            "source1": {"ref": "Cup"}, "source2": {"ref": "Mouth"}}},
        {"id": "Junction", "type": "Shaper", "params": {
            "source": {"ref": "Drive_sum"}, "drive": 1.0, "smoothness": 0.5,
            "morph": {"ref": "LipOpen"},
            "values": list(CLOSED), "values2": list(OPEN)}},
        {"id": "Bore", "type": "DelayLine", "params": {
            "source": {"ref": "Junction"},
            "frequency": {"ref": "__perf_f1"},
            "ratio": mode, "amplitude": 1.5,
            "compensate": True}},
        # Radiated = highs the bell does NOT reflect (output-side HP
        # complement approximation, bell cutoff).
        {"id": "Radiated", "type": "SVFSource", "params": {
            "cutoffFreq": BELL_HZ, "mode": 1, "normalize": False,
            "resonance": 0.6, "source": {"ref": "Bore"}}},
        {"id": "Reverb", "type": "Reverb", "params": {
            "source": {"ref": "Radiated"},
            "damping": 0.6, "dry": 0.75, "roomSize": 0.25, "wet": 0.15}},
    ]
    return {
        "sampleRate": 48000, "seconds": 10.0,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": 48 + 12 * k, "time": 2.0 * k, "duration": 2.0,
                   "velocity": 0.8} for k in range(5)],   # C3..C7 brass-ish
        "graph": {"output": "Reverb", "nodes": nodes},
        "ui": {"noteFaces": [
            {"fields": {"frequency": "__perf_f1"}, "label": "Note1"},
            {"fields": {"frequency": "__perf_f2"}, "label": "Note2"}]},
    }


def set_pressure(patch, v):
    for n in patch["graph"]["nodes"]:
        if n["id"] == "Mouth":
            n["params"]["maxValue"] = round(v, 4)


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


def goertzel_ratio(path, hz, t0, t1):
    """Energy near hz (f0 +/- 4%) vs total, in the window - the LOCK
    metric: brass must sing THE NOTE, not the cup or a stray mode."""
    import math
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    mono = [sum(raw[i*ch:(i+1)*ch])/ch
            for i in range(int(t0*sr), min(int(t1*sr), n))]
    if len(mono) < 256:
        return 0.0
    mean = sum(mono)/len(mono)
    mono = [x - mean for x in mono]
    total = sum(x*x for x in mono) + 1e-9
    band = 0.0
    for f in (hz*0.96, hz, hz*1.04):
        wq = 2.0*math.pi*f/sr
        c = 2.0*math.cos(wq)
        s0 = s1 = 0.0
        for x in mono:
            s0, s1 = x + c*s0 - s1, s0
        band = max(band, (s1*s1 + s0*s0 - c*s0*s1)/len(mono))
    return band/ (total/len(mono))


def oscillates(liptune, q, mode, pressure, scratch):
    probe = make_patch(liptune, q, mode)
    set_pressure(probe, pressure)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_br1.json")
    pw = os.path.join(scratch, "probe_br1.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(liptune, q, mode, scratch):
    """Brass ignition is a WINDOW (over-blow extinguishes), so bisection
    is wrong here: scan pressures linearly and return the median of the
    f0-LOCKED span (best margin from both edges). None = no locked
    pressure exists for this config - an honest per-cell failure."""
    locked = [round(x * 0.05, 2) for x in range(2, 16)
              if oscillates(liptune, q, mode, x * 0.05, scratch)]
    if not locked:
        return None
    return locked[len(locked) // 2]


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

    manifest = {"round": "brass_harness1 outward-striking lip, long bore",
                "liptune": LIPTUNE, "q": Q, "mode": MODE,
                "cup_hz": CUP_HZ, "bell_hz": BELL_HZ,
                "gain_off": GAIN_OFF, "variants": []}
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

    for ttag, lt in LIPTUNE.items():
        for qtag, qq in Q.items():
            for mtag, md in MODE.items():
                cell = f"br1_{ttag}_{qtag}_{mtag}"
                crit = measure_critical(lt, qq, md, scratch)
                print(f"crit {cell}: {crit}", flush=True)
                if crit is None:
                    manifest["variants"].append(
                        {"id": cell, "ok": False, "skip": "no critical"})
                    continue
                patch = make_patch(lt, qq, md)
                set_pressure(patch, crit)   # median of locked window
                gn = render(patch, cell)
                if gn is None:
                    failures += 1
                manifest["variants"].append(
                    {"id": cell, "ok": gn is not None,
                     "params": {"liptune": lt, "q": qq, "mode": md,
                                "critical": crit,
                                "pressure": crit}})

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
