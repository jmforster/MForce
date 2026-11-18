"""Make an inline expand rule an ExpandRule node.

On a FullPartials, SequencePartials or ExplicitPartials whose "expandRule"
is an object with no "ref" key (the inline form):
  a node {"id": "<host id>_expand", "type": "ExpandRule", "params": {...}}
  is inserted immediately before the host, in the node list that holds the
  host, so it is built before the host reads it. Its params are the twelve
  values in the order count, recurse, spacing1, spacing2, dt1, dt2, loPct1,
  loPct2, power1, power2, po1, po2: each the inline object's, or where the
  object omits it the value the loader read (2, 0, 0.5, 0.5, 0.01, 0.01,
  0.1, 0.1, 1.0, 1.0, 0.0, 0.0). All twelve are written.
  the host's expandRule becomes {"ref": "<host id>_expand"}, where it stood.
An expandRule that holds "ref" is the node form and is left as it is; an
expandRule on any other type is not this rule's.

Refused, naming the host, before anything in the document changes: an
inline key outside the twelve; a value that is not a number (a bool is not
one); a host with no string id; an id <host id>_expand already a node id in
the document, or wanted by two hosts; an expandRule on a host that is not an
object (a string, a number, a list).
"""
from collections import Counter

from ._nodes import is_number, node_label, walk_node_lists

HOSTS = ("FullPartials", "SequencePartials", "ExplicitPartials")
# The ExpandRule fields in the loader's order, each with the value it read
# for an absent key.
FIELDS = (("count", 2), ("recurse", 0), ("spacing1", 0.5), ("spacing2", 0.5),
          ("dt1", 0.01), ("dt2", 0.01), ("loPct1", 0.1), ("loPct2", 0.1),
          ("power1", 1.0), ("power2", 1.0), ("po1", 0.0), ("po2", 0.0))
FIELD_NAMES = tuple(k for k, _ in FIELDS)


def plan(nodes, node, params, taken):
    """Check one host holding an expandRule; returns its write, None for the
    node form, or raises ValueError. `taken` is the set of the document's
    node ids, to which the new id is added."""
    name = "node %s (%s)" % (node_label(node), node.get("type"))

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    rule = params["expandRule"]
    if not isinstance(rule, dict):
        refuse("expandRule is %r, neither an inline object nor a {\"ref\"} to an ExpandRule node" % (rule,))
    if "ref" in rule:
        return None
    extra = sorted(k for k in rule if k not in FIELD_NAMES)
    if extra:
        refuse("expandRule has %s, which an ExpandRule does not hold"
               % ", ".join("'%s'" % k for k in extra))
    for k, v in rule.items():
        if not is_number(v):
            refuse("expandRule %s is %r, not a number" % (k, v))
    host_id = node.get("id")
    if not (isinstance(host_id, str) and host_id):
        refuse("an inline expandRule becomes a node named after its host, and the host has no id")
    new_id = host_id + "_expand"
    if new_id in taken:
        refuse("the expandRule's node id '%s' is already in the document" % new_id)
    taken.add(new_id)
    rule_node = {"id": new_id, "type": "ExpandRule",
                 "params": {k: rule.get(k, default) for k, default in FIELDS}}

    def write(notes, counts):
        at = next(i for i, n in enumerate(nodes) if n is node)
        nodes.insert(at, rule_node)
        params["expandRule"] = {"ref": new_id}
        counts["expandRule"] += 1
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("expandRule" per inline rule
    made a node), empty when nothing changed. Every host is checked before
    any is written, so a refused document is unchanged. Asks nothing of
    `ctx`."""
    pairs = walk_node_lists(doc)
    taken = {n.get("id") for _, n in pairs if isinstance(n.get("id"), str)}
    writes = []
    for nodes, node in pairs:
        params = node.get("params")
        if node.get("type") not in HOSTS or not isinstance(params, dict) or "expandRule" not in params:
            continue
        w = plan(nodes, node, params, taken)
        if w is not None:
            writes.append(w)
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
