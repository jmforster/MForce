"""Feedback resonant-valve round 1 — the brass/lip probe (Matt 09-08 go).

Physics: the classical one-mass lip model is a mass-spring-damper driven by
mouth pressure — mathematically a 2nd-order resonant bandpass — coupled to a
nonlinear valve. Our loop already has the nonlinearity (Junction); this round
adds the mass-spring as an SVF Bandpass IN the loop feeding the Junction,
keytracked near f0 (brass lips are tuned near the sounding tube mode; reeds
resonate far above it, which is why the memoryless junction alone lands on
the reed spectrum — docs/research/feedback_sweeps/LOOP_PATCH_ANATOMY.md).

Base = patches/library/winds/oboe1.json AS ON DISK. Never modified.
(Round 1 ran 2026-09-08 against the same patch under its pre-rename name
oboe_default.json — Matt's numbered-names curation landed the same day.)

Topology change per cell:
  Drive_inputs -> Valve (SVF Bandpass, normalize=true) -> Junction.source
  Valve.cutoffFreq <- Valve_curve (CurveNode linear a=RATIO on a new
  __perf_frequency3 PerformNode) => center = RATIO * f0 every note.
  normalize=true: unity gain at center regardless of res, so res sets how
  hard off-center partials are culled per pass without dragging loop gain.
  compensate=true on Delay_line absorbs the valve's phase delay (SVFSource
  has an exact closed-form phase_delay_at for the Bandpass tap).

Axes:
  RATIO (valve center / f0): 0.5, 0.75, 0.9, 1.0, 1.25
    (outward-striking lips sit just below the played mode -> the 0.75-1.0
     band is the "physical" region; 0.5 and 1.25 bracket it)
  RES (valve Q): 2, 5, 10, 20

Critical drive bisected PER CELL (unlike inloop1: here the loop's gain at f0
depends on both axes — off-center ratios put f0 on the bandpass skirt).
Probe = junction-referenced (output=Junction, breath muted to 0.001 seed,
RMS 0.8-1.1 s > 0.05), same method as gen_feedback_inloop1.py. Drive_env
min/max transplanted as ratios to the stock critical, applied per cell.

Renders: full grid + stock control -> renders/dsp/sweep/feedback_valve1/;
every ok cell + control peak-normalized to 0.7 and COPIED to
renders/dsp/audition/feedback_valve1/ (the listening queue).
Patches -> patches/sweep/feedback_valve1/.

Usage:  python tools/gen_feedback_valve1.py
"""
import copy
import json
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
BASE = os.path.join(ROOT, "patches", "library", "winds", "oboe_default.json")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "feedback_valve1")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "feedback_valve1")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "feedback_valve1")

RATIOS = [0.5, 0.75, 0.9, 1.0, 1.25]
RES = [2.0, 5.0, 10.0, 20.0]
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.05
BISECT_LO, BISECT_HI = 0.05, 10.0
PROBE_SEED = 0.001
NORM_PEAK = 0.7


def nodes_by_id(patch):
    return {n["id"]: n for n in patch["graph"]["nodes"]}


def make_patch(base, ratio, res):
    """ratio None = stock topology (control / stock probe)."""
    patch = copy.deepcopy(base)
    if ratio is None:
        return patch
    arr = patch["graph"]["nodes"]
    perf = {"id": "__perf_frequency3", "params": {"field": "frequency"},
            "type": "PerformNode"}
    curve = {"id": "Valve_curve", "type": "CurveNode", "params": {
        "exprKnots": [{"a": ratio, "b": 0.0, "form": "linear", "x": 440.0}],
        "interp": "linear", "knots": [], "mode": "expressions",
        "source": {"ref": "__perf_frequency3"}}}
    valve = {"id": "Valve", "type": "SVFSource", "params": {
        "cutoffFreq": {"ref": "Valve_curve"}, "mode": 2,
        "normalize": True, "resonance": res,
        "source": {"ref": "Drive_inputs"}}}
    at = next(i for i, n in enumerate(arr) if n["id"] == "Junction")
    arr[at:at] = [perf, curve, valve]
    nodes_by_id(patch)["Junction"]["params"]["source"] = {"ref": "Valve"}
    patch["ui"]["noteFaces"].append(
        {"fields": {"frequency": "__perf_frequency3"}, "label": "Note4"})
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


