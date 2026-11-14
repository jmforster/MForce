"""The envelope-table rule: a stage list as eight columns, each absent key's
default, a label the loader read as its default written out, holdPct at 0
dropped, the dead preset keys by form, the sustainLevel cases, the refusals
(each leaving the document unchanged), the walk over both node-container
shapes and a nested node list, idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import envelope_table  # noqa: E402

COLUMNS = ["startVal", "endVal", "power", "type", "percent", "minSec", "maxSec", "nominal"]


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the envelope-table rule asked for descriptors")


def stage(start, end, pct, typ="Linear", power=0.0, mn=0.0, mx=0.0, **extra):
    s = {"startVal": start, "endVal": end, "power": power, "type": typ,
         "percent": pct, "minSec": mn, "maxSec": mx}
    s.update(extra)
    return s


def env(params, nid="e"):
    node = {"type": "Envelope", "params": params}
    if nid is not None:
        node["id"] = nid
    return node


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "e"}}


def apply(doc):
    notes = []
    counts = envelope_table.apply(doc, notes, NoContext())
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


def adsr_stages(s=0.6):
    return [stage(0.0, 1.0, 0.1), stage(1.0, s, 0.2), stage(s, s, 0.0), stage(s, 0.0, 0.3, "Sine")]


class EmissionTests(unittest.TestCase):
    def test_a_stage_list_becomes_eight_columns_in_order(self):
        doc = patch(env({"stages": [stage(0.0, 1.0, 0.1, "Expo", 2.0, 0.01, 0.5),
                                    stage(1.0, 0.0, 0.9, "Sine", 0.0, 0.0, 1.0)]}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1}))
        self.assertEqual(notes, [])
        p = params_of(doc)
        self.assertEqual(list(p), COLUMNS)
        self.assertEqual(p["startVal"], [0.0, 1.0])
        self.assertEqual(p["endVal"], [1.0, 0.0])
        self.assertEqual(p["power"], [2.0, 0.0])
        self.assertEqual(p["type"], ["Expo", "Sine"])
        self.assertEqual(p["percent"], [0.1, 0.9])
        self.assertEqual(p["minSec"], [0.01, 0.0])
        self.assertEqual(p["maxSec"], [0.5, 1.0])
        self.assertEqual(p["nominal"], [0.0, 0.0])

    def test_the_columns_stand_where_stages_stood(self):
        doc = patch(env({"seed": 7, "stages": [stage(0, 1, 1)], "timeMode": "seconds",
                         "maxValue": 0.5}))
        apply(doc)
        self.assertEqual(list(params_of(doc)), ["seed"] + COLUMNS + ["timeMode", "maxValue"])
        self.assertEqual(params_of(doc)["timeMode"], "seconds")

    def test_each_absent_key_takes_the_loaders_default(self):
        for key in COLUMNS:
            s = stage(0.25, 0.75, 0.5, "Expo", 3.0, 0.1, 0.2, nominal=0.4)
            del s[key]
            doc = patch(env({"stages": [s]}))
            apply(doc)
            expected = "Linear" if key == "type" else 0.0
            self.assertEqual(params_of(doc)[key], [expected], key)
            self.assertIs(type(params_of(doc)[key][0]), type(expected), key)

    def test_a_stage_with_no_keys_is_all_defaults(self):
        doc = patch(env({"stages": [{}]}))
        apply(doc)
        p = params_of(doc)
        self.assertEqual([p[c] for c in COLUMNS],
                         [[0.0], [0.0], [0.0], ["Linear"], [0.0], [0.0], [0.0], [0.0]])

    def test_an_int_cell_stays_an_int(self):
        doc = patch(env({"stages": [{"startVal": 0, "endVal": 1, "percent": 2, "power": 3,
                                     "minSec": 4, "maxSec": 5, "nominal": 6, "type": "Hold"}]}))
        apply(doc)
        p = params_of(doc)
        for key, value in (("startVal", 0), ("endVal", 1), ("percent", 2), ("power", 3),
                           ("minSec", 4), ("maxSec", 5), ("nominal", 6)):
            self.assertEqual(p[key], [value])
            self.assertIs(type(p[key][0]), int, key)
        self.assertEqual(p["type"], ["Hold"])

    def test_each_of_the_five_labels_is_kept(self):
        labels = ["Linear", "Expo", "InverseExpo", "Sine", "Hold"]
        doc = patch(env({"stages": [stage(0, 1, 0.2, t) for t in labels]}))
        counts, notes = apply(doc)
        self.assertEqual(params_of(doc)["type"], labels)
        self.assertEqual(counts, Counter({"stage list": 1}))
        self.assertEqual(notes, [])

    def test_another_type_string_is_written_as_linear_with_a_note(self):
        for bad in ("expo", "EXPO", "Exponential", "", "Sine "):
            doc = patch(env({"stages": [stage(0, 1, 0.5, "Sine"), stage(1, 0, 0.5, bad)]}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["type"], ["Sine", "Linear"], bad)
            self.assertEqual(counts, Counter({"stage list": 1, "label": 1}))
            self.assertEqual(len(notes), 1)
            self.assertIn("'e'", notes[0])
            self.assertIn(repr(bad), notes[0])
            self.assertIn("Linear", notes[0])

    def test_hold_pct_at_zero_is_dropped_silently(self):
        for zero in (0, 0.0, -0.0):
            doc = patch(env({"stages": [stage(0, 1, 1, holdPct=zero)]}))
            counts, notes = apply(doc)
            self.assertEqual(list(params_of(doc)), COLUMNS)
            self.assertEqual(counts, Counter({"stage list": 1}))
            self.assertEqual(notes, [])

    def test_an_empty_stage_list_is_eight_empty_columns(self):
        doc = patch(env({"stages": []}))
        counts, _ = apply(doc)
        self.assertEqual(params_of(doc), {c: [] for c in COLUMNS})
        self.assertEqual(counts, Counter({"stage list": 1}))

    def test_time_mode_labels_are_kept(self):
        for tm in ("seconds", "fraction"):
            doc = patch(env({"stages": [stage(0, 1, 1)], "timeMode": tm}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["timeMode"], tm)
            self.assertEqual(counts, Counter({"stage list": 1}))
            self.assertEqual(notes, [])

    def test_another_time_mode_string_is_written_as_fraction_with_a_note(self):
        for bad in ("percent", "Seconds", "SECONDS", ""):
            doc = patch(env({"timeMode": bad, "stages": [stage(0, 1, 1)]}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["timeMode"], "fraction", bad)
            self.assertEqual(list(params_of(doc)), ["timeMode"] + COLUMNS)
            self.assertEqual(counts, Counter({"stage list": 1, "label": 1}))
            self.assertEqual(len(notes), 1)
            self.assertIn(repr(bad), notes[0])
            self.assertIn("fraction", notes[0])

    def test_an_absent_time_mode_stays_absent(self):
        doc = patch(env({"stages": [stage(0, 1, 1)]}))
        apply(doc)
        self.assertNotIn("timeMode", params_of(doc))

    def test_a_dead_preset_key_on_a_stage_list_is_dropped_with_a_note(self):
        dead = ["attack", "attackCurve", "attackPower", "attackMin", "attackMax",
                "decay", "decayCurve", "decayPower", "decayMin", "decayMax", "decayLevel",
                "release", "releaseCurve", "releasePower", "releaseMin", "releaseMax", "reverse"]
        self.assertEqual(sorted(envelope_table.STAGE_DEAD_KEYS), sorted(dead))
        for key in dead:
            doc = patch(env({key: 0.3, "stages": [stage(0, 1, 1)], "seed": 3}))
            counts, notes = apply(doc)
            self.assertEqual(list(params_of(doc)), COLUMNS + ["seed"], key)
            self.assertEqual(counts, Counter({"stage list": 1, "dead key": 1}))
            self.assertEqual(len(notes), 1)
            self.assertIn(key, notes[0])
            self.assertIn("'e'", notes[0])

    def test_two_dead_keys_count_two(self):
        doc = patch(env({"stages": [stage(0, 1, 1)], "attack": 0.1, "release": 0.2}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1, "dead key": 2}))
        self.assertEqual(len(notes), 2)

    def test_other_params_are_kept(self):
        doc = patch(env({"stages": [stage(0, 1, 1)], "stage_accuracy": 0.6, "ramp_accuracy": 0.7,
                         "seed": 9, "minValue": 0.1, "maxValue": {"ref": "lfo"},
                         "timeScale": 2.0, "dynamicPins": None}))
        apply(doc)
        p = params_of(doc)
        self.assertEqual(list(p), COLUMNS + ["stage_accuracy", "ramp_accuracy", "seed", "minValue",
                                             "maxValue", "timeScale", "dynamicPins"])
        self.assertEqual(p["maxValue"], {"ref": "lfo"})

    def test_the_rest_of_the_node_is_kept(self):
        node = env({"stages": [stage(0, 1, 1)]})
        node["dynamicPins"] = {"sustainLevel": {"ref": "c"}}
        doc = patch(node)
        apply(doc)
        self.assertEqual(doc["graph"]["nodes"][0]["dynamicPins"], {"sustainLevel": {"ref": "c"}})
        self.assertEqual(list(doc["graph"]["nodes"][0]), ["type", "params", "id", "dynamicPins"])


class SustainLevelTests(unittest.TestCase):
    def test_four_slot_shape_equal_to_the_key_is_kept(self):
        doc = patch(env({"stages": adsr_stages(0.6), "sustainLevel": 0.6}))
        counts, _ = apply(doc)
        self.assertEqual(params_of(doc)["sustainLevel"], 0.6)
        self.assertEqual(counts, Counter({"stage list": 1}))

    def test_equal_in_float32_is_equal(self):
        # 0.6 and the nearest float32 to it differ as doubles, not as floats.
        f32 = 0.6000000238418579
        doc = patch(env({"stages": adsr_stages(f32), "sustainLevel": 0.6}))
        apply(doc)
        self.assertEqual(params_of(doc)["sustainLevel"], 0.6)

    def test_four_slot_shape_differing_from_the_key_is_refused(self):
        doc = patch(env({"stages": adsr_stages(0.6), "sustainLevel": 0.5}))
        before = copy.deepcopy(doc)
        with self.assertRaises(ValueError) as cm:
            apply(doc)
        self.assertIn("'e'", str(cm.exception))
        self.assertIn("sustainLevel", str(cm.exception))
        self.assertEqual(doc, before)

    def test_an_int_key_on_a_four_slot_shape(self):
        doc = patch(env({"stages": adsr_stages(1), "sustainLevel": 1}))
        apply(doc)
        self.assertEqual(params_of(doc)["sustainLevel"], 1)
        doc = patch(env({"stages": adsr_stages(1), "sustainLevel": 0}))
        with self.assertRaises(ValueError):
            apply(doc)

    def test_another_shape_leaves_the_key_alone(self):
        shapes = (adsr_stages(0.6)[:3],                                          # three stages
                  adsr_stages(0.6) + [stage(0, 0, 0.1)],                         # five
                  [stage(0, 1, 0.1), stage(1, 0.6, 0.2), stage(0.6, 0.6, 0.1),   # expand not at 0
                   stage(0.6, 0, 0.3)],
                  [stage(0, 1, 0.1), stage(1, 0.5, 0.2), stage(0.6, 0.6, 0.0),   # slots unequal
                   stage(0.6, 0, 0.3)])
        for stages in shapes:
            doc = patch(env({"stages": stages, "sustainLevel": 0.2}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["sustainLevel"], 0.2)
            self.assertEqual(counts, Counter({"stage list": 1}))
            self.assertEqual(notes, [])

    def test_a_wired_sustain_level_is_left_alone(self):
        doc = patch(env({"stages": adsr_stages(0.6), "sustainLevel": {"ref": "c"}}))
        apply(doc)
        self.assertEqual(params_of(doc)["sustainLevel"], {"ref": "c"})


class PresetTests(unittest.TestCase):
    PRESET_KEYS = ["preset", "attack", "attackCurve", "attackPower", "attackMin", "attackMax",
                   "decay", "decayCurve", "decayPower", "decayMin", "decayMax", "decayLevel",
                   "release", "releaseCurve", "releasePower", "releaseMin", "releaseMax",
                   "reverse", "sustainLevel", "timeMode"]

    # What each builder in envelope_json.h reads, besides "preset".
    READS = {
        "ar": {"attack", "attackMin", "attackMax", "attackCurve", "attackPower",
               "releaseCurve", "releasePower"},
        "as": {"attack", "sustainLevel", "reverse", "attackCurve", "attackPower"},
        "asr": {"attack", "sustainLevel", "release", "attackCurve", "attackPower",
                "releaseCurve", "releasePower"},
        "ads": {"attack", "decay", "sustainLevel", "attackCurve", "attackPower",
                "decayCurve", "decayPower"},
        "adr": {"attack", "decay", "decayLevel", "attackCurve", "attackPower",
                "decayCurve", "decayPower", "releaseCurve", "releasePower"},
        "adsr": {"timeMode", "attack", "decay", "sustainLevel", "release",
                 "attackMin", "attackMax", "decayMin", "decayMax", "releaseMin", "releaseMax",
                 "attackCurve", "attackPower", "decayCurve", "decayPower",
                 "releaseCurve", "releasePower"},
    }

    def test_the_table_matches_the_builders(self):
        self.assertEqual(list(envelope_table.PRESET_KEYS), self.PRESET_KEYS)
        self.assertEqual({k: set(v) for k, v in envelope_table.PRESET_READS.items()}, self.READS)

    def test_each_form_drops_each_key_it_does_not_read(self):
        for preset, reads in self.READS.items():
            for key in self.PRESET_KEYS:
                if key in ("preset", "sustainLevel"):
                    continue
                doc = patch(env({"preset": preset, key: 0.0, "seed": 5}))
                counts, notes = apply(doc)
                if key in reads:
                    self.assertEqual(counts, Counter(), (preset, key))
                    self.assertEqual(params_of(doc), {"preset": preset, key: 0.0, "seed": 5})
                    self.assertEqual(notes, [])
                else:
                    self.assertEqual(counts, Counter({"dead key": 1}), (preset, key))
                    self.assertEqual(params_of(doc), {"preset": preset, "seed": 5})
                    self.assertEqual(len(notes), 1)
                    self.assertIn(key, notes[0])
                    self.assertIn(preset, notes[0])

    def test_sustain_level_is_never_dropped(self):
        for preset in self.READS:
            doc = patch(env({"preset": preset, "sustainLevel": 0.4}))
            counts, _ = apply(doc)
            self.assertEqual(counts, Counter())
            self.assertEqual(params_of(doc), {"preset": preset, "sustainLevel": 0.4})

    def test_time_mode_is_dead_off_adsr(self):
        doc = patch(env({"preset": "as", "timeMode": "seconds", "attack": 0.1}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"dead key": 1}))
        self.assertEqual(params_of(doc), {"preset": "as", "attack": 0.1})
        doc = patch(env({"preset": "adsr", "timeMode": "seconds", "attack": 0.1}))
        self.assertEqual(apply(doc)[0], Counter())

    def test_the_algev_ar_shape(self):
        doc = patch(env({"preset": "ar", "attack": 0.01, "release": 0.5, "releaseMax": 0.5}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"dead key": 2}))
        self.assertEqual(params_of(doc), {"preset": "ar", "attack": 0.01})
        self.assertEqual(len(notes), 2)

    def test_a_preset_outside_the_six_is_untouched(self):
        for preset in ("adsrr", "ADSR", 3, None):
            doc = patch(env({"preset": preset, "decay": 0.2, "startVal": [0.0]}))
            before = copy.deepcopy(doc)
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter())
            self.assertEqual(doc, before)
            self.assertEqual(notes, [])

    def test_a_preset_form_holding_a_column_key_is_refused(self):
        for col in COLUMNS:
            doc = patch(env({"preset": "adsr", col: [0.0], "attackMin": 0.1, "timeMode": "seconds"}))
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                apply(doc)
            self.assertIn("'e'", str(cm.exception))
            self.assertIn(col, str(cm.exception))
            self.assertEqual(doc, before)

    def test_neither_form_is_untouched(self):
        for params in ({}, {"startVal": [0.0, 1.0], "endVal": [1.0, 0.0], "timeMode": "percent",
                            "attack": 0.1}):
            doc = patch(env(params))
            before = copy.deepcopy(doc)
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter())
            self.assertEqual(doc, before)
        doc = patch({"id": "e", "type": "Envelope"})
        self.assertEqual(apply(doc)[0], Counter())


class RefusalTests(unittest.TestCase):
    def refused(self, params, needles, nid="e"):
        doc = patch(env(params, nid), env({"stages": [stage(0, 1, 1)]}, "fine"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            envelope_table.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)                                           # the other node too
        self.assertEqual(notes, [])
        return msg

    def test_stages_not_a_list_of_objects(self):
        for bad in ({"startVal": [0]}, "adsr", 4, None, [stage(0, 1, 1), 5], [[0, 1, 1]], [None]):
            self.refused({"stages": bad}, ["'e'", "stages"])

    def test_a_stage_key_outside_the_eight_and_hold_pct(self):
        msg = self.refused({"stages": [stage(0, 1, 1, holdpct=0.0)]}, ["'e'", "holdpct"])
        self.refused({"stages": [stage(0, 1, 1, curve=1)]}, ["'e'", "curve"])
        self.assertTrue(msg)

    def test_a_non_zero_hold_pct(self):
        for bad in (0.5, 1, -0.1, "0", None, False, True, [0]):
            self.refused({"stages": [stage(0, 1, 1, holdPct=bad)]}, ["'e'", "holdPct"])

    def test_a_non_number_where_a_number_goes(self):
        for key in ("startVal", "endVal", "power", "percent", "minSec", "maxSec", "nominal"):
            for bad in ("0.5", None, True, False, [0.5], {"ref": "x"}):
                s = stage(0, 1, 1)
                s[key] = bad
                self.refused({"stages": [s]}, ["'e'", key])

    def test_a_type_that_is_not_a_string(self):
        for bad in (1, None, True, ["Linear"]):
            self.refused({"stages": [stage(0, 1, 1, bad)]}, ["'e'", "type"])

    def test_a_time_mode_that_is_not_a_string(self):
        for bad in (1, None, True, {"ref": "x"}):
            self.refused({"stages": [stage(0, 1, 1)], "timeMode": bad}, ["'e'", "timeMode"])

    def test_stages_beside_preset(self):
        self.refused({"preset": "adsr", "stages": [stage(0, 1, 1)]}, ["'e'", "preset"])

    def test_stages_beside_a_column_key(self):
        for col in COLUMNS:
            self.refused({"stages": [stage(0, 1, 1)], col: [0.0]}, ["'e'", col])

    def test_a_node_with_no_id_is_named(self):
        self.refused({"stages": "x"}, ["<no id>"], nid=None)

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        doc = patch(env({"stages": [stage(0, 1, 1)], "attack": 0.1}, "first"),
                    env({"preset": "ar", "release": 0.3}, "second"),
                    env({"stages": [stage(0, 1, 1, holdPct=1.0)]}, "bad"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            envelope_table.apply(doc, notes, NoContext())
        self.assertIn("'bad'", str(cm.exception))
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])


class WalkTests(unittest.TestCase):
    def test_stages_in_a_document_with_no_graph(self):
        doc = {"nodes": [env({"stages": [stage(0, 1, 1)]})], "output": "e"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1}))
        self.assertEqual(list(doc["nodes"][0]["params"]), COLUMNS)

    def test_a_document_with_no_node_list_is_untouched(self):
        for doc in ({"sampleRate": 48000}, {"graph": {"output": "e"}}, {"graph": [1, 2]}):
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc)[0], Counter())
            self.assertEqual(doc, before)

    def test_a_multiplex_template_node_is_in_the_node_list(self):
        doc = patch(env({"stages": [stage(0, 1, 1)]}, "tmpl"),
                    {"id": "mux", "type": "MultiplexSource",
                     "params": {"count": 3, "source": {"ref": "tmpl"}}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 1}))
        self.assertEqual(list(params_of(doc)), COLUMNS)

    def test_a_nested_node_list_inside_a_node(self):
        inner = env({"stages": [stage(0, 1, 1)], "attack": 0.2}, "inner")
        doc = patch({"id": "host", "type": "SomeHost", "params": {"template": {"nodes": [inner]}}},
                    env({"stages": [stage(1, 0, 1)]}, "outer"))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 2, "dead key": 1}))
        self.assertEqual(list(inner["params"]), COLUMNS)
        self.assertIn("'inner'", notes[0])

    def test_other_types_are_untouched(self):
        doc = patch({"id": "s", "type": "SineSource", "params": {"stages": "not mine", "attack": 1}})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc)[0], Counter())
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(env({"stages": adsr_stages(0.6) + [stage(0, 0, 0.1, "expo", holdPct=0.0)],
                         "timeMode": "percent", "attack": 0.1, "sustainLevel": 0.2}),
                    env({"preset": "ar", "release": 0.5, "releaseMax": 0.5}, "p"),
                    env({"stages": []}, "empty"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"stage list": 2, "dead key": 3, "label": 2}))
        once = copy.deepcopy(doc)
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter())
        self.assertFalse(counts)
        self.assertEqual(notes, [])
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()
