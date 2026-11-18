"""The converter's rules, in the order tools/convert_patch.py applies them, and
the context they share.

A rule is a module exposing `apply(doc, notes, ctx) -> collections.Counter`:
it mutates `doc` in place, appends human-readable lines to `notes`, returns a
Counter that is non-empty if and only if it changed the document, and raises
ValueError to refuse the file.
"""
import json
import subprocess
from pathlib import Path

from . import (curve_table, deleted_types, envelope_table, expand_rule_node, key_renames,
               multi_weights, old_defaults, parammap, phased_table, string_evolution)

ROOT = Path(__file__).resolve().parent.parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
DESC_CACHE = ROOT / "renders/scratch/descriptors.json"

# The deleted-type rule's formant fold reads the paramMap entries aimed at the
# peaks it consumes, and it renames types the paramMap rule looks up in the
# registry, so the paramMap rule runs after it. The string-evolution rule is
# independent of both. The two table rules run last: the deleted-type rule
# turns the six preset classes into preset-form Envelopes the envelope-table
# rule reads, and the paramMap rule writes each curve as a CurveNode with a
# knot list, which the curve-table rule rewrites as columns. The five rules
# after them each rewrite one old form of a node type no earlier rule writes
# in that form, and they come last for two reasons: the old-defaults rule
# writes "val": 0.0 only where a VarSource holds no val, so it runs after the
# paramMap rule, which may wire one; the expand-rule rule adds a node to a
# node list, so it runs after the rules that add nodes or take them away (the
# deleted-type rule's formant fold, the paramMap rule), and the ids it checks
# its new ones against are the document's last.
RULES = (deleted_types, string_evolution, parammap, envelope_table, curve_table,
         key_renames, multi_weights, phased_table, old_defaults, expand_rule_node)


def rule_name(rule):
    return rule.__name__.rsplit(".", 1)[-1]


def descriptors():
    """{typeName: {"params": [...], "settings": [...], ...}} straight from the
    registry. Regenerated whenever the exe is newer than the cache, so a
    rebuilt engine cannot leave a stale map behind."""
    if DESC_CACHE.exists() and DESC_CACHE.stat().st_mtime > CLI.stat().st_mtime:
        return json.loads(DESC_CACHE.read_text(encoding="utf-8"))
    r = subprocess.run([str(CLI), "--dump-descriptors"],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        raise RuntimeError("mforce_cli --dump-descriptors failed: " + r.stderr)
    DESC_CACHE.parent.mkdir(parents=True, exist_ok=True)
    DESC_CACHE.write_text(r.stdout, encoding="utf-8")
    return json.loads(r.stdout)


class Context:
    """What a rule may ask of the world beyond its document. `descriptors()`
    is the registry's table; nothing runs until a rule asks, and the answer is
    kept for the rest of the run. `Context(descriptors=table)` answers from
    `table`, so a test needs no built CLI."""
    def __init__(self, descriptors=None):
        self._descriptors = descriptors

    def descriptors(self):
        if self._descriptors is None:
            self._descriptors = descriptors()
        return self._descriptors