def oscillates(base, ratio, res, drive, scratch):
    """Junction-referenced probe; output branch stripped so the wormhole tap
    is the delay's only consumer and the advance list keeps the loop ticking
    (feedback_loop_design.md section 3.3, same as gen_feedback_inloop1)."""
    probe = make_patch(base, ratio, res)
    arr = probe["graph"]["nodes"]
    drop = {"Reverb", "Combined", "Formant1", "SVF2"}
    arr[:] = [n for n in arr if n["id"] not in drop]
    probe["graph"]["output"] = "Junction"
    nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
    set_drive(probe, drive, drive)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_valve1.json")
    pw = os.path.join(scratch, "probe_valve1.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, ratio, res, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, ratio, res, hi, scratch):
        return None
    if oscillates(base, ratio, res, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, ratio, res, mid, scratch):
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
    for d in (PATCH_OUT, SWEEP_OUT, AUD_OUT):
        os.makedirs(d, exist_ok=True)
    scratch = os.path.join(ROOT, "renders", "scratch")
    os.makedirs(scratch, exist_ok=True)
    for d in (SWEEP_OUT, AUD_OUT):
        for f in os.listdir(d):
            if f.endswith(".wav"):
                os.remove(os.path.join(d, f))
    base = json.load(open(BASE))
    de = nodes_by_id(base)["Drive_env"]["params"]
    mn0, mx0 = de["minValue"], de["maxValue"]

    crit0 = measure_critical(base, None, None, scratch)
    if crit0 is None:
        sys.exit("stock oboe_default never oscillated in probe — aborting")
    rmin, rmax = mn0 / crit0, mx0 / crit0
    print(f"stock crit={crit0}  drive ratios: min={rmin:.3f} max={rmax:.3f}",
          flush=True)

    manifest = {"round": "feedback_valve1 oboe_default in-loop lip valve",
                "ratios": RATIOS, "res": RES,
                "stock_critical": crit0,
                "drive_ratios": {"min": round(rmin, 4), "max": round(rmax, 4)},
                "probe": {"note": PROBE_NOTE, "rms_win": RMS_WIN,
                          "thresh": RMS_THRESH, "seed": PROBE_SEED,
                          "output": "Junction"},
                "norm_peak": NORM_PEAK, "variants": []}
    failures = 0

    def render(patch, cell):
        pj = os.path.join(PATCH_OUT, cell + ".json")
        pw = os.path.join(SWEEP_OUT, cell + ".wav")
        json.dump(patch, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"FAIL {cell}:"
                  f" {r.stderr.decode(errors='replace')[:160]}", flush=True)
            return None
        return normalize_copy(pw, os.path.join(AUD_OUT, cell + ".wav"))

    for ratio in RATIOS:
        for res in RES:
            rtag = f"{int(round(ratio * 100)):03d}"
            cell = f"fbv_r{rtag}_q{int(res)}"
            crit = measure_critical(base, ratio, res, scratch)
            print(f"crit {cell}: {crit}", flush=True)
            if crit is None:
                manifest["variants"].append(
                    {"id": cell, "ok": False,
                     "skip": f"no critical <= {BISECT_HI}"})
                continue
            patch = make_patch(base, ratio, res)
            set_drive(patch, rmin * crit, rmax * crit)
            gain = render(patch, cell)
            if gain is None:
                failures += 1
            manifest["variants"].append(
                {"id": cell, "patch": cell + ".json", "file": cell + ".wav",
                 "ok": gain is not None,
                 "params": {"ratio": ratio, "res": res, "critical": crit,
                            "norm_gain": gain,
                            "drive_min": round(rmin * crit, 4),
                            "drive_max": round(rmax * crit, 4)}})

    gain = render(make_patch(base, None, None), "x_control")
    if gain is None:
        failures += 1
    manifest["control_norm_gain"] = gain

    json.dump(manifest, open(os.path.join(SWEEP_OUT, "manifest.json"), "w"),
              indent=1)
    n = sum(1 for v in manifest["variants"] if v.get("ok"))
    print(f"{n} cells rendered ok, {failures} failures -> {SWEEP_OUT}")
    print(f"queue -> {AUD_OUT}")
    if failures:
        sys.exit(1)


main()
