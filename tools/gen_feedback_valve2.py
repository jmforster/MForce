"""Feedback valve round 2 — DRIVE-COUPLED lip valve (Matt 09-08 "Go").

Round 1 (gen_feedback_valve1.py) put the lip's mass-spring bandpass in
SERIES in the loop; verdict: sustain stayed generic-reed, because a series
filter culls harmonics the junction just regenerates — the resonator state
never touches the nonlinearity. Round 2 couples it the way a lip actually
works: the valve's ringing GATES THE FLOW, i.e. modulates Junction.drive
at audio rate. Existing primitives only:

  Valve   = SVF Bandpass (normalize) on Drive_inputs (the pressure sum),
            cutoff <- Valve_curve = RATIO * f0 (new __perf_frequency3)
  LipOpen = Shaper on Valve, drive=K: curve = flat 0 below -1 (lip closed),
            1+x in the middle (linear opening), capped 2 (max opening)
  DriveMul= CombinedSource Multiply: Drive_vib * LipOpen -> Junction.drive

Junction.source stays stock (Drive_inputs) — the coupling is in drive, so
the operating point breathes with the valve's own resonance. Same-sample
algebraic coupling (Shaper pulls source before drive), no added loop lag.

Round-1 lessons applied:
  * Probes run the SHIPPED topology and root (output=Reverb) — rooting the
    probe mid-loop measured a parasitic mode last time (see the
    loop-root-sensitivity baselines + backlog entry).
  * Inserted nodes join the Drive group WITH ui positions (round 1's
    group-less inserts triggered the known group-interface display bug).
  * Off-center ratios are physics-dead in series; here the valve is not in
    the signal path so ratio only tunes the modulator — still kept near
    center (0.9, 1.0), the physical lip region.

Axes: RATIO {0.90, 1.00} x Q {10, 20} x K (coupling depth) {0.25, 0.5,
1.0} = 12 loop configs, criticals bisected per config; each rendered at
two drive headrooms: s = stock ratios (1.20/1.33 x crit), h = hot
(min 1.20 x crit, max 2.0 x crit) — Matt's "all quite similar" round-1
note suggests under-driven differences. 24 cells + control.

Outputs: patches/sweep/feedback_valve2/ (all cells);
PLAYABLE COPIES -> patches/audition/feedback_valve2/ (the queue — Matt
judges patches at the keyboard, not just Cs);
renders -> renders/dsp/sweep/feedback_valve2/ (raw + manifest) and
renders/dsp/audition/feedback_valve2/ (peak-normalized 0.7).

Usage:  python tools/gen_feedback_valve2.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "feedback_valve2")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "feedback_valve2")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "feedback_valve2")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "feedback_valve2")

RATIOS = [0.9, 1.0]
RES = [10.0, 20.0]
COUPLE = [0.25, 0.5, 1.0]
HEADROOM = {"s": None, "h": 2.0}   # None = stock max ratio; h = max 2.0*crit
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.05
BISECT_LO, BISECT_HI = 0.05, 10.0
PROBE_SEED = 0.001
NORM_PEAK = 0.7

NEW_POS = {
    "__perf_frequency3": [-1560.0, -560.0],
    "Note4": [-1560.0, -560.0],
    "Valve_curve": [-1400.0, -560.0],
    "Valve": [-1240.0, -560.0],
    "LipOpen": [-1080.0, -560.0],
    "DriveMul": [-1080.0, -410.0],
}


def nodes_by_id(patch):
    return {n["id"]: n for n in patch["graph"]["nodes"]}


def make_patch(base, ratio, res, k):
    """ratio None = stock topology (control / stock probe)."""
    patch = copy.deepcopy(base)
    if ratio is None:
        return patch
    arr = patch["graph"]["nodes"]
    new = [
        {"id": "__perf_frequency3", "params": {"field": "frequency"},
         "type": "PerformNode"},
        {"id": "Valve_curve", "type": "CurveNode", "params": {
            "exprKnots": [{"a": ratio, "b": 0.0, "form": "linear",
                           "x": 440.0}],
            "interp": "linear", "knots": [], "mode": "expressions",
            "source": {"ref": "__perf_frequency3"}}},
        {"id": "Valve", "type": "SVFSource", "params": {
            "cutoffFreq": {"ref": "Valve_curve"}, "mode": 2,
            "normalize": True, "resonance": res,
            "source": {"ref": "Drive_inputs"}}},
        # Lip opening: 0 when driven shut, 1+x linear, capped at 2.
        {"id": "LipOpen", "type": "Shaper", "params": {
            "source": {"ref": "Valve"}, "drive": k, "smoothness": 0.5,
            "morph": 0.0,
            "values": [-2.0, 0.0, -1.0, 0.0, 1.0, 2.0, 2.0, 2.0]}},
        {"id": "DriveMul", "type": "CombinedSource", "params": {
            "gainAdj": 0.0, "operation": 1,
            "source1": {"ref": "Drive_vib"},
            "source2": {"ref": "LipOpen"}}},
    ]
    at = next(i for i, n in enumerate(arr) if n["id"] == "Junction")
    arr[at:at] = new
    nodes_by_id(patch)["Junction"]["params"]["drive"] = {"ref": "DriveMul"}
    patch["ui"]["noteFaces"].append(
        {"fields": {"frequency": "__perf_frequency3"}, "label": "Note4"})
    patch["ui"]["positions"].update(NEW_POS)
    for grp in patch.get("groups", []):
        if grp["name"] == "Drive":
            grp["members"] += ["__perf_frequency3", "Note4", "Valve_curve",
                               "Valve", "LipOpen", "DriveMul"]
    return patch


def set_drive(patch, mn, mx):
    de = nodes_by_id(patch)["Drive_env"]["params"]
    de["minValue"] = round(mn, 4)
    de["maxValue"] = round(mx, 4)


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


def oscillates(base, ratio, res, k, drive, scratch):
    """SHIPPED topology and root; only drive const + seeded breath + score
    differ from the real cell (both proven behavior-neutral in the round-1
    bridge tests)."""
    probe = make_patch(base, ratio, res, k)
    nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
    set_drive(probe, drive, drive)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_valve2.json")
    pw = os.path.join(scratch, "probe_valve2.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, ratio, res, k, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, ratio, res, k, hi, scratch):
        return None
    if oscillates(base, ratio, res, k, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, ratio, res, k, mid, scratch):
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
    de = nodes_by_id(base)["Drive_env"]["params"]
    mn0, mx0 = de["minValue"], de["maxValue"]

    crit0 = measure_critical(base, None, None, None, scratch)
    if crit0 is None:
        sys.exit("stock oboe1 never oscillated in probe — aborting")
    rmin, rmax = mn0 / crit0, mx0 / crit0
    print(f"stock crit={crit0}  drive ratios: min={rmin:.3f} max={rmax:.3f}",
          flush=True)

    manifest = {"round": "feedback_valve2 oboe1 drive-coupled lip valve",
                "ratios": RATIOS, "res": RES, "couple": COUPLE,
                "headroom": {"s": "stock max ratio", "h": "max 2.0*crit"},
                "stock_critical": crit0,
                "drive_ratios": {"min": round(rmin, 4), "max": round(rmax, 4)},
                "probe": {"note": PROBE_NOTE, "rms_win": RMS_WIN,
                          "thresh": RMS_THRESH, "seed": PROBE_SEED,
                          "topology": "shipped, output=Reverb"},
                "norm_peak": NORM_PEAK, "variants": []}
    failures = 0

    def render(patch, cell, audition_patch):
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

    for ratio in RATIOS:
        for res in RES:
            for k in COUPLE:
                crit = measure_critical(base, ratio, res, k, scratch)
                rtag = f"{int(round(ratio * 100)):03d}"
                ktag = f"{int(round(k * 100)):03d}"
                stem = f"fbv2_r{rtag}_q{int(res)}_k{ktag}"
                print(f"crit {stem}: {crit}", flush=True)
                if crit is None:
                    manifest["variants"].append(
                        {"id": stem, "ok": False,
                         "skip": f"no critical <= {BISECT_HI}"})
                    continue
                for htag, hmax in HEADROOM.items():
                    cell = f"{stem}_{htag}"
                    patch = make_patch(base, ratio, res, k)
                    dmax = (hmax if hmax else rmax) * crit
                    set_drive(patch, rmin * crit, dmax)
                    gain = render(patch, cell, audition_patch=True)
                    if gain is None:
                        failures += 1
                    manifest["variants"].append(
                        {"id": cell, "patch": cell + ".json",
                         "file": cell + ".wav", "ok": gain is not None,
                         "params": {"ratio": ratio, "res": res, "couple": k,
                                    "headroom": htag, "critical": crit,
                                    "norm_gain": gain,
                                    "drive_min": round(rmin * crit, 4),
                                    "drive_max": round(dmax, 4)}})

    gain = render(make_patch(base, None, None, None), "x_control",
                  audition_patch=False)
    if gain is None:
        failures += 1
    manifest["control_norm_gain"] = gain

    json.dump(manifest, open(os.path.join(SWEEP_OUT, "manifest.json"), "w"),
              indent=1)
    n = sum(1 for v in manifest["variants"] if v.get("ok"))
    print(f"{n} cells rendered ok, {failures} failures -> {SWEEP_OUT}")
    print(f"render queue -> {AUD_OUT}")
    print(f"patch queue  -> {PATCH_AUD}")
    if failures:
        sys.exit(1)


main()
