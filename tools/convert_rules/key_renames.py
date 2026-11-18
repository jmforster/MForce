"""Rename the two keys the loader read under another name.

  cutoff  on a BWLowpassFilter, BWHighpassFilter or SVFSource, becomes
          cutoffFreq (a number, or a {"ref"} or {"tap"} feed object)
  gap     on a RepeatingSource, becomes gapDuration (a number)

The value moves as it is, under the new key, written where the old key
stood; the other keys keep their order. Where the new key stands beside the
old one, the old key's value is written and the new key's value is dropped,
with a note naming the node and both values: the loader wired cutoff after
the cutoffFreq pin, and wrote gap over the gapDuration setting. A
gapDuration with no gap beside it is left as it is, so the rule cannot tell
a file whose gapDuration the loader overwrote with gap's default of 0.2 from
a file this rule wrote; it changes nothing on its own output.

Refused, naming the node, before anything in the document changes: cutoff on
a BWBandpassFilter, which has no cutoffFreq pin and which the loader refused;
a cutoff that is neither a number nor a feed object; a gap that is not a
number (a bool is not one).
"""
from collections import Counter

from ._nodes import is_feed, is_number, node_label, walk_nodes

# (the types, the old key, the new key, what the value may be, in words)
RENAMES = (
    (("BWLowpassFilter", "BWHighpassFilter", "SVFSource"), "cutoff", "cutoffFreq",
     lambda v: is_number(v) or is_feed(v), "a number or a {\"ref\"} or {\"tap\"} feed object"),
    (("RepeatingSource",), "gap", "gapDuration", is_number, "a number"),
)

REFUSED = {"BWBandpassFilter": ("cutoff", "a bandpass has no cutoffFreq pin; its band is lowCutoff to highCutoff")}


def plan(node, params, old, new, valid, what):
    """Check one node holding `old`; returns its write, or raises ValueError."""
    name = "node %s (%s)" % (node_label(node), node.get("type"))
    value = params[old]
    if not valid(value):
        raise ValueError("%s: %s is %r, not %s" % (name, old, value, what))
    shadowed = new in params
    note = None
    if shadowed:
        note = ("%s: %s %r written as %s; %s %r beside it dropped: the loader read %s over it"
                % (name, old, value, new, new, params[new], old))

    def write(notes, counts):
        rewritten = {}
        for k, v in params.items():
            if k == old:
                rewritten[new] = v
            elif k != new:
                rewritten[k] = v
        params.clear()
        params.update(rewritten)
        counts[old] += 1
        if note is not None:
            notes.append(note)
            counts["dropped"] += 1
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("cutoff" and "gap" per key
    renamed, "dropped" per new key's value dropped beside the old key), empty
    when nothing changed. Every node is checked before any is written, so a
    refused document is unchanged. Asks nothing of `ctx`."""
    writes = []
    for node in walk_nodes(doc):
        params = node.get("params")
        ntype = node.get("type")
        if not isinstance(params, dict):
            continue
        if ntype in REFUSED and REFUSED[ntype][0] in params:
            raise ValueError("node %s (%s): %s: %s"
                             % (node_label(node), ntype, REFUSED[ntype][0], REFUSED[ntype][1]))
        for types, old, new, valid, what in RENAMES:
            if ntype in types and old in params:
                writes.append(plan(node, params, old, new, valid, what))
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
