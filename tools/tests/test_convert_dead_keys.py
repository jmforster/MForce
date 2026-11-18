"""The dead-key rule: instrument.release dropped with a note giving its
value; a null dynamicPins inside a node's params dropped with a note naming
the node; a dynamicPins inside params that is not null refused (the document
unchanged); an Envelope's own release left alone; idempotence; a file with
nothing to do."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import dead_keys  # noqa: E402


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the dead-key rule asked for descriptors")


def instrument_patch(instrument, *nodes):
    return {"sampleRate": 48000, "instrument": instrument,
            "graph": {"nodes": list(nodes) or [{"id": "osc", "type": "SineSource", "params": {}}],
                      "output": "osc"}}


def envelope(params, nid="env"):
    return {"id": nid, "type": "Envelope", "params": params}


def apply(doc):
    notes = []
    counts = dead_keys.apply(doc, notes, NoContext())
    return counts, notes


class ReleaseTests(unittest.TestCase):
    def test_instrument_release_is_dropped_with_a_note(self):
        for value, text in ((0.3, "0.3"), (2, "2"), ("long", '"long"'), (None, "null")):
            doc = instrument_patch({"polyphony": 4, "release": value, "volume": 0.5})
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"instrument.release": 1}))
            self.assertEqual(doc["instrument"], {"polyphony": 4, "volume": 0.5})
            self.assertEqual(list(doc["instrument"]), ["polyphony", "volume"])
            self.assertEqual(notes, ["instrument.release %s dropped (nothing reads it)" % text])

    def test_an_envelopes_release_is_not_this_rules(self):
        doc = instrument_patch({"polyphony": 1},
                               envelope({"preset": "adsr", "release": 0.4}),
                               {"id": "osc", "type": "SineSource", "params": {"release": 1.0}})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_release_elsewhere_than_the_instrument_block_is_left_alone(self):
        doc = instrument_patch({"polyphony": 1})
        doc["release"] = 0.2
        doc["graph"]["release"] = 0.2
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class DynamicPinsTests(unittest.TestCase):
    def test_a_null_dynamic_pins_inside_params_is_dropped_with_a_note(self):
        doc = instrument_patch({"polyphony": 1},
                               envelope({"preset": "ar", "dynamicPins": None, "attack": 0.1}),
                               envelope({"dynamicPins": None}, "e2"),
                               {"id": "osc", "type": "SineSource", "params": {"frequency": 1.0},
                                "dynamicPins": {"a": 1}})
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"dynamicPins": 2}))
        nodes = doc["graph"]["nodes"]
        self.assertEqual(nodes[0]["params"], {"preset": "ar", "attack": 0.1})
        self.assertEqual(list(nodes[0]["params"]), ["preset", "attack"])
        self.assertEqual(nodes[1]["params"], {})
        self.assertEqual(nodes[2], {"id": "osc", "type": "SineSource", "params": {"frequency": 1.0},
                                    "dynamicPins": {"a": 1}})                  # beside params: not this rule's
        self.assertEqual(len(notes), 2)
        for note, nid in zip(notes, ("'env'", "'e2'")):
            for needle in (nid, "Envelope", "dynamicPins", "null"):
                self.assertIn(needle, note)

    def test_a_nested_node_list(self):
        inner = envelope({"dynamicPins": None}, "inner")
        doc = instrument_patch({"polyphony": 1},
                               {"id": "osc", "type": "MultiplexSource", "params": {"template": {"nodes": [inner]}}})
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"dynamicPins": 1}))
        self.assertEqual(inner["params"], {})
        self.assertIn("'inner'", notes[0])


class RefusalTests(unittest.TestCase):
    def test_a_dynamic_pins_inside_params_that_is_not_null(self):
        for bad in ({"freq": "x"}, {}, [], 0, False, "x"):
            doc = instrument_patch({"polyphony": 1, "release": 0.3},
                                   envelope({"dynamicPins": None}, "fine"),
                                   envelope({"attack": 0.1, "dynamicPins": copy.deepcopy(bad)}, "bad"))
            before = copy.deepcopy(doc)
            notes = []
            with self.assertRaises(ValueError) as cm:
                dead_keys.apply(doc, notes, NoContext())
            msg = str(cm.exception)
            for needle in ("'bad'", "Envelope", "dynamicPins", "beside params"):
                self.assertIn(needle, msg)
            self.assertEqual(doc, before)
            self.assertEqual(notes, [])

    def test_a_node_with_no_id_is_named(self):
        doc = instrument_patch({"polyphony": 1}, {"type": "Envelope", "params": {"dynamicPins": {}}})
        with self.assertRaises(ValueError) as cm:
            dead_keys.apply(doc, [], NoContext())
        self.assertIn("<no id>", str(cm.exception))


class OtherTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = instrument_patch({"polyphony": 1, "release": 0.5}, envelope({"dynamicPins": None}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"instrument.release": 1, "dynamicPins": 1}))
        self.assertEqual(len(notes), 2)
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)

    def test_a_file_with_nothing_to_do(self):
        for doc in (instrument_patch({"polyphony": 1, "volume": 1.0}),
                    {"sampleRate": 48000, "graph": {"nodes": [], "output": "x"}},
                    {"instrument": "x", "nodes": [{"id": "a", "type": "SineSource", "params": [1]}]}):
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc), (Counter(), []))
            self.assertEqual(doc, before)


if __name__ == "__main__":
    unittest.main()
