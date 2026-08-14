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
Keys consumed by hand-written branches (patch_loader.cpp) or by JsonConfigurator
lambdas (source_registrations.cpp) are invisible to the descriptor sets, so they
must be allow-listed. Those are now SCRAPED FROM THE SOURCES at lint time rather
than hand-copied, because the hand-copied list drifted exactly as predicted:
run 22 added Envelope "timeMode" (the absolute-seconds chuff fix), nobody
updated the list, and the linter then reported 15 false positives across the
piano template and every ks_piano patch -- including the v5 set queued for
Matt's ears. Proven live, not argued: deleting "timeMode" from v5a_desc moves
the render peak 0.601 -> 0.018.

MANUAL_KEYS below is only the residue the scraper cannot see (keys reached by
`at()`, iteration, or a helper). The selftest reports any entry the scraper
already covers, so the residue shrinks instead of rotting.

Usage:
  python tools/lint_patches.py [root_dir]     # default: patches/
  python tools/lint_patches.py --selftest     # verify it catches a known case
"""
import json
import os
import re
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
# Both are now SCRAPED at lint time (scrape_special_keys) instead of copied by
# hand. Run 25 found fifteen more false positives after run 22 added Envelope
# "timeMode" without touching the old hand-copied list.
# Global rather than per-type so a stale entry can only hide a warning, never
# invent one.
SCRAPE_SOURCES = ("engine/src/patch_loader.cpp",
                  "engine/src/source_registrations.cpp",
                  # Shared preset-form Envelope dispatch — the adsr/ar keys
                  # moved here from patch_loader.cpp 2026-08-13.
                  "engine/include/mforce/core/envelope_json.h")

# `p.value("k", ...)`, `p.contains("k")`, `p.at("k")`, `p["k"]`, and the
# `for (const char* k : {"a","b"})` loops patch_loader uses.
# `.at(` matters as much as `.value(`: AdditiveSource2's evolving/envelope
# branches read startPartials/endAmplitudes/amplitudes exclusively through
# at(), so omitting it cost four more false positives on the as2_* patches.
_SCRAPE_RE = re.compile(
    r'(?:\.(?:value|contains|at)\(\s*"([A-Za-z_][A-Za-z0-9_]*)"'
    r'|\[\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\])')
# String literals inside a braced initialiser list of const char* keys.
_KEYLIST_RE = re.compile(
    r'const\s+char\*\s+\w+\s*:\s*\{([^}]*)\}', re.S)
_LIT_RE = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*)"')

# Residue the scraper cannot see: keys reached via at(), iteration, or a helper
# rather than a literal value()/contains()/[] call. Keep this SHORT -- the
# selftest prints anything here the scraper already covers.
MANUAL_KEYS = {
    "sections",     # filter section list, reached by iteration
    "oneShot",      # SegmentSource
}
# Pruned run 25: formants, values, partialCount, partialMode, absolute,
# normalized, depthVar and speedVar were all in the old hand-copied list and
# are all found by the scraper, so keeping them by hand bought nothing.


def scrape_special_keys(include_manual=True):
    """JSON keys consumed by hand-written loader code, read from the sources.

    Global rather than per-type, so a spurious entry can only hide a warning,
    never invent one. include_manual=False returns ONLY what the sources
    actually yield, which is how the selftest spots prunable MANUAL_KEYS.
    """
    keys = set(MANUAL_KEYS) if include_manual else set()
    for rel in SCRAPE_SOURCES:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            raise SystemExit(f"lint_patches: missing {rel} -- cannot build the "
                             f"allowlist, refusing to report false positives")
        text = open(path, encoding="utf-8", errors="replace").read()
        for a, b in _SCRAPE_RE.findall(text):
            keys.add(a or b)
        for block in _KEYLIST_RE.findall(text):
            keys.update(_LIT_RE.findall(block))
    keys.discard("params")      # the container itself, not a param
    return keys


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


def lint_file(path, desc, special):
    """Yield (node_id, type, key) for every silently-ignored param key."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            patch = json.load(f)
    except (OSError, ValueError):
        return
    if not isinstance(patch, dict):
        return  # not a patch (e.g. a bare JSON list of presets)
    # instrument.release retired 2026-08-13 (note-contained sound): release
    # is the final stage of the patch's envelope; the loader ignores the key.
    inst = patch.get("instrument")
    if isinstance(inst, dict) and "release" in inst:
        yield "instrument", "instrument", "release (retired 2026-08-13)"
    # Curve-endpoint convention (Matt 2026-08-13): paramMap curves span the
    # full domain (20 Hz..16 kHz frequency, 0..1 velocity) with clamps as
    # EXPLICIT repeated endpoint values — an implicit end-clamp is how the
    # unscored exc_level top anchor blasted octave 8 by +24 dB unseen.
    if isinstance(inst, dict):
        pm = inst.get("paramMap")
        entries = pm.get("frequency", []) if isinstance(pm, dict) else []
        for e in entries:
            if not isinstance(e, dict):
                continue
            tgt = e.get("target", "?")
            c = e.get("curve")
            if isinstance(c, list) and c:
                if c[0][0] > 20.0 or c[-1][0] < 16000.0:
                    yield "paramMap", tgt, ("curve endpoints %g..%g (want 20..16000)"
                                            % (c[0][0], c[-1][0]))
            v = e.get("vcurve")
            if isinstance(v, list) and v:
                if v[0][0] > 0.0 or v[-1][0] < 1.0:
                    yield "paramMap", tgt, ("vcurve endpoints %g..%g (want 0..1)"
                                            % (v[0][0], v[-1][0]))
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
            if key not in ok and key not in special:
                yield node.get("id", "?"), tname, key


def selftest(desc, special):
    """The linter must flag a known-bad key and stay quiet on a known-good one."""
    import tempfile
    bad = {"graph": {"nodes": [{"id": "vn", "type": "VelvetNoiseSource",
                                "params": {"frequency": 20.0, "amplitude": 1.0}}]}}
    good = {"graph": {"nodes": [{"id": "vn", "type": "VelvetNoiseSource",
                                 "params": {"density": 20.0, "amplitude": 1.0}}]}}
    ok = True
    print("  scraped %d allowlist key(s) from %d source file(s)"
          % (len(special), len(SCRAPE_SOURCES)))
    for k in ("timeMode", "releaseMax", "seed"):
        print("  scraped contains %-11s %s" % (k, k in special))
    redundant = sorted(MANUAL_KEYS & scrape_special_keys(include_manual=False))
    if redundant:
        print("  MANUAL_KEYS the scraper already covers (prunable): %s"
              % ", ".join(redundant))
    with tempfile.TemporaryDirectory() as d:
        for name, doc, want in (("bad", bad, 1), ("good", good, 0)):
            p = os.path.join(d, name + ".json")
            with open(p, "w", encoding="utf-8") as f:
                json.dump(doc, f)
            got = len(list(lint_file(p, desc, special)))
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
    special = scrape_special_keys()
    print("loaded %d source types from the engine; %d hand-written keys "
          "scraped from the loader sources" % (len(desc), len(special)))

    if "--selftest" in args:
        print("selftest:")
        return 0 if selftest(desc, special) else 1

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
            for nid, tname, key in lint_file(p, desc, special):
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
