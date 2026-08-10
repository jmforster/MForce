"""Migrate pre-decomposition AdditiveSource params onto a FullPartials node.

dsp backlog 14 group (a). Four `add_*_test` patches set evenWeight / oddWeight /
rolloff / freqVar* / amplVar* directly on an AdditiveSource. Those keys moved to
the Partials node in the 2026-05-30 decomposition and the loader has dropped
them silently ever since -- none of these four patches even wires a `partials`
node, so they have been rendering default partials the whole time. (The
2026-05-30 "no remaining debt" note was wrong.)

Mapping. Note both endpoints are written, not just `_1`: FullPartials
interpolates `_1 -> _2` across the partial range and BOTH default to 1.0, so
setting `evenWeight1` alone would produce a sweep rather than the flat legacy
value.

    evenWeight  -> evenWeight1 = evenWeight2
    oddWeight   -> oddWeight1  = oddWeight2
    rolloff     -> rolloff1    = rolloff2
    freqVarPct  -> motionDepth1 = motionDepth2,  freqVarSpeed -> motionHz
    amplVarPct  -> shimmerDepth1 = shimmerDepth2, amplVarSpeed -> shimmerHz

The motion/shimmer half of that mapping is an INFERENCE from the legacy names,
not a documented equivalence -- flagged in the run-25 report. It only affects
add_string_test, the one patch with nonzero variation; the other three have all
four at 0.0, where the keys are inert and are simply dropped.

NOT migrated, deliberately:
  - patches/Squeaker.json -- its evenWeight/oddWeight are {"ref": ...} value
    sources. FullPartials exposes them as ConfigType::Float configs, so a
    modulated even/odd weight has no equivalent. That is a real capability
    gap, not a patch defect; backlogged rather than silently flattened.

Usage:  python tools/migrate_legacy_additive_params.py [--apply]
Without --apply it prints the plan and changes nothing.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

TARGETS = ["add_organ_test", "add_saw_test", "add_square_test",
           "add_string_test"]

# legacy key -> (FullPartials keys to receive the value)
PAIRED = {
    "evenWeight": ("evenWeight1", "evenWeight2"),
    "oddWeight": ("oddWeight1", "oddWeight2"),
    "rolloff": ("rolloff1", "rolloff2"),
    "freqVarPct": ("motionDepth1", "motionDepth2"),
    "amplVarPct": ("shimmerDepth1", "shimmerDepth2"),
}
SINGLE = {"freqVarSpeed": "motionHz", "amplVarSpeed": "shimmerHz"}
# dropped when zero (inert in the legacy engine too)
DROP_IF_ZERO = {"freqVarPct", "freqVarSpeed", "amplVarPct", "amplVarSpeed"}


def migrate(path, apply):
    p = json.loads(path.read_text())
    nodes = p["graph"]["nodes"]
    changed = False
    for node in nodes:
        if node.get("type") != "AdditiveSource":
            continue
        params = node.get("params", {})
        legacy = {k: params[k] for k in list(PAIRED) + list(SINGLE)
                  if k in params}
        if not legacy:
            continue
        if any(isinstance(v, dict) for v in legacy.values()):
            print(f"  SKIP {path.name}: node {node['id']} has a modulated "
                  f"legacy weight (no FullPartials equivalent)")
            continue
        if "partials" in params:
            print(f"  SKIP {path.name}: node {node['id']} already wires "
                  f"partials -- needs a hand read")
            continue

        cfg = {"maxPartials": 64, "minMult": 1}
        for k, (a, b) in PAIRED.items():
            if k not in legacy:
                continue
            v = float(legacy[k])
            if k in DROP_IF_ZERO and v == 0.0:
                continue
            cfg[a] = v
            cfg[b] = v
        for k, dest in SINGLE.items():
            if k not in legacy:
                continue
            v = float(legacy[k])
            if k in DROP_IF_ZERO and v == 0.0:
                continue
            cfg[dest] = v

        pid = f"{node['id']}_partials"
        for k in legacy:
            del params[k]
        params["partials"] = {"ref": pid}
        nodes.insert(nodes.index(node),
                     {"id": pid, "type": "FullPartials", "params": cfg})
        dropped = sorted(k for k in legacy
                         if k in DROP_IF_ZERO and float(legacy[k]) == 0.0)
        print(f"  {path.name}: node {node['id']} -> +{pid} "
              f"{ {k: v for k, v in cfg.items() if k not in ('maxPartials', 'minMult')} }")
        if dropped:
            print(f"      dropped as inert (were 0.0): {', '.join(dropped)}")
        changed = True

    if changed and apply:
        path.write_text(json.dumps(p, indent=2) + "\n")
    return changed


def main():
    apply = "--apply" in sys.argv
    print("APPLYING" if apply else "DRY RUN (pass --apply to write)")
    n = 0
    for name in TARGETS:
        path = ROOT / "patches" / f"{name}.json"
        if not path.exists():
            print(f"  MISSING {name}.json")
            continue
        if migrate(path, apply):
            n += 1
    print(f"\n{n} patch(es) {'migrated' if apply else 'would be migrated'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
