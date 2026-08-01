#!/usr/bin/env python3
"""Cost of each optional additive layer on a REAL instrument patch (item 8).

tools/ablate_additive.py measures the partial loop on the prof patches, which
have every optional layer OFF. That is the configuration a vectorized fast path
covers most easily — and it is NOT the configuration the flagship patches use.
viola_default and the v6 CMA-ES target run motion + shimmer + onset + bandwidth
together, and bandwidth carries a per-partial walk off a SHARED rng, which is
the one part of the body that does not vectorize without changing the noise
realization.

So before sizing a SIMD stage, this asks: how much of the loop do those layers
actually cost? It zeroes one layer at a time in a COPY of the patch and
compares. No engine change, no rebuild.

Instrument+score patches pre-render at LOAD, so `load=` is the render cost for
them (see item 8 stage 2c) — this reads whichever of load=/render= is larger.

Usage: python tools/ablate_layers.py [patch.json] [reps]
"""
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
WAV = os.path.join(ROOT, "renders", "nulltest", "_layers.wav")

RE_LOAD = re.compile(r"load=([\d.]+)ms")
RE_RNDR = re.compile(r"render=([\d.]+)ms")

# layer name -> params to zero (absent params are simply skipped)
LAYERS = {
    "motion":    ["motionDepth1", "motionDepth2"],
    "shimmer":   ["shimmerDepth1", "shimmerDepth2"],
    "trade":     ["tradeDepth"],
    "onset":     ["onsetSpread"],
    "bandwidth": ["bandwidth1", "bandwidth2"],
}


def zero_params(node, names, hits):
    """Recursively zero every occurrence of `names` anywhere in the patch."""
    if isinstance(node, dict):
        for k, v in list(node.items()):
            if k in names and isinstance(v, (int, float)):
                node[k] = 0.0
                hits.append(k)
            else:
                zero_params(v, names, hits)
    elif isinstance(node, list):
        for v in node:
            zero_params(v, names, hits)


def timed(path, reps):
    best = None
    for _ in range(reps):
        r = subprocess.run([CLI, path, WAV], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"render failed for {path}:\n{r.stderr[-600:]}")
        blob = r.stderr + r.stdout
        ml, mr = RE_LOAD.search(blob), RE_RNDR.search(blob)
        if not (ml or mr):
            sys.exit("no timing in output:\n" + blob[:400])
        t = max(float(ml.group(1)) if ml else 0.0,
                float(mr.group(1)) if mr else 0.0)
        best = t if best is None else min(best, t)
    return best


def main():
    patch = sys.argv[1] if len(sys.argv) > 1 else "patches/viola_default.json"
    reps = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    src = os.path.join(ROOT, patch)
    base_doc = json.load(open(src, encoding="utf-8"))

    os.makedirs(os.path.dirname(WAV), exist_ok=True)
    tmpdir = tempfile.mkdtemp(prefix="layers_")

    full = timed(src, reps)
    print(f"patch: {patch}   (min of {reps} renders, ms)")
    print(f"{'variant':14s} {'ms':>9} {'delta':>9} {'share':>8}")
    print("-" * 44)
    print(f"{'full':14s} {full:>9.1f} {'':>9} {'':>8}")

    results = {}
    for name, params in LAYERS.items():
        doc = json.loads(json.dumps(base_doc))
        hits = []
        zero_params(doc, set(params), hits)
        if not hits:
            print(f"{name:14s} {'--':>9}   (not present in this patch)")
            continue
        p = os.path.join(tmpdir, f"{name}_off.json")
        json.dump(doc, open(p, "w", encoding="utf-8"))
        t = timed(p, reps)
        results[name] = t
        d = full - t
        print(f"{name+'  off':14s} {t:>9.1f} {d:>9.1f} {100.0*d/full:>7.1f}%")

    if results:
        doc = json.loads(json.dumps(base_doc))
        allp = set(p for ps in LAYERS.values() for p in ps)
        hits = []
        zero_params(doc, allp, hits)
        p = os.path.join(tmpdir, "all_off.json")
        json.dump(doc, open(p, "w", encoding="utf-8"))
        t = timed(p, reps)
        d = full - t
        print("-" * 44)
        print(f"{'ALL off':14s} {t:>9.1f} {d:>9.1f} {100.0*d/full:>7.1f}%")
        print(f"\nA vector fast path that cannot handle these layers would run "
              f"on the\n'ALL off' configuration only — {100.0*t/full:.0f}% of "
              f"this patch's loop cost is\nreachable by it, the rest sits "
              f"behind the layers.")


if __name__ == "__main__":
    main()
