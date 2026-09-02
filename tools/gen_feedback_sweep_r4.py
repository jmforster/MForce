"""Feedback sweep r4 — the CRITICAL-DRIVE round (backlog 37 + 37a(c)).

Matt's r3 verdict (2026-09-01): range of settings, not range of character;
uniform fast attacks; universal overtone overload; junction edits
unpredictable. Diagnosis: every cell sat at an uncontrolled, comfortable
distance above its curve's critical drive with the same fast ramp through
it. Distance-above-critical sets BOTH overtone depth (how far the limit
cycle pushes into saturation) and, with the ramp time, the attack (real
players ramp loop gain through critical slowly — bow pressure, breath).

r4 therefore: MEASURE each junction's critical drive by bisection (probe
renders, oscillation = tail RMS), then sweep exactly two axes per junction —
offset above critical and attack time through critical — everything else
frozen. Junction sources: the four nature archetypes + Matt's picks
(patches/scratch/"loop sweep 3", read-only; family names his, "could have
called them all hybrid").

Usage:
  python tools/gen_feedback_sweep_r4.py measure <picks_dir> <out_json>
  python tools/gen_feedback_sweep_r4.py sweep   <criticals_json> <out_dir>
Renders probes via mforce_cli directly (measure mode is its own driver).
"""
import glob
import json
import os
import struct
import subprocess
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_feedback_sweep_r3 import ANCHORS, build_patch, flatten_curve  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")

OFFSETS = [0.03, 0.10, 0.30]       # drive = crit * (1 + offset)
ATTACKS = [0.03, 0.12, 0.40]       # seconds of ramp through critical
NOTE_DUR = 3.0                     # near-critical blooms are slow by design


# ------------------------------------------------------------- extraction ---
def junction_from_patch(path):
    """Pull the junction + frozen-context params out of an r3-style patch.
    Reads only; scratch files are Matt's."""
    p = json.load(open(path))
    nodes = {n["id"]: n for n in p["graph"]["nodes"]}
    jx = nodes["junction"]["params"]
    kt = nodes["keytrack"]["params"]["exprKnots"][0]
    res = nodes["damp"]["params"].get("resonance", 0.7)
    if isinstance(res, dict):
        res = 0.7                   # modulated resonance: freeze to default
    hiss = nodes["hiss"]["params"].get("amplitude", 0.005)
    if isinstance(hiss, dict):
        hiss = 0.01                 # modulated hiss (flute001's ASEnvelope)
    sm = jx.get("smoothness", 0.5)
    if isinstance(sm, dict):
        sm = 0.5
    return {
        "curve": jx["values"],
        "smoothness": sm,
        "m": kt.get("a", 6.0),
        "res": min(float(res), 1.0),   # r3 verdict: res >= 1.2 squeaks
        "hiss": float(hiss),
        "contact": "exsum" in nodes,
        "note": p["score"][0]["note"] if p.get("score") else 48,
    }


def all_junctions(picks_dir):
    js = {}
    for name, a in ANCHORS.items():
        js["arch_" + name] = {
            "curve": flatten_curve(a["pos"], a["neg"]),
            "smoothness": a["smoothness"], "m": a["m"], "res": a["res"],
            "hiss": a["hiss"], "contact": a["contact"],
            "note": 48 if a["contact"] else 41,
        }
    for f in sorted(glob.glob(os.path.join(picks_dir, "*.json"))):
        js[os.path.splitext(os.path.basename(f))[0]] = junction_from_patch(f)
    return js


# ------------------------------------------------------------ measurement ---
def probe_patch(j, drive):
    """Constant-drive probe: does this junction oscillate at `drive`?
    Flat drive (min==max), 0.9 s note, oscillation judged on the tail."""
    return build_patch(
        "critical-drive probe", j["curve"], j["smoothness"], drive,
        j["m"], j["res"], j["hiss"], j["contact"],
        [{"note": j["note"], "time": 0.0, "duration": 0.9, "velocity": 0.8}],
        noise_seed=774242, drive_attack=0.01)


