"""Gather the editor's data, ui and the top-level groups, into one layout.

ui (an object holding no key but positions, panning and noteFaces) and the
top-level groups (a list) become one "layout" object, written where ui
stood, or where groups stood when there is no ui; ui and groups are removed.
layout holds, in this order, each only when non-empty:
  positions  the ui.positions entries whose key is the id of a node of the
             graph (a nested node list's included)
  output     the value of the __output entry
  panning    ui.panning
  groups     the top-level list, as it is
  faces      ui.noteFaces, each entry as it is but for gaining "pos", the
             ui.positions entry under the face's label, when there is one
A layout with nothing to hold is not written. A ui.positions entry whose
key is no node's id, not __output and no face's label is dropped with a note
naming the key, and so is an entry whose value is not a list of two numbers
under 1e7 in magnitude, which the editor ignores at load.

Refused, before anything in the document changes: layout already present
beside ui or groups; ui that is not an object, or that holds a key beside its
three; ui.positions that is not an object; ui.noteFaces that is not a list of
objects; a face's label equal to a node's id; groups that is not a list.
"""
from collections import Counter

from ._nodes import is_number, walk_nodes

UI_KEYS = ("positions", "panning", "noteFaces")
OUTPUT_KEY = "__output"
BOUND = 1e7                  # the editor's load guard: a coordinate at or beyond it is ignored


def a_position(v):
    return (isinstance(v, list) and len(v) == 2
            and all(is_number(x) and abs(x) < BOUND for x in v))


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("ui" and "groups" per key
    folded, "face pos" per face given its position, "position dropped" per
    entry dropped), empty when nothing changed. Everything is checked before
    anything is written, so a refused document is unchanged. Asks nothing of
    `ctx`."""
    has_ui, has_groups = "ui" in doc, "groups" in doc
    if not (has_ui or has_groups):
        return Counter()

    def refuse(why):
        raise ValueError("the document: %s" % why)

    if "layout" in doc:
        refuse("layout already present beside %s" % " and ".join(k for k in ("ui", "groups") if k in doc))
    ui = doc.get("ui", {})
    if not isinstance(ui, dict):
        refuse("ui is %r, not an object" % (ui,))
    extra = [k for k in ui if k not in UI_KEYS]
    if extra:
        refuse("ui holds %s beside positions, panning and noteFaces" % ", ".join("'%s'" % k for k in extra))
    positions = ui.get("positions", {})
    if not isinstance(positions, dict):
        refuse("ui.positions is %r, not an object" % (positions,))
    faces = ui.get("noteFaces", [])
    if not (isinstance(faces, list) and all(isinstance(f, dict) for f in faces)):
        refuse("ui.noteFaces is %r, not a list of objects" % (faces,))
    groups = doc.get("groups", [])
    if not isinstance(groups, list):
        refuse("groups is %r, not a list" % (groups,))
    ids = {n["id"] for n in walk_nodes(doc) if isinstance(n.get("id"), str)}
    labels = [f["label"] for f in faces if isinstance(f.get("label"), str)]
    for label in labels:
        if label in ids:
            refuse("the note face '%s' has a node's id as its label" % label)

    counts = Counter()
    kept, output, face_pos, dropped = {}, None, {}, []
    for key, value in positions.items():
        if not a_position(value):
            dropped.append("ui.positions '%s' dropped: %r is not two numbers under 1e7 in magnitude "
                           "(the editor ignores it at load)" % (key, value))
        elif key in ids:
            kept[key] = value
        elif key == OUTPUT_KEY:
            output = value
        elif key in labels:
            face_pos[key] = value
        else:
            dropped.append("ui.positions '%s' dropped: it names no node, no note face and not %s"
                           % (key, OUTPUT_KEY))
    new_faces = []
    for f in faces:
        f = dict(f)
        if isinstance(f.get("label"), str) and f["label"] in face_pos:
            f["pos"] = face_pos[f["label"]]
            counts["face pos"] += 1
        new_faces.append(f)
    lay = {}
    for k, v in (("positions", kept), ("output", output), ("panning", ui.get("panning")),
                 ("groups", groups), ("faces", new_faces)):
        if v is not None and v != [] and v != {}:
            lay[k] = v

    place = "ui" if has_ui else "groups"
    rewritten = {}
    for k, v in doc.items():
        if k == place:
            if lay:
                rewritten["layout"] = lay
        elif k not in ("ui", "groups"):
            rewritten[k] = v
    doc.clear()
    doc.update(rewritten)
    notes.extend(dropped)
    if has_ui:
        counts["ui"] += 1
    if has_groups:
        counts["groups"] += 1
    if dropped:
        counts["position dropped"] += len(dropped)
    return counts
