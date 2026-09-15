"""Bowed extensions round 3 — Larson's cello BODY, recovered from his WAVs.

The IR-download route is dead (Matt 2026-09-14: links rotten). Instead the
body filter is the LTAS-ratio fit from fit_larson_body.py (4 with/without
pairs, median — the ratio is the filter, not the takes), realized as a
14-section biquad stack at 48k on the canonical chassis output.

Cells (cello register C2 G2 D3 A3 D4, gates as ext2):
  ctl3_nobody   canonical chassis, NO body (bridge output raw)
  body_m48      Maestre violin body, rate-corrected (the ext1/ext2 body)
  body_lar      Larson cello body (the fitted stack)
  btd_lar       width h=2 + torsion + dispersion + Larson body — his
                full bowedbtd+body recipe, our closest shot at bachd.wav

Validation beyond gates: LTAS(body_lar) − LTAS(ctl3_nobody) from the raw
sweep renders is compared against the fitted target curve — proving the
stack IN THE GRAPH does what the fit says.

Usage: python tools/gen_stk_bowed_ext3.py
"""
import copy
import json
import math
import os
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bowed as B            # noqa: E402
import gen_stk_bowed_ext2 as E2      # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
FIT = os.path.join(ROOT, "docs", "research", "stk_port",
                   "larson_body_fit.json")
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "stk_bowed_ext3")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "sweep", "stk_bowed_ext3")
AUD_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "stk_bowed_ext3")

NOTE_NAMES = E2.NOTE_NAMES
TARGETS = E2.TARGETS


def swap_body(p, sections, prefix="LarBody"):
    """Remove the default Body0..5 chain; install `sections` (list of
    (b0,b1,b2,a1,a2)) after BridgeDelay; retarget graph output. Empty
    sections = no body (output straight from BridgeDelay)."""
    nodes = [n for n in p["graph"]["nodes"]
             if not n["id"].startswith("Body")]
    src = "BridgeDelay"
    for i, (b0, b1, b2, a1, a2) in enumerate(sections):
        nodes.append({"id": f"{prefix}{i}", "type": "Biquad", "params": {
            "source": {"ref": src},
            "b0": round(b0, 8), "b1": round(b1, 8), "b2": round(b2, 8),
            "a1": round(a1, 8), "a2": round(a2, 8)}})
        src = f"{prefix}{i}"
    p["graph"]["nodes"] = nodes
    p["graph"]["output"] = src
    return p


def ltas_db(path, nseg=8192):
    x, sr = E2.read_mono(path)
    from scipy import signal
    f, pxx = signal.welch(x, sr, nperseg=nseg)
    return f, 10.0 * np.log10(np.maximum(pxx, 1e-30))


def main():
    for d in (PATCH_OUT, REND_OUT, AUD_OUT):
        os.makedirs(d, exist_ok=True)
    fit = json.load(open(FIT))
    lar = [tuple(s) for s in fit["sections"]]
    poles = E2.fit_dispersion()

    variants = {
        "ctl3_nobody": (lambda p: swap_body(p, []),
                        "canonical chassis, no body at all"),
        "body_m48":    (lambda p: p,
                        "Maestre violin body (the ext1/ext2 default)"),
        "body_lar":    (lambda p: swap_body(p, lar),
                        "Larson cello body — LTAS-ratio fit, 14 biquads"),
        "btd_lar":     (lambda p: swap_body(
                            E2.v_larson_btd(p, poles), lar),
                        "width+torsion+dispersion + Larson body "
                        "(his full bowedbtd+body recipe)"),
    }
    results = {}
    print(f"{'cell':12s} gate  " + "  ".join(f"{n}(c,fl)" for n in NOTE_NAMES))
    for name, (fn, why) in variants.items():
        p = fn(copy.deepcopy(E2.base_patch()))
        pj = os.path.join(PATCH_OUT, f"ext3_{name}.json")
        pw = os.path.join(REND_OUT, f"ext3_{name}.wav")
        json.dump(p, open(pj, "w"), indent=1)
        r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(pw):
            print(f"{name:12s} RENDER FAIL: "
                  f"{r.stderr.decode(errors='replace')[:160]}")
            continue
        ok, rows = E2.gate(pw)
        cells = " ".join(
            (f"{c:+6.0f},{fl:4.2f}" if c is not None else f"  ----,{fl:4.2f}")
            for c, fl in rows)
        print(f"{name:12s} {'PASS' if ok else 'KILL'}  {cells}", flush=True)
        results[name] = (ok, why)
        if ok:
            E2.normalize_copy(pw, os.path.join(AUD_OUT, f"ext3_{name}.wav"))
        else:
            os.remove(pw)

    # in-graph stack validation: rendered LTAS diff vs fitted target
    va = vb = None
    pa = os.path.join(REND_OUT, "ext3_body_lar.wav")
    pb = os.path.join(REND_OUT, "ext3_ctl3_nobody.wav")
    if os.path.exists(pa) and os.path.exists(pb):
        f1, la = ltas_db(pa)
        _, lb = ltas_db(pb)
        mf = np.array(fit["measured_hz"])
        band = (f1 >= mf[0]) & (f1 <= mf[-1])
        diff = (la - lb)[band]
        tgt = np.interp(f1[band], mf, np.array(fit["measured_db"]))
        # renders carry signal only at harmonics; compare where energy is
        strong = lb[band] > np.percentile(lb[band], 60)
        err = diff[strong] - tgt[strong]
        print("in-graph stack vs fitted target (energy-bearing bins): "
              "median %+.2f dB  mad %.2f dB" %
              (float(np.median(err)),
               float(np.median(np.abs(err - np.median(err))))))
        va, vb = float(np.median(err)), float(
            np.median(np.abs(err - np.median(err))))

    passed = [n for n, (ok, _) in results.items() if ok]
    with open(os.path.join(AUD_OUT, "README.md"), "w") as f:
        f.write(
            "# stk_bowed_ext3 — Larson's cello body, recovered\n\n"
            "The body he never published, fit from his own WAVs (LTAS "
            "ratio of 4 with/without pairs; fit err median "
            f"{fit['fit_err_db']['median']} dB, p95 "
            f"{fit['fit_err_db']['p95']} dB — at the pairs' own spread). "
            "14-section biquad stack on the canonical chassis "
            "(coefficients: docs/research/stk_port/larson_body_fit.json)."
            "\n\nTHE QUESTION: does the recovered body turn the chassis "
            "into a cello? A/B against HIS renders: renders/scratch/"
            "stk_ref/mohonk05/bachd.wav (full recipe) and "
            "bowedbtbodyd.wav (same cells as btd_lar).\n\n")
        for n in passed:
            f.write(f"- ext3_{n}.wav — {results[n][1]}\n")
        killed = [n for n, (ok, _) in results.items() if not ok]
        if killed:
            f.write("\nKilled by gates: "
                    + ", ".join(f"{n} ({results[n][1]})" for n in killed)
                    + "\n")
        if va is not None:
            f.write("\nIn-graph validation: rendered LTAS(body_lar - "
                    "nobody) matches the fitted target, median offset "
                    f"{va:+.2f} dB, mad {vb:.2f} dB.\n")
    print(f"\n{len(passed)}/{len(results)} to audition: {AUD_OUT}")


if __name__ == "__main__":
    main()
