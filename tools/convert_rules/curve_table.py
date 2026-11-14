"""Rewrite a CurveNode's knot list and expression rows as columns.

On a CurveNode whose params hold "knots" or "exprKnots":
  knots      a list of [x, y] pairs, becomes "x" and "y", written where
             "knots" stood
  exprKnots  a list of {"x", "form", "a", "b"} rows, becomes "exprX",
             "exprForm", "exprA" and "exprB", written where "exprKnots" stood;
             a row's absent form is "linear" and its absent a and b are 0.0,
             what the loader read
A cell is the list's own JSON value; an empty list becomes empty columns; a
node with one of the two lists gets only that table. The other keys keep
their order. The loader compared each word exactly, so:
  form    "power" or "linear" is kept; any other string is written "linear"
  interp  "linear", "logx" or "loglog" is kept; any other string is written
          "linear"
  mode    "points" or "expressions" is kept; any other string is written
          "points"
each the word the loader read, with a note. An absent interp or mode stays
absent. A CurveNode with neither list is left as it is.

Refused, naming the node, before anything in the document changes: knots
that is not a list, or a knot that is not a list of exactly two numbers;
exprKnots that is not a list, or a row that is not an object, lacks x, holds
a key besides x, form, a and b, or holds an x, a or b that is not a number
or a form that is not a string; an interp or mode that is not a string; any
of the six column keys beside knots or exprKnots.
"""
from collections import Counter

from ._nodes import is_number, node_label, walk_nodes

KNOT_COLUMNS = ("x", "y")
EXPR_COLUMNS = ("exprX", "exprForm", "exprA", "exprB")
FORMS = ("linear", "power")
# Each word setting: its labels and the one the loader read for any other word.
WORDS = {"interp": (("linear", "logx", "loglog"), "linear"),
         "mode":   (("points", "expressions"), "points")}


def plan(node, params):
    """Check one CurveNode holding an old list; returns its write, or raises
    ValueError."""
    name = "node %s (CurveNode)" % node_label(node)

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    clash = [c for c in KNOT_COLUMNS + EXPR_COLUMNS if c in params]
    if clash:
        refuse("%s beside the column%s %s" % (
            " and ".join(k for k in ("knots", "exprKnots") if k in params),
            "s" if len(clash) > 1 else "", ", ".join(clash)))
    labels = []
    knot_cols = None
    if "knots" in params:
        knots = params["knots"]
        if not isinstance(knots, list):
            refuse("knots is not a list")
        knot_cols = {"x": [], "y": []}
        for i, k in enumerate(knots):
            if not (isinstance(k, list) and len(k) == 2 and is_number(k[0]) and is_number(k[1])):
                refuse("knot %d is %r, not a list of two numbers" % (i, k))
            knot_cols["x"].append(k[0])
            knot_cols["y"].append(k[1])
    expr_cols = None
    if "exprKnots" in params:
        rows = params["exprKnots"]
        if not isinstance(rows, list):
            refuse("exprKnots is not a list")
        expr_cols = {c: [] for c in EXPR_COLUMNS}
        for i, r in enumerate(rows):
            if not isinstance(r, dict):
                refuse("expression row %d is %r, not an object" % (i, r))
            if "x" not in r:
                refuse("expression row %d has no x" % i)
            extra = sorted(k for k in r if k not in ("x", "form", "a", "b"))
            if extra:
                refuse("expression row %d has %s, which no column holds"
                       % (i, ", ".join("'%s'" % k for k in extra)))
            for key, col, default in (("x", "exprX", None), ("a", "exprA", 0.0), ("b", "exprB", 0.0)):
                v = r.get(key, default)
                if not is_number(v):
                    refuse("expression row %d %s is %r, not a number" % (i, key, v))
                expr_cols[col].append(v)
            form = r.get("form", "linear")
            if not isinstance(form, str):
                refuse("expression row %d form is %r, not a string" % (i, form))
            if form not in FORMS:
                labels.append("%s: expression row %d form %r is not one the loader knew; "
                              "written as 'linear', which it read" % (name, i, form))
                form = "linear"
            expr_cols["exprForm"].append(form)
    words = {}
    for key, (known, read) in WORDS.items():
        if key not in params:
            continue
        v = params[key]
        if not isinstance(v, str):
            refuse("%s is %r, not a string" % (key, v))
        if v not in known:
            labels.append("%s: %s %r is not one the loader knew; written as '%s', which it read"
                          % (name, key, v, read))
            words[key] = read

    def write(notes, counts):
        rewritten = {}
        for k, v in params.items():
            if k == "knots":
                rewritten.update(knot_cols)
            elif k == "exprKnots":
                rewritten.update(expr_cols)
            else:
                rewritten[k] = words.get(k, v)
        params.clear()
        params.update(rewritten)
        notes.extend(labels)
        if knot_cols is not None:
            counts["knots"] += 1
        if expr_cols is not None:
            counts["expression rows"] += 1
        if labels:
            counts["label"] += len(labels)
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("knots" per knot list
    rewritten, "expression rows" per exprKnots list rewritten, "label" per
    word written as the one the loader read), empty when nothing changed.
    Every node is checked before any is written, so a refused document is
    unchanged. Asks nothing of `ctx`."""
    writes = []
    for node in walk_nodes(doc):
        params = node.get("params")
        if node.get("type") != "CurveNode" or not isinstance(params, dict):
            continue
        if "knots" in params or "exprKnots" in params:
            writes.append(plan(node, params))
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
