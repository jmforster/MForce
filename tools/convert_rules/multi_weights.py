"""Move a MultiSource's per-feed weight and delay onto the node.

On a MultiSource whose "source" is a list in which a feed object holds a key
besides its "ref" or "tap", or is one such feed object:
  every feed becomes plain: {"ref": id} or {"tap": id}
  weights   written when any feed held a weight: one number per feed, in
            order, the feed's weight, or 1.0 for a feed that held none
  delays    written when any feed held a delay: one number per feed, the
            feed's delay in samples, or 0 for a feed that held none
weights and then delays are written right after "source"; the other keys
keep their order. A weight or a delay moves as it is.

The loader read a feed's weight and delay only from a list: a "source" that
is one feed object played at weight 1 and delay 0 whatever it held. On such
a node the feed becomes plain and stays one object, no weights or delays is
written, and each weight or delay is dropped with a note naming the node and
the value. A single plain feed object, and a list of plain feeds, are left
as they are.

Refused, naming the node and the feed, before anything in the document
changes: a feed that is not an object holding exactly one of "ref" and "tap"
as a string (a number or a string in the list among them); a feed holding a
key besides ref, tap, weight and delay; a weight or a delay that is not a
number (a bool is not one); "weights" or "delays" already on the node beside
a feed that holds a weight or a delay.
"""
from collections import Counter

from ._nodes import is_feed, is_number, node_label, walk_nodes

ATTRIBUTES = (("weight", "weights", 1.0), ("delay", "delays", 0))


def plain_part(feed):
    """The feed with its weight and delay taken out."""
    return {k: v for k, v in feed.items() if k not in ("weight", "delay")}


def needs_rewrite(source):
    """Whether a feed object of `source` holds a key besides its ref or tap."""
    feeds = source if isinstance(source, list) else [source]
    return any(isinstance(f, dict) and any(k not in ("ref", "tap") for k in f) for f in feeds)


def plan(node, params):
    """Check one MultiSource whose feeds carry more than a ref or a tap;
    returns its write, or raises ValueError."""
    name = "node %s (MultiSource)" % node_label(node)

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    source = params["source"]
    single = not isinstance(source, list)
    feeds = [source] if single else source
    plain = []
    columns = {arr: [] for _, arr, _ in ATTRIBUTES}
    held = {arr: 0 for _, arr, _ in ATTRIBUTES}
    for i, f in enumerate(feeds):
        if not isinstance(f, dict):
            refuse("feed %d is %r, not a {\"ref\"} or {\"tap\"} feed object" % (i, f))
        extra = sorted(k for k in f if k not in ("ref", "tap", "weight", "delay"))
        if extra:
            refuse("feed %d has %s, which the node does not read"
                   % (i, ", ".join("'%s'" % k for k in extra)))
        p = plain_part(f)
        if not is_feed(p):
            refuse("feed %d is %r, not one string \"ref\" or \"tap\" with a weight or a delay" % (i, f))
        plain.append(p)
        for key, arr, default in ATTRIBUTES:
            if key in f:
                if not is_number(f[key]):
                    refuse("feed %d %s is %r, not a number" % (i, key, f[key]))
                held[arr] += 1
                columns[arr].append(f[key])
            else:
                columns[arr].append(default)
    arrays = [arr for _, arr, _ in ATTRIBUTES if arr in params]
    if arrays and any(held.values()):
        refuse("%s beside a feed that holds a weight or a delay; the node takes one form"
               % " and ".join(arrays))
    dropped = []
    if single:
        dropped = ["%s: %s %r on its single feed object dropped: the loader read a weight and a "
                   "delay only from a list of feeds, and played this one at %s %r"
                   % (name, key, source[key], key, default)
                   for key, _, default in ATTRIBUTES if key in source]

    def write(notes, counts):
        if single:
            params["source"] = plain[0]
            notes.extend(dropped)
            counts["node"] += 1
            counts["dropped"] += len(dropped)
            return
        rewritten = {}
        for k, v in params.items():
            if k == "source":
                rewritten[k] = plain
                for _, arr, _ in ATTRIBUTES:
                    if held[arr]:
                        rewritten[arr] = columns[arr]
            else:
                rewritten[k] = v
        params.clear()
        params.update(rewritten)
        counts["node"] += 1
        for key, arr, _ in ATTRIBUTES:
            if held[arr]:
                counts[key] += held[arr]
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("node" per MultiSource
    rewritten, "weight" and "delay" per feed attribute moved, "dropped" per
    attribute dropped from a single feed object), empty when
    nothing changed. Every node is checked before any is written, so a
    refused document is unchanged. Asks nothing of `ctx`."""
    writes = []
    for node in walk_nodes(doc):
        params = node.get("params")
        if node.get("type") != "MultiSource" or not isinstance(params, dict) or "source" not in params:
            continue
        if needs_rewrite(params["source"]):
            writes.append(plan(node, params))
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
