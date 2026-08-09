#!/usr/bin/env python3
"""Add an `instrument` block to migrated render-style patches so they're
playable in the UI's PatchGraph (keyboard-driven) mode.

For each patch missing `instrument`:
  - find the AdditiveSource (or other source) node — prefer ids 'fas', 'src',
    or any node with a numeric `frequency` param
  - add  "instrument": { "paramMap": { "frequency": "<id>.frequency" },
                         "polyphony": 1 }

Idempotent: skips patches that already have an `instrument` block.
"""
import json
import sys
from pathlib import Path

SOURCE_TYPES = {
    "AdditiveSource", "AdditiveSource2", "WavetableSource",
    "SineSource", "SawSource", "SquareSource", "TriangleSource",
    "FMSource", "HybridKSSource",
}


def pick_source_node(nodes):
    """Pick a node to drive the frequency from. Prefer common ids, then
    fall back to any source-typed node with a numeric `frequency` param."""
    by_id = {n.get("id", ""): n for n in nodes}
    for pref in ("fas", "src", "osc", "source"):
        if pref in by_id:
            n = by_id[pref]
            if "frequency" in n.get("params", {}):
                return pref
    # Fall back: any source-typed node with a numeric frequency
    for n in nodes:
        if n.get("type") in SOURCE_TYPES:
            params = n.get("params", {})
            f = params.get("frequency")
            if isinstance(f, (int, float)):
                return n["id"]
    # Last resort: any node with a numeric frequency
    for n in nodes:
        f = n.get("params", {}).get("frequency")
        if isinstance(f, (int, float)):
            return n["id"]
    return None


def process(path):
    with open(path) as f:
        doc = json.load(f)
    if "instrument" in doc:
        return False
    graph = doc.get("graph")
    if not graph or "nodes" not in graph:
        return False
    src_id = pick_source_node(graph["nodes"])
    if not src_id:
        print(f"  [skip] {path}: no source node with numeric frequency", file=sys.stderr)
        return False
    doc["instrument"] = {
        "paramMap": {"frequency": f"{src_id}.frequency"},
        "polyphony": 1,
    }
    with open(path, "w") as f:
        json.dump(doc, f, indent=2)
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: add_instrument_block.py <dir-or-file> [<dir-or-file>...]")
        sys.exit(1)
    added = 0
    seen = 0
    for t in sys.argv[1:]:
        p = Path(t)
        paths = [p] if p.is_file() else list(p.rglob("*.json"))
        for path in paths:
            seen += 1
            try:
                if process(path):
                    added += 1
                    print(f"added instrument: {path}")
            except Exception as e:
                print(f"  [error] {path}: {e}", file=sys.stderr)
    print(f"\nSeen: {seen}  Added: {added}")


if __name__ == "__main__":
    main()
