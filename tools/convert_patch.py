"""Rewrite patches into the form this engine reads, in place.

  python tools/convert_patch.py <file-or-folder> [...]
  python tools/convert_patch.py --check <file-or-folder> [...]   # exit 1 while a rewrite is pending

A folder is walked recursively for .json files, folder and file names sorted,
so a run visits files in one order on every machine. A file whose top
level is not an object is not a patch and is left as it is. Each other file is
read, parsed, and handed to the rules in tools/convert_rules/, in this order,
each on the one document the previous left:

  deleted_types     the node types this engine no longer has
  string_evolution  a wavetable's string-typed evolution, as an evolution node
  parammap          instrument.paramMap, as graph wiring

The deleted-type rule runs before the paramMap rule because its formant fold
reads paramMap entries aimed at the peaks it consumes and it renames types the
paramMap rule looks up in the registry. Each rule's own docstring says what it
does to a document. Each rule's notes are printed prefixed by the path, and a
changed file gets one line with each rule's counts.

A file is written only when a rule changed it: in place, with the file's own
indent step (an inline object or array is laid out one value per line), LF
line endings, a final newline and key order kept. A file with nothing to
rewrite is not written, so a second run changes nothing. A file that does not
read or parse, or that any rule refuses (each rule's docstring says what it
refuses), is not written at all, even when an earlier rule changed the document:
it is reported as "NOT rewritten" with the reason, and the run goes on.

Exit 1 if any file was refused or unreadable, else 0. With --check nothing is
written, and the exit is 1 while any rewrite is pending or any file is
refused, else 0. With no file or folder: this text, exit 2. A later rewrite
adds a rule, not a tool.
"""
import json
import os
import sys
from collections import Counter
from pathlib import Path

from convert_rules import RULES, Context, rule_name


def indent_of(text):
    """The file's own indent step: the leading spaces of its first indented
    line (2 when it has none), so a rewrite keeps the file's shape."""
    for line in text.splitlines():
        stripped = line.lstrip(" ")
        if stripped != line and stripped[:1] in "\"{[":
            return len(line) - len(stripped)
    return 2


def json_files(roots):
    for root in roots:
        p = Path(root)
        if p.is_dir():
            for dirpath, dirs, names in os.walk(p):
                dirs.sort()
                for n in sorted(names):
                    if n.endswith(".json"):
                        yield Path(dirpath) / n
        else:
            yield p


def convert_file(path, check=False, ctx=None):
    """Apply RULES to one file and write it in place (unless `check`) when a
    rule changed it. Returns {rule name: Counter} for the rules that changed
    the document, in rule order. A rule's ValueError propagates and nothing is
    written; the notes are printed only for a file every rule accepted."""
    if ctx is None:
        ctx = Context()
    text = Path(path).read_text(encoding="utf-8")
    doc = json.loads(text)
    if not isinstance(doc, dict):
        return {}                      # a folder's manifest, say: not a patch
    notes = []
    changed = {}
    for rule in RULES:
        counts = rule.apply(doc, notes, ctx)
        if counts:
            changed[rule_name(rule)] = counts
    for line in notes:
        print("%s: %s" % (path, line))
    if changed and not check:
        Path(path).write_text(json.dumps(doc, indent=indent_of(text)) + "\n",
                              encoding="utf-8", newline="\n")
    return changed


def main(argv, ctx=None):
    check = "--check" in argv
    roots = [a for a in argv if a != "--check"]
    if not roots:
        print(__doc__)
        return 2
    if ctx is None:
        ctx = Context()
    total = {rule_name(r): Counter() for r in RULES}
    changed_files, failed = 0, 0
    for path in json_files(roots):
        try:
            changed = convert_file(path, check, ctx)
        except (ValueError, OSError, json.JSONDecodeError) as e:
            print("%s: NOT rewritten: %s" % (path, e))
            failed += 1
            continue
        if changed:
            changed_files += 1
            for name, counts in changed.items():
                total[name].update(counts)
            print("%s: %s" % (path, "; ".join(
                "%s: %s" % (name, ", ".join("%s x%d" % kv for kv in sorted(counts.items())))
                for name, counts in changed.items())))
    verb = "pending" if check else "rewritten"
    print("%d file(s) %s, %d refused; %s" % (
        changed_files, verb, failed,
        "; ".join("%s: %s" % (name, dict(sorted(c.items())) or "nothing") for name, c in total.items())))
    if check:
        return 1 if changed_files or failed else 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
