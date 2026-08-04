#!/usr/bin/env python3
"""Find patch params the loader silently ignores.

`wire_params_generic` iterates the engine's DESCRIPTORS and picks matching JSON
keys. Any key that matches no descriptor is dropped without a word -- so a typo
or a param borrowed from the wrong source type renders as if it were never
written, and the patch looks fine.

That is not hypothetical: t3_23_phase_velvet_jumps set "frequency" on a
VelvetNoiseSource (whose params are density/amplitude) and sat in a review batch
doing nothing (dsp run 19, 2026-08-04).

Truth comes from the engine itself via `mforce_cli --dump-descriptors`, not from
a list maintained here -- a hand-copied list is exactly the thing that drifts.
The one thing this file does own is SPECIAL_KEYS: keys consumed by hand-written
branches in patch_loader.cpp rather than by a descriptor loop. Those are
allow-listed globally (not per type) so the linter under-reports rather than
cries wolf.

Usage:
  python tools/lint_patches.py [root_dir]     # default: patches/
  python tools/lint_patches.py --selftest     # verify it catches a known case
"""
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

# Keys consumed by hand-written code rather than by a descriptor loop. TWO
# sources, and missing either one makes this tool cry wolf:
#   1. patch_loader.cpp        -- per-type branches in build_graph
#   2. source_registrations.cpp -- JsonConfigurator lambdas, which run for every
#      registered type and are NOT visible in any descriptor set. Skipping this
#      file cost two false positives on first run (RepeatingSource "gap" and
#      PhasedValueSource "overlap" are both correct JSON consumed by their
#      configurators; note "gap" sets a member called gapDuration, so the JSON
#      key and the config descriptor name legitimately differ).
# Re-extract with:
#   grep -oE '\b(p|params)\.(value|contains)\("[a-zA-Z_]+"' \
#     engine/src/patch_loader.cpp engine/src/source_registrations.cpp
# Global rather than per-type so a stale entry can only hide a warning, never
# invent one.
SPECIAL_KEYS = {
    # universal / structural
    "seed", "preset", "source", "expandRule",
    # Envelope
    "stages", "stage_accuracy", "ramp_accuracy",
    "attack", "attackMin", "attackMax", "decay", "release", "sustainLevel",
    # SegmentSource
    "values", "oneShot",
    # WavetableSource evolution family
    "evolution", "evolutionSeed", "muting", "sampleCount", "speed",
    "decayFactor", "leading", "autoAdjust", "targetWave", "targetPartials",
    "targetLength", "interpolate", "morphDuration", "morphRate",
    "zeroCrossTendency", "holdCycles", "partialMode", "partialCount",
    # partials / spectrum / formants
    "formants", "gains", "numPartials", "absolute", "normalized",
    # filters / misc
    "cutoff", "sections", "threshold", "depthVar", "speedVar",
    # JsonConfigurator lambdas in source_registrations.cpp
    "baseValue", "bias", "durVarPct", "duration", "gainAdj", "gap",
    "gapVarPct", "max", "min", "operation", "overlap", "ratio", "varPct",
}


def load_descriptors():
    out = subprocess.run([CLI, "--dump-descriptors"], capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("--dump-descriptors failed:\n" + out.stderr)
    return json.loads(out.stdout)


def accepted_keys(desc, type_name):
    e = desc.get(type_name)
    if e is None:
        return None
    return set(e["params"]) | set(e["configs"]) | set(e["arrays"]) | set(e["inputs"])


def lint_file(path, desc):
    """Yield (node_id, type, key) for every silently-ignored param key."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            patch = json.load(f)
    except (OSError, ValueError):
        return
    if not isinstance(patch, dict):
        return  # not a patch (e.g. a bare JSON list of presets)
    graph = patch.get("graph")
    if not isinstance(graph, dict):
        return
    for node in graph.get("nodes", []):
        if not isinstance(node, dict):
            continue
        tname = node.get("type")
        params = node.get("params")
        if not isinstance(params, dict) or not tname:
            continue
        ok = accepted_keys(desc, tname)
        if ok is None:
            continue  # type handled entirely outside the registry
        for key in params:
            if key not in ok and key not in SPECIAL_KEYS:
                yield node.get("id", "?"), tname, key


def selftest(desc):
    """The linter must flag a known-bad key and stay quiet on a known-good one."""
    import tempfile
    bad = {"graph": {"nodes": [{"id": "vn", "type": "VelvetNoiseSource",
                                "params": {"frequency": 20.0, "amplitude": 1.0}}]}}
    good = {"graph": {"nodes": [{"id": "vn", "type": "VelvetNoiseSource",
                                 "params": {"density": 20.0, "amplitude": 1.0}}]}}
    ok = True
    with tempfile.TemporaryDirectory() as d:
        for name, doc, want in (("bad", bad, 1), ("good", good, 0)):
            p = os.path.join(d, name + ".json")
            with open(p, "w", encoding="utf-8") as f:
                json.dump(doc, f)
            got = len(list(lint_file(p, desc)))
            print("  selftest %-5s expected %d finding(s), got %d  %s"
                  % (name, want, got, "OK" if got == want else "FAIL"))
            ok = ok and got == want
    return ok


def report_duplicates(root):
    """Group patches whose graph content is identical modulo key order.

    A sweep cell that duplicates another cell costs a render and a review slot
    while adding no information (run 18 lost an expand cell to exactly this).
    Duplicates are NOT automatically bugs -- gen_fm_matrix2 defines the `med`
    rung of its lo/med/hi bracket AS the original patch, so those pairs are
    intentional. This only reports; triage is a human read.
    """
    import collections
    groups, n = collections.defaultdict(list), 0
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith(".json"):
                continue
            p = os.path.join(dirpath, fn)
            try:
                with open(p, "r", encoding="utf-8") as f:
                    d = json.load(f)
            except (OSError, ValueError):
                continue
            if not isinstance(d, dict) or "graph" not in d:
                continue
            n += 1
            import hashlib
            key = hashlib.sha256(json.dumps(d, sort_keys=True).encode()).hexdigest()
            groups[key].append(os.path.relpath(p, ROOT))
    dups = [v for v in groups.values() if len(v) > 1]
    print("scanned %d patches; %d duplicate group(s) covering %d files\n"
          % (n, len(dups), sum(len(v) for v in dups)))
    for v in sorted(dups, key=len, reverse=True):
        print("  %d identical:" % len(v))
        for f in v:
            print("      %s" % f)
    return 0


def main():
    args = sys.argv[1:]
    desc = load_descriptors()
    print("loaded %d source types from the engine" % len(desc))

    if "--selftest" in args:
        print("selftest:")
        return 0 if selftest(desc) else 1

    if "--dups" in args:
        return report_duplicates(os.path.join(ROOT, "patches"))

    root = os.path.join(ROOT, args[0]) if args else os.path.join(ROOT, "patches")
    findings, nfiles = [], 0
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith(".json"):
                continue
            nfiles += 1
            p = os.path.join(dirpath, fn)
            for nid, tname, key in lint_file(p, desc):
                findings.append((os.path.relpath(p, ROOT), nid, tname, key))

    print("scanned %d patch files under %s\n" % (nfiles, os.path.relpath(root, ROOT)))
    if not findings:
        print("PASS: no silently-ignored params")
        return 0

    for relp, nid, tname, key in findings:
        print("  %s\n      node %-16s %-22s ignores %r" % (relp, nid, tname, key))
    print("\n%d silently-ignored param(s) in %d file(s)"
          % (len(findings), len({f[0] for f in findings})))
    return 1


if __name__ == "__main__":
    sys.exit(main())
