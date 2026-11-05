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
            notes.append("node '%s' (%s): %s dropped — the class never read %s, the %s form would"
                         % (node.get("id"), node.get("type"), ", ".join(dropped),
                            "it" if len(dropped) == 1 else "them", self.name))
        rewritten = {"preset": self.name}
        rewritten.update((k, v) for k, v in params.items() if k not in self.dead)
        for k, v in self.defaults.items():
            rewritten.setdefault(k, v)
        node["type"] = "Envelope"
        node["params"] = rewritten


ADSR_CLAMPS = ("attackMin", "attackMax", "decayMin", "decayMax", "releaseMin", "releaseMax")

RULES = {
    "KSPianoString": Rename("KSString"),
    "AREnvelope": Preset("ar", dead=("attackMin", "attackMax")),
    "ASEnvelope": Preset("as"),
    "ASREnvelope": Preset("asr"),
    "ADSEnvelope": Preset("ads"),
    "ADREnvelope": Preset("adr"),
    "ADSREnvelope": Preset("adsr", defaults={"release": 0.1}, dead=ADSR_CLAMPS + ("timeMode",)),
}


def rewrite(doc, notes=None):
    """Apply RULES to every node in `doc`, wherever its node list sits.
    Returns a Counter of rewrites per deleted name; appends a line to `notes`
    for every key dropped. Raises ValueError for a node the rules refuse."""
    counts = Counter()
    if notes is None:
        notes = []

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
