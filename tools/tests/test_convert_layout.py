"""The layout rule: ui and the top-level groups as one layout object at ui's
place (groups' when there is no ui); positions keyed by node id only, the
__output entry as output, panning, groups as they are, the note faces each
gaining its pos; each member only when non-empty; the entries dropped with a
note (naming nothing, or a value the editor ignores); the refusals (each
leaving the document unchanged); idempotence; a file with nothing to do."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import layout  # noqa: E402


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the layout rule asked for descriptors")


def graph(*ids):
    return {"nodes": [{"id": i, "type": "SineSource", "params": {}} for i in ids], "output": ids[0]}


FACES = [{"label": "Note", "fields": {"frequency": "__perf_freq"}},
         {"fields": {"velocity": "__perf_velocity"}, "label": "Note2"}]
GROUPS = [{"name": "Body", "members": ["osc", "Note"], "pos": [1.0, 2.0]}]


def apply(doc):
    notes = []
    counts = layout.apply(doc, notes, NoContext())
    return counts, notes


class EmissionTests(unittest.TestCase):
    def test_every_member_in_order_at_uis_place(self):
        doc = {"sampleRate": 48000,
               "graph": graph("__perf_freq", "__perf_velocity", "osc"),
               "ui": {"noteFaces": copy.deepcopy(FACES), "panning": [10.0, -4.0],
                      "positions": {"Note2": [5, 6], "osc": [1.5, 2.5], "__output": [9.0, 9.0],
                                    "__perf_freq": [0, 0], "Note": [-3, 4]}},
               "groups": copy.deepcopy(GROUPS),
               "seconds": 3}
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"ui": 1, "groups": 1, "face pos": 2}))
        self.assertEqual(notes, [])
        self.assertEqual(list(doc), ["sampleRate", "graph", "layout", "seconds"])
        lay = doc["layout"]
        self.assertEqual(list(lay), ["positions", "output", "panning", "groups", "faces"])
        self.assertEqual(lay["positions"], {"osc": [1.5, 2.5], "__perf_freq": [0, 0]})
        self.assertEqual(list(lay["positions"]), ["osc", "__perf_freq"])
        self.assertEqual(lay["output"], [9.0, 9.0])
        self.assertEqual(lay["panning"], [10.0, -4.0])
        self.assertEqual(lay["groups"], GROUPS)
        self.assertEqual(lay["faces"], [{"label": "Note", "fields": {"frequency": "__perf_freq"}, "pos": [-3, 4]},
                                        {"fields": {"velocity": "__perf_velocity"}, "label": "Note2",
                                         "pos": [5, 6]}])
        self.assertEqual(list(lay["faces"][1]), ["fields", "label", "pos"])

    def test_groups_place_when_there_is_no_ui(self):
        doc = {"groups": copy.deepcopy(GROUPS), "sampleRate": 48000, "graph": graph("osc")}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"groups": 1}))
        self.assertEqual(list(doc), ["layout", "sampleRate", "graph"])
        self.assertEqual(doc["layout"], {"groups": GROUPS})

    def test_ui_before_groups_takes_uis_place(self):
        doc = {"groups": copy.deepcopy(GROUPS), "graph": graph("osc"), "ui": {"positions": {"osc": [1, 2]}}}
        apply(doc)
        self.assertEqual(list(doc), ["graph", "layout"])
        self.assertEqual(doc["layout"], {"positions": {"osc": [1, 2]}, "groups": GROUPS})

    def test_a_face_without_a_position_and_a_member_left_empty(self):
        doc = {"graph": graph("osc"), "ui": {"positions": {}, "panning": [], "noteFaces": copy.deepcopy(FACES)},
               "groups": []}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"ui": 1, "groups": 1}))
        self.assertEqual(doc["layout"], {"faces": FACES})

    def test_an_empty_layout_is_not_written(self):
        for doc in ({"graph": graph("osc"), "ui": {}}, {"graph": graph("osc"), "groups": []},
                    {"graph": graph("osc"), "ui": {"positions": {"gone": [1, 2]}}}):
            apply(doc)
            self.assertEqual(doc, {"graph": graph("osc")})

    def test_node_ids_from_a_nested_list_and_top_level_nodes(self):
        inner = {"id": "inner", "type": "SineSource"}
        doc = {"nodes": [{"id": "host", "type": "MultiplexSource", "params": {"template": {"nodes": [inner]}}}],
               "ui": {"positions": {"inner": [1, 1], "host": [2, 2]}}}
        counts, notes = apply(doc)
        self.assertEqual(notes, [])
        self.assertEqual(doc["layout"], {"positions": {"inner": [1, 1], "host": [2, 2]}})


class DropTests(unittest.TestCase):
    def test_an_entry_naming_nothing_is_dropped_with_a_note(self):
        doc = {"graph": graph("osc"),
               "ui": {"positions": {"__param_frequency": [1, 2], "osc": [0, 0], "Body": [3, 3]}},
               "groups": [{"name": "Body", "members": ["osc"]}]}
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"ui": 1, "groups": 1, "position dropped": 2}))
        self.assertEqual(doc["layout"]["positions"], {"osc": [0, 0]})
        self.assertEqual(len(notes), 2)
        self.assertIn("'__param_frequency'", notes[0])
        self.assertIn("'Body'", notes[1])
        for note in notes:
            self.assertIn("names no node", note)

    def test_an_entry_the_editor_ignores_is_dropped_with_a_note(self):
        for bad in ([1], [1, 2, 3], [1, "2"], [True, 1], "1,2", None, {"x": 1}, [1e7, 0], [0, -1e7],
                    [12345678.0, 0]):
            for key in ("osc", "__output", "Note"):
                doc = {"graph": graph("osc"),
                       "ui": {"positions": {key: copy.deepcopy(bad)},
                              "noteFaces": [{"label": "Note", "fields": {}}]}}
                counts, notes = apply(doc)
                self.assertEqual(counts, Counter({"ui": 1, "position dropped": 1}), (bad, key))
                self.assertEqual(doc["layout"], {"faces": [{"label": "Note", "fields": {}}]})
                self.assertEqual(len(notes), 1)
                self.assertIn("'%s'" % key, notes[0])
                self.assertIn("1e7", notes[0])

    def test_a_large_value_under_the_bound_is_kept(self):
        doc = {"graph": graph("osc"), "ui": {"positions": {"osc": [9999999.0, -9999999]}}}
        counts, notes = apply(doc)
        self.assertEqual(notes, [])
        self.assertEqual(doc["layout"], {"positions": {"osc": [9999999.0, -9999999]}})


class RefusalTests(unittest.TestCase):
    def refused(self, doc, needles):
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            layout.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_layout_beside_ui_or_groups(self):
        self.refused({"graph": graph("osc"), "layout": {}, "ui": {"positions": {"osc": [1, 2]}}},
                     ["layout", "ui"])
        self.refused({"graph": graph("osc"), "layout": {}, "groups": []}, ["layout", "groups"])

    def test_ui_that_is_not_an_object(self):
        for bad in ([], "ui", None, 3):
            self.refused({"graph": graph("osc"), "ui": bad}, ["ui"])

    def test_ui_holding_another_key(self):
        self.refused({"graph": graph("osc"), "ui": {"positions": {}, "zoom": 2}}, ["ui", "zoom"])

    def test_a_faces_label_equal_to_a_node_id(self):
        self.refused({"graph": graph("osc", "Note"), "ui": {"noteFaces": copy.deepcopy(FACES)}},
                     ["'Note'", "face", "node"])

    def test_groups_that_is_not_a_list(self):
        for bad in ({}, "g", None):
            self.refused({"graph": graph("osc"), "groups": bad}, ["groups"])

    def test_positions_or_faces_of_another_shape(self):
        self.refused({"graph": graph("osc"), "ui": {"positions": [[1, 2]]}}, ["ui.positions"])
        self.refused({"graph": graph("osc"), "ui": {"noteFaces": {"label": "Note"}}}, ["ui.noteFaces"])
        self.refused({"graph": graph("osc"), "ui": {"noteFaces": ["Note"]}}, ["ui.noteFaces"])


class OtherTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = {"graph": graph("__perf_freq", "osc"),
               "ui": {"positions": {"osc": [1, 2], "Note": [3, 4], "__output": [5, 6], "stale": [0, 0]},
                      "panning": [1, 1], "noteFaces": copy.deepcopy(FACES)},
               "groups": copy.deepcopy(GROUPS)}
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"ui": 1, "groups": 1, "face pos": 1, "position dropped": 1}))
        self.assertEqual(len(notes), 1)
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)

    def test_a_file_with_nothing_to_do(self):
        doc = {"graph": graph("osc"), "layout": {"positions": {"osc": [1, 2]}},
               "instrument": {"ui": 1, "groups": 2}}
        doc["graph"]["groups"] = 3
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


if __name__ == "__main__":
    unittest.main()
