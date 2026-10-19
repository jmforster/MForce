"""Predict, from a patch's JSON alone, whether a tail pull re-reaches the root
cone: a node the root pull already computed this sample is pulled again by a
tap-only tail's forward inputs. Today that is a direct double advance (no
wrapper is involved, so the sharing trace never saw it); under the memo the
second pull is served, so the render changes.

Rules mirror patch_loader.cpp: normal edges = {"ref": id} anywhere under
params and plain ids under "inputs"; a tap edge = {"tap": id}; the tail list =
nodes with a tap consumer, no normal consumer, not the output (in nodeOrder);
the first consumer of a shared source (wiring order) advances it, later ones
read a wrapper -- so a node is pulled by its FIRST consumer only, and the
walk follows only first-consumer edges.

usage: python predict_double_pull.py <set.txt>...
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATELESS = {"PerformNode", "CurveNode", "Constant", "NameGate"}


def edges_of(node):
    out = []
    def scan(v):
        if isinstance(v, dict):
            if set(v) == {"ref"} and isinstance(v["ref"], str): out.append(("ref", v["ref"]))
            elif set(v) == {"tap"} and isinstance(v["tap"], str): out.append(("tap", v["tap"]))
            else:
                for x in v.values(): scan(x)
        elif isinstance(v, list):
            for x in v: scan(x)
    scan(node.get("params", {}))
    def scan_ids(v):
        if isinstance(v, str): out.append(("ref", v))
        elif isinstance(v, dict):
            for x in v.values(): scan_ids(x)
        elif isinstance(v, list):
            for x in v: scan_ids(x)
    if "inputs" in node: scan_ids(node["inputs"])
    return out


def analyse(path):
    p = json.loads(Path(ROOT / path).read_text(encoding="utf-8"))
    g = p.get("graph")
    if not isinstance(g, dict) or "nodes" not in g or "output" not in g:
        return None
    nodes = g["nodes"] if isinstance(g["nodes"], list) else list(g["nodes"].values())
    order = [n["id"] for n in nodes]
    by_id = {n["id"]: n for n in nodes}
    output = g["output"]
    normal, tapped = set(), set()
    advancer = {}            # source id -> first consumer (wiring order)
    first_edges = {i: [] for i in order}   # consumer -> sources it advances
    for nid in order:
        for kind, src in edges_of(by_id[nid]):
            if kind == "tap":
                tapped.add(src)
            else:
                normal.add(src)
                if src not in advancer:
                    advancer[src] = nid
                    first_edges[nid].append(src)
    tails = [i for i in order if i in tapped and i not in normal and i != output]
    def cone(start):
        seen, stack = set(), [start]
        while stack:
            x = stack.pop()
            if x in seen or x not in by_id: continue
            seen.add(x)
            stack.extend(first_edges.get(x, []))
        return seen
    root_cone = cone(output)
    hits = set()
    for t in tails:
        hits |= cone(t) & root_cone
    stateful = sorted(h for h in hits if by_id[h].get("type") not in STATELESS)
    return tails, sorted(hits), stateful


def main():
    keys = []
    for s in sys.argv[1:]:
        keys += [l.strip() for l in Path(s).read_text().splitlines() if l.strip()]
    n_graph = 0
    for k in sorted(set(keys)):
        try:
            r = analyse(k)
        except Exception as e:
            print(f"ERR {k}: {e}"); continue
        if r is None: continue
        n_graph += 1
        tails, hits, stateful = r
        if hits:
            print(f"{k}: tails={tails} re-reached={hits} stateful={stateful}")
    print(f"({n_graph} graph patches scanned)")


main()
