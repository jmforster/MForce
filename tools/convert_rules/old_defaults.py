"""Write out two values the old loader supplied where a file said nothing or
used a word of its own.

  VarSource       params holding no "val" gain "val": 0.0 as their first key,
                  with a note: the loader read an absent val as 0, where the
                  pin's own default is 1. A VarSource with no params object
                  is left as it is (the loader refused it).
  CombinedSource  an "operation" that is the string "add", in any case,
                  becomes "Mix", the setting's own label for the operation
                  the loader read. Every other string and every number is
                  left as it is.

The rule cannot tell a file written for the old loader, whose absent val
meant 0, from a file that leaves val out to mean the pin's default: it writes
0.0 into either. It is for files the old loader read. It runs after the
paramMap rule, which may wire a VarSource's val, so a val wired there is
present here. It refuses nothing.
"""
from collections import Counter

from ._nodes import node_label, walk_nodes


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("val" per VarSource given
    its val, "add" per operation written "Mix"), empty when nothing changed.
    Asks nothing of `ctx`."""
    counts = Counter()
    for node in walk_nodes(doc):
        params = node.get("params")
        ntype = node.get("type")
        if not isinstance(params, dict):
            continue
        if ntype == "VarSource" and "val" not in params:
            rewritten = {"val": 0.0}
            rewritten.update(params)
            params.clear()
            params.update(rewritten)
            notes.append("node %s (VarSource): val written as 0.0, what the loader read for an "
                         "absent val (the pin's default is 1)" % node_label(node))
            counts["val"] += 1
        elif ntype == "CombinedSource":
            op = params.get("operation")
            if isinstance(op, str) and op.isascii() and op.lower() == "add":
                params["operation"] = "Mix"
                counts["add"] += 1
    return counts
