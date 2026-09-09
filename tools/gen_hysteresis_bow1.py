"""Bow round 1 — hysteresis junction on the oboe1 loop skeleton
(plan Task 5, spec docs/superpowers/specs/2026-09-08-hysteresis-junction-design.md).

The Junction's curves are replaced with a stick/slip pair (7 matched
points each): STICK = slope-2 line through the origin, clamped at |x|>0.5;
SLIP = the classic friction falling flank (peak near |x|=0.15, falling
toward the clamps). hysteresis=true, breakaway/capture per cell.

Axes: BREAKAWAY {0.4, 0.6, 0.9} x CAPTURE {0.0, 0.3} x HEADROOM {s, h}.
Controls: x_memoryless = SAME slip curve, hysteresis off (the spec §6.4
kill-test — hysteresis must clearly beat this or the feature failed);
x_control = stock oboe1. Pitch_vibrato 0.003 round-wide (family lesson).

Criticals bisected per (breakaway, capture) at the SHIPPED topology and
root (backlog 65 rule), breath seeded 0.001, drive min=max. Drive
transplanted as min/max ratios to the stock critical; h = max 2.0x crit.

Extra measurement (spec §6.5): corners_per_period on the C4 note of each
ok cell — zero crossings of the first difference per fundamental period
(sawtooth/Helmholtz trends LOW ~2; reed equilibria higher). Report only.

Writes patches/baselines/feedback/loop_hysteresis_bow.json (breakaway
0.6, capture 0, stock-ratio drive) as the tracked regression baseline.

Outputs: patches/sweep/hysteresis_bow1/, playable copies ->
patches/audition/hysteresis_bow1/; renders -> renders/dsp/{sweep,
audition}/hysteresis_bow1/ (audition normalized to 0.7). README is
copied from renders audition to the patch queue if present.

Usage:  python tools/gen_hysteresis_bow1.py
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
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "hysteresis_bow1")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "hysteresis_bow1")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "hysteresis_bow1")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "hysteresis_bow1")
BASELINE = os.path.join(ROOT, "patches", "baselines", "feedback",
                        "loop_hysteresis_bow.json")

BREAKAWAY = [0.4, 0.6, 0.9]
CAPTURE = [0.0, 0.3]
HEADROOM = {"s": None, "h": 2.0}
PROBE_NOTE = 60
PROBE_SEC = 1.5
RMS_WIN = (0.8, 1.1)
RMS_THRESH = 0.05
BISECT_LO, BISECT_HI = 0.05, 10.0
PROBE_SEED = 0.001
NORM_PEAK = 0.7
C4 = 261.626

STICK = [-1.0, -1.0, -0.6, -1.0, -0.15, -0.3, 0.0, 0.0,
         0.15, 0.3, 0.6, 1.0, 1.0, 1.0]          # slope 2 through origin
SLIP = [-1.0, -0.25, -0.6, -0.35, -0.15, -1.0, 0.0, 0.0,
        0.15, 1.0, 0.6, 0.35, 1.0, 0.25]         # friction falling flank


def nodes_by_id(patch):
    return {n["id"]: n for n in patch["graph"]["nodes"]}


def make_patch(base, ba, cap):
    """ba None = stock control."""
    patch = copy.deepcopy(base)
    nodes_by_id(patch)["Pitch_vibrato"]["params"]["depth"] = 0.003
    if ba is None:
        return patch
    j = nodes_by_id(patch)["Junction"]["params"]
    j["values"] = list(STICK)
    j["values2"] = list(SLIP)
    j["hysteresis"] = True
    j["breakaway"] = ba
    j["capture"] = cap
    j["morph"] = 0.0
    return patch


def make_memoryless(base):
    """Spec §6.4 control: best single-curve rendering of the same slip
    shape — hysteresis off, values = SLIP, no curve B."""
    patch = copy.deepcopy(base)
    nodes_by_id(patch)["Pitch_vibrato"]["params"]["depth"] = 0.003
    j = nodes_by_id(patch)["Junction"]["params"]
    j["values"] = list(SLIP)
    j["values2"] = []
    j["hysteresis"] = False
    j["morph"] = 0.0
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


def corners_per_period(path, t0=2.6, t1=3.8, f0=C4):
    """Zero crossings of the first difference per fundamental period on
    the C4 note window. Report-only roughness-of-waveform metric."""
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


def make_probe(base, ba, cap, drive):
    """SHIPPED topology and root; drive const + seeded breath + short
    score are the only changes (behavior-neutral per the valve2 bridge
    tests)."""
    probe = make_patch(base, ba, cap)
    nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
    set_drive(probe, drive, drive)
    probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                       "duration": PROBE_SEC, "velocity": 0.8}]
    probe["seconds"] = PROBE_SEC
    return probe


def oscillates(base, ba, cap, drive, scratch):
    pj = os.path.join(scratch, "probe_hyb1.json")
    pw = os.path.join(scratch, "probe_hyb1.wav")
    json.dump(make_probe(base, ba, cap, drive), open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    return r.returncode == 0 and os.path.exists(pw) and \
        window_rms(pw) > RMS_THRESH


def measure_critical(base, ba, cap, scratch):
    lo, hi = BISECT_LO, BISECT_HI
    if not oscillates(base, ba, cap, hi, scratch):
        return None
    if oscillates(base, ba, cap, lo, scratch):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(base, ba, cap, mid, scratch):
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

    # Stock critical: ba=None routes make_patch to stock; probe needs the
    # stock loop, so bisect via a direct stock probe.
    crit0 = measure_critical(base, None, None, scratch)
    if crit0 is None:
        sys.exit("stock oboe1 never oscillated in probe — aborting")
    rmin, rmax = mn0 / crit0, mx0 / crit0
    print(f"stock crit={crit0}  drive ratios: min={rmin:.3f} max={rmax:.3f}",
          flush=True)

    manifest = {"round": "hysteresis_bow1 stick/slip junction on oboe1",
                "breakaway": BREAKAWAY, "capture": CAPTURE,
                "headroom": {"s": "stock max ratio", "h": "max 2.0*crit"},
                "stick": STICK, "slip": SLIP,
                "stock_critical": crit0,
                "drive_ratios": {"min": round(rmin, 4), "max": round(rmax, 4)},
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

    for ba in BREAKAWAY:
        for cap in CAPTURE:
            crit = measure_critical(base, ba, cap, scratch)
            stem = f"hyb_b{int(ba*100):03d}_c{int(cap*100):03d}"
            print(f"crit {stem}: {crit}", flush=True)
            if crit is None:
                manifest["variants"].append(
                    {"id": stem, "ok": False,
                     "skip": f"no critical <= {BISECT_HI}"})
                continue
            for htag, hmax in HEADROOM.items():
                cell = f"{stem}_{htag}"
                patch = make_patch(base, ba, cap)
                dmax = (hmax if hmax else rmax) * crit
                set_drive(patch, rmin * crit, dmax)
                gain, cpp = render(patch, cell)
                if gain is None:
                    failures += 1
                manifest["variants"].append(
                    {"id": cell, "patch": cell + ".json",
                     "file": cell + ".wav", "ok": gain is not None,
                     "params": {"breakaway": ba, "capture": cap,
                                "headroom": htag, "critical": crit,
                                "norm_gain": gain,
                                "corners_per_period": cpp,
                                "drive_min": round(rmin * crit, 4),
                                "drive_max": round(dmax, 4)}})

    # Controls. x_memoryless gets its own critical (different curve).
    critm = measure_critical_memoryless(base, scratch)
    print(f"crit x_memoryless: {critm}", flush=True)
    if critm is not None:
        p = make_memoryless(base)
        set_drive(p, rmin * critm, rmax * critm)
        gain, cpp = render(p, "x_memoryless")
        if gain is None:
            failures += 1
        manifest["memoryless"] = {"critical": critm, "norm_gain": gain,
                                  "corners_per_period": cpp}
    else:
        manifest["memoryless"] = {"skip": "no critical <= 10"}

    gain, cpp = render(make_patch(base, None, None), "x_control",
                       audition_patch=False)
    if gain is None:
        failures += 1
    manifest["control"] = {"norm_gain": gain, "corners_per_period": cpp}

    # Tracked regression baseline: ba 0.6 / cap 0 at stock-ratio drive.
    bl = make_patch(base, 0.6, 0.0)
    c = next(v["params"]["critical"] for v in manifest["variants"]
             if v.get("ok") and v["params"]["breakaway"] == 0.6
             and v["params"]["capture"] == 0.0)
    set_drive(bl, rmin * c, rmax * c)
    json.dump(bl, open(BASELINE, "w"), indent=1)
    print(f"baseline -> {BASELINE}", flush=True)

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


def measure_critical_memoryless(base, scratch):
    lo, hi = BISECT_LO, BISECT_HI

    def osc(drive):
        probe = make_memoryless(base)
        nodes_by_id(probe)["WhiteNoise"]["params"]["amplitude"] = PROBE_SEED
        set_drive(probe, drive, drive)
        probe["score"] = [{"note": PROBE_NOTE, "time": 0.0,
                           "duration": PROBE_SEC, "velocity": 0.8}]
        probe["seconds"] = PROBE_SEC
        pj = os.path.join(scratch, "probe_hyb1m.json")
        pw = os.path.join(scratch, "probe_hyb1m.wav")
        json.dump(probe, open(pj, "w"))
        if os.path.exists(pw):
            os.remove(pw)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
        return r.returncode == 0 and os.path.exists(pw) and \
            window_rms(pw) > RMS_THRESH

    if not osc(hi):
        return None
    if osc(lo):
        return lo
    for _ in range(10):
        mid = (lo + hi) / 2
        if osc(mid):
            hi = mid
        else:
            lo = mid
    return round(hi, 4)


main()
