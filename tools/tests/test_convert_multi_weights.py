"""The multi-weights rule: a MultiSource's per-feed weight and delay moved
onto the node as weights and delays, written right after source, the feeds
left plain; the defaults for a feed that held none; a single feed object's
weight and delay dropped with a note, the feed left one plain object; the
refusals (each leaving the
document unchanged); the walk over both node-container shapes and a nested
node list; other types untouched; idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import multi_weights  # noqa: E402


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the multi-weights rule asked for descriptors")


def multi(params, nid="m"):
    n = {"type": "MultiSource", "params": params}
    if nid is not None:
        n["id"] = nid
    return n


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "m"}}


def apply(doc):
    notes = []
    counts = multi_weights.apply(doc, notes, NoContext())
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


class WeightTests(unittest.TestCase):
    def test_a_weight_on_every_feed(self):
        doc = patch(multi({"gain": 0.5,
                           "source": [{"ref": "a", "weight": 0.5}, {"weight": 2, "ref": "b"},
                                      {"tap": "c", "weight": 0.25}],
                           "other": 1}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "weight": 3}))
        self.assertEqual(notes, [])
        p = params_of(doc)
        self.assertEqual(list(p), ["gain", "source", "weights", "other"])
        self.assertEqual(p, {"gain": 0.5, "source": [{"ref": "a"}, {"ref": "b"}, {"tap": "c"}],
                             "weights": [0.5, 2, 0.25], "other": 1})
        self.assertIs(type(p["weights"][1]), int)                                     # moved as it is

    def test_a_missing_weight_is_one(self):
        doc = patch(multi({"source": [{"ref": "a"}, {"ref": "b", "weight": 0.5}, {"ref": "c"}]}))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "weight": 1}))
        self.assertEqual(params_of(doc), {"source": [{"ref": "a"}, {"ref": "b"}, {"ref": "c"}],
                                          "weights": [1.0, 0.5, 1.0]})
        self.assertIs(type(params_of(doc)["weights"][0]), float)

    def test_a_delay_alone_writes_delays_and_no_weights(self):
        doc = patch(multi({"source": [{"ref": "a"}, {"ref": "b", "delay": 100}]}))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "delay": 1}))
        self.assertEqual(params_of(doc), {"source": [{"ref": "a"}, {"ref": "b"}], "delays": [0, 100]})
        self.assertIs(type(params_of(doc)["delays"][0]), int)

    def test_weights_and_delays_both_follow_source(self):
        doc = patch(multi({"source": [{"ref": "a", "delay": 3, "weight": 0.5}, {"ref": "b", "weight": 2.0},
                                      {"ref": "c", "delay": 1.5}], "gain": 1}))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "weight": 2, "delay": 2}))
        p = params_of(doc)
        self.assertEqual(list(p), ["source", "weights", "delays", "gain"])
        self.assertEqual(p["weights"], [0.5, 2.0, 1.0])
        self.assertEqual(p["delays"], [3, 0, 1.5])

    def test_a_single_objects_weight_is_dropped_with_a_note(self):
        doc = patch(multi({"gain": 1, "source": {"ref": "a", "weight": 0.5}, "tail": 2}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "dropped": 1}))
        p = params_of(doc)
        self.assertEqual(list(p), ["gain", "source", "tail"])
        self.assertEqual(p, {"gain": 1, "source": {"ref": "a"}, "tail": 2})
        self.assertEqual(len(notes), 1)
        for needle in ("'m'", "MultiSource", "weight", "0.5"):
            self.assertIn(needle, notes[0])

    def test_a_single_objects_weight_and_delay_are_both_dropped(self):
        doc = patch(multi({"source": {"delay": 7, "tap": "t", "weight": 2}}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "dropped": 2}))
        self.assertEqual(params_of(doc), {"source": {"tap": "t"}})
        self.assertEqual(len(notes), 2)
        self.assertIn("weight 2", notes[0])
        self.assertIn("delay 7", notes[1])

    def test_a_single_plain_feed_object_is_left_as_it_is(self):
        for feed in ({"ref": "a"}, {"tap": "t"}):
            doc = patch(multi({"source": dict(feed)}))
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc), (Counter(), []))
            self.assertEqual(doc, before)

    def test_plain_feeds_are_untouched(self):
        doc = patch(multi({"source": [{"ref": "a"}, {"tap": "b"}], "weights": [0.5, 2.0]}),
                    multi({"source": []}, "e"), multi({}, "f"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class RefusalTests(unittest.TestCase):
    def refused(self, params, needles, nid="m"):
        doc = patch(multi(params, nid), multi({"source": [{"ref": "a", "weight": 2}]}, "fine"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            multi_weights.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_a_feed_that_is_not_one_ref_or_tap_object(self):
        for bad in (0.5, "a", None, [{"ref": "a"}], {"weight": 1}, {"ref": "a", "tap": "b", "weight": 1},
                    {"ref": 3, "weight": 1}):
            self.refused({"source": [{"ref": "a", "weight": 0.5}, bad]}, ["'m'", "MultiSource", "feed 1"])

    def test_a_feed_with_another_key(self):
        self.refused({"source": [{"ref": "a", "weight": 0.5}, {"ref": "b", "gain": 2}]},
                     ["'m'", "feed 1", "'gain'"])
        self.refused({"source": [{"ref": "a", "gain": 2}]}, ["'m'", "feed 0", "'gain'"])
        self.refused({"source": {"ref": "a", "mute": True}}, ["'m'", "feed 0", "'mute'"])

    def test_a_weight_or_delay_that_is_not_a_number(self):
        for key in ("weight", "delay"):
            for bad in ("0.5", None, True, False, [1], {"ref": "x"}):
                self.refused({"source": [{"ref": "a", key: bad}]}, ["'m'", "feed 0", key])

    def test_weights_or_delays_beside_a_feed_that_holds_one(self):
        for arr in ("weights", "delays"):
            for feed_key in ("weight", "delay"):
                self.refused({"source": [{"ref": "a", feed_key: 1}], arr: [1]}, ["'m'", arr])

    def test_a_node_with_no_id_is_named(self):
        self.refused({"source": [{"ref": "a", "weight": "x"}]}, ["<no id>"], nid=None)

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        doc = patch(multi({"source": [{"ref": "a", "weight": 0.5}]}, "first"),
                    multi({"source": [{"ref": "b", "weight": 0.5}, 3]}, "bad"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            multi_weights.apply(doc, notes, NoContext())
        self.assertIn("'bad'", str(cm.exception))
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])


class WalkTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"nodes": [multi({"source": [{"ref": "a", "weight": 0.5}]})], "output": "m"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "weight": 1}))
        self.assertEqual(doc["nodes"][0]["params"], {"source": [{"ref": "a"}], "weights": [0.5]})

    def test_a_nested_node_list_inside_a_node(self):
        inner = multi({"source": [{"ref": "a", "weight": 0.5}]}, "inner")
        doc = patch({"id": "host", "type": "MultiplexSource", "params": {"template": {"nodes": [inner]}}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"node": 1, "weight": 1}))
        self.assertEqual(inner["params"], {"source": [{"ref": "a"}], "weights": [0.5]})

    def test_other_types_are_untouched(self):
        doc = patch({"id": "c", "type": "CombinedSource",
                     "params": {"source": [{"ref": "a", "weight": 0.5}, 3]}},
                    {"id": "n", "type": "MultiSource"})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(multi({"source": [{"ref": "a", "weight": 0.5}, {"ref": "b", "delay": 2}]}),
                    multi({"source": {"tap": "t", "weight": 2}}, "m2"))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"node": 2, "weight": 1, "delay": 1, "dropped": 1}))
        self.assertEqual(params_of(doc, 1), {"source": {"tap": "t"}})
        self.assertEqual(len(notes), 1)
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()
