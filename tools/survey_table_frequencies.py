#!/usr/bin/env python3
"""The lowest frequency a frequency-dependent table is asked for.

WavetableSource and HybridKSSource size a table by sample rate / frequency.
The engine reserves that table once, at prepare, for a floor frequency;
this survey says how low the patches in a folder actually go, so the floor
is chosen from use, not guessed.

  python tools/survey_table_frequencies.py patches/library patches/baselines ...

For every WavetableSource or HybridKSSource node: a constant "frequency" is
reported as is; a wired one ({"ref": id} or a paramMap entry) is reported with
the referenced node's type and its constant params, for a reader to judge the
lowest value it can reach. The lowest constant per folder closes the report.
"""
import json, sys
from pathlib import Path

TABLE_TYPES = {"WavetableSource", "HybridKSSource"}


def nodes_of(doc):
    return doc.get("graph", {}).get("nodes", []) if isinstance(doc, dict) else []


def describe_ref(nodes, ref):
    for n in nodes:
        if n.get("id") == ref:
            consts = {k: v for k, v in n.get("params", {}).items() if isinstance(v, (int, float))}
            return f"{n.get('type')} {consts}"
    return "? (no such node)"


def survey(folder):
    lowest = None
    rows = []
    for path in sorted(Path(folder).rglob("*.json")):
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        nodes = nodes_of(doc)
        for n in nodes:
            if n.get("type") not in TABLE_TYPES:
                continue
            f = n.get("params", {}).get("frequency", None)
            if isinstance(f, (int, float)):
                rows.append((str(path), n["id"], n["type"], f"{f} Hz"))
                lowest = f if lowest is None else min(lowest, f)
            elif isinstance(f, dict) and "ref" in f:
                rows.append((str(path), n["id"], n["type"], f"ref {f['ref']} -> {describe_ref(nodes, f['ref'])}"))
            elif f is None:
                rows.append((str(path), n["id"], n["type"], "default (440 Hz) or a paramMap/wiring delivery"))
            else:
                rows.append((str(path), n["id"], n["type"], f"{f!r}"))
    return rows, lowest


def main(argv):
    for folder in argv:
        rows, lowest = survey(folder)
        print(f"== {folder}: {len(rows)} table node(s); lowest constant frequency: "
              f"{'none' if lowest is None else f'{lowest} Hz'}")
        for path, nid, typ, what in rows:
            print(f"  {path}  {nid} ({typ}): {what}")


if __name__ == "__main__":
    main(sys.argv[1:] or ["patches/library", "patches/baselines"])
