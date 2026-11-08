"""Rewrite the node types this engine no longer has, in place.

  python tools/migrate_deleted_types.py <patch-or-dir> [...]
  python tools/migrate_deleted_types.py --check <patch-or-dir> [...]   # exit 1 while a rewrite is pending

RULES, one per deleted name:
  KSPianoString -> KSString                   a registered alias of the same class; params untouched
  AREnvelope    -> Envelope, "preset": "ar"   the six preset classes: their knobs become the keys of the
  ASEnvelope    -> Envelope, "preset": "as"   Envelope node's preset form, in the same order, "preset" first
  ASREnvelope   -> Envelope, "preset": "asr"
  ADSEnvelope   -> Envelope, "preset": "ads"
  ADREnvelope   -> Envelope, "preset": "adr"
  ADSREnvelope  -> Envelope, "preset": "adsr", plus "release": 0.1 when the node has no release key
                   (the class's default; the adsr form's is 0.0)
  AdditiveSource2      refused: no mechanical form keeps the render; the node is
  BasicAdditiveSource  reported by name with the hand form to author instead
  FormantSpectrum -> FormantPeaks, by the fold below, with its Formant children
  BandSpectrum    -> FormantBands               the same class renamed; params untouched
  FixedSpectrum   refused: author a FormantBands or FormantPeaks
  Formant         refused when it reaches the rules: a peak no FormantSpectrum
                  consumed; author a one-row FormantPeaks

THE FOLD runs before the rules, because a spectrum's rewrite consumes other
nodes. A FormantSpectrum whose "formants" is a list of {"ref": id} becomes a
FormantPeaks of the same id, at the same place in the node list, whose four
arrays (frequency, gain, width, power) hold one row per peak in "formants"
order; the Formant nodes it named leave the list (and the editor's position
table). A peak missing a key takes the Formant's default for it (frequency
1000, gain 1, width 500, power 2), with a note. The spectrum is refused by
name when a peak is wired (a {"ref"} value, which no row can hold), when a
peak is referenced by anything else in the document, when an entry is not a
{"ref"} (the legacy inline form), names a node that is not a Formant or names
no node at all, or when the spectrum has a param besides "formants". A
FormantSequence names its spectra by id, which the fold keeps, so it follows.

THE PARAMMAP RULE: an instrument.paramMap entry {"target": "<peak>.<pin>",
"curve": [...]} aimed at a consumed peak is folded rather than refused when
the pin is one of the four and the curve's first knot's output equals the
peak's constant: the row keeps the constant, the entry leaves the paramMap (a
key's list left with one string collapses to that string, an empty key or
paramMap goes) and a note names the target and the constant. Any other aim at
a peak (a plain string target, a "vcurve", another pin, a first knot that
moves the pin) is refused by name: that shape is a human's.

The preset form's other defaults are the deleted classes' own, so a key the
node omitted stays omitted. A key the class never read but the form would —
attackMin/attackMax on an AREnvelope, the six clamps and timeMode on an
ADSREnvelope (the global allowlist let them through, dead) — is dropped and
reported by node, since carrying it across would change the render. A
dynamicPins entry naming a layout knob (attack, decay, release, decayLevel,
reverse, a curve or a power) is refused by name: those were settings on the
class and are keys of a form now, not pins. Key
order and the file's indent step survive (an inline object or array is laid
out one value per line, as every other rewrite here does); a file with
nothing to rewrite is not written; a second run changes nothing. A later
deletion adds a row to RULES, not a tool.
"""
import json
import os
import sys
from collections import Counter
from pathlib import Path

LAYOUT_KNOBS = {"attack", "attackCurve", "attackPower", "decay", "decayCurve", "decayPower",
                "decayLevel", "release", "releaseCurve", "releasePower", "reverse"}


class Rename:
    def __init__(self, to):
        self.to = to

    def apply(self, node, notes):
        node["type"] = self.to


class Refuse:
    """A deleted type with no mechanical rewrite: the node is reported by name
    with `hint`, the hand form to author instead, and its file is not written."""
    def __init__(self, hint):
        self.hint = hint

    def apply(self, node, notes):
        raise ValueError("node '%s' (%s): deleted; %s; no mechanical rewrite"
                         % (node.get("id"), node.get("type"), self.hint))


