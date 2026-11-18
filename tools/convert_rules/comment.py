"""Write a patch's and a node's comment under the one key "comment".

  top level  _comment (a string) becomes comment, where _comment stood.
             _provenance (an object whose values are strings or numbers)
             becomes text: one "key: value" line per entry, in the object's
             order, joined by newlines. A file holding both gets the
             _comment text, a blank line, then the provenance lines, as
             comment where _comment stood; a file holding only _provenance
             gets comment where _provenance stood.
  a node     _comment (a string) becomes the node's comment, where _comment
             stood.

Refused, naming the level, before anything in the document changes: comment
already present beside _comment or _provenance at that level; a _comment
that is not a string; a _provenance that is not an object of strings and
numbers (a bool is not a number).
"""
import json
from collections import Counter

from ._nodes import is_number, node_label, walk_nodes


def provenance_text(prov, refuse):
    if not (isinstance(prov, dict) and all(isinstance(v, str) or is_number(v) for v in prov.values())):
        refuse("_provenance is %s, not an object of strings and numbers" % json.dumps(prov))
    return "\n".join("%s: %s" % (k, v if isinstance(v, str) else json.dumps(v)) for k, v in prov.items())


def plan(obj, where, provenance):
    """Check one level; returns its write, None when it holds neither key,
    or raises ValueError. `provenance` says whether _provenance is read at
    this level."""
    keys = [k for k in ("_comment", "_provenance") if k in obj and (k == "_comment" or provenance)]
    if not keys:
        return None

    def refuse(why):
        raise ValueError("%s: %s" % (where, why))

    if "comment" in obj:
        refuse("comment already present beside %s" % " and ".join(keys))
    parts = []
    if "_comment" in keys:
        if not isinstance(obj["_comment"], str):
            refuse("_comment is %r, not a string" % (obj["_comment"],))
        parts.append(obj["_comment"])
    if "_provenance" in keys:
        parts.append(provenance_text(obj["_provenance"], refuse))
    text = "\n\n".join(parts)
    place = keys[0] if "_comment" not in keys else "_comment"

    def write(counts, kind):
        rewritten = {}
        for k, v in obj.items():
            if k == place:
                rewritten["comment"] = text
            elif k not in keys:
                rewritten[k] = v
        obj.clear()
        obj.update(rewritten)
        for k in keys:
            counts[kind % k] += 1
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("_comment" and
    "_provenance" per top-level key folded, "node _comment" per node's),
    empty when nothing changed. Every level is checked before any is
    written, so a refused document is unchanged. Asks nothing of `ctx`."""
    writes = []
    top = plan(doc, "the document", True)
    if top is not None:
        writes.append((top, "%s"))
    for node in walk_nodes(doc):
        w = plan(node, "node %s (%s)" % (node_label(node), node.get("type")), False)
        if w is not None:
            writes.append((w, "node %s"))
    counts = Counter()
    for w, kind in writes:
        w(counts, kind)
    return counts
