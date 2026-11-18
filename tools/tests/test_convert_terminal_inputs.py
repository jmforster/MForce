"""The terminal-inputs rule: a SoundChannel's inputs.source as params.source
{"ref"}, a StereoMixer's inputs.channels as params.channels {"ref"} feeds in
order; params made where inputs stood, or the new key appended to the
params there; an empty inputs dropped with a note; the refusals (each
leaving the document unchanged); a nested node list; idempotence; a file
with nothing to do."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import terminal_inputs  # noqa: E402


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the terminal-inputs rule asked for descriptors")


def plain_sound(*extra, channels=("ch",)):
    """A plain sound in the old form: an oscillator, a channel and a mixer."""
    return {"sampleRate": 48000, "seconds": 2.0, "graph": {"nodes": [
        {"id": "osc", "type": "SineSource", "params": {"frequency": 440.0}},
        {"id": "ch", "type": "SoundChannel", "inputs": {"source": "osc"}, "params": {"volume": 0.8, "pan": 0.0}},
        *extra,
        {"id": "mix", "type": "StereoMixer", "params": {"gainL": 1.0, "gainR": 1.0},
         "inputs": {"channels": list(channels)}},
    ], "output": "mix"}}


def apply(doc):
    notes = []
    counts = terminal_inputs.apply(doc, notes, NoContext())
    return counts, notes


def nodes_of(doc):
    return {n["id"]: n for n in doc["graph"]["nodes"]}


class EmissionTests(unittest.TestCase):
    def test_a_channel_and_a_mixer_with_params(self):
        doc = plain_sound()
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"source": 1, "channels": 1}))
        self.assertEqual(notes, [])
        n = nodes_of(doc)
        self.assertEqual(n["ch"], {"id": "ch", "type": "SoundChannel",
                                   "params": {"volume": 0.8, "pan": 0.0, "source": {"ref": "osc"}}})
        self.assertEqual(list(n["ch"]), ["id", "type", "params"])
        self.assertEqual(list(n["ch"]["params"]), ["volume", "pan", "source"])          # appended
        self.assertEqual(n["mix"], {"id": "mix", "type": "StereoMixer",
                                    "params": {"gainL": 1.0, "gainR": 1.0, "channels": [{"ref": "ch"}]}})
        self.assertEqual(list(n["mix"]["params"]), ["gainL", "gainR", "channels"])
        self.assertEqual(n["osc"], {"id": "osc", "type": "SineSource", "params": {"frequency": 440.0}})

    def test_params_is_made_where_inputs_stood(self):
        doc = {"graph": {"nodes": [
            {"id": "ch", "type": "SoundChannel", "inputs": {"source": "osc"}, "extra": 1},
            {"inputs": {"channels": ["ch"]}, "id": "mix", "type": "StereoMixer"}], "output": "mix"}}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"source": 1, "channels": 1}))
        ch, mix = doc["graph"]["nodes"]
        self.assertEqual(list(ch), ["id", "type", "params", "extra"])
        self.assertEqual(ch["params"], {"source": {"ref": "osc"}})
        self.assertEqual(list(mix), ["params", "id", "type"])
        self.assertEqual(mix["params"], {"channels": [{"ref": "ch"}]})

    def test_the_channels_keep_their_order(self):
        doc = plain_sound({"id": "b", "type": "SoundChannel", "inputs": {"source": "osc"}},
                          {"id": "a", "type": "SoundChannel", "inputs": {"source": "osc"}},
                          channels=("b", "ch", "a"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"source": 3, "channels": 1}))
        self.assertEqual(nodes_of(doc)["mix"]["params"]["channels"],
                         [{"ref": "b"}, {"ref": "ch"}, {"ref": "a"}])

    def test_an_empty_channel_list(self):
        doc = plain_sound(channels=())
        apply(doc)
        self.assertEqual(nodes_of(doc)["mix"]["params"]["channels"], [])

    def test_an_empty_inputs_is_dropped_with_a_note(self):
        for ntype in ("SoundChannel", "StereoMixer"):
            doc = {"graph": {"nodes": [{"id": "t", "type": ntype, "inputs": {}, "params": {"volume": 1.0}}],
                             "output": "t"}}
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"empty": 1}))
            self.assertEqual(doc["graph"]["nodes"][0], {"id": "t", "type": ntype, "params": {"volume": 1.0}})
            self.assertEqual(len(notes), 1)
            for needle in ("'t'", ntype, "inputs"):
                self.assertIn(needle, notes[0])

    def test_a_nested_node_list(self):
        inner = {"id": "ich", "type": "SoundChannel", "inputs": {"source": "x"}}
        doc = {"graph": {"nodes": [{"id": "host", "type": "MultiplexSource",
                                    "params": {"template": {"nodes": [inner]}}}], "output": "host"}}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"source": 1}))
        self.assertEqual(inner, {"id": "ich", "type": "SoundChannel", "params": {"source": {"ref": "x"}}})

    def test_top_level_nodes(self):
        doc = {"nodes": [{"id": "ch", "type": "SoundChannel", "inputs": {"source": "o"}}], "output": "ch"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"source": 1}))
        self.assertEqual(doc["nodes"][0]["params"], {"source": {"ref": "o"}})


class RefusalTests(unittest.TestCase):
    def refused(self, doc, needles):
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            terminal_inputs.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_inputs_on_another_type(self):
        for ntype in ("SineSource", "Mixer", None):
            doc = plain_sound({"id": "x", "type": ntype, "inputs": {"source": "osc"}})
            self.refused(doc, ["'x'", "inputs"])

    def test_an_empty_inputs_on_another_type(self):
        self.refused(plain_sound({"id": "x", "type": "SineSource", "inputs": {}}), ["'x'", "SineSource", "inputs"])

    def test_a_key_beside_the_types_one(self):
        self.refused(plain_sound({"id": "c2", "type": "SoundChannel", "inputs": {"source": "osc", "pan": "osc"}}),
                     ["'c2'", "SoundChannel", "pan"])
        self.refused(plain_sound({"id": "c2", "type": "SoundChannel", "inputs": {"channels": ["osc"]}}),
                     ["'c2'", "SoundChannel", "channels"])
        doc = plain_sound()
        nodes_of(doc)["mix"]["inputs"]["source"] = "ch"
        self.refused(doc, ["'mix'", "StereoMixer", "source"])

    def test_a_value_of_another_shape(self):
        for bad in (["osc"], {"ref": "osc"}, 3, None, True):
            self.refused(plain_sound({"id": "c2", "type": "SoundChannel", "inputs": {"source": bad}}),
                         ["'c2'", "SoundChannel", "source"])
        for bad in ("ch", [{"ref": "ch"}], ["ch", 2], {"a": "ch"}, None):
            doc = plain_sound()
            nodes_of(doc)["mix"]["inputs"]["channels"] = bad
            self.refused(doc, ["'mix'", "StereoMixer", "channels"])

    def test_inputs_that_is_not_an_object(self):
        for bad in (["osc"], "osc", None):
            self.refused(plain_sound({"id": "c2", "type": "SoundChannel", "inputs": bad}),
                         ["'c2'", "SoundChannel", "inputs"])

    def test_params_that_is_not_an_object(self):
        self.refused(plain_sound({"id": "c2", "type": "SoundChannel", "inputs": {"source": "osc"}, "params": [1]}),
                     ["'c2'", "SoundChannel", "params"])

    def test_the_key_already_under_params(self):
        self.refused(plain_sound({"id": "c2", "type": "SoundChannel", "inputs": {"source": "osc"},
                                  "params": {"source": {"ref": "osc"}}}),
                     ["'c2'", "SoundChannel", "source"])
        doc = plain_sound()
        nodes_of(doc)["mix"]["params"]["channels"] = [{"ref": "ch"}]
        self.refused(doc, ["'mix'", "StereoMixer", "channels"])

    def test_a_node_with_no_id_is_named(self):
        self.refused(plain_sound({"type": "SineSource", "inputs": {"source": "osc"}}), ["<no id>"])

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        # The channel and the mixer before the refused node would be rewritten.
        self.refused(plain_sound({"id": "bad", "type": "SoundChannel", "inputs": {"source": 1}}), ["'bad'"])


class OtherTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = plain_sound({"id": "e", "type": "SoundChannel", "inputs": {}})
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"source": 1, "channels": 1, "empty": 1}))
        self.assertEqual(len(notes), 1)
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)

    def test_a_file_with_nothing_to_do(self):
        doc = {"sampleRate": 48000, "instrument": {"polyphony": 2},
               "graph": {"nodes": [{"id": "osc", "type": "SineSource", "params": {"source": "x"}},
                                   {"id": "ch", "type": "SoundChannel", "params": {"source": {"ref": "osc"}}}],
                         "output": "osc"}}
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)
        self.assertEqual(apply({"sampleRate": 1}), (Counter(), []))


if __name__ == "__main__":
    unittest.main()
