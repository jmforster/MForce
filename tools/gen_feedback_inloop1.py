"""Feedback in-loop formant round 1 — oboe_default, bells moved into the loop
(Matt 09-05: hand attempts hit overblow/flutter knife-edge; "Yes please" to a
normalized, critical-relative grid. Context: docs/research/feedback_sweeps/
JUNCTION_OPERATING_POINT.md).

Base = patches/library/winds/oboe_default.json AS ON DISK (includes Matt's
uncommitted 09-03 tweaks). Library file is never modified.

Axes:
  P (placement):
    in = SVF(1500)/SVF2(3000) moved INTO the loop (damp -> SVF -> SVF2 ->
         delay), post-loop branch removed (Reverb reads delay directly)
    hy = hybrid: bells in-loop as above, PLUS copies of the original
         post-loop branch (SVFp/SVF2p at the library res values,
         normalize off) feeding Combined as before
  R (in-loop resonance, both bells, normalize=true so res shapes color
     without dragging loop gain): 1, 2, 4, 8, 16

Critical drive bisected per RES (the loop is identical for both placements
— only the output branch differs). Probes read the LOOP, not the output:
probe graph output = Junction (advance list keeps the loop ticking, per
feedback_loop_design.md §3.3), breath muted to a 0.001 seed, RMS window
0.8-1.1 s > 0.05. First version of this script read output-chain RMS and
got placement-dependent "criticals" (normalize divides the delay's level
by ~res^2) — junction-referenced measurement fixes that. Drive_env stage
shapes are Matt's; minValue/maxValue are transplanted as ratios to the
stock-topology critical (0.5332/0.7297 vs stock crit), then applied per
cell as ratio * res crit. Audition copies are peak-normalized to 0.7
(raw levels differ by topology, not by merit); sweep/ keeps raw.

Renders: full grid + stock control -> renders/dsp/sweep/feedback_inloop1/;
whole round is small, so every ok cell + control is COPIED to
renders/dsp/audition/feedback_inloop1/ (the listening queue).
Patches -> patches/sweep/feedback_inloop1/.

Usage:  python tools/gen_feedback_inloop1.py
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
BASE = os.path.join(ROOT, "patches", "library", "winds", "oboe_default.json")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "feedback_inloop1")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "feedback_inloop1")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "feedback_inloop1")

PLACEMENTS = ["in", "hy"]
RES = [1.0, 2.0, 4.0, 8.0, 16.0]
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.05
BISECT_LO, BISECT_HI = 0.05, 10.0
PROBE_SEED = 0.001
NORM_PEAK = 0.7


def nodes_by_id(patch):
    return {n["id"]: n for n in patch["graph"]["nodes"]}


def make_patch(base, placement, res):
    """placement None = stock topology (control / stock probes)."""
    patch = copy.deepcopy(base)
    if placement is None:
        return patch
    arr = patch["graph"]["nodes"]
    nodes = nodes_by_id(patch)
    svf_orig = copy.deepcopy(nodes["SVF"]["params"])
    svf2_orig = copy.deepcopy(nodes["SVF2"]["params"])
    nodes["SVF"]["params"].update(
        {"source": {"ref": "damp"}, "resonance": res, "normalize": True})
    nodes["SVF2"]["params"].update({"resonance": res, "normalize": True})
    nodes["delay"]["params"]["source"] = {"ref": "SVF2"}
    svf, svf2 = nodes["SVF"], nodes["SVF2"]
    arr[:] = [n for n in arr if n["id"] not in ("SVF", "SVF2")]
    at = next(i for i, n in enumerate(arr) if n["id"] == "delay")
    arr[at:at] = [svf, svf2]
    if placement == "in":
        nodes["Reverb"]["params"]["source"] = {"ref": "delay"}
        arr[:] = [n for n in arr if n["id"] != "Combined"]
    else:
        svf_orig["source"] = {"ref": "delay"}
        svf2_orig["source"] = {"ref": "SVFp"}
        post = [{"id": "SVFp", "type": "SVFSource", "params": svf_orig},
                {"id": "SVF2p", "type": "SVFSource", "params": svf2_orig}]
        at = next(i for i, n in enumerate(arr) if n["id"] == "Combined")
        arr[at:at] = post
        nodes["Combined"]["params"]["source2"] = {"ref": "SVF2p"}
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


def oscillates(base, res, drive, scratch):
    """Loop-referenced probe: output = Junction, breath muted to a seed.
    res None = stock loop (no bells in the cycle). The output branch is
    stripped so the delay's only consumer is the tap -> advance list keeps
    the loop ticking with the shaper as root (design doc §3.3 example)."""
    probe = make_patch(base, None if res is None else "in", res)
    arr = probe["graph"]["nodes"]
    drop = {"Reverb", "Combined"} | ({"SVF", "SVF2"} if res is None else set())
    arr[:] = [n for n in arr if n["id"] not in drop]
    probe["graph"]["output"] = "Junction"
    nodes_by_id(probe)["Breath_noise"]["params"]["amplitude"] = PROBE_SEED
    set_drive(probe, drive, drive)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    pj = os.path.join(scratch, "probe_inloop1.json")
    pw = os.path.join(scratch, "probe_inloop1.wav")
    json.dump(probe, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, res, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, res, hi, scratch):
        return None
    if oscillates(base, res, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, res, mid, scratch):
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

    crit0 = measure_critical(base, None, scratch)
    if crit0 is None:
        sys.exit("stock oboe_default never oscillated in probe — aborting")
    rmin, rmax = mn0 / crit0, mx0 / crit0
    print(f"stock crit={crit0}  drive ratios: min={rmin:.3f} max={rmax:.3f}",
          flush=True)

    crits = {}
    for res in RES:
        crits[res] = measure_critical(base, res, scratch)
        print(f"crit r{int(res)}: {crits[res]}", flush=True)

    manifest = {"round": "feedback_inloop1 oboe_default in-loop formants",
                "placements": PLACEMENTS, "res": RES,
                "stock_critical": crit0,
                "drive_ratios": {"min": round(rmin, 4), "max": round(rmax, 4)},
                "criticals": {f"r{int(r)}": c for r, c in crits.items()},
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

    for placement in PLACEMENTS:
        for res in RES:
            cell = f"fbin_{placement}_r{int(res)}"
            crit = crits[res]
            if crit is None:
                manifest["variants"].append(
                    {"id": cell, "ok": False,
                     "skip": f"no critical <= {BISECT_HI}"})
                continue
            patch = make_patch(base, placement, res)
            set_drive(patch, rmin * crit, rmax * crit)
            gain = render(patch, cell)
            if gain is None:
                failures += 1
            manifest["variants"].append(
                {"id": cell, "patch": cell + ".json", "file": cell + ".wav",
                 "ok": gain is not None,
                 "params": {"placement": placement, "res": res,
                            "critical": crit, "norm_gain": gain,
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