class Preset:
    """`dead` names the keys the class never read but the form would: carried
    across, they would change the render, so they are dropped with a note."""
    def __init__(self, name, defaults=None, dead=()):
        self.name = name
        self.defaults = defaults or {}
        self.dead = frozenset(dead)

    def apply(self, node, notes):
        pins = node.get("dynamicPins") or {}
        bad = sorted(k for k in pins if k in LAYOUT_KNOBS)
        if bad:
            raise ValueError("node '%s' (%s): dynamicPins names %s, a setting the class had; "
                             "the Envelope preset form has no pin for it"
                             % (node.get("id"), node.get("type"), ", ".join(bad)))
        params = node.get("params") or {}
        dropped = sorted(k for k in params if k in self.dead)
        if dropped:
            notes.append("node '%s' (%s): %s dropped: the class never read %s, the %s form would"
                         % (node.get("id"), node.get("type"), ", ".join(dropped),
                            "it" if len(dropped) == 1 else "them", self.name))
        rewritten = {"preset": self.name}
        rewritten.update((k, v) for k, v in params.items() if k not in self.dead)
        for k, v in self.defaults.items():
            rewritten.setdefault(k, v)
        node["type"] = "Envelope"
        node["params"] = rewritten


ADDITIVE_HINT = ("author an AdditiveSource with a FullPartials "
                 "(or ExplicitPartials under a CompositePartials for per-group envelopes)")

ADSR_CLAMPS = ("attackMin", "attackMax", "decayMin", "decayMax", "releaseMin", "releaseMax")

RULES = {
    "KSPianoString": Rename("KSString"),
    "AREnvelope": Preset("ar", dead=("attackMin", "attackMax")),
    "ASEnvelope": Preset("as"),
    "ASREnvelope": Preset("asr"),
    "ADSEnvelope": Preset("ads"),
    "ADREnvelope": Preset("adr"),
    "ADSREnvelope": Preset("adsr", defaults={"release": 0.1}, dead=ADSR_CLAMPS + ("timeMode",)),
    "AdditiveSource2": Refuse(ADDITIVE_HINT),
    "BasicAdditiveSource": Refuse(ADDITIVE_HINT),
    "BandSpectrum": Rename("FormantBands"),
    "FixedSpectrum": Refuse("author a FormantBands or FormantPeaks"),
    "Formant": Refuse("a peak outside a FormantSpectrum: author a one-row FormantPeaks"),
    # The fold consumes every FormantSpectrum in a node list or refuses it by
    # name; one reaching the rules sits outside any node list.
    "FormantSpectrum": Refuse("a spectrum outside a node list, which the fold does not reach: "
                              "author a FormantPeaks"),
}

PEAK_PINS = ("frequency", "gain", "width", "power")
PEAK_DEFAULTS = {"frequency": 1000.0, "gain": 1.0, "width": 500.0, "power": 2.0}   # the Formant's own


def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def count_refs(o, counts):
    """Count every {"ref": id} under `o`."""
    if isinstance(o, dict):
        r = o.get("ref")
        if isinstance(r, str) and len(o) == 1:
            counts[r] += 1
        for v in o.values():
            count_refs(v, counts)
    elif isinstance(o, list):
        for v in o:
            count_refs(v, counts)


def graphs(doc):
    """Every object holding a "nodes" list, wherever it sits."""
    found = []

    def walk(o):
        if isinstance(o, dict):
            if isinstance(o.get("nodes"), list):
                found.append(o)
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(doc)
    return found


def parammap_aims(doc):
    """Every instrument.paramMap entry as (key, entry, target): a string entry
    is its own target, an object's is its "target"."""
    inst = doc.get("instrument")
    pm = inst.get("paramMap") if isinstance(inst, dict) else None
    if not isinstance(pm, dict):
        return []
    aims = []
    for key, v in pm.items():
        for e in (v if isinstance(v, list) else [v]):
            target = e if isinstance(e, str) else e.get("target") if isinstance(e, dict) else None
            if isinstance(target, str):
                aims.append((key, e, target))
    return aims


