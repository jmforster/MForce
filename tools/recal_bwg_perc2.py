"""bwg_perc2 loudness recalibration (REVIEW 62 — Matt 09-16: "so quiet I
can barely hear it at 100%"). The round-2 generator never calibrated
output level; queue cells sat ~20 dB under the library patches.

Fix, house convention (gen_trombone2): measure the library reference
(oboe1) sounding rms through the same CLI, then per cell solve a trim
that lands the cell's sounding rms on the reference, capped so peak
stays under 0.90. The trim is BAKED INTO THE PATCH (the Out node's
constant), so live play in the UI matches the WAV, then the cell is
re-rendered and verified.

Usage: python tools/recal_bwg_perc2.py
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bandedwg as g

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_DIR = os.path.join(ROOT, "patches", "audition", "bwg_perc2")
REND_DIR = os.path.join(ROOT, "renders", "dsp", "audition", "bwg_perc2")
LIB_REF_PATCH = os.path.join(ROOT, "patches", "library", "winds",
                             "oboe1.json")
LIB_REF_FALLBACK = 0.1761
PEAK_CEIL = 0.90
SCRATCH = os.path.join(ROOT, "renders", "scratch", "bwg_perc2_recal")


def rms(x):
    return float(np.sqrt(np.mean(x ** 2) + 1e-20))


def sounding_rms(x):
    nz = x[np.abs(x) > 1e-4]
    return rms(nz) if len(nz) else 0.0


def library_reference():
    wp = os.path.join(SCRATCH, "lib_ref.wav")
    r = subprocess.run([CLI, LIB_REF_PATCH, wp], capture_output=True,
                       text=True)
    if r.returncode != 0 or not os.path.exists(wp):
        print("  library reference render FAILED - using recorded %.4f"
              % LIB_REF_FALLBACK)
        return LIB_REF_FALLBACK
    x, _ = g.read_mono(wp)
    return sounding_rms(x)


def main():
    os.makedirs(SCRATCH, exist_ok=True)
    libref = library_reference()
    print("library reference (oboe1) sounding rms %.4f" % libref)
    rows = []
    for fn in sorted(os.listdir(PATCH_DIR)):
        if not fn.endswith(".json"):
            continue
        name = fn[:-5]
        ppath = os.path.join(PATCH_DIR, fn)
        wpath = os.path.join(REND_DIR, name + ".wav")
        with open(ppath) as f:
            patch = json.load(f)
        out = next(n for n in patch["graph"]["nodes"] if n["id"] == "Out")
        # Out is mul(sum, constant) — the constant is params["source2"]
        # (gen_stk_bandedwg.mul spells a scalar operand inline).
        base = out["params"]["source2"]
        if not isinstance(base, (int, float)):
            print("%-16s SKIP: Out has no scalar gain (%r)" % (name, base))
            continue
        # pass 1: current loudness
        w0 = os.path.join(SCRATCH, name + "_pre.wav")
        r = subprocess.run([CLI, ppath, w0], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(w0):
            print("%-16s RENDER_FAIL (pre)" % name)
            continue
        x, sr = g.read_mono(w0)
        sr0, pk0 = sounding_rms(x), float(np.abs(x).max())
        want = libref / max(sr0, 1e-9)
        cap = PEAK_CEIL / max(pk0, 1e-9)
        trim = min(want, cap)
        capped_db = 20 * math.log10(want / cap) if want > cap else 0.0
        out["params"]["source2"] = round(base * trim, 8)
        with open(ppath, "w") as f:
            json.dump(patch, f, indent=1)
        # pass 2: re-render the queue WAV and verify
        r = subprocess.run([CLI, ppath, wpath], capture_output=True,
                           text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            print("%-16s RENDER_FAIL (post)" % name)
            continue
        y, _ = g.read_mono(wpath)
        sr1, pk1 = sounding_rms(y), float(np.abs(y).max())
        rows.append((name, sr0, sr1, pk1, capped_db))
        print("%-16s sounding %.4f -> %.4f (%+.1f dB)  peak %.3f%s"
              % (name, sr0, sr1, 20 * math.log10(sr1 / max(sr0, 1e-9)),
                 pk1, ("  CAPPED %.1f dB short" % capped_db)
                 if capped_db else ""))
    if rows:
        worst = min(20 * math.log10(r[2] / libref) for r in rows)
        print("\nworst cell vs library reference: %.1f dB" % worst)


if __name__ == "__main__":
    main()
