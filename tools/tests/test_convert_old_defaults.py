"""The old-defaults rule: a VarSource with no val gains "val": 0.0 as its
first key, with a note; a VarSource with no params object left alone; a
CombinedSource's operation "add", in any case, written "Mix"; every other
operation left alone; the walk over both node-container shapes and a nested
node list; other types untouched; idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import old_defaults  # noqa: E402


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the old-defaults rule asked for descriptors")


def node(ntype, params, nid="n"):
    n = {"id": nid, "type": ntype}
    if params is not None:
        n["params"] = params
    return n


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "n"}}


def apply(doc):
    notes = []
    counts = old_defaults.apply(doc, notes, NoContext())
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


class VarSourceTests(unittest.TestCase):
    def test_an_empty_var_source_gains_val_zero(self):
        doc = patch(node("VarSource", {}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"val": 1}))
        self.assertEqual(params_of(doc), {"val": 0.0})
        self.assertIs(type(params_of(doc)["val"]), float)
        self.assertEqual(len(notes), 1)
        for needle in ("'n'", "VarSource", "val", "0.0"):
            self.assertIn(needle, notes[0])

    def test_val_is_written_as_the_first_key(self):
        doc = patch(node("VarSource", {"var": 0.2, "varPct": {"ref": "e"}, "absolute": False}))
        apply(doc)
        p = params_of(doc)
        self.assertEqual(list(p), ["val", "var", "varPct", "absolute"])
        self.assertEqual(p, {"val": 0.0, "var": 0.2, "varPct": {"ref": "e"}, "absolute": False})

    def test_a_var_source_holding_val_is_untouched(self):
        for val in (1.0, 0, {"ref": "lfo"}, {"tap": "t"}):
            doc = patch(node("VarSource", {"var": 0.1, "val": val}))
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc), (Counter(), []))
            self.assertEqual(doc, before)

    def test_a_var_source_with_no_params_object_is_left_alone(self):
        doc = patch(node("VarSource", None), {"id": "m", "type": "VarSource", "params": [1]})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class OperationTests(unittest.TestCase):
    def test_add_in_any_case_becomes_mix(self):
        for op in ("add", "Add", "ADD", "aDd"):
            doc = patch(node("CombinedSource", {"source1": {"ref": "a"}, "operation": op, "gainAdj": 0.0}))
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"add": 1}), op)
            p = params_of(doc)
            self.assertEqual(list(p), ["source1", "operation", "gainAdj"])
            self.assertEqual(p["operation"], "Mix")
            self.assertEqual(notes, [])

    def test_every_other_operation_is_left_alone(self):
        for op in ("Mix", "mix", "multiply", "sum", "Fade", "fade", "add ", " add", "adds", "", 0, 1, 2, 3,
                   0.0, None, True):
            doc = patch(node("CombinedSource", {"operation": op}))
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc), (Counter(), []), op)
            self.assertEqual(doc, before)

    def test_an_absent_operation_is_left_alone(self):
        doc = patch(node("CombinedSource", {"source1": 1.0}), node("CombinedSource", None, "m"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class WalkTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"nodes": [node("VarSource", {}), node("CombinedSource", {"operation": "add"}, "c")],
               "output": "n"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"val": 1, "add": 1}))
        self.assertEqual(doc["nodes"][0]["params"], {"val": 0.0})
        self.assertEqual(doc["nodes"][1]["params"], {"operation": "Mix"})

    def test_a_nested_node_list_inside_a_node(self):
        inner = node("VarSource", {}, "inner")
        doc = patch({"id": "host", "type": "MultiplexSource", "params": {"template": {"nodes": [inner]}}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"val": 1}))
        self.assertEqual(inner["params"], {"val": 0.0})

    def test_other_types_are_untouched(self):
        doc = patch(node("RangeSource", {}), node("StaticVarSource", {}, "s"),
                    node("MultiSource", {"operation": "add"}, "m"), node("Shaper", {"operation": "Add"}, "h"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(node("VarSource", {"var": 1.0}), node("VarSource", {}, "v2"),
                    node("CombinedSource", {"operation": "Add"}, "c"))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"val": 2, "add": 1}))
        self.assertEqual(len(notes), 2)
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()
