"""The paramMap rule: each emission form, the slot decision from the registry's
descriptors, both node-container shapes, the refusals, the prepend order,
idempotence, and a document with no paramMap never asking for descriptors."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import Context, parammap  # noqa: E402

DESC = {
    "SineSource": {"params": ["amplitude", "frequency", "phase"], "inputs": [], "settings": []},
    "SVFSource": {"params": ["cutoffFreq", "resonance"], "inputs": ["source"], "settings": []},
    "CombinedSource": {"params": [], "inputs": ["source1", "source2"],
                       "settings": [{"name": "operation"}, {"name": "gainAdj"}]},
    "KSString": {"params": ["frequency", "amplitude"], "inputs": ["source"],
                 "settings": [{"name": "t60"}, {"name": "brightness"}]},
    "Envelope": {"params": [], "inputs": [], "settings": [{"name": "timeScale"}]},
}


class FailingContext:
    """A context whose descriptors() fails the test: a rule that asks has
    triggered the CLI where it must not."""
    def __init__(self, test):
        self.test = test

    def descriptors(self):
        self.test.fail("descriptors() asked for a document with no paramMap")


def instrument(param_map, *nodes):
    return {"sampleRate": 48000,
            "instrument": {"polyphony": 1, "paramMap": param_map},
            "graph": {"nodes": list(nodes), "output": nodes[-1]["id"]}}


def sine(nid="osc", **params):
    return {"id": nid, "type": "SineSource", "params": dict(params)}


def apply(doc, desc=DESC):
    notes = []
    counts = parammap.apply(doc, notes, Context(descriptors=desc))
    return counts, notes


def nodes_of(doc):
    return doc["graph"]["nodes"]


def by_id(doc):
    return {n["id"]: n for n in nodes_of(doc)}


class EmissionTests(unittest.TestCase):
    def test_bare_entry_refs_the_frequency_perform_node(self):
        doc = instrument({"frequency": "osc.frequency"}, sine(amplitude=0.5, frequency=440.0))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"paramMap entry": 1, "paramMap": 1}))
        self.assertEqual(notes, [])
        self.assertNotIn("paramMap", doc["instrument"])
        self.assertEqual(doc["instrument"], {"polyphony": 1})
        self.assertEqual(nodes_of(doc)[0], {"id": "__perf_freq", "type": "PerformNode",
                                            "params": {"field": "frequency"}})
        self.assertEqual(by_id(doc)["osc"]["params"], {"amplitude": 0.5, "frequency": {"ref": "__perf_freq"}})

    def test_curve_is_a_logx_curve_node_reading_the_frequency(self):
        doc = instrument({"frequency": {"target": "osc.frequency", "curve": [[100, 200], [1000, 2000]]}}, sine())
        apply(doc)
        n = by_id(doc)
        self.assertEqual(n["__curve_1"], {"id": "__curve_1", "type": "CurveNode",
                                          "params": {"interp": "logx",
                                                     "knots": [[100.0, 200.0], [1000.0, 2000.0]],
                                                     "source": {"ref": "__perf_freq"}}})
        self.assertIsInstance(n["__curve_1"]["params"]["knots"][0][0], float)
        self.assertEqual(n["osc"]["params"]["frequency"], {"ref": "__curve_1"})

    def test_curve_with_interp_loglog_is_loglog(self):
        doc = instrument({"frequency": {"target": "osc.frequency", "curve": [[100, 200], [1000, 2000]],
                                        "interp": "loglog"}},
                         sine())
        apply(doc)
        self.assertEqual(by_id(doc)["__curve_1"]["params"]["interp"], "loglog")
        doc = instrument({"frequency": {"target": "osc.frequency", "curve": [[100, 200], [1000, 2000]],
                                        "interp": "linear"}},
                         sine())
        apply(doc)
        self.assertEqual(by_id(doc)["__curve_1"]["params"]["interp"], "logx")    # anything else is logx

    def test_vcurve_alone_multiplies_the_frequency_by_a_velocity_curve(self):
        doc = instrument({"frequency": {"target": "osc.frequency", "vcurve": [[0, 0.5], [1, 1]]}}, sine())
        apply(doc)
        n = by_id(doc)
        self.assertEqual(n["__perf_vel"], {"id": "__perf_vel", "type": "PerformNode",
                                           "params": {"field": "velocity"}})
        self.assertEqual(n["__curve_1"]["params"], {"interp": "linear", "knots": [[0.0, 0.5], [1.0, 1.0]],
                                                    "source": {"ref": "__perf_vel"}})
        self.assertEqual(n["__mul_2"], {"id": "__mul_2", "type": "CombinedSource",
                                        "params": {"source1": {"ref": "__perf_freq"},
                                                   "source2": {"ref": "__curve_1"},
                                                   "operation": "multiply", "gainAdj": 0.0}})
        self.assertEqual(n["osc"]["params"]["frequency"], {"ref": "__mul_2"})

    def test_curve_and_vcurve_chain_both(self):
        doc = instrument({"frequency": {"target": "f.cutoffFreq", "curve": [[100, 400], [1000, 4000]],
                                        "vcurve": [[0, 0.5], [1, 1]]}},
                         {"id": "f", "type": "SVFSource", "params": {"cutoffFreq": 1000.0}})
        apply(doc)
        n = by_id(doc)
        self.assertEqual(n["__curve_1"]["params"]["source"], {"ref": "__perf_freq"})
        self.assertEqual(n["__curve_2"]["params"]["source"], {"ref": "__perf_vel"})
        self.assertEqual(n["__mul_3"]["params"]["source1"], {"ref": "__curve_1"})
        self.assertEqual(n["__mul_3"]["params"]["source2"], {"ref": "__curve_2"})
        self.assertEqual(n["f"]["params"]["cutoffFreq"], {"ref": "__mul_3"})

    def test_a_target_with_no_dot_defaults_to_the_frequency_pin(self):
        doc = instrument({"frequency": "osc"}, sine(frequency=220.0))
        apply(doc)
        self.assertEqual(by_id(doc)["osc"]["params"]["frequency"], {"ref": "__perf_freq"})

    def test_a_name_other_than_frequency_is_dropped_with_a_note_and_the_map_still_goes(self):
        doc = instrument({"velocity": "osc.amplitude"}, sine(amplitude=0.5))
        before_osc = copy.deepcopy(by_id(doc)["osc"])
        counts, notes = apply(doc)
        self.assertTrue(counts)                                                  # the map went: a change
        self.assertEqual(counts["paramMap entry"], 0)
        self.assertEqual(len(notes), 1)
        self.assertIn("'velocity' dropped", notes[0])
        self.assertNotIn("paramMap", doc["instrument"])
        self.assertEqual(by_id(doc)["osc"], before_osc)
        self.assertEqual([n["id"] for n in nodes_of(doc)], ["osc"])              # nothing synthesized

    def test_an_empty_map_is_removed_and_counted(self):
        doc = instrument({}, sine())
        counts, _ = apply(doc)
        self.assertTrue(counts)
        self.assertNotIn("paramMap", doc["instrument"])
        self.assertEqual([n["id"] for n in nodes_of(doc)], ["osc"])

    def test_a_list_of_entries_under_frequency(self):
        doc = instrument({"frequency": ["s.frequency",
                                        {"target": "s.t60", "curve": [[100, 8], [1000, 2]]},
                                        {"target": "s.brightness", "curve": [[100, 0.4], [1000, 0.8]]}]},
                         {"id": "s", "type": "KSString", "params": {"frequency": 110.0, "t60": 6.0}})
        counts, _ = apply(doc)
        self.assertEqual(counts["paramMap entry"], 3)
        s = by_id(doc)["s"]
        self.assertEqual(s["params"], {"frequency": {"ref": "__perf_freq"}, "t60": 6.0})
        self.assertEqual(s["dynamicPins"], {"t60": {"ref": "__curve_1"}, "brightness": {"ref": "__curve_2"}})
        self.assertEqual([n["id"] for n in nodes_of(doc)], ["__perf_freq", "__curve_1", "__curve_2", "s"])


class SlotTests(unittest.TestCase):
    def test_a_setting_lands_in_dynamic_pins(self):
        doc = instrument({"frequency": {"target": "e.timeScale", "curve": [[100, 1], [1000, 0.5]]}},
                         {"id": "e", "type": "Envelope", "params": {"preset": "ar"}})
        apply(doc)
        e = by_id(doc)["e"]
        self.assertEqual(e["dynamicPins"], {"timeScale": {"ref": "__curve_1"}})
        self.assertEqual(e["params"], {"preset": "ar"})

    def test_a_pin_lands_in_params(self):
        doc = instrument({"frequency": "s.frequency"}, {"id": "s", "type": "KSString", "params": {"frequency": 1.0}})
        apply(doc)
        self.assertNotIn("dynamicPins", by_id(doc)["s"])
        self.assertEqual(by_id(doc)["s"]["params"]["frequency"], {"ref": "__perf_freq"})

    def test_a_pin_the_node_leaves_unset_is_added(self):
        doc = instrument({"frequency": "osc.frequency"}, sine(amplitude=0.3))
        apply(doc)
        self.assertEqual(by_id(doc)["osc"]["params"], {"amplitude": 0.3, "frequency": {"ref": "__perf_freq"}})
        doc = instrument({"frequency": "osc.frequency"}, {"id": "osc", "type": "SineSource"})
        apply(doc)
        self.assertEqual(by_id(doc)["osc"]["params"], {"frequency": {"ref": "__perf_freq"}})

    def test_an_input_lands_in_params(self):
        doc = instrument({"frequency": {"target": "mix.source2", "curve": [[100, 0.2], [1000, 0.8]]}},
                         sine(), {"id": "mix", "type": "CombinedSource",
                                  "params": {"source1": {"ref": "osc"}, "operation": "multiply"}})
        counts, _ = apply(doc)
        self.assertEqual(counts["paramMap entry"], 1)
        mix = by_id(doc)["mix"]
        self.assertNotIn("dynamicPins", mix)
        self.assertEqual(mix["params"]["source2"], {"ref": "__curve_1"})
        self.assertEqual(mix["params"]["source1"], {"ref": "osc"})

    def test_the_slot_follows_the_injected_table(self):
        desc = copy.deepcopy(DESC)
        desc["SineSource"]["settings"] = [{"name": "frequency"}]                 # the table decides, not the name
        doc = instrument({"frequency": "osc.frequency"}, sine())
        apply(doc, desc)
        self.assertEqual(by_id(doc)["osc"]["dynamicPins"], {"frequency": {"ref": "__perf_freq"}})


class ShapeTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"instrument": {"paramMap": {"frequency": "osc.frequency"}},
               "nodes": [sine()], "output": "osc"}
        apply(doc)
        self.assertEqual([n["id"] for n in doc["nodes"]], ["__perf_freq", "osc"])
        self.assertEqual(doc["nodes"][1]["params"]["frequency"], {"ref": "__perf_freq"})

    def test_no_node_list_is_refused(self):
        with self.assertRaises(ValueError):
            apply({"instrument": {"paramMap": {"frequency": "osc.frequency"}}})


class RefusalTests(unittest.TestCase):
    def test_an_unknown_node(self):
        with self.assertRaises(ValueError) as cm:
            apply(instrument({"frequency": "nobody.frequency"}, sine()))
        self.assertIn("no node 'nobody'", str(cm.exception))

    def test_an_unregistered_type(self):
        with self.assertRaises(ValueError) as cm:
            apply(instrument({"frequency": "x.frequency"}, {"id": "x", "type": "NoSuchType", "params": {}}))
        self.assertIn("unregistered type 'NoSuchType'", str(cm.exception))

    def test_an_entry_that_is_neither_a_string_nor_an_object_with_a_target(self):
        for entry in ({"curve": [[1, 1], [2, 2]]}, {"target": 7}, 440.0, None):
            doc = instrument({"frequency": ["osc.frequency", entry]}, sine())
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("neither", str(cm.exception))
            self.assertEqual(doc, before)                                        # refused before anything changed

    def test_a_param_map_that_is_not_an_object(self):
        for pm in (None, ["osc.frequency"], "osc.frequency", 440.0):
            doc = instrument(pm, sine())
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("instrument.paramMap", str(cm.exception))
            self.assertIn("not an object", str(cm.exception))
            self.assertEqual(doc, before)

    def test_a_malformed_entry_under_another_name(self):
        for entry in ({"curve": [[1, 1], [2, 2]]}, {"target": 7}, 440.0, None):
            doc = instrument({"velocity": ["osc.amplitude", entry], "frequency": "osc.frequency"}, sine())
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("neither", str(cm.exception))
            self.assertEqual(doc, before)

    def test_a_curve_that_is_not_two_or_more_number_pairs(self):
        bad = ([[100, 200]], [], "steep", [[100, 200], [1000]], [[100, 200], [1000, 2000, 3]],
               [[100, 200], [True, 2000]], [[100, 200], ["1000", 2000]], [[100, 200], None])
        for key in ("curve", "vcurve"):
            for knots in bad:
                for name in ("frequency", "velocity"):
                    entry = {"target": "osc.frequency", key: knots}
                    doc = instrument({name: entry}, sine())
                    before = copy.deepcopy(doc)
                    with self.assertRaises(ValueError, msg="%s %r under %s" % (key, knots, name)) as cm:
                        apply(doc)
                    msg = str(cm.exception)
                    self.assertIn("'osc.frequency'", msg)
                    self.assertIn(key + " ", msg)
                    self.assertIn("knots", msg)
                    self.assertEqual(doc, before)

    def test_a_curve_of_ints_and_floats_is_taken(self):
        doc = instrument({"frequency": {"target": "osc.frequency", "curve": [[100, 2.5], [1000.0, 3]]}}, sine())
        counts, _ = apply(doc)
        self.assertEqual(counts["paramMap entry"], 1)

    def test_a_target_that_ends_in_a_dot(self):
        doc = instrument({"frequency": "osc."}, sine(frequency=440.0))
        before = copy.deepcopy(doc)
        with self.assertRaises(ValueError) as cm:
            apply(doc)
        self.assertIn("no pin after the dot", str(cm.exception))
        self.assertEqual(doc, before)

    def test_a_target_with_no_pin_or_setting_of_that_name(self):
        for pm in ({"frequency": "osc.cutoff"},
                   {"frequency": ["osc.frequency", {"target": "f.mode", "curve": [[1, 1], [2, 2]]}]},
                   {"frequency": "f"}):
            doc = instrument(pm, sine(), {"id": "f", "type": "SVFSource", "params": {"source": {"ref": "osc"}}})
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("has no pin or setting", str(cm.exception))
            self.assertEqual(doc, before)

    def test_a_synthesized_id_already_taken(self):
        for taken, pm in (("__perf_freq", {"frequency": "osc.frequency"}),
                          ("__perf_vel", {"frequency": {"target": "osc.frequency", "vcurve": [[0, 1], [1, 1]]}}),
                          ("__curve_1", {"frequency": {"target": "osc.frequency", "curve": [[1, 1], [2, 2]]}}),
                          ("__mul_2", {"frequency": {"target": "osc.frequency", "vcurve": [[0, 1], [1, 1]]}})):
            doc = instrument(pm, {"id": taken, "type": "SineSource", "params": {}}, sine())
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn(taken, str(cm.exception))
            self.assertIn("already", str(cm.exception))
            self.assertEqual(doc, before)

    def test_an_unsynthesized_lookalike_id_is_not_refused(self):
        doc = instrument({"frequency": "osc.frequency"}, {"id": "__curve_7", "type": "SineSource", "params": {}},
                         sine())
        counts, _ = apply(doc)
        self.assertEqual(counts["paramMap entry"], 1)

    def test_a_target_prewired_in_params(self):
        for wired in ({"ref": "lfo"}, {"tap": "lfo"}, [{"ref": "lfo"}, {"ref": "lfo2"}]):
            doc = instrument({"frequency": "osc.frequency"}, sine("lfo"), sine(frequency=wired))
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("osc.frequency", str(cm.exception))
            self.assertIn("already wired", str(cm.exception))

    def test_a_target_prewired_in_dynamic_pins(self):
        node = {"id": "s", "type": "KSString", "params": {}, "dynamicPins": {"t60": {"ref": "lfo"}}}
        doc = instrument({"frequency": {"target": "s.t60", "curve": [[1, 1], [2, 2]]}}, sine("lfo"), node)
        with self.assertRaises(ValueError) as cm:
            apply(doc)
        self.assertIn("s.t60", str(cm.exception))
        self.assertIn("already wired", str(cm.exception))

    def test_a_constant_target_is_not_prewired(self):
        doc = instrument({"frequency": "osc.frequency"}, sine(frequency=440.0))
        counts, _ = apply(doc)
        self.assertEqual(counts["paramMap entry"], 1)

    def test_a_duplicate_target(self):
        for pm in ({"frequency": ["osc.frequency", "osc.frequency"]},
                   {"frequency": ["osc", {"target": "osc.frequency", "curve": [[1, 1], [2, 2]]}]}):
            doc = instrument(pm, sine())
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("osc.frequency", str(cm.exception))
            self.assertIn("more than once", str(cm.exception))


class OrderAndIdempotenceTests(unittest.TestCase):
    def test_new_nodes_are_prepended_in_dependency_order(self):
        doc = instrument({"frequency": ["s.frequency",
                                        {"target": "f.cutoffFreq", "curve": [[100, 400], [1000, 4000]],
                                         "vcurve": [[0, 1], [1, 1]]},
                                        {"target": "s.t60", "curve": [[100, 4], [1000, 2]], "interp": "loglog"}]},
                         {"id": "s", "type": "KSString", "params": {}},
                         {"id": "f", "type": "SVFSource", "params": {"source": {"ref": "s"}}})
        apply(doc)
        seen = set()

        def refs(o):
            if isinstance(o, dict):
                if isinstance(o.get("ref"), str):
                    yield o["ref"]
                for v in o.values():
                    yield from refs(v)
            elif isinstance(o, list):
                for v in o:
                    yield from refs(v)

        for n in nodes_of(doc):
            for r in refs(n):
                self.assertIn(r, seen, "%s refs %s before it is built" % (n["id"], r))
            seen.add(n["id"])
        self.assertEqual([n["id"] for n in nodes_of(doc)][-2:], ["s", "f"])     # the file's own nodes after

    def test_a_second_apply_changes_nothing(self):
        doc = instrument({"frequency": ["osc.frequency"]}, sine())
        apply(doc)
        once = copy.deepcopy(doc)
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter())
        self.assertFalse(counts)
        self.assertEqual(notes, [])
        self.assertEqual(doc, once)

    def test_no_parammap_is_untouched_and_never_asks_for_descriptors(self):
        for doc in ({"graph": {"nodes": [sine()], "output": "osc"}, "instrument": {"polyphony": 1}},
                    {"graph": {"nodes": [sine()], "output": "osc"}},
                    {"nodes": [sine()], "output": "osc"}):
            before = copy.deepcopy(doc)
            notes = []
            counts = parammap.apply(doc, notes, FailingContext(self))
            self.assertEqual(counts, Counter())
            self.assertEqual(doc, before)
            self.assertEqual(notes, [])

    def test_an_injected_table_is_answered_as_given(self):
        ctx = Context(descriptors=DESC)
        self.assertIs(ctx.descriptors(), DESC)


if __name__ == "__main__":
    unittest.main()
