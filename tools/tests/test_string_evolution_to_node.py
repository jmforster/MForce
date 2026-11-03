"""The string-typed evolution converter: the three forms, the seed default,
the insertion point, idempotence, and the forms it refuses."""
import copy
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import string_evolution_to_node as conv  # noqa: E402


def wavetable(params):
    return {"sampleRate": 48000, "graph": {"nodes": [
        {"id": "env", "type": "Envelope", "params": {"preset": "ar"}},
        {"id": "wt", "type": "WavetableSource", "params": params},
        {"id": "ch", "type": "SoundChannel", "inputs": {"source": "wt"}},
    ], "output": "ch"}}


class ConvertTests(unittest.TestCase):
    def test_pluck_moves_muting_and_seed(self):
        doc = wavetable({"seed": 12345, "frequency": 330.0, "amplitude": {"ref": "env"},
                         "evolution": "pluck", "muting": 0.25, "evolutionSeed": 7})
        self.assertEqual(conv.convert(doc), 1)
        nodes = doc["graph"]["nodes"]
        self.assertEqual([n["id"] for n in nodes], ["env", "wt_evolution", "wt", "ch"])   # inserted before the wavetable
        self.assertEqual(nodes[1], {"id": "wt_evolution", "type": "PluckEvolution",
                                    "params": {"muting": 0.25, "seed": 7}})
        self.assertEqual(nodes[2]["params"], {"seed": 12345, "frequency": 330.0, "amplitude": {"ref": "env"},
                                              "evolution": {"ref": "wt_evolution"}})
        self.assertEqual(list(nodes[2]["params"]), ["seed", "frequency", "amplitude", "evolution"])  # key order kept

    def test_seed_defaults_to_the_string_forms_42(self):
        doc = wavetable({"evolution": "pluck", "muting": 0.0})
        conv.convert(doc)
        self.assertEqual(doc["graph"]["nodes"][1]["params"], {"muting": 0.0, "seed": 42})

    def test_only_present_keys_move_plus_the_differing_branch_defaults(self):
        doc = wavetable({"evolution": "averaging", "sampleCount": 4.0, "leading": True, "evolutionSeed": 3})
        conv.convert(doc)
        ev = doc["graph"]["nodes"][1]
        self.assertEqual(ev["type"], "AveragingEvolution")
        self.assertEqual(ev["params"], {"sampleCount": 4.0, "leading": True, "decayFactor": 0.996, "seed": 3})
        pluck = wavetable({"evolution": "pluck"})                        # pluck's and target's defaults are equal: nothing added
        conv.convert(pluck)
        self.assertEqual(pluck["graph"]["nodes"][1]["params"], {"seed": 42})

    def test_a_bare_averaging_form_gets_both_branch_defaults(self):
        doc = wavetable({"evolution": "averaging"})
        conv.convert(doc)
        self.assertEqual(doc["graph"]["nodes"][1]["params"], {"sampleCount": 2.0, "decayFactor": 0.996, "seed": 42})

    def test_seed_is_written_as_an_int(self):
        doc = wavetable({"evolution": "pluck", "evolutionSeed": 7.0})
        conv.convert(doc)
        seed = doc["graph"]["nodes"][1]["params"]["seed"]
        self.assertIsInstance(seed, int)
        self.assertEqual(seed, 7)

    def test_target_moves_its_five_keys(self):
        doc = wavetable({"evolution": "target", "morphRate": 0.0005, "holdCycles": 5, "decayFactor": 0.999,
                         "targetPartials": [1.0, 0.5], "targetLength": 2048})
        conv.convert(doc)
        ev = doc["graph"]["nodes"][1]
        self.assertEqual(ev["type"], "TargetEvolution")
        self.assertEqual(ev["params"], {"morphRate": 0.0005, "holdCycles": 5, "decayFactor": 0.999,
                                        "targetPartials": [1.0, 0.5], "targetLength": 2048, "seed": 42})
        self.assertNotIn("holdCycles", doc["graph"]["nodes"][2]["params"])

    def test_idempotent_and_untouched_without_the_form(self):
        doc = wavetable({"evolution": "pluck", "muting": 0.0})
        conv.convert(doc)
        again = copy.deepcopy(doc)
        self.assertEqual(conv.convert(again), 0)
        self.assertEqual(again, doc)
        plain = wavetable({"evolution": {"ref": "env"}, "frequency": 220.0})
        before = copy.deepcopy(plain)
        self.assertEqual(conv.convert(plain), 0)
        self.assertEqual(plain, before)
        self.assertEqual(conv.convert({"nothing": 1}), 0)

    def test_top_level_nodes_and_a_taken_id(self):
        doc = {"nodes": [{"id": "wt_evolution", "type": "Envelope", "params": {}},
                         {"id": "wt", "type": "WavetableSource", "params": {"evolution": "pluck"}}],
               "output": "wt"}
        conv.convert(doc)
        self.assertEqual([n["id"] for n in doc["nodes"]], ["wt_evolution", "wt_evolution2", "wt"])
        self.assertEqual(doc["nodes"][2]["params"]["evolution"], {"ref": "wt_evolution2"})

    def test_stray_evolution_seed_on_a_node_form_wavetable_is_dropped(self):
        doc = wavetable({"evolution": {"ref": "ev"}, "evolutionSeed": 42, "frequency": 220.0})
        self.assertEqual(conv.convert(doc), 1)
        self.assertEqual(doc["graph"]["nodes"][1]["params"], {"evolution": {"ref": "ev"}, "frequency": 220.0})
        self.assertEqual(len(doc["graph"]["nodes"]), 3)                    # no node added
        self.assertEqual(conv.convert(doc), 0)

    def test_refusals(self):
        with self.assertRaises(ValueError):
            conv.convert(wavetable({"evolution": "target", "targetWave": [0.0, 1.0, 0.0, -1.0]}))
        with self.assertRaises(ValueError):
            conv.convert(wavetable({"evolution": "none"}))

    def test_file_round_trip(self):
        doc = wavetable({"evolution": "pluck", "muting": 0.1, "evolutionSeed": 9})
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "p.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump(doc, f)
            self.assertEqual(conv.convert_file(path), 1)
            text = open(path, encoding="utf-8").read()
            self.assertTrue(text.endswith("}\n"))
            self.assertEqual(json.loads(text)["graph"]["nodes"][1]["params"], {"muting": 0.1, "seed": 9})
            self.assertEqual(conv.convert_file(path), 0)


if __name__ == "__main__":
    unittest.main()
