"""The enum-label rule: an enum setting given as an index or as a label in
another case written as its declared label, from the descriptor table's
"labels"; a labelled column's elements the same, from "arrayLabels"; a
preset-form Envelope's three curve keys against the first four labels of its
type column; what is left alone (a bool, an object, a type the table lacks, a
setting with no labels); the refusals (each leaving the document unchanged);
the table not asked when no node could need it; idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import Context, enum_labels  # noqa: E402

STAGE_TYPES = ["Linear", "Expo", "InverseExpo", "Sine", "Hold"]

# The dump's shape: an enum setting carries "labels"; a labelled column sits
# under "arrayLabels" and is listed in "arrays".
DESC = {
    "CombinedSource": {"params": [], "inputs": ["source1", "source2"], "arrays": [], "settings": [
        {"name": "operation", "type": "int", "isEnum": True, "default": 0.0, "min": 0.0, "max": 3.0,
         "labels": ["Mix", "Multiply", "Fade", "Sum"]},
        {"name": "gainAdj", "type": "float", "isEnum": False, "default": 0.0, "min": -1.0, "max": 10.0}]},
    "SVFSource": {"params": ["cutoffFreq"], "inputs": ["source"], "arrays": [], "settings": [
        {"name": "mode", "type": "int", "isEnum": True, "default": 0.0, "min": 0.0, "max": 4.0,
         "labels": ["Lowpass", "Highpass", "Bandpass", "Lowpass1P", "Highpass1P"]}]},
    "Envelope": {"params": ["minValue", "maxValue", "trigger"], "inputs": [],
                 "arrays": ["startVal", "endVal", "power", "type", "percent", "minSec", "maxSec", "nominal"],
                 "arrayLabels": {"type": STAGE_TYPES},
                 "settings": [{"name": "timeMode", "type": "int", "isEnum": True, "default": 0.0, "min": 0.0,
                               "max": 1.0, "labels": ["fraction", "seconds"]},
                              {"name": "sustainLevel", "type": "float", "isEnum": False, "default": 0.7,
                               "min": 0.0, "max": 1.0}]},
    "CurveNode": {"params": [], "inputs": ["source"], "arrays": ["x", "y", "exprForm"],
                  "arrayLabels": {"exprForm": ["linear", "power"]},
                  "settings": [{"name": "interp", "type": "int", "isEnum": True, "default": 0.0, "min": 0.0,
                                "max": 2.0, "labels": ["linear", "logx", "loglog"]}]},
    "SineSource": {"params": ["amplitude", "frequency", "phase"], "inputs": [], "arrays": [], "settings": []},
}


class Asked:
    """A context that counts the questions and answers from a table."""
    def __init__(self, table=DESC):
        self.table, self.asked = table, 0

    def descriptors(self):
        self.asked += 1
        return self.table


class NoContext:
    """A context whose question fails the test."""
    def descriptors(self):
        raise AssertionError("the enum-label rule asked for descriptors")


def node(ntype, params, nid="n"):
    n = {"type": ntype, "params": params}
    if nid is not None:
        n["id"] = nid
    return n


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "n"}}


def apply(doc, ctx=None):
    notes = []
    counts = enum_labels.apply(doc, notes, ctx or Context(descriptors=DESC))
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


class SettingTests(unittest.TestCase):
    def test_every_index_becomes_its_label(self):
        for ntype, key, labels in (("CombinedSource", "operation", ["Mix", "Multiply", "Fade", "Sum"]),
                                   ("SVFSource", "mode", ["Lowpass", "Highpass", "Bandpass", "Lowpass1P",
                                                          "Highpass1P"]),
                                   ("Envelope", "timeMode", ["fraction", "seconds"]),
                                   ("CurveNode", "interp", ["linear", "logx", "loglog"])):
            for i, label in enumerate(labels):
                for index in (i, float(i)):
                    doc = patch(node(ntype, {"a": 1.0, key: index, "z": {"ref": "x"}}))
                    counts, notes = apply(doc)
                    self.assertEqual(counts, Counter({"index": 1}), (ntype, index))
                    self.assertEqual(notes, [])
                    self.assertEqual(params_of(doc), {"a": 1.0, key: label, "z": {"ref": "x"}})
                    self.assertEqual(list(params_of(doc)), ["a", key, "z"])

    def test_a_label_in_another_case_becomes_the_label(self):
        for ntype, key, given, label in (("CombinedSource", "operation", "multiply", "Multiply"),
                                         ("CombinedSource", "operation", "SUM", "Sum"),
                                         ("SVFSource", "mode", "lowpass", "Lowpass"),
                                         ("SVFSource", "mode", "hIGHPASS1p", "Highpass1P"),
                                         ("Envelope", "timeMode", "Seconds", "seconds"),
                                         ("CurveNode", "interp", "LogX", "logx")):
            doc = patch(node(ntype, {key: given}))
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"case": 1}), given)
            self.assertEqual(notes, [])
            self.assertEqual(params_of(doc), {key: label})

    def test_a_label_as_declared_is_left_alone(self):
        doc = patch(node("CombinedSource", {"operation": "Fade"}), node("SVFSource", {"mode": "Bandpass"}, "s"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_a_bool_and_other_shapes_are_left_alone(self):
        for value in (True, False, {"ref": "x"}, None, [1], {"a": 1}):
            doc = patch(node("CombinedSource", {"operation": copy.deepcopy(value)}))
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc), (Counter(), []), value)
            self.assertEqual(doc, before)

    def test_a_setting_without_labels_and_a_param_are_left_alone(self):
        doc = patch(node("CombinedSource", {"gainAdj": 2, "source1": 3, "seed": 1}),
                    node("SineSource", {"frequency": 1, "phase": "x"}, "s"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_a_type_absent_from_the_table_is_left_alone(self):
        doc = patch(node("Biquad", {"mode": 1}), node("Mesh2D", {"edgeMode": "STK"}, "m"),
                    node(None, {"operation": 1}, "t"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_a_nested_node_list(self):
        inner = node("SVFSource", {"mode": 2}, "inner")
        doc = patch(node("MultiplexSource", {"template": {"nodes": [inner]}}, "host"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"index": 1}))
        self.assertEqual(inner["params"], {"mode": "Bandpass"})


class ColumnTests(unittest.TestCase):
    def test_each_element_of_a_labelled_column(self):
        doc = patch(node("Envelope", {"startVal": [0, 1, 2], "type": [0, "expo", "Sine", 4.0, "HOLD", True],
                                      "percent": [1, 2, 3]}),
                    node("CurveNode", {"exprForm": ["Power", 0, "linear"], "x": [0, 1]}, "c"))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"index": 3, "case": 3}))
        self.assertEqual(notes, [])
        self.assertEqual(params_of(doc), {"startVal": [0, 1, 2],
                                          "type": ["Linear", "Expo", "Sine", "Hold", "Hold", True],
                                          "percent": [1, 2, 3]})
        self.assertEqual(params_of(doc, 1), {"exprForm": ["power", "linear", "linear"], "x": [0, 1]})

    def test_a_column_that_is_not_a_list_is_left_alone(self):
        doc = patch(node("Envelope", {"type": "expo"}), node("CurveNode", {"exprForm": 1}, "c"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class PresetCurveTests(unittest.TestCase):
    def test_each_curve_key_by_index_and_by_case(self):
        for key in ("attackCurve", "decayCurve", "releaseCurve"):
            for given, label in ((0, "Linear"), (1, "Expo"), (2.0, "InverseExpo"), (3, "Sine"),
                                 ("expo", "Expo"), ("SINE", "Sine")):
                doc = patch(node("Envelope", {"preset": "adsr", key: given, "attack": 0.1}))
                counts, notes = apply(doc)
                self.assertEqual(counts, Counter({"preset curve": 1}), (key, given))
                self.assertEqual(notes, [])
                self.assertEqual(params_of(doc), {"preset": "adsr", key: label, "attack": 0.1})

    def test_a_curve_label_as_declared_is_left_alone(self):
        doc = patch(node("Envelope", {"preset": "ar", "attackCurve": "Expo", "releaseCurve": True}))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_curve_keys_without_a_preset_are_left_alone(self):
        doc = patch(node("Envelope", {"attackCurve": 1, "decayCurve": "expo"}), node("SineSource", {
            "attackCurve": 1}, "s"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class RefusalTests(unittest.TestCase):
    def refused(self, params, needles, ntype="CombinedSource", nid="n"):
        doc = patch(node("SVFSource", {"mode": 1}, "fine"), node(ntype, params, nid))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            enum_labels.apply(doc, notes, Context(descriptors=DESC))
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_a_number_that_is_not_whole_or_outside_the_labels(self):
        for bad in (1.5, -1, 4, 4.0, 1e9, -0.5):
            self.refused({"operation": bad}, ["'n'", "CombinedSource", "operation", repr(bad)])

    def test_a_string_that_matches_no_label(self):
        for bad in ("add", "Mixx", "", " Mix", "fade "):
            self.refused({"operation": bad}, ["'n'", "CombinedSource", "operation", repr(bad)])

    def test_a_bad_column_element(self):
        for bad in (5, 0.5, "hold!", -1):
            self.refused({"type": [0, bad]}, ["'n'", "Envelope", "type", repr(bad)], ntype="Envelope")

    def test_a_bad_preset_curve(self):
        for bad in (4, "Hold", "hold", 2.5, -1, "linearr"):
            self.refused({"preset": "ar", "releaseCurve": bad}, ["'n'", "Envelope", "releaseCurve", repr(bad)],
                         ntype="Envelope")

    def test_a_node_with_no_id_is_named(self):
        self.refused({"operation": 9}, ["<no id>"], nid=None)

    def test_a_table_without_labels_stops_every_document_it_is_asked_for(self):
        # The dump of a tool that prints "isEnum" and no labels: no
        # "arrayLabels", and no PerformNode.
        stale = {"CombinedSource": {"params": [], "inputs": [], "arrays": [], "settings": [
                     {"name": "operation", "type": "int", "isEnum": True, "default": 0.0, "min": 0.0, "max": 3.0},
                     {"name": "gainAdj", "type": "float", "isEnum": False, "default": 0.0, "min": -1.0,
                      "max": 10.0}]},
                 "Envelope": {"params": ["minValue", "maxValue", "trigger"], "inputs": [],
                              "arrays": ["startVal", "endVal", "power", "type", "percent", "minSec", "maxSec",
                                         "nominal"], "settings": []}}
        for doc in (patch(node("Envelope", {"type": [0, "expo"], "percent": [0.5, 0.5]})),
                    patch(node("PerformNode", {"field": "Velocity"})),
                    patch(node("CombinedSource", {"operation": 1, "gainAdj": 0.5}))):
            before = copy.deepcopy(doc)
            with self.assertRaises(RuntimeError) as cm:
                enum_labels.apply(doc, [], Context(descriptors=stale))
            self.assertIn("CombinedSource.operation", str(cm.exception))
            self.assertIn("no labels", str(cm.exception))
            self.assertEqual(doc, before)
        # A document the rule never asks the table for is untouched.
        doc = patch(node("CombinedSource", {"source1": {"ref": "a"}, "on": True}))
        before = copy.deepcopy(doc)
        self.assertEqual(enum_labels.apply(doc, [], Context(descriptors=stale)), Counter())
        self.assertEqual(doc, before)


class AskTests(unittest.TestCase):
    def test_not_asked_when_no_node_could_need_it(self):
        for doc in ({"sampleRate": 48000},
                    {"graph": {"nodes": [], "output": "n"}},
                    patch(node("SineSource", {}), {"id": "x", "type": "SVFSource"},
                          node("CombinedSource", {"source1": {"ref": "a"}, "on": True}, "c"),
                          node("SVFSource", [1, 2], "l"), {"id": "y", "params": {"mode": 1}},
                          node("SVFSource", {"mode": [{"ref": "a"}, True]}, "z"))):
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc, NoContext()), (Counter(), []))
            self.assertEqual(doc, before)

    def test_asked_once_when_a_node_could_need_it(self):
        for params in ({"mode": 1}, {"x": "y"}, {"x": [0]}, {"x": ["a"]}):
            ctx = Asked()
            apply(patch(node("SineSource", params)), ctx)
            self.assertEqual(ctx.asked, 1, params)


class OtherTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(node("CombinedSource", {"operation": 1}),
                    node("SVFSource", {"mode": "highpass"}, "s"),
                    node("Envelope", {"preset": "adsr", "attackCurve": 2, "decayCurve": "sine",
                                      "timeMode": 1}, "e"),
                    node("Envelope", {"type": [1, "hold"]}, "t"))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"index": 3, "case": 2, "preset curve": 2}))
        self.assertEqual(notes, [])
        self.assertEqual(params_of(doc, 2), {"preset": "adsr", "attackCurve": "InverseExpo",
                                             "decayCurve": "Sine", "timeMode": "seconds"})
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)

    def test_a_file_with_nothing_to_do(self):
        doc = patch(node("CombinedSource", {"operation": "Mix", "gainAdj": 1.0}),
                    node("Envelope", {"preset": "ar", "attackCurve": "Linear", "attack": 0.1}, "e"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


if __name__ == "__main__":
    unittest.main()
