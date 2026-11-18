"""The key-rename rule: cutoff as cutoffFreq on the two Butterworth filters
and the SVF, gap as gapDuration on RepeatingSource, each written where the old
key stood; the old key winning over the new one beside it, with a note; a lone
gapDuration left alone; the refusals (each leaving the document unchanged);
the walk over both node-container shapes and a nested node list; other types
untouched; idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import key_renames  # noqa: E402

CUTOFF_TYPES = ("BWLowpassFilter", "BWHighpassFilter", "SVFSource")


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the key-rename rule asked for descriptors")


def node(ntype, params, nid="n"):
    n = {"type": ntype, "params": params}
    if nid is not None:
        n["id"] = nid
    return n


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "n"}}


def apply(doc):
    notes = []
    counts = key_renames.apply(doc, notes, NoContext())
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


class CutoffTests(unittest.TestCase):
    def test_cutoff_becomes_cutoff_freq_where_it_stood(self):
        for ntype in CUTOFF_TYPES:
            for value in (800.0, 1200, {"ref": "lfo"}, {"tap": "env"}):
                doc = patch(node(ntype, {"source": {"ref": "osc"}, "cutoff": copy.deepcopy(value),
                                         "resonance": 0.3}))
                counts, notes = apply(doc)
                self.assertEqual(counts, Counter({"cutoff": 1}), ntype)
                self.assertEqual(notes, [])
                p = params_of(doc)
                self.assertEqual(list(p), ["source", "cutoffFreq", "resonance"])
                self.assertEqual(p, {"source": {"ref": "osc"}, "cutoffFreq": value, "resonance": 0.3})
                if isinstance(value, int):
                    self.assertIs(type(p["cutoffFreq"]), int)                      # moved as it is

    def test_cutoff_beside_cutoff_freq_wins_and_the_other_is_dropped_with_a_note(self):
        for ntype in CUTOFF_TYPES:
            doc = patch(node(ntype, {"cutoffFreq": 500.0, "source": {"ref": "osc"}, "cutoff": 900.0}))
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"cutoff": 1, "dropped": 1}))
            p = params_of(doc)
            self.assertEqual(list(p), ["source", "cutoffFreq"])                    # where cutoff stood
            self.assertEqual(p["cutoffFreq"], 900.0)
            self.assertEqual(len(notes), 1)
            for needle in ("'n'", ntype, "cutoff", "900.0", "500.0"):
                self.assertIn(needle, notes[0])

    def test_a_feed_beside_cutoff_freq_wins_too(self):
        doc = patch(node("SVFSource", {"cutoff": {"ref": "lfo"}, "cutoffFreq": {"ref": "env"}}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"cutoff": 1, "dropped": 1}))
        self.assertEqual(params_of(doc), {"cutoffFreq": {"ref": "lfo"}})
        self.assertEqual(len(notes), 1)

    def test_cutoff_freq_alone_is_untouched(self):
        doc = patch(node("BWLowpassFilter", {"cutoffFreq": 500.0, "sections": 3}))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class GapTests(unittest.TestCase):
    def test_gap_becomes_gap_duration_where_it_stood(self):
        for value in (0.3, 1):
            doc = patch(node("RepeatingSource", {"duration": 0.5, "gap": value, "gapVarPct": 0.1,
                                                 "source": {"ref": "s"}}))
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"gap": 1}))
            self.assertEqual(notes, [])
            p = params_of(doc)
            self.assertEqual(list(p), ["duration", "gapDuration", "gapVarPct", "source"])
            self.assertEqual(p["gapDuration"], value)
            self.assertIs(type(p["gapDuration"]), type(value))

    def test_gap_beside_gap_duration_wins_and_the_other_is_dropped_with_a_note(self):
        doc = patch(node("RepeatingSource", {"gap": 0.3, "duration": 0.5, "gapDuration": 0.7}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"gap": 1, "dropped": 1}))
        p = params_of(doc)
        self.assertEqual(list(p), ["gapDuration", "duration"])
        self.assertEqual(p["gapDuration"], 0.3)
        self.assertEqual(len(notes), 1)
        for needle in ("'n'", "RepeatingSource", "gap", "0.3", "0.7"):
            self.assertIn(needle, notes[0])

    def test_a_lone_gap_duration_is_left_alone(self):
        for value in (0.2, 0.5):
            doc = patch(node("RepeatingSource", {"duration": 0.5, "gapDuration": value}))
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc), (Counter(), []))
            self.assertEqual(doc, before)


class RefusalTests(unittest.TestCase):
    def refused(self, ntype, params, needles, nid="n"):
        doc = patch(node(ntype, params, nid), node("SVFSource", {"cutoff": 100.0}, "fine"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            key_renames.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_cutoff_on_a_bandpass(self):
        self.refused("BWBandpassFilter", {"cutoff": 500.0, "highCutoff": 800.0},
                     ["'n'", "BWBandpassFilter", "cutoff"])

    def test_a_cutoff_that_is_neither_a_number_nor_a_feed(self):
        for ntype in CUTOFF_TYPES:
            for bad in ("800", None, True, [1, 2], {}, {"ref": "a", "tap": "b"},
                        {"ref": "a", "weight": 1}, {"ref": 3}, {"value": 2}):
                self.refused(ntype, {"cutoff": bad}, ["'n'", ntype, "cutoff"])

    def test_a_gap_that_is_not_a_number(self):
        for bad in ("0.2", None, False, {"ref": "x"}, [0.2]):
            self.refused("RepeatingSource", {"gap": bad}, ["'n'", "RepeatingSource", "gap"])

    def test_a_node_with_no_id_is_named(self):
        self.refused("BWBandpassFilter", {"cutoff": 1.0}, ["<no id>"], nid=None)

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        doc = patch(node("RepeatingSource", {"gap": 0.3, "gapDuration": 0.1}, "first"),
                    node("BWBandpassFilter", {"cutoff": 1.0}, "bad"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            key_renames.apply(doc, notes, NoContext())
        self.assertIn("'bad'", str(cm.exception))
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])


class WalkTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"nodes": [node("BWHighpassFilter", {"cutoff": 80.0})], "output": "n"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"cutoff": 1}))
        self.assertEqual(doc["nodes"][0]["params"], {"cutoffFreq": 80.0})

    def test_a_nested_node_list_inside_a_node(self):
        inner = node("RepeatingSource", {"gap": 0.4}, "inner")
        doc = patch({"id": "host", "type": "MultiplexSource", "params": {"template": {"nodes": [inner]}}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"gap": 1}))
        self.assertEqual(inner["params"], {"gapDuration": 0.4})

    def test_other_types_are_untouched(self):
        doc = patch(node("Biquad", {"cutoff": 500.0}), node("DelayFilter", {"gap": "x"}, "d"),
                    node("BWLowpassFilter", {"gap": 0.2}, "l"), node("RepeatingSource", {"cutoff": "x"}, "r"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_a_node_without_a_params_object_is_untouched(self):
        doc = patch({"id": "n", "type": "SVFSource"}, {"id": "m", "type": "SVFSource", "params": [1]})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(node("SVFSource", {"cutoffFreq": 1.0, "cutoff": 2.0}),
                    node("RepeatingSource", {"gap": 0.3}, "r"),
                    node("BWLowpassFilter", {"cutoff": {"ref": "n"}}, "l"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"cutoff": 2, "gap": 1, "dropped": 1}))
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()
