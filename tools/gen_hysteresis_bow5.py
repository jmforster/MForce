"""Bow round 5 - capture-band hunt for the period-1 regime (Matt go,
2026-09-11). Reuses gen_hysteresis_bow4's machinery with three changes:
delay RATIO 1.0 (loop at f0 - if a one-slip-per-trip cycle exists, the
note is the loop's OWN mode, no subharmonic architecture), CAPTURE > 0
swept for the first time in the biased regime (cap 0 forces zero-cross
recapture = the two-trip cycle; a capture band lets the junction
re-stick on the SAME trip), and fast gain attack (ga 0.05 - round 4's
slow traversal was the wrong direction).

Axes: CAP {0.10, 0.25, 0.40} x BIAS_RATIO {0.6, 0.9} at sustain gain
1.40 x critical. Note cap 0.40 > bias 0.36 (v060 cells) = the
instant-recapture regime, deliberately included.

Success metric (why this round exists): C4 sustain peak/f0 = 1.0 means
period-1 found (native f0, octave architecture gone); 0.5 means
period-2 persists and the retrofit ceiling is named with data.

Outputs: the usual {patches,renders}/{sweep,audition}/hysteresis_bow5/.
Usage:  python tools/gen_hysteresis_bow5.py
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "gen_hysteresis_bow4.py")).read()
_src = _src.replace("\nmain()\n", "\n")
B4 = {"__name__": "bow4", "__file__": os.path.join(HERE, "gen_hysteresis_bow4.py")}
exec(compile(_src, "gen_hysteresis_bow4.py", "exec"), B4)

ROOT = B4["ROOT"]
CLI = B4["CLI"]
B4["RATIO"] = 1.0            # loop at f0 - the whole point of the round
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "hysteresis_bow5")
PATCH_AUD = os.path.join(ROOT, "patches", "audition", "hysteresis_bow5")
SWEEP_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "hysteresis_bow5")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "hysteresis_bow5")

CAP = {"c10": 0.10, "c25": 0.25, "c40": 0.40}
BIAS_RATIO = {"v060": 0.6, "v090": 0.9}
SUSTAIN_GAIN = 1.40
GA = 0.05                    # fast gain attack (round-4 lesson)


def cell_patch(base, br, cap):
    B4["BIAS"] = round(br * B4["BA"], 4)
    p = B4["make_patch"](base, GA, 12.0)     # dcblock stock 12 Hz
    B4["nodes_by_id"](p)["Junction"]["params"]["capture"] = cap
    return p


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
    base = json.load(open(B4["BASE"]))

    # Criticals per bias (capture is inert below breakaway; probe's
    # oscillates() builds with the CURRENT B4["BIAS"], cap 0 - fine for
    # threshold purposes at ratio 1.0).
    crits = {}
    for vtag, br in BIAS_RATIO.items():
        B4["BIAS"] = round(br * B4["BA"], 4)
        crits[vtag] = B4["measure_critical"](base, 12.0, scratch)
        print(f"crit {vtag} (ratio 1.0): {crits[vtag]}", flush=True)

    manifest = {"round": "hysteresis_bow5 capture-band period-1 hunt",
                "ratio": 1.0, "sustain_gain": SUSTAIN_GAIN, "ga": GA,
                "cap": CAP, "bias_ratio": BIAS_RATIO, "criticals": crits,
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
        return B4["normalize_copy"](pw, os.path.join(AUD_OUT, cell + ".wav"))

    for vtag, br in BIAS_RATIO.items():
        crit = crits[vtag]
        if crit is None:
            manifest["variants"].append({"id": vtag, "ok": False,
                                         "skip": "no critical"})
            continue
        for ctag, cap in CAP.items():
            cell = f"hyb5_{vtag}_{ctag}"
            p = cell_patch(base, br, cap)
            B4["set_loopgain"](p, SUSTAIN_GAIN * crit)
            gn = render(p, cell)
            if gn is None:
                failures += 1
            manifest["variants"].append(
                {"id": cell, "ok": gn is not None,
                 "params": {"bias": round(br * B4["BA"], 4), "cap": cap,
                            "critical": crit,
                            "loop_gain": round(SUSTAIN_GAIN * crit, 4)}})

    if render(B4["make_patch"](base, None, None), "x_control",
              audition_patch=False) is None:
        failures += 1

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