def remove_parammap_entry(doc, key, entry):
    """Take one entry out of the paramMap: a key's list left with one string
    collapses to it, an empty key goes, an empty paramMap goes."""
    inst = doc["instrument"]
    pm = inst["paramMap"]
    v = pm[key]
    rest = [e for e in v if e is not entry] if isinstance(v, list) else []
    if not rest:
        del pm[key]
    elif len(rest) == 1 and isinstance(rest[0], str):
        pm[key] = rest[0]
    else:
        pm[key] = rest
    if not pm:
        del inst["paramMap"]


def fold_spectrum(spec, by_id, refs, aims, notes):
    """One FormantSpectrum's fold: returns (rows, consumed peak ids, paramMap
    entries to fold), or raises ValueError naming the spectrum."""
    sid = spec.get("id")

    def refuse(why):
        raise ValueError("node '%s' (FormantSpectrum): %s; no mechanical rewrite" % (sid, why))

    params = spec.get("params") or {}
    extra = sorted(k for k in params if k != "formants")
    if extra:
        refuse("params %s besides formants" % ", ".join(extra))
    entries = params.get("formants")
    if not isinstance(entries, list):
        refuse("formants is not a list of refs")
    rows = {pin: [] for pin in PEAK_PINS}
    consumed, folds = [], []
    for e in entries:
        if not (isinstance(e, dict) and list(e) == ["ref"] and isinstance(e["ref"], str)):
            refuse("a formants entry is not a {\"ref\"} (the inline form): %s" % json.dumps(e))
        cid = e["ref"]
        child = by_id.get(cid)
        if child is None:
            refuse("formants names '%s', which no node has as its id" % cid)
        if child.get("type") != "Formant":
            refuse("formants names '%s', a %s, not a Formant" % (cid, child.get("type")))
        if refs[cid] != 1 or cid in consumed:
            refuse("peak '%s' is referenced by another node too" % cid)
        others = sorted(k for k in child if k not in ("id", "type", "params"))
        if others:
            refuse("peak '%s' has %s besides its params" % (cid, ", ".join(others)))
        cparams = child.get("params") or {}
        for k, v in cparams.items():
            if k not in PEAK_PINS:
                refuse("peak '%s' has a param '%s', which no row holds" % (cid, k))
            if not is_number(v):
                refuse("peak '%s' has its %s wired (%s), which no row can hold" % (cid, k, json.dumps(v)))
        for pin in PEAK_PINS:
            if pin in cparams:
                rows[pin].append(cparams[pin])
            else:
                rows[pin].append(PEAK_DEFAULTS[pin])
                notes.append("node '%s' (FormantSpectrum): peak '%s' has no %s; its row takes the default (%s)"
                             % (sid, cid, pin, PEAK_DEFAULTS[pin]))
        for key, entry, target in aims:
            tid, _, pin = target.partition(".")
            if tid != cid:
                continue
            if not (isinstance(entry, dict) and sorted(entry) == ["curve", "target"]):
                refuse("paramMap '%s' aims at '%s' in a form the fold does not take: %s"
                       % (key, target, json.dumps(entry)))
            if pin not in PEAK_PINS:
                refuse("paramMap '%s' aims at '%s', not one of the four pins" % (key, target))
            curve = entry["curve"]
            knot = curve[0] if isinstance(curve, list) and curve else None
            first = knot[1] if isinstance(knot, list) and len(knot) == 2 else None
            const = cparams.get(pin, PEAK_DEFAULTS[pin])
            if not is_number(first) or first != const:
                refuse("paramMap '%s' drives '%s' from %s, not its constant (%s)"
                       % (key, target, json.dumps(first), const))
            folds.append((key, entry, target, const))
        consumed.append(cid)
    return rows, consumed, folds


