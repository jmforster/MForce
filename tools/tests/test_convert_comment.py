"""The comment rule: a top-level _comment as comment, in its place; a
_provenance object as one "key: value" line per entry, in its order; both
as the _comment text, a blank line and the provenance lines, at _comment's
place; a node's _comment as its comment; the refusals (each leaving the
document unchanged); idempotence; a file with nothing to do."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import comment  # noqa: E402


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the comment rule asked for descriptors")


def patch(**top):
    doc = {"sampleRate": 48000}
    doc.update(top)
    doc.setdefault("graph", {"nodes": [{"id": "osc", "type": "SineSource", "params": {}}], "output": "osc"})
    return doc


PROVENANCE = {"source": "iowa/soprano_I.wav", "note": "A4", "why": "the vowel's reference", "run": 17}
PROVENANCE_TEXT = "source: iowa/soprano_I.wav\nnote: A4\nwhy: the vowel's reference\nrun: 17"


def apply(doc):
    notes = []
    counts = comment.apply(doc, notes, NoContext())
    return counts, notes


class TopLevelTests(unittest.TestCase):
    def test_a_comment_takes_its_place(self):
        doc = {"_comment": "a bell", "sampleRate": 48000, "graph": {"nodes": [], "output": "x"}}
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"_comment": 1}))
        self.assertEqual(notes, [])
        self.assertEqual(doc, {"comment": "a bell", "sampleRate": 48000, "graph": {"nodes": [], "output": "x"}})
        self.assertEqual(list(doc), ["comment", "sampleRate", "graph"])

    def test_provenance_becomes_lines_in_its_order(self):
        doc = {"sampleRate": 48000, "_provenance": dict(PROVENANCE), "graph": {"nodes": [], "output": "x"}}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"_provenance": 1}))
        self.assertEqual(list(doc), ["sampleRate", "comment", "graph"])
        self.assertEqual(doc["comment"], PROVENANCE_TEXT)

    def test_numbers_and_strings_as_values(self):
        doc = patch(_provenance={"a": 1, "b": 0.5, "c": "x y", "d": -2e-05, "e": ""})
        apply(doc)
        self.assertEqual(doc["comment"], "a: 1\nb: 0.5\nc: x y\nd: -2e-05\ne: ")

    def test_both_at_the_comments_place(self):
        for order in (("_comment", "_provenance"), ("_provenance", "_comment")):
            doc = {"sampleRate": 48000}
            for k in order:
                doc[k] = "the vowel" if k == "_comment" else dict(PROVENANCE)
            doc["graph"] = {"nodes": [], "output": "x"}
            counts, _ = apply(doc)
            self.assertEqual(counts, Counter({"_comment": 1, "_provenance": 1}))
            self.assertEqual(list(doc), ["sampleRate", "comment", "graph"])
            self.assertEqual(doc["comment"], "the vowel\n\n" + PROVENANCE_TEXT)

    def test_both_apart_take_the_comments_place(self):
        for first, last, order in (("_comment", "_provenance", ["comment", "sampleRate", "graph"]),
                                   ("_provenance", "_comment", ["sampleRate", "graph", "comment"])):
            doc = {first: None, "sampleRate": 48000, "graph": {"nodes": [], "output": "x"}, last: None}
            doc["_comment"], doc["_provenance"] = "the vowel", {"run": 3}
            apply(doc)
            self.assertEqual(list(doc), order)
            self.assertEqual(doc["comment"], "the vowel\n\nrun: 3")


class NodeTests(unittest.TestCase):
    def test_a_nodes_comment_takes_its_place(self):
        doc = patch(graph={"nodes": [{"id": "osc", "_comment": "the body", "type": "SineSource", "params": {}},
                                     {"id": "o2", "type": "SineSource"}], "output": "osc"})
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"node _comment": 1}))
        self.assertEqual(notes, [])
        n = doc["graph"]["nodes"][0]
        self.assertEqual(n, {"id": "osc", "comment": "the body", "type": "SineSource", "params": {}})
        self.assertEqual(list(n), ["id", "comment", "type", "params"])
        self.assertEqual(doc["graph"]["nodes"][1], {"id": "o2", "type": "SineSource"})

    def test_a_nested_node_list(self):
        inner = {"id": "in", "type": "SineSource", "_comment": "inner"}
        doc = patch(graph={"nodes": [{"id": "h", "type": "MultiplexSource",
                                      "params": {"template": {"nodes": [inner]}}}], "output": "h"})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"node _comment": 1}))
        self.assertEqual(inner, {"id": "in", "type": "SineSource", "comment": "inner"})

    def test_both_levels_in_one_file(self):
        doc = patch(_comment="top", graph={"nodes": [{"id": "a", "_comment": "one"},
                                                     {"id": "b", "_comment": "two"}], "output": "a"})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"_comment": 1, "node _comment": 2}))
        self.assertEqual(doc["comment"], "top")
        self.assertEqual([n["comment"] for n in doc["graph"]["nodes"]], ["one", "two"])


class RefusalTests(unittest.TestCase):
    def refused(self, doc, needles):
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            comment.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_comment_beside_comment_or_provenance(self):
        self.refused(patch(_comment="a", comment="b"), ["comment", "_comment"])
        self.refused(patch(_provenance={"a": 1}, comment="b"), ["comment", "_provenance"])

    def test_a_comment_that_is_not_a_string(self):
        for bad in (1, None, ["a"], {"a": "b"}, True):
            self.refused(patch(_comment=bad), ["_comment", repr(bad)])

    def test_a_provenance_that_is_not_an_object_of_scalars(self):
        for bad in ("a", ["a"], None, 3, {"a": None}, {"a": True}, {"a": [1]}, {"a": {"b": 1}}):
            self.refused(patch(_provenance=bad), ["_provenance"])

    def test_on_a_node(self):
        self.refused(patch(graph={"nodes": [{"id": "osc", "_comment": "a", "comment": "b"}], "output": "osc"}),
                     ["'osc'", "comment", "_comment"])
        self.refused(patch(graph={"nodes": [{"id": "osc", "_comment": 3}], "output": "osc"}),
                     ["'osc'", "_comment", "3"])
        self.refused(patch(graph={"nodes": [{"_comment": 3}], "output": "osc"}), ["<no id>"])

    def test_a_refusal_anywhere_leaves_every_level_unchanged(self):
        self.refused(patch(_comment="fine", graph={"nodes": [{"id": "a", "_comment": "fine"},
                                                             {"id": "bad", "_comment": None}], "output": "a"}),
                     ["'bad'"])
        self.refused(patch(_comment=None, graph={"nodes": [{"id": "a", "_comment": "fine"}], "output": "a"}),
                     ["_comment"])


class OtherTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(_comment="x", _provenance={"a": 1},
                    graph={"nodes": [{"id": "a", "_comment": "y"}], "output": "a"})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"_comment": 1, "_provenance": 1, "node _comment": 1}))
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)

    def test_a_file_with_nothing_to_do(self):
        doc = patch(comment="already", graph={"nodes": [{"id": "a", "comment": "y", "params": {"_comment": 1}}],
                                              "output": "a"})
        doc["instrument"] = {"_comment": 3}
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


if __name__ == "__main__":
    unittest.main()
