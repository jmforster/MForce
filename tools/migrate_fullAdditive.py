#!/usr/bin/env python3
"""Migrate consolidated FullAdditiveSource patches to decomposed
AdditiveSource + Partials + wire form.

Idempotent: skips files that don't contain consolidated FullAdditiveSource.
"""
import json
import os
import sys
from pathlib import Path

# Params that stay on the AdditiveSource (the thin oscillator loop) node.
SOURCE_PARAMS = {
    "seed", "frequency", "amplitude", "phase",
    "formant", "formantWeight",
}

# Params common to all Partials subclasses.
COMMON_PARTIALS_PARAMS = {
    "rolloff1", "rolloff2",
    "detune1", "detune2",
    "multEnv", "amplEnv", "poEnv", "roEnv", "dtEnv",
    "expandRule",
}

# Mode → (target type, mode-specific param set)
MODE_DISPATCH = {
    "full": ("FullPartials", {
        "maxPartials", "minMult",
        "evenWeight1", "evenWeight2", "oddWeight1", "oddWeight2",
        "unitPO1", "unitPO2",
    }),
    "sequence": ("SequencePartials", {
        "maxPartials",
        "minMult1", "minMult2", "incr1", "incr2",
        "unitPO1", "unitPO2",
    }),
    "explicit": ("ExplicitPartials", {
        "mult1", "mult2", "ampl1", "ampl2",
        "unitPO1", "unitPO2",
    }),
}


def split_node(node):
    """Split a consolidated FullAdditiveSource node into (source, partials).
    Returns (new_source_node, new_partials_node)."""
    src_id = node["id"]
    params = dict(node.get("params", {}))
    mode = params.pop("mode", "full")
    if mode not in MODE_DISPATCH:
        raise ValueError(f"Unknown mode '{mode}' in node {src_id}")
    partials_type, mode_params = MODE_DISPATCH[mode]
    allowed_partials = COMMON_PARTIALS_PARAMS | mode_params

    partials_id = f"{src_id}_partials"
    partials_params = {}
    source_params = {}
    for k, v in params.items():
        if k in SOURCE_PARAMS:
            source_params[k] = v
        elif k in allowed_partials:
            partials_params[k] = v
        else:
            # Defensive: unknown params. Log and drop.
            print(f"  [warn] dropping unknown param '{k}' on {src_id}", file=sys.stderr)

    # Wire partials into source via ref
    source_params["partials"] = {"ref": partials_id}

    new_source = {
        "id": src_id,
        "type": "AdditiveSource",
        "params": source_params,
    }
    new_partials = {
        "id": partials_id,
        "type": partials_type,
        "params": partials_params,
    }
    # Preserve inputs/outputs/etc. on source node if present (rare)
    for k, v in node.items():
        if k not in ("id", "type", "params"):
            new_source[k] = v
    return new_source, new_partials


def migrate_file(path):
    """Returns True if file was modified."""
    with open(path) as f:
        doc = json.load(f)
    graph = doc.get("graph")
    if not graph or "nodes" not in graph:
        return False
    nodes = graph["nodes"]
    new_nodes = []
    modified = False
    for node in nodes:
        if node.get("type") == "FullAdditiveSource":
            src, parts = split_node(node)
            # Insert partials BEFORE source so refs resolve top-down.
            new_nodes.append(parts)
            new_nodes.append(src)
            modified = True
        else:
            new_nodes.append(node)
    if not modified:
        return False
    graph["nodes"] = new_nodes
    with open(path, "w") as f:
        json.dump(doc, f, indent=2)
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: migrate_fullAdditive.py <dir-or-file> [<dir-or-file>...] [--dry-run]")
        sys.exit(1)
    dry_run = "--dry-run" in sys.argv
    targets = [a for a in sys.argv[1:] if a != "--dry-run"]
    found = 0
    migrated = 0
    for t in targets:
        p = Path(t)
        if p.is_file():
            paths = [p]
        else:
            paths = list(p.rglob("*.json"))
        for path in paths:
            try:
                with open(path) as f:
                    content = f.read()
                if "FullAdditiveSource" not in content:
                    continue
                if '"type": "FullAdditiveSource"' not in content and \
                   '"type":"FullAdditiveSource"' not in content:
                    continue
                found += 1
                if dry_run:
                    print(f"[would migrate] {path}")
                    continue
                if migrate_file(path):
                    migrated += 1
                    print(f"migrated: {path}")
            except Exception as e:
                print(f"  [error] {path}: {e}", file=sys.stderr)
    print(f"\nFound: {found}  Migrated: {migrated}  (dry_run={dry_run})")


if __name__ == "__main__":
    main()
