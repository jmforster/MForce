"""Rewrite a PhasedValueSource's stage list as its list of sources and a
four-column table.

On a PhasedValueSource whose params hold "stages" (a list of rows, each
{"source", "percent", "min", "max", "gain"}), the list becomes five keys,
written where "stages" stood, the other keys ("overlap" among them) keeping
their order:
  sources  the rows' source values, in order, each as it is: a {"ref"} or
           {"tap"} feed object, or a number
  percent  the rows' percent
  minSec   the rows' min
  maxSec   the rows' max
  gain     the rows' gain
A cell is the row's own JSON value; a key the row omits is the value the
loader read for it: 0.0 for percent, min and max, 1.0 for gain. An empty list
becomes an empty sources list and four empty columns. A node with no
"stages" is left as it is.

Refused, naming the node and the row, before anything in the document
changes: "stages" that is not a list; a row that is not an object, has no
source, or holds a key besides the five; a source that is neither a feed
object nor a number; a percent, min, max or gain that is not a number (a
bool is not one); "stages" beside any of sources, percent, minSec, maxSec
and gain.
"""
from collections import Counter

from ._nodes import is_feed, is_number, node_label, walk_nodes

# (the row's key, the column it fills, the loader's default)
CELLS = (("percent", "percent", 0.0), ("min", "minSec", 0.0), ("max", "maxSec", 0.0), ("gain", "gain", 1.0))
NEW_KEYS = ("sources",) + tuple(col for _, col, _ in CELLS)
ROW_KEYS = ("source",) + tuple(key for key, _, _ in CELLS)


def plan(node, params):
    """Check one node holding a stage list; returns its write, or raises
    ValueError."""
    name = "node %s (PhasedValueSource)" % node_label(node)

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    clash = [k for k in NEW_KEYS if k in params]
    if clash:
        refuse("stages beside %s; the node takes one form" % ", ".join(clash))
    rows = params["stages"]
    if not isinstance(rows, list):
        refuse("stages is %r, not a list of rows" % (rows,))
    table = {k: [] for k in NEW_KEYS}
    for i, r in enumerate(rows):
        if not isinstance(r, dict):
            refuse("row %d is %r, not an object" % (i, r))
        if "source" not in r:
            refuse("row %d has no source" % i)
        extra = sorted(k for k in r if k not in ROW_KEYS)
        if extra:
            refuse("row %d has %s, which no column holds" % (i, ", ".join("'%s'" % k for k in extra)))
        src = r["source"]
        if not (is_feed(src) or is_number(src)):
            refuse("row %d source is %r, neither a {\"ref\"} or {\"tap\"} feed object nor a number" % (i, src))
        table["sources"].append(src)
        for key, col, default in CELLS:
            v = r.get(key, default)
            if not is_number(v):
                refuse("row %d %s is %r, not a number" % (i, key, v))
            table[col].append(v)

    def write(notes, counts):
        rewritten = {}
        for k, v in params.items():
            if k == "stages":
                rewritten.update(table)
            else:
                rewritten[k] = v
        params.clear()
        params.update(rewritten)
        counts["stage list"] += 1
        if rows:
            counts["row"] += len(rows)
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("stage list" per list
    rewritten, "row" per row), empty when nothing changed. Every node is
    checked before any is written, so a refused document is unchanged. Asks
    nothing of `ctx`."""
    writes = []
    for node in walk_nodes(doc):
        params = node.get("params")
        if node.get("type") != "PhasedValueSource" or not isinstance(params, dict):
            continue
        if "stages" in params:
            writes.append(plan(node, params))
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
