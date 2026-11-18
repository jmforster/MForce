"""Drop two keys nothing reads.

  instrument.release       removed, with a note giving its value: the
                           instrument block's reader takes no release
  params.dynamicPins null  on any node, removed, with a note naming the
                           node: a node's dynamic pins are the map beside
                           its params, and a null inside params is no map

An Envelope's own release, a key of its preset form inside params, is not
this rule's. A dynamicPins beside params is not this rule's either.

Refused, naming the node, before anything in the document changes: a
dynamicPins inside params that is not null.
"""
import json
from collections import Counter

from ._nodes import node_label, walk_nodes


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("instrument.release" for
    the instrument block's key, "dynamicPins" per null removed from a node's
    params), empty when nothing changed. Every node is checked before
    anything is written, so a refused document is unchanged. Asks nothing of
    `ctx`."""
    nulls = []
    for node in walk_nodes(doc):
        params = node.get("params")
        if isinstance(params, dict) and "dynamicPins" in params:
            name = "node %s (%s)" % (node_label(node), node.get("type"))
            if params["dynamicPins"] is not None:
                raise ValueError("%s: params.dynamicPins is %s, not null; a dynamicPins map belongs beside "
                                 "params, not inside it" % (name, json.dumps(params["dynamicPins"])))
            nulls.append((name, params))
    counts = Counter()
    instrument = doc.get("instrument")
    if isinstance(instrument, dict) and "release" in instrument:
        value = instrument.pop("release")
        notes.append("instrument.release %s dropped (nothing reads it)" % json.dumps(value))
        counts["instrument.release"] += 1
    for name, params in nulls:
        del params["dynamicPins"]
        notes.append("%s: params.dynamicPins null dropped (nothing reads it)" % name)
        counts["dynamicPins"] += 1
    return counts