def fold_formant_spectra(doc, notes):
    """THE FOLD (see the module doc): every FormantSpectrum in a node list
    becomes a FormantPeaks and its Formant children leave the list. Returns a
    Counter of spectra folded; raises ValueError naming a spectrum it refuses,
    before anything in `doc` changes."""
    plans = []
    for g in graphs(doc):
        nodes = g["nodes"]
        spectra = [n for n in nodes if isinstance(n, dict) and n.get("type") == "FormantSpectrum"]
        if not spectra:
            continue
        by_id = {n.get("id"): n for n in nodes if isinstance(n, dict)}
        refs = Counter()
        count_refs(nodes, refs)
        if isinstance(g.get("output"), str):                    # the graph's output names a node too
            refs[g["output"]] += 1
        aims = parammap_aims(doc) if g is doc.get("graph") else []
        taken = set()
        for spec in spectra:
            rows, consumed, folds = fold_spectrum(spec, by_id, refs, aims, notes)
            for cid in consumed:
                if cid in taken:
                    raise ValueError("node '%s' (FormantSpectrum): peak '%s' is referenced by another "
                                     "node too; no mechanical rewrite" % (spec.get("id"), cid))
                taken.add(cid)
            plans.append((g, spec, rows, consumed, folds))
    counts = Counter()
    for g, spec, rows, consumed, folds in plans:
        for key, entry, target, const in folds:
            remove_parammap_entry(doc, key, entry)
            notes.append("node '%s' (FormantSpectrum): paramMap target '%s' folded to a constant (%s); "
                         "the curve that drove it is gone" % (spec.get("id"), target, const))
        sid = spec.get("id")
        spec.clear()
        spec.update({"id": sid, "type": "FormantPeaks", "params": rows})
        gone = set(consumed)
        g["nodes"][:] = [n for n in g["nodes"]
                         if not (isinstance(n, dict) and n.get("id") in gone and n.get("type") == "Formant")]
        ui = doc.get("ui")
        positions = ui.get("positions") if isinstance(ui, dict) else None
        if isinstance(positions, dict) and g is doc.get("graph"):
            for cid in consumed:
                positions.pop(cid, None)
        counts["FormantSpectrum"] += 1
    return counts


def rewrite(doc, notes=None):
    """Apply the fold, then RULES, to every node in `doc`, wherever its node
    list sits. Returns a Counter of rewrites per deleted name; appends a line
    to `notes` for every key dropped, default filled or paramMap entry folded.
    Raises ValueError for a node the fold or the rules refuse."""
    if notes is None:
        notes = []
    counts = fold_formant_spectra(doc, notes)

    def walk(o):
        if isinstance(o, dict):
            t = o.get("type")
            if isinstance(t, str) and t in RULES and ("id" in o or "params" in o):
                RULES[t].apply(o, notes)
                counts[t] += 1
            for v in list(o.values()):
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(doc)
    return counts


def indent_of(text):
    """The file's own indent step: the leading spaces of its first indented
    line (2 when it has none), so a rewrite keeps the file's shape."""
    for line in text.splitlines():
        stripped = line.lstrip(" ")
        if stripped != line and stripped[:1] in "\"{[":
            return len(line) - len(stripped)
    return 2


def rewrite_file(path, check=False):
    """Rewrite one file in place (unless `check`). Returns the Counter."""
    text = Path(path).read_text(encoding="utf-8")
    doc = json.loads(text)
    notes = []
    counts = rewrite(doc, notes)
    for line in notes:
        print("%s: %s" % (path, line))
    if counts and not check:
        Path(path).write_text(json.dumps(doc, indent=indent_of(text)) + "\n",
                              encoding="utf-8", newline="\n")
    return counts


def json_files(roots):
    for root in roots:
        p = Path(root)
        if p.is_dir():
            for dirpath, _, names in os.walk(p):
                for n in sorted(names):
                    if n.endswith(".json"):
                        yield Path(dirpath) / n
        else:
            yield p


def main(argv):
    check = "--check" in argv
    roots = [a for a in argv if a != "--check"]
    if not roots:
        print(__doc__)
        return 2
    total, pending_files, failed = Counter(), 0, 0
    for path in json_files(roots):
        try:
            counts = rewrite_file(path, check)
        except (ValueError, OSError, json.JSONDecodeError) as e:
            print("%s: NOT rewritten: %s" % (path, e))
            failed += 1
            continue
        if counts:
            pending_files += 1
            total.update(counts)
            print("%s: %s" % (path, ", ".join("%s x%d" % kv for kv in sorted(counts.items()))))
    verb = "pending in" if check else "rewritten in"
    print("%d node(s) %s %d file(s): %s" % (sum(total.values()), verb, pending_files,
                                             dict(sorted(total.items())) or "nothing"))
    if check:
        return 1 if pending_files or failed else 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
