"""The baseline: every violation the tree had when it was recorded, by file,
rule and key, with its value. A measured value may only stay at or below its
entry; a violation with no entry fails. Pure functions over measurements,
plus load and save of the one file.

An entry is {"file", "rule", "key", "value"}:
  file_lines      key ""                       value = the file's lines
  function_lines  key = the function's key     value = its length
  mutable_vars    key ""                       value = the count in the file
  const_cast      key ""                       value = lines using it
  dynamic_cast    key ""                       value = lines using it (engine/ files)
  changelog       key ""                       value = comment lines holding a date or a
                                               tracker id (history in words is a reviewer's)
  include         key = the #include target    value = 1
  unmapped        key ""                       value = 1 (a file on no module)

A function's key is its signature (lizard's long name: qualified name and
parameter list), so an entry survives edits elsewhere in the file and a new
long function beside a recorded one is a new key. Functions that share a
signature in one file (two lambdas in one host, say) are told apart by
their order among those that share it: " #2", " #3".
"""
import json
from pathlib import Path

RULES = ("file_lines", "function_lines", "mutable_vars", "const_cast", "dynamic_cast",
         "changelog", "include", "unmapped")


def function_keys(functions):
    """[(key, length)] for [(name, signature, start, length)], in file order."""
    seen, out = {}, []
    for _, sig, _, length in functions:
        seen[sig] = seen.get(sig, 0) + 1
        out.append((sig if seen[sig] == 1 else f"{sig} #{seen[sig]}", length))
    return out


def violations(files, include_violations, limits):
    """Every violation in the measured files, as entries. `files` are the
    meter's per-file measurements (with "signatures" from measure.functions);
    `include_violations` the rules module's findings."""
    out = []

    def add(path, rule, key, value):
        out.append({"file": path, "rule": rule, "key": key, "value": value})

    for f in files:
        p = f["path"]
        if f["lines"] > limits["file_lines"]:
            add(p, "file_lines", "", f["lines"])
        for key, length in function_keys(f["signatures"]):
            if length > limits["function_lines"]:
                add(p, "function_lines", key, length)
        if f["mutable_vars"] > limits["file_level_mutable_vars"]:
            add(p, "mutable_vars", "", f["mutable_vars"])
        if f["flags"]["const_cast"]:
            add(p, "const_cast", "", f["flags"]["const_cast"])
        if f["flags"]["dynamic_cast"] and p.startswith("engine/"):
            add(p, "dynamic_cast", "", f["flags"]["dynamic_cast"])
        if f["flags"]["changelog_comments"]:
            add(p, "changelog", "", f["flags"]["changelog_comments"])
        if f["module"] is None:
            add(p, "unmapped", "", 1)
    for v in include_violations:
        add(v["file"], "include", v["include"], 1)
    return sorted(out, key=_order)


def _order(e):
    return (e["file"], RULES.index(e["rule"]), e["key"])


def _limit(rule, limits):
    return {"file_lines": limits["file_lines"], "function_lines": limits["function_lines"],
            "mutable_vars": limits["file_level_mutable_vars"]}.get(rule, 0)


def compare(measured, base, limits):
    """Failures: a measured violation with no entry, or above its entry.
    [(file, rule, key, measured value, allowed value)]. A value below its
    entry passes."""
    allowed = {(e["file"], e["rule"], e["key"]): e["value"] for e in base}
    out = []
    for e in measured:
        k = (e["file"], e["rule"], e["key"])
        limit = allowed.get(k, _limit(e["rule"], limits))
        if e["value"] > limit:
            out.append((e["file"], e["rule"], e["key"], e["value"], limit))
    return out


def tighten(measured, base):
    """The baseline lowered to what is measured: each entry takes the smaller
    of its value and the measured one, and an entry whose violation is gone
    is dropped. Never raises an entry, never adds one."""
    now = {(e["file"], e["rule"], e["key"]): e["value"] for e in measured}
    out = []
    for e in base:
        k = (e["file"], e["rule"], e["key"])
        if k in now:
            out.append({**e, "value": min(e["value"], now[k])})
    return sorted(out, key=_order)


def dumps(entries):
    """One entry per line, sorted, so the file's diff reads as a table."""
    lines = [json.dumps(e, sort_keys=True, ensure_ascii=False) for e in sorted(entries, key=_order)]
    return "[\n" + ",\n".join(lines) + "\n]\n"


def load(path):
    p = Path(path)
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else []


def save(path, entries):
    Path(path).write_text(dumps(entries), encoding="utf-8", newline="\n")


def counts(entries):
    """{rule: number of entries}."""
    out = {r: 0 for r in RULES}
    for e in entries:
        out[e["rule"]] += 1
    return out
