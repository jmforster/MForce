"""Rewrite a patch's legacy instrument.paramMap as pure graph wiring
(plan_perform_source_p2a.md Task 5).

Mirrors the loader's build_bindings matrix (engine/src/patch_loader.cpp) in
JSON space. The proof that the mirror is faithful is the conversion null gate
(tools/null_gate_wiring.py): every converted patch must render bit-identical
to the frozen manifest.

  python tools/parammap_to_wiring.py in.json out.json
  python tools/parammap_to_wiring.py --check in.json      # 0 = nothing to do

Emission rules, per paramMap entry:
  bare "node.pin"          -> pin refs __perf_freq
  {target, curve}          -> CurveNode(logx | loglog) reading __perf_freq
  {target, vcurve}         -> CombinedSource multiply of the freq leg and a
                              linear CurveNode reading __perf_vel
  {target, curve, vcurve}  -> both
  target with no "."       -> pin name defaults to "frequency"
  name != "frequency"      -> dropped with a note; the engine ignored these too

A target lands in "dynamicPins" when it names a SETTING on that node type, and
in "params" when it names a fixed pin. The registry decides, via
`mforce_cli --dump-descriptors` — never a hand-copied list, which is how the
patch linter produced 59 false findings in run 25.
"""
import json, subprocess, sys
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
DESC_CACHE = ROOT / "renders/scratch/descriptors.json"

PERF_F = "__perf_freq"
PERF_V = "__perf_vel"


def descriptors():
    """{typeName: {"params": [...], "settings": [...], ...}} straight from the
    registry. Regenerated whenever the exe is newer than the cache, so a
    rebuilt engine cannot leave a stale map behind."""
    if DESC_CACHE.exists() and DESC_CACHE.stat().st_mtime > CLI.stat().st_mtime:
        return json.loads(DESC_CACHE.read_text(encoding="utf-8"))
    r = subprocess.run([str(CLI), "--dump-descriptors"],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        raise RuntimeError("mforce_cli --dump-descriptors failed: " + r.stderr)
    DESC_CACHE.parent.mkdir(parents=True, exist_ok=True)
    DESC_CACHE.write_text(r.stdout, encoding="utf-8")
    return json.loads(r.stdout)


def node_container(doc):
    """Patches come in two shapes: instrument patches nest the graph under a
    "graph" key, NodeGraph-mode patches put nodes at top level. Returns the
    dict that OWNS the "nodes" list, so callers can read and replace it."""
    g = doc.get("graph")
    if isinstance(g, dict) and isinstance(g.get("nodes"), list):
        return g
    if isinstance(doc.get("nodes"), list):
        return doc
    raise ValueError("patch has no node list at doc['nodes'] or doc['graph']['nodes']")


def convert(doc):
    """Returns (doc, n_entries_converted). Raises ValueError if inexpressible."""
    inst = doc.get("instrument")
    if not inst or "paramMap" not in inst:
        return doc, 0
    pm = inst["paramMap"]

    container = node_container(doc)
    by_id = {n["id"]: n for n in container["nodes"]}
    new_nodes = OrderedDict()
    counter = [0]

    def need_perf(field):
        nid = PERF_F if field == "frequency" else PERF_V
        if nid not in new_nodes:
            new_nodes[nid] = {"id": nid, "type": "PerformNode",
                              "params": {"field": field}}
        return nid

    def add_curve(knots, interp, src_id):
        counter[0] += 1
        nid = "__curve_%d" % counter[0]
        new_nodes[nid] = {"id": nid, "type": "CurveNode",
                          "params": {"interp": interp,
                                     "knots": [[float(a), float(b)] for a, b in knots],
                                     "source": {"ref": src_id}}}
        return nid

    def add_mul(a_id, b_id):
        counter[0] += 1
        nid = "__mul_%d" % counter[0]
        new_nodes[nid] = {"id": nid, "type": "CombinedSource",
                          "params": {"source1": {"ref": a_id},
                                     "source2": {"ref": b_id},
                                     "operation": "multiply", "gainAdj": 0.0}}
        return nid

    def chain_for(entry):
        if isinstance(entry, str):
            return entry, need_perf("frequency")
        target = entry["target"]
        head = need_perf("frequency")
        if "curve" in entry:
            interp = "loglog" if entry.get("interp") == "loglog" else "logx"
            head = add_curve(entry["curve"], interp, head)
        if "vcurve" in entry:
            v = add_curve(entry["vcurve"], "linear", need_perf("velocity"))
            head = add_mul(head, v)
        return target, head

    desc = descriptors()
    converted = 0
    for name, entries in pm.items():
        if name != "frequency":
            print("  note: paramMap name %r dropped (the engine never consumed "
                  "it either)" % name, file=sys.stderr)
            continue
        for entry in (entries if isinstance(entries, list) else [entries]):
            target, head = chain_for(entry)
            node_id, _, pin = target.partition(".")
            pin = pin or "frequency"
            if node_id not in by_id:
                raise ValueError("paramMap target %r: no node %r" % (target, node_id))
            node = by_id[node_id]
            ntype = node["type"]
            if ntype not in desc:
                raise ValueError("node %r has unregistered type %r — cannot tell "
                                 "a setting from a fixed pin" % (node_id, ntype))
            settings = {s["name"] for s in desc[ntype]["settings"]}
            slot = "dynamicPins" if pin in settings else "params"
            node.setdefault(slot, {})[pin] = {"ref": head}
            converted += 1

    del inst["paramMap"]
    # Prepended in dependency order: build_graph resolves refs against nodes
    # already built, walking the array in order.
    container["nodes"] = list(new_nodes.values()) + container["nodes"]
    return doc, converted


def main():
    if "--check" in sys.argv:
        doc = json.load(open(sys.argv[-1], encoding="utf-8"))
        sys.exit(2 if "paramMap" in doc.get("instrument", {}) else 0)
    src, dst = sys.argv[1], sys.argv[2]
    doc = json.load(open(src, encoding="utf-8"), object_pairs_hook=OrderedDict)
    doc, n = convert(doc)
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
    print("converted %d entr%s -> %s" % (n, "y" if n == 1 else "ies", dst))


main()
