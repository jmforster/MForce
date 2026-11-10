"""Rewrite a patch's instrument.paramMap as graph wiring.

A paramMap entry under the "frequency" name says: at each note, drive this
target from the note's frequency, through an optional curve, scaled by an
optional curve of the note's velocity. The rule says the same thing in nodes:
a PerformNode per field read, a CurveNode per curve, a CombinedSource for the
product, and the target wired to the head of that chain.

Emission rules, per paramMap entry:
  bare "node.pin"          -> pin refs __perf_freq
  {target, curve}          -> CurveNode(logx | loglog) reading __perf_freq
  {target, vcurve}         -> CombinedSource multiply of the freq leg and a
                              linear CurveNode reading __perf_vel
  {target, curve, vcurve}  -> both
  target with no "."       -> pin name defaults to "frequency"
  name != "frequency"      -> dropped with a note: no other name ever drove
                              anything

A target lands in "dynamicPins" when it names a SETTING on that node type, and
in "params" when it names a fixed pin. The registry decides, via
`mforce_cli --dump-descriptors` — never a hand-copied list.

Refused, naming the id or the target, for a human to resolve by hand: a
paramMap that is not an object; an entry, under any name, that is neither a
string nor an object with a string target, or whose curve or vcurve is not a
list of two or more [number, number] knots; a target whose node type has no
pin or setting of that name; an id the rule synthesizes for this document
(__perf_freq, __perf_vel, __curve_N, __mul_N) that is already a node id in it;
a target that already holds an object or a list (a {"ref"}, a {"tap"}, a
multi-input list) in its slot, or that an earlier entry of the same paramMap
named. Each shape was made by hand, and a rewrite that picked a winner would
invent a meaning.
"""
import json
from collections import Counter, OrderedDict

PERF_F = "__perf_freq"
PERF_V = "__perf_vel"


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


def is_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def check_entry(entry):
    """Raises ValueError unless `entry` is a "node.pin" string, or an object
    with a string target whose curve and vcurve, where present, are each a list
    of two or more [number, number] knots."""
    if isinstance(entry, str):
        return
    if not (isinstance(entry, dict) and isinstance(entry.get("target"), str)):
        raise ValueError("paramMap entry %s is neither a \"node.pin\" string "
                         "nor an object with a target" % json.dumps(entry))
    for key in ("curve", "vcurve"):
        if key not in entry:
            continue
        knots = entry[key]
        if not (isinstance(knots, list) and len(knots) >= 2
                and all(isinstance(k, list) and len(k) == 2
                        and is_number(k[0]) and is_number(k[1]) for k in knots)):
            raise ValueError("paramMap target %r: %s %s is not a list of two or more "
                             "[number, number] knots"
                             % (entry["target"], key, json.dumps(knots)))


def convert(doc, notes, ctx):
    """Returns a Counter: "paramMap entry" per entry converted and "paramMap"
    once for the map removed, empty when the document has none. Appends a line
    to `notes` per name dropped. Raises ValueError if inexpressible, before
    anything in `doc` changes."""
    inst = doc.get("instrument")
    if not inst or "paramMap" not in inst:
        return Counter()
    pm = inst["paramMap"]
    if not isinstance(pm, dict):
        raise ValueError("instrument.paramMap is %s, not an object" % json.dumps(pm))
    for entries in pm.values():
        for entry in (entries if isinstance(entries, list) else [entries]):
            check_entry(entry)

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

    desc = ctx.descriptors()
    converted = Counter()
    writes = []
    claimed = set()
    for name, entries in pm.items():
        if name != "frequency":
            notes.append("paramMap name %r dropped (no name but \"frequency\" "
                         "ever drove anything)" % name)
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
            if pin not in settings and pin not in desc[ntype]["params"] \
                    and pin not in desc[ntype]["inputs"]:
                raise ValueError("paramMap target %r: node %r (%s) has no pin or setting %r"
                                 % (target, node_id, ntype, pin))
            slot = "dynamicPins" if pin in settings else "params"
            if (node_id, pin) in claimed:
                raise ValueError("paramMap target '%s.%s' is named more than once"
                                 % (node_id, pin))
            if isinstance((node.get(slot) or {}).get(pin), (dict, list)):
                raise ValueError("paramMap target '%s.%s' is already wired in %s"
                                 % (node_id, pin, slot))
            claimed.add((node_id, pin))
            writes.append((node, slot, pin, head))
            converted["paramMap entry"] += 1

    taken = sorted(nid for nid in new_nodes if nid in by_id)
    if taken:
        raise ValueError("node id %s is already in the document; the paramMap "
                         "rule synthesizes it" % ", ".join("'%s'" % t for t in taken))
    for node, slot, pin, head in writes:
        node.setdefault(slot, {})[pin] = {"ref": head}
    del inst["paramMap"]
    converted["paramMap"] += 1
    # Prepended in dependency order: build_graph resolves refs against nodes
    # already built, walking the array in order.
    container["nodes"] = list(new_nodes.values()) + container["nodes"]
    return converted


def apply(doc, notes, ctx):
    """The rule's entry point: `convert`'s Counter."""
    return convert(doc, notes, ctx)
