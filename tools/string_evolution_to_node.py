"""Rewrite a wavetable's string-typed evolution as an evolution node.

The loader once built an evolution from a string on the WavetableSource node
("evolution": "pluck" | "averaging" | "target", with the evolution's settings
and "evolutionSeed" beside it). That path is gone: an evolution is a node of
its own, wired into the wavetable's `evolution` slot. This tool moves the keys.

  python tools/string_evolution_to_node.py patch.json [more.json ...]

For each WavetableSource whose "evolution" is a string, a node
{"id": "<wavetable id>_evolution", "type": <PluckEvolution | AveragingEvolution
| TargetEvolution>, "params": {<the moved keys>, "seed": <evolutionSeed, or 42
— the string form's default>}} is inserted immediately before the wavetable,
and the wavetable's "evolution" becomes {"ref": "<that id>"}. Only the keys
present move, plus the two averaging defaults the node type declares
differently (sampleCount 2.0, decayFactor 0.996), written out. A node-form
wavetable that still carries "evolutionSeed" (a key only the string form read)
loses it. A patch with nothing to convert is left untouched. "targetWave" (an
explicit period) has no node form and is refused. The file is rewritten with an
indent of 2.
"""
import json
import sys
from pathlib import Path

FORMS = {
    "pluck":     ("PluckEvolution",     ("muting",)),
    "averaging": ("AveragingEvolution", ("sampleCount", "speed", "decayFactor", "leading", "autoAdjust")),
    "target":    ("TargetEvolution",    ("morphRate", "holdCycles", "decayFactor", "targetPartials", "targetLength")),
}
# Where the deleted branch's default for an absent key differed from the node
# type's declared default, the converter writes the branch's, so a converted
# patch renders as the string form did (every other default is equal).
BRANCH_DEFAULTS = {
    "averaging": {"sampleCount": 2.0, "decayFactor": 0.996},
}
DEFAULT_SEED = 42          # uint32_t(p.value("evolutionSeed", 42)) in the deleted loader branch


def node_container(doc):
    """The dict that owns the "nodes" list: "graph" in an instrument patch, the
    document itself in a node-graph patch; None when there is neither."""
    g = doc.get("graph")
    if isinstance(g, dict) and isinstance(g.get("nodes"), list):
        return g
    if isinstance(doc.get("nodes"), list):
        return doc
    return None


def _fresh_id(base, taken):
    candidate, n = base, 2
    while candidate in taken:
        candidate = "%s%d" % (base, n)
        n += 1
    return candidate


def convert(doc):
    """Convert every string-typed evolution in `doc` in place. Returns how many
    wavetables were converted. Raises ValueError for a form with no node."""
    g = node_container(doc)
    if g is None:
        return 0
    taken = {n.get("id") for n in g["nodes"] if isinstance(n, dict)}
    out, converted = [], 0
    for node in g["nodes"]:
        params = node.get("params") if isinstance(node, dict) else None
        wavetable = isinstance(node, dict) and node.get("type") == "WavetableSource" and isinstance(params, dict)
        if wavetable and not isinstance(params.get("evolution"), str) and "evolutionSeed" in params:
            del params["evolutionSeed"]          # the string form's key on a node-form wavetable: never read
            out.append(node)
            converted += 1
            continue
        if not (wavetable and isinstance(params.get("evolution"), str)):
            out.append(node)
            continue
        form = params["evolution"]
        if form not in FORMS:
            raise ValueError("node '%s': evolution \"%s\" has no node form" % (node.get("id"), form))
        if "targetWave" in params:
            raise ValueError("node '%s': targetWave (an explicit period) has no node form; "
                             "express the target as targetPartials" % node.get("id"))
        type_name, keys = FORMS[form]
        ev_id = _fresh_id("%s_evolution" % node.get("id"), taken)
        taken.add(ev_id)
        ev_params = {k: params[k] for k in keys if k in params}
        for k, v in BRANCH_DEFAULTS.get(form, {}).items():
            ev_params.setdefault(k, v)
        ev_params["seed"] = int(params.get("evolutionSeed", DEFAULT_SEED))
        moved = set(keys) | {"evolutionSeed"}
        node["params"] = {k: ({"ref": ev_id} if k == "evolution" else v)
                          for k, v in params.items() if k not in moved}
        out.append({"id": ev_id, "type": type_name, "params": ev_params})
        out.append(node)
        converted += 1
    g["nodes"] = out
    return converted


def convert_file(path):
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    n = convert(doc)
    if n:
        Path(path).write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")
    return n


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    failed = 0
    for arg in argv:
        try:
            n = convert_file(arg)
        except (ValueError, OSError, json.JSONDecodeError) as e:
            print("%s: NOT converted: %s" % (arg, e))
            failed += 1
            continue
        print("%s: %s" % (arg, ("converted %d wavetable(s)" % n) if n else "nothing to convert"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
