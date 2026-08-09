#!/usr/bin/env python3
"""Repair patches over-rewired by rewire_instrument_output.py.

Heuristic: if a patch has a MultiSource / CombinedSource / CrossfadeSource
combiner node, and graph.output currently points to a single source, restore
output to the combiner. The combiners DO have valid dspSources in the UI
(via the generic registry path) so they're legal output targets.

Skips patches whose output already points to a combiner or to the only
source node.
"""
import json
import sys
from pathlib import Path

COMBINER_TYPES = {"MultiSource", "CombinedSource", "CrossfadeSource", "MultiplexSource"}
SOURCE_TYPES = {
    "AdditiveSource", "AdditiveSource2", "WavetableSource",
    "SineSource", "SawSource", "SquareSource", "TriangleSource",
    "FMSource", "HybridKSSource", "SegmentSource",
    "WhiteNoiseSource", "PinkNoiseSource", "RedNoiseSource",
}


def process(path):
    with open(path) as f:
        doc = json.load(f)
    graph = doc.get("graph")
    if not graph or "nodes" not in graph:
        return False
    nodes = graph["nodes"]
    by_id = {n.get("id", ""): n for n in nodes}
    cur_out = graph.get("output")
    if cur_out not in by_id:
        return False
    cur_out_type = by_id[cur_out].get("type")

    # If output is already a combiner, nothing to do.
    if cur_out_type in COMBINER_TYPES:
        return False

    # Find combiner nodes in the patch.
    combiners = [n for n in nodes if n.get("type") in COMBINER_TYPES]
    if not combiners:
        return False  # No combiner → over-rewire didn't damage this one

    # If there's exactly one combiner, restore output to it.
    if len(combiners) == 1:
        graph["output"] = combiners[0]["id"]
        with open(path, "w") as f:
            json.dump(doc, f, indent=2)
        return True

    # Multiple combiners — prefer one named "mix", else leave for manual review
    mix = next((c for c in combiners if c.get("id") == "mix"), None)
    if mix:
        graph["output"] = "mix"
        with open(path, "w") as f:
            json.dump(doc, f, indent=2)
        return True
    print(f"  [skip] {path}: multiple combiners, none named 'mix'", file=sys.stderr)
    return False


def main():
    if len(sys.argv) < 2:
        print("Usage: fix_over_rewire.py <dir-or-file> [...]")
        sys.exit(1)
    fixed = 0
    seen = 0
    for t in sys.argv[1:]:
        p = Path(t)
        paths = [p] if p.is_file() else list(p.rglob("*.json"))
        for path in paths:
            seen += 1
            try:
                if process(path):
                    fixed += 1
                    print(f"fixed: {path}")
            except Exception as e:
                print(f"  [error] {path}: {e}", file=sys.stderr)
    print(f"\nSeen: {seen}  Fixed: {fixed}")


if __name__ == "__main__":
    main()
