#!/usr/bin/env python3
"""For patches with an `instrument` block: reroute graph.output to the
AdditiveSource (or other source) node directly, and drop the now-unused
SoundChannel / StereoMixer chain. UI's PatchGraph mode wants a single
ValueSource as output — it can't pull samples through a StereoMixer
(whose dspSource is intentionally null in the UI's graph builder).

Idempotent: skips patches whose output is already the source node.
"""
import json
import sys
from pathlib import Path

UNUSED_TYPES = {"SoundChannel", "StereoMixer"}
SOURCE_TYPES = {
    "AdditiveSource", "AdditiveSource2", "WavetableSource",
    "SineSource", "SawSource", "SquareSource", "TriangleSource",
    "FMSource", "HybridKSSource",
}


def pick_source_node(nodes):
    by_id = {n.get("id", ""): n for n in nodes}
    # Prefer common ids if present
    for pref in ("fas", "src", "osc", "source"):
        if pref in by_id and by_id[pref].get("type") in SOURCE_TYPES:
            return pref
    # Any node typed as a source
    for n in nodes:
        if n.get("type") in SOURCE_TYPES:
            return n["id"]
    return None


def process(path):
    with open(path) as f:
        doc = json.load(f)
    if "instrument" not in doc:
        return False
    graph = doc.get("graph")
    if not graph or "nodes" not in graph:
        return False
    nodes = graph["nodes"]
    src_id = pick_source_node(nodes)
    if not src_id:
        return False
    cur_out = graph.get("output")
    if cur_out == src_id:
        return False  # Already correct
    # Reroute
    graph["output"] = src_id
    # Drop SoundChannel / StereoMixer nodes (now unused)
    new_nodes = [n for n in nodes if n.get("type") not in UNUSED_TYPES]
    graph["nodes"] = new_nodes
    # Also drop any positions for removed nodes (cosmetic)
    if "ui" in doc and "positions" in doc.get("ui", {}):
        kept_ids = {n.get("id") for n in new_nodes}
        kept_ids.update({"__output", "__param_frequency"})
        doc["ui"]["positions"] = {
            k: v for k, v in doc["ui"]["positions"].items()
            if k in kept_ids or k.startswith("__")
        }
    with open(path, "w") as f:
        json.dump(doc, f, indent=2)
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: rewire_instrument_output.py <dir-or-file> [...]")
        sys.exit(1)
    rewired = 0
    seen = 0
    for t in sys.argv[1:]:
        p = Path(t)
        paths = [p] if p.is_file() else list(p.rglob("*.json"))
        for path in paths:
            seen += 1
            try:
                if process(path):
                    rewired += 1
                    print(f"rewired: {path}")
            except Exception as e:
                print(f"  [error] {path}: {e}", file=sys.stderr)
    print(f"\nSeen: {seen}  Rewired: {rewired}")


if __name__ == "__main__":
    main()