def tail_rms(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    tail = raw[-int(0.3 * 48000) * ch:]
    return (sum(s * s for s in tail) / len(tail)) ** 0.5 / 32767.0


def oscillates(j, drive, scratch):
    pj = os.path.join(scratch, "probe.json")
    pw = os.path.join(scratch, "probe.wav")
    patch = probe_patch(j, drive)
    # Flat drive: envelope min = max = drive (attack/release become inert).
    for n in patch["graph"]["nodes"]:
        if n["id"] == "driveenv":
            n["params"]["minValue"] = drive
    json.dump(patch, open(pj, "w"))
    if os.path.exists(pw):
        os.remove(pw)
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    if r.returncode != 0 or not os.path.exists(pw):
        return False
    return tail_rms(pw) > 0.02


def measure_critical(j, scratch):
    lo, hi = 0.05, 6.0
    if not oscillates(j, hi, scratch):
        return None                     # never oscillates in range
    if oscillates(j, lo, scratch):
        return lo                       # oscillates even at floor drive
    for _ in range(10):
        mid = (lo + hi) / 2
        if oscillates(j, mid, scratch):
            hi = mid
        else:
            lo = mid
    return round(hi, 4)


def run_measure(picks_dir, out_json):
    scratch = os.path.join(ROOT, "renders", "scratch")
    os.makedirs(scratch, exist_ok=True)
    js = all_junctions(picks_dir)
    out = {}
    for name, j in js.items():
        crit = measure_critical(j, scratch)
        out[name] = {"junction": j, "critical": crit}
        print(f"{name}: critical drive = {crit}", flush=True)
    json.dump(out, open(out_json, "w"), indent=1)
    n_ok = sum(1 for v in out.values() if v["critical"])
    print(f"{n_ok}/{len(out)} junctions have a measurable critical -> {out_json}")


# ------------------------------------------------------------------ sweep ---
def run_sweep(criticals_json, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    crits = json.load(open(criticals_json))
    manifest = {"skeleton": "r4 critical-relative", "criticals": criticals_json,
                "offsets": OFFSETS, "attacks": ATTACKS, "variants": []}
    i = 0
    for name, entry in sorted(crits.items()):
        crit, j = entry["critical"], entry["junction"]
        if not crit:
            print(f"skip {name}: no critical in range")
            continue
        for off in OFFSETS:
            for atk in ATTACKS:
                drive = round(crit * (1.0 + off), 4)
                cell = f"fb4_{i:03d}_{name}_o{int(off*100):02d}_a{int(atk*1000):03d}"
                patch = build_patch(
                    f"r4 cell: junction '{name}' at {off:+.0%} above critical "
                    f"({crit}), {atk*1000:.0f} ms ramp through it",
                    j["curve"], j["smoothness"], drive, j["m"], j["res"],
                    j["hiss"], j["contact"],
                    [{"note": j["note"], "time": 0.0, "duration": NOTE_DUR,
                      "velocity": 0.8}],
                    noise_seed=774300 + i,
                    drive_attack=atk / NOTE_DUR)   # fraction timeMode
                # Ramp starts just UNDER critical: a 0.6*crit start spends
                # nearly the whole ramp below threshold where nothing is
                # audible, and the measured attack collapsed to the
                # offset-set bloom rate alone (first r4 render: medians
                # 87/75/100 ms across 30/120/400 ms ramps). Starting at
                # 0.95*crit puts the ramp time into the audible approach.
                for n in patch["graph"]["nodes"]:
                    if n["id"] == "driveenv":
                        n["params"]["minValue"] = round(0.95 * crit, 4)
                        n["params"]["decay"] = 0.0
                json.dump(patch, open(os.path.join(out_dir, cell + ".json"),
                                      "w"), indent=1)
                manifest["variants"].append(
                    {"id": cell, "file": cell + ".wav",
                     "patch": cell + ".json",
                     "params": {"junction": name, "critical": crit,
                                "offset": off, "attack_s": atk,
                                "drive": drive, "note": j["note"],
                                "m": j["m"], "res": j["res"],
                                "hiss": j["hiss"]}})
                i += 1
    json.dump(manifest, open(os.path.join(out_dir, "manifest.json"), "w"),
              indent=1)
    print(f"{i} cells -> {out_dir}")


def main():
    mode = sys.argv[1]
    if mode == "measure":
        run_measure(sys.argv[2], sys.argv[3])
    elif mode == "sweep":
        run_sweep(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit("mode must be 'measure' or 'sweep'")


main()
