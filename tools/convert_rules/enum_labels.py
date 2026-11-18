"""Write every enum value as its declared label.

The labels are the registry's: the descriptor table gives an enum setting's
"labels", and a labelled column's under the type's "arrayLabels". For every
node whose type the table holds, each value is rewritten by one rule:
  a whole number inside the labels    becomes the label at that index
  a string equal to a label ignoring  becomes that label
  ASCII case, and not equal to it
  a bool, or any other shape          is left as it is (an object is wiring,
                                      which the loader checks)
It is applied to
  each setting with labels            the value under its name in params
  each column in arrayLabels          each element of the list under its name
  on an Envelope whose params hold    attackCurve, decayCurve, releaseCurve,
  "preset"                            against the first four labels of the
                                      Envelope's type column (Linear, Expo,
                                      InverseExpo, Sine): the preset form
                                      builds no Hold stage
A node whose type the table does not hold is left as it is. The table is
asked for only when some node of a type holds, in its params, a number, a
string, or a list holding either.

Refused, naming the node and the key, before anything in the document
changes: a number that is not whole or not an index of the labels; a string
that matches no label in any case. A table in which any setting of any type
is marked an enum and given no labels was dumped by an mforce_cli that
prints no labels, and such a tool prints no "arrayLabels" and no PerformNode
either: when the rule takes the table, that stops the file with a
RuntimeError, which the driver reports as the file's refusal, whatever the
document holds, so a stale table leaves no setting, column element or
PerformNode field silently unwritten.
"""
from collections import Counter

from ._nodes import is_number, node_label, walk_nodes

CURVE_KEYS = ("attackCurve", "decayCurve", "releaseCurve")
PRESET_CURVES = 4            # the type column's labels a preset curve may name


def ascii_lower(s):
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def candidate(v):
    """A value an enum rewrite could touch, or a list holding one."""
    if isinstance(v, list):
        return any(is_number(x) or isinstance(x, str) for x in v)
    return is_number(v) or isinstance(v, str)


def could_need(doc):
    return any(isinstance(n.get("type"), str) and isinstance(n.get("params"), dict)
               and any(candidate(v) for v in n["params"].values())
               for n in walk_nodes(doc))


def label_for(value, labels, refuse, where):
    """The label `value` is written as, or None when it is left as it is."""
    if is_number(value):
        whole = isinstance(value, int) or value.is_integer()
        if whole and 0 <= value < len(labels):
            return labels[int(value)], "index"
        refuse("%s %r is not a whole index 0 to %d of %s" % (where, value, len(labels) - 1, ", ".join(labels)))
    if isinstance(value, str):
        if value in labels:
            return None
        for label in labels:
            if ascii_lower(label) == ascii_lower(value):
                return label, "case"
        refuse("%s %r matches none of %s in any case" % (where, value, ", ".join(labels)))
    return None


def plan(node, params, entry):
    """Check one node; returns its writes as (container, key, label, kind)."""
    ntype = node["type"]
    name = "node %s (%s)" % (node_label(node), ntype)

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    writes = []

    def check(container, key, labels, where, kind=None):
        got = label_for(container[key], labels, refuse, where)
        if got is not None:
            writes.append((container, key, got[0], kind or got[1]))

    for s in entry.get("settings", []):
        if s["name"] in params and "labels" in s:
            check(params, s["name"], s["labels"], s["name"])
    for column, labels in entry.get("arrayLabels", {}).items():
        cells = params.get(column)
        if isinstance(cells, list):
            for i in range(len(cells)):
                check(cells, i, labels, "%s[%d]" % (column, i))
    if ntype == "Envelope" and "preset" in params:
        curves = [k for k in CURVE_KEYS if k in params]
        if curves:
            labels = entry.get("arrayLabels", {}).get("type")
            if not labels:
                refuse("the descriptor table gives the Envelope's type column no labels to read %s against"
                       % curves[0])
            for k in curves:
                check(params, k, labels[:PRESET_CURVES], k, "preset curve")
    return writes


def refuse_stale(desc):
    """Stop on a table in which some setting is marked an enum and given no labels."""
    for ntype, entry in desc.items():
        for s in entry.get("settings", []):
            if s.get("isEnum") and "labels" not in s:
                raise RuntimeError("the descriptor table marks %s.%s an enum and gives it no labels: it was "
                                   "dumped by an mforce_cli that prints none (rebuild it)" % (ntype, s["name"]))


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("index" per number written
    as its label, "case" per label respelled, "preset curve" per preset
    curve key written as a label), empty when nothing changed. Every node is
    checked before any is written, so a refused document is unchanged. Asks
    `ctx` for the descriptor table only when some node could need it, and
    stops on a stale table there."""
    if not could_need(doc):
        return Counter()
    desc = ctx.descriptors()
    refuse_stale(desc)
    writes = []
    for node in walk_nodes(doc):
        params = node.get("params")
        if isinstance(node.get("type"), str) and node["type"] in desc and isinstance(params, dict):
            writes.extend(plan(node, params, desc[node["type"]]))
    counts = Counter()
    for container, key, label, kind in writes:
        container[key] = label
        counts[kind] += 1
    return counts
