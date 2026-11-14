"""Rewrite an Envelope's stage list as the eight columns of its stage table.

On an Envelope whose params hold "stages" (a list of stage objects), the list
becomes eight keys, written where "stages" stood, the other keys keeping their
order: startVal, endVal, power, type, percent, minSec, maxSec, nominal. A
cell is the stage's own JSON value; a key the stage omits is the value the
stage-list loader read for it, 0.0, and "Linear" for type. The loader
compared each word exactly, so:
  type      Linear, Expo, InverseExpo, Sine or Hold is kept; any other string
            is written "Linear", what it read, with a note
  timeMode  "seconds" or "fraction" on the node is kept; any other string is
            written "fraction", what it read, with a note
A stage's holdPct at 0, a key the loader never read, is dropped. A key of the
preset form on a stage list (every one but sustainLevel and timeMode, which
the stage list reads too) was never read there and is dropped with a note.
sustainLevel is a view of a four-slot stage list (four stages, the third of
percent 0, the decay's end, the third's start and end and the release's start
equal in float32): on that shape a number equal to the slots is kept; on any
other shape the key is left as it is.

On a preset-form Envelope ("preset" one of the six forms, no "stages"), a key
of the preset form that the node's form does not read is dropped with a note:
PRESET_READS is read off envelope_json.h builder by builder. sustainLevel is
a setting on every Envelope and is never dropped; timeMode is dead on every
form but adsr. A preset name outside the six is the loader's to refuse, and
the node is left as it is; so is an Envelope with neither form.

Refused, naming the node, before anything in the document changes: "stages"
that is not a list of objects; a stage key outside the eight and holdPct; a
holdPct that is not 0; a value that is not a number where a number goes (a
bool is not one); a type or timeMode that is not a string; "stages" beside
"preset" or beside a column key; a numeric sustainLevel on a four-slot list
that differs from its slots; a column key on a preset-form node.
"""
import math
import struct
from collections import Counter

from ._nodes import is_number, node_label, walk_nodes

COLUMNS = ("startVal", "endVal", "power", "type", "percent", "minSec", "maxSec", "nominal")
NUMERIC = tuple(c for c in COLUMNS if c != "type")
RAMP_TYPES = ("Linear", "Expo", "InverseExpo", "Sine", "Hold")
TIME_MODES = ("fraction", "seconds")

# envelope_preset_keys(), in its order.
PRESET_KEYS = ("preset", "attack", "attackCurve", "attackPower", "attackMin", "attackMax",
               "decay", "decayCurve", "decayPower", "decayMin", "decayMax", "decayLevel",
               "release", "releaseCurve", "releasePower", "releaseMin", "releaseMax",
               "reverse", "sustainLevel", "timeMode")

# The preset keys a stage list never read: all but the two it reads itself.
STAGE_DEAD_KEYS = tuple(k for k in PRESET_KEYS if k not in ("preset", "sustainLevel", "timeMode"))

# What each builder in envelope_json.h reads besides "preset": its factory's
# p.value(...) keys, and "<stage>Curve" and "<stage>Power" for each shape(...)
# call.
PRESET_READS = {
    "ar":   ("attack", "attackMin", "attackMax",
             "attackCurve", "attackPower", "releaseCurve", "releasePower"),
    "as":   ("attack", "sustainLevel", "reverse",
             "attackCurve", "attackPower"),
    "asr":  ("attack", "sustainLevel", "release",
             "attackCurve", "attackPower", "releaseCurve", "releasePower"),
    "ads":  ("attack", "decay", "sustainLevel",
             "attackCurve", "attackPower", "decayCurve", "decayPower"),
    "adr":  ("attack", "decay", "decayLevel",
             "attackCurve", "attackPower", "decayCurve", "decayPower", "releaseCurve", "releasePower"),
    "adsr": ("timeMode", "attack", "decay", "sustainLevel", "release",
             "attackMin", "attackMax", "decayMin", "decayMax", "releaseMin", "releaseMax",
             "attackCurve", "attackPower", "decayCurve", "decayPower", "releaseCurve", "releasePower"),
}


def f32(x):
    """`x` as the float the engine holds (beyond float range, an infinity)."""
    try:
        return struct.unpack("<f", struct.pack("<f", x))[0]
    except OverflowError:
        return math.copysign(math.inf, x)


def four_slot(cols):
    """The engine's four-slot sustain invariant over the columns, in float32."""
    if len(cols["percent"]) != 4:
        return False
    s, e = cols["startVal"], cols["endVal"]
    return (f32(cols["percent"][2]) == 0.0
            and f32(e[1]) == f32(s[2]) == f32(e[2]) == f32(s[3]))


