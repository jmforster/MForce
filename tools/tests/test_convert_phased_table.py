"""The Phased-table rule: a PhasedValueSource's stage list as sources and the
four columns percent, minSec, maxSec and gain, written where stages stood; an
absent key as the loader's default; an empty list as empty columns; overlap
untouched; the refusals (each leaving the document unchanged); the walk over
both node-container shapes and a nested node list; other types untouched;
idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import phased_table  # noqa: E402

COLUMNS = ["sources", "percent", "minSec", "maxSec", "gain"]


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the Phased-table rule asked for descriptors")


def phased(params, nid="p"):
    n = {"type": "PhasedValueSource", "params": params}
    if nid is not None:
        n["id"] = nid
    return n


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "p"}}


def apply(doc):
    notes = []
    counts = phased_table.apply(doc, notes, NoContext())
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


class TableTests(unittest.TestCase):
    def test_rows_become_sources_and_four_columns_where_stages_stood(self):
        doc = patch(phased({"overlap": 0.15,
                            "stages": [{"source": {"ref": "a"}, "percent": 0.2, "min": 0.05, "max": 0.3, "gain": 1.0},
                                       {"percent": 0, "source": {"ref": "b"}, "min": 0, "max": 0, "gain": 0.8},
                                       {"source": {"tap": "c"}, "percent": 0.3, "min": 0.1, "max": 2, "gain": 1.2}],
                            "tail": "x"}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1, "row": 3}))
        self.assertEqual(notes, [])
        p = params_of(doc)
        self.assertEqual(list(p), ["overlap"] + COLUMNS + ["tail"])
        self.assertEqual(p, {"overlap": 0.15,
                             "sources": [{"ref": "a"}, {"ref": "b"}, {"tap": "c"}],
                             "percent": [0.2, 0, 0.3], "minSec": [0.05, 0, 0.1],
                             "maxSec": [0.3, 0, 2], "gain": [1.0, 0.8, 1.2], "tail": "x"})
        self.assertIs(type(p["maxSec"][2]), int)                                       # moved as it is

    def test_absent_keys_take_the_loaders_defaults(self):
        doc = patch(phased({"stages": [{"source": {"ref": "a"}}, {"source": 0.5, "gain": 2},
                                       {"source": {"ref": "c"}, "min": 0.1}]}))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1, "row": 3}))
        p = params_of(doc)
        self.assertEqual(p, {"sources": [{"ref": "a"}, 0.5, {"ref": "c"}],
                             "percent": [0.0, 0.0, 0.0], "minSec": [0.0, 0.0, 0.1],
                             "maxSec": [0.0, 0.0, 0.0], "gain": [1.0, 2, 1.0]})
        self.assertIs(type(p["gain"][0]), float)
        self.assertIs(type(p["percent"][0]), float)

    def test_an_empty_list_is_empty_columns(self):
        doc = patch(phased({"stages": [], "overlap": 0.1}))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1}))
        self.assertEqual(params_of(doc), {"sources": [], "percent": [], "minSec": [], "maxSec": [],
                                          "gain": [], "overlap": 0.1})
        self.assertEqual(list(params_of(doc)), COLUMNS + ["overlap"])

    def test_a_table_form_node_is_untouched(self):
        doc = patch(phased({"sources": [{"ref": "a"}], "percent": [0.0], "minSec": [0.0], "maxSec": [0.0],
                            "gain": [1.0], "overlap": 0.05}), phased({}, "q"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class RefusalTests(unittest.TestCase):
    def refused(self, params, needles, nid="p"):
        doc = patch(phased(params, nid), phased({"stages": [{"source": {"ref": "a"}}]}, "fine"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            phased_table.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_stages_not_a_list(self):
        for bad in ({"source": {"ref": "a"}}, "x", 3, None):
            self.refused({"stages": bad}, ["'p'", "PhasedValueSource", "stages"])

    def test_a_row_that_is_not_an_object(self):
        for bad in ([{"ref": "a"}, 0.1], 0, None, "a"):
            self.refused({"stages": [{"source": {"ref": "a"}}, bad]}, ["'p'", "row 1"])

    def test_a_row_without_source(self):
        self.refused({"stages": [{"percent": 0.5, "gain": 1}]}, ["'p'", "row 0", "source"])

    def test_a_row_with_another_key(self):
        self.refused({"stages": [{"source": {"ref": "a"}, "minSec": 0.1}]}, ["'p'", "row 0", "'minSec'"])
        self.refused({"stages": [{"source": {"ref": "a"}, "hold": 1}]}, ["'p'", "row 0", "'hold'"])

    def test_a_source_that_is_neither_a_feed_nor_a_number(self):
        for bad in ("a", None, True, [{"ref": "a"}], {}, {"ref": "a", "weight": 1}, {"ref": "a", "tap": "b"},
                    {"ref": 1}):
            self.refused({"stages": [{"source": bad}]}, ["'p'", "row 0", "source"])

    def test_a_column_value_that_is_not_a_number(self):
        for key in ("percent", "min", "max", "gain"):
            for bad in ("0.1", None, True, [0.1], {"ref": "a"}):
                self.refused({"stages": [{"source": {"ref": "a"}, key: bad}]}, ["'p'", "row 0", key])

    def test_stages_beside_a_new_key(self):
        for col in COLUMNS:
            self.refused({"stages": [{"source": {"ref": "a"}}], col: []}, ["'p'", "stages", col])

    def test_a_node_with_no_id_is_named(self):
        self.refused({"stages": 1}, ["<no id>"], nid=None)

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        doc = patch(phased({"stages": [{"source": {"ref": "a"}}]}, "first"),
                    phased({"stages": [{"source": {"ref": "a"}, "x": 1}]}, "bad"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            phased_table.apply(doc, notes, NoContext())
        self.assertIn("'bad'", str(cm.exception))
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])


class WalkTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"nodes": [phased({"stages": [{"source": {"ref": "a"}, "percent": 1}]})], "output": "p"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1, "row": 1}))
        self.assertEqual(doc["nodes"][0]["params"], {"sources": [{"ref": "a"}], "percent": [1],
                                                     "minSec": [0.0], "maxSec": [0.0], "gain": [1.0]})

    def test_a_nested_node_list_inside_a_node(self):
        inner = phased({"stages": [{"source": {"ref": "a"}}]}, "inner")
        doc = patch({"id": "host", "type": "MultiplexSource", "params": {"template": {"nodes": [inner]}}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1, "row": 1}))
        self.assertEqual(inner["params"]["sources"], [{"ref": "a"}])

    def test_other_types_are_untouched(self):
        doc = patch({"id": "e", "type": "Envelope", "params": {"stages": [{"source": 1, "min": "x"}]}},
                    {"id": "n", "type": "PhasedValueSource"})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(phased({"stages": [{"source": {"ref": "a"}, "gain": 2}, {"source": 1}], "overlap": 0.2}),
                    phased({"stages": []}, "q"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 2, "row": 2}))
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()
