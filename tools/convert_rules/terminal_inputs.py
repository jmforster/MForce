"""Write a SoundChannel's and a StereoMixer's inputs as feeds in params.

  SoundChannel  inputs {"source": "<id>"} becomes params.source
                {"ref": "<id>"}
  StereoMixer   inputs {"channels": ["<id>", ...]} becomes params.channels
                [{"ref": "<id>"}, ...], in the list's order

inputs is removed. A node with no params gains one, written where inputs
stood, the other keys keeping their order; a node with params gains the new
key as its last. An empty inputs object on either type is removed with a
note.

Refused, naming the node, before anything in the document changes: inputs on
a node of any other type; inputs that is not an object; a key under inputs
beside the type's one; a source that is not a string, or channels that is not
a list of strings; params that is not an object; the type's key already
under params.
"""
from collections import Counter

from ._nodes import node_label, walk_nodes

# The type, the one key its inputs held, and that key's value in words.
TERMINALS = {"SoundChannel": ("source", "a node id (a string)"),
             "StereoMixer": ("channels", "a list of node ids (strings)")}


def valid(key, value):
    if key == "source":
        return isinstance(value, str)
    return isinstance(value, list) and all(isinstance(v, str) for v in value)


def as_feeds(key, value):
    if key == "source":
        return {"ref": value}
    return [{"ref": v} for v in value]


def plan(node):
    """Check one node holding inputs; returns its write, or raises ValueError."""
    ntype = node.get("type")
    name = "node %s (%s)" % (node_label(node), ntype)

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    if ntype not in TERMINALS:
        refuse("inputs on a %s; only a SoundChannel's and a StereoMixer's inputs are read" % ntype)
    key, what = TERMINALS[ntype]
    inputs = node["inputs"]
    if not isinstance(inputs, dict):
        refuse("inputs is %r, not an object" % (inputs,))
    if "params" in node and not isinstance(node["params"], dict):
        refuse("params is %r, not an object" % (node["params"],))
    if not inputs:
        def drop(notes, counts):
            del node["inputs"]
            notes.append("%s: an empty inputs object dropped" % name)
            counts["empty"] += 1
        return drop
    extra = [k for k in inputs if k != key]
    if extra:
        refuse("inputs holds %s beside %s; a %s's inputs hold %s alone"
               % (", ".join("'%s'" % k for k in extra), key, ntype, key))
    value = inputs[key]
    if not valid(key, value):
        refuse("inputs.%s is %r, not %s" % (key, value, what))
    if key in node.get("params", {}):
        refuse("params already holds %s beside inputs.%s" % (key, key))

    def write(notes, counts):
        feeds = as_feeds(key, value)
        if "params" in node:
            node["params"][key] = feeds
            del node["inputs"]
        else:
            rewritten = {}
            for k, v in node.items():
                if k == "inputs":
                    rewritten["params"] = {key: feeds}
                else:
                    rewritten[k] = v
            node.clear()
            node.update(rewritten)
        counts[key] += 1
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("source" per channel,
    "channels" per mixer, "empty" per empty inputs dropped), empty when
    nothing changed. Every node is checked before any is written, so a
    refused document is unchanged. Asks nothing of `ctx`."""
    writes = [plan(node) for node in walk_nodes(doc) if "inputs" in node]
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