def plan_stages(node, params):
    """Check one stage-form node; returns its write, or raises ValueError."""
    name = "node %s (Envelope)" % node_label(node)

    def refuse(why):
        raise ValueError("%s: %s" % (name, why))

    if "preset" in params:
        refuse("stages beside preset; a node takes one form")
    clash = [c for c in COLUMNS if c in params]
    if clash:
        refuse("stages beside the column%s %s" % ("s" if len(clash) > 1 else "", ", ".join(clash)))
    stages = params["stages"]
    if not (isinstance(stages, list) and all(isinstance(s, dict) for s in stages)):
        refuse("stages is not a list of stage objects")
    cols = {c: [] for c in COLUMNS}
    labels = []
    for i, s in enumerate(stages):
        extra = sorted(k for k in s if k not in COLUMNS and k != "holdPct")
        if extra:
            refuse("stage %d has %s, which no column holds" % (i, ", ".join(extra)))
        if "holdPct" in s and not (is_number(s["holdPct"]) and s["holdPct"] == 0):
            refuse("stage %d has holdPct %r; the loader ignored it and no column holds it"
                   % (i, s["holdPct"]))
        for c in NUMERIC:
            v = s.get(c, 0.0)
            if not is_number(v):
                refuse("stage %d %s is %r, not a number" % (i, c, v))
            cols[c].append(v)
        t = s.get("type", "Linear")
        if not isinstance(t, str):
            refuse("stage %d type is %r, not a string" % (i, t))
        if t not in RAMP_TYPES:
            labels.append("%s: stage %d type %r is not one the loader knew; written as "
                          "'Linear', which it read" % (name, i, t))
            t = "Linear"
        cols["type"].append(t)
    time_mode = None
    if "timeMode" in params:
        tm = params["timeMode"]
        if not isinstance(tm, str):
            refuse("timeMode is %r, not a string" % (tm,))
        if tm not in TIME_MODES:
            labels.append("%s: timeMode %r is not one the loader knew; written as "
                          "'fraction', which it read" % (name, tm))
            time_mode = "fraction"
    sustain = params.get("sustainLevel")
    if is_number(sustain) and four_slot(cols) and f32(sustain) != f32(cols["endVal"][1]):
        refuse("sustainLevel %r differs from the sustain slots of its four-slot stage list (%r)"
               % (sustain, cols["endVal"][1]))
    dead = [k for k in params if k in STAGE_DEAD_KEYS]

    def write(notes, counts):
        rewritten = {}
        for k, v in params.items():
            if k == "stages":
                rewritten.update(cols)
            elif k in dead:
                continue
            elif k == "timeMode" and time_mode is not None:
                rewritten[k] = time_mode
            else:
                rewritten[k] = v
        params.clear()
        params.update(rewritten)
        for k in dead:
            notes.append("%s: %s dropped: a stage list never read it" % (name, k))
        notes.extend(labels)
        counts["stage list"] += 1
        if dead:
            counts["dead key"] += len(dead)
        if labels:
            counts["label"] += len(labels)
    return write


def plan_preset(node, params):
    """Check one preset-form node; returns its write, None when there is
    nothing to write, or raises ValueError."""
    preset = params["preset"]
    if not (isinstance(preset, str) and preset in PRESET_READS):
        return None
    name = "node %s (Envelope)" % node_label(node)
    clash = [c for c in COLUMNS if c in params]
    if clash:
        raise ValueError("%s: preset %r beside the column%s %s; a node takes one form"
                         % (name, preset, "s" if len(clash) > 1 else "", ", ".join(clash)))
    reads = PRESET_READS[preset]
    dead = [k for k in params
            if k in PRESET_KEYS and k not in ("preset", "sustainLevel") and k not in reads]
    if not dead:
        return None

    def write(notes, counts):
        for k in dead:
            del params[k]
            notes.append("%s: %s dropped: the %s form never read it" % (name, k, preset))
        counts["dead key"] += len(dead)
    return write


def apply(doc, notes, ctx):
    """The rule's entry point. Returns a Counter ("stage list" per list
    rewritten, "dead key" per key dropped, "label" per word written as the
    one the loader read), empty when nothing changed. Every node is checked
    before any is written, so a refused document is unchanged. Asks nothing
    of `ctx`."""
    writes = []
    for node in walk_nodes(doc):
        params = node.get("params")
        if node.get("type") != "Envelope" or not isinstance(params, dict):
            continue
        if "stages" in params:
            writes.append(plan_stages(node, params))
        elif "preset" in params:
            w = plan_preset(node, params)
            if w is not None:
                writes.append(w)
    counts = Counter()
    for w in writes:
        w(notes, counts)
    return counts
