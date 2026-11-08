"""The deleted-type migration: each rule, the adsr release default, key order,
idempotence, the refusal, check mode, the tree walk and the formant fold."""
import copy
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import migrate_deleted_types as mig  # noqa: E402

PRESET_NAMES = {"AREnvelope": "ar", "ASEnvelope": "as", "ASREnvelope": "asr",
                "ADSEnvelope": "ads", "ADREnvelope": "adr", "ADSREnvelope": "adsr"}


def graph(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": nodes[-1]["id"]}}


def env_node(type_name, params, **extra):
    node = {"id": "env", "type": type_name, "params": params}
    node.update(extra)
    return node


class RewriteTests(unittest.TestCase):
    def test_each_preset_class_becomes_the_form_with_preset_first(self):
        for cls, preset in PRESET_NAMES.items():
            doc = graph(env_node(cls, {"attack": 0.3, "sustainLevel": 0.5, "release": 0.2}))
            self.assertEqual(mig.rewrite(doc), {cls: 1})
            node = doc["graph"]["nodes"][0]
            self.assertEqual(node["type"], "Envelope")
            self.assertEqual(list(node["params"]), ["preset", "attack", "sustainLevel", "release"])
            self.assertEqual(node["params"]["preset"], preset)
            self.assertEqual(list(node), ["id", "type", "params"])             # the node's own key order

    def test_a_bare_adsr_gains_the_class_release_default(self):
        doc = graph(env_node("ADSREnvelope", {"attack": 0.12, "decay": 0.2, "sustainLevel": 0.5}))
        mig.rewrite(doc)
        self.assertEqual(doc["graph"]["nodes"][0]["params"],
                         {"preset": "adsr", "attack": 0.12, "decay": 0.2, "sustainLevel": 0.5, "release": 0.1})

    def test_an_adsr_with_a_release_keeps_it(self):
        doc = graph(env_node("ADSREnvelope", {"attack": 0.12, "release": 0.0}))
        mig.rewrite(doc)
        self.assertEqual(doc["graph"]["nodes"][0]["params"]["release"], 0.0)

    def test_the_other_forms_gain_no_default(self):
        for cls in ("AREnvelope", "ASEnvelope", "ASREnvelope", "ADSEnvelope", "ADREnvelope"):
            doc = graph(env_node(cls, {"attack": 0.3}))
            mig.rewrite(doc)
            self.assertEqual(doc["graph"]["nodes"][0]["params"], {"preset": PRESET_NAMES[cls], "attack": 0.3})

    def test_a_node_without_params_gets_the_preset_alone(self):
        doc = graph({"id": "env", "type": "ASEnvelope"})
        mig.rewrite(doc)
        self.assertEqual(doc["graph"]["nodes"][0], {"id": "env", "type": "Envelope", "params": {"preset": "as"}})

    def test_kspianostring_is_renamed_with_params_untouched(self):
        params = {"source": {"ref": "exc"}, "frequency": {"ref": "f"}, "t60": 6900.0, "bowGain": 0.2}
        doc = graph({"id": "exc", "type": "WhiteNoiseSource", "params": {}},
                    {"id": "string", "type": "KSPianoString", "params": dict(params)})
        self.assertEqual(mig.rewrite(doc), {"KSPianoString": 1})
        node = doc["graph"]["nodes"][1]
        self.assertEqual(node, {"id": "string", "type": "KSString", "params": params})

    def test_live_names_and_the_form_are_untouched_and_a_second_run_is_a_no_op(self):
        doc = graph({"id": "s", "type": "KSString", "params": {"t60": 1.0}},
                    env_node("Envelope", {"preset": "as", "attack": 0.3}),
                    env_node("Envelope", {"stages": [{"percent": 0.1}]}))
        before = copy.deepcopy(doc)
        self.assertEqual(mig.rewrite(doc), {})
        self.assertEqual(doc, before)
        doc2 = graph(env_node("ASEnvelope", {"attack": 0.3}))
        mig.rewrite(doc2)
        once = copy.deepcopy(doc2)
        self.assertEqual(mig.rewrite(doc2), {})
        self.assertEqual(doc2, once)

    def test_nodes_are_found_wherever_the_list_sits(self):
        top = {"nodes": [env_node("ASREnvelope", {"attack": 0.1})], "output": "env"}
        nested = {"instrument": {"template": {"nodes": [{"id": "k", "type": "KSPianoString", "params": {}}]}}}
        self.assertEqual(mig.rewrite(top), {"ASREnvelope": 1})
        self.assertEqual(mig.rewrite(nested), {"KSPianoString": 1})
        self.assertEqual(nested["instrument"]["template"]["nodes"][0]["type"], "KSString")

    def test_a_dynamic_pin_on_any_layout_knob_is_refused_by_name(self):
        self.assertEqual(len(mig.LAYOUT_KNOBS), 11)
        for knob in sorted(mig.LAYOUT_KNOBS):
            doc = graph(env_node("ASEnvelope", {"attack": 0.3}, dynamicPins={knob: {"ref": "c"}}))
            with self.assertRaises(ValueError) as cm:
                mig.rewrite(doc)
            self.assertIn("node 'env' (ASEnvelope): dynamicPins names " + knob, str(cm.exception))
        for knob in ("sustainLevel", "stage_accuracy", "timeScale"):
            ok = graph(env_node("ASEnvelope", {"attack": 0.3}, dynamicPins={knob: {"ref": "c"}}))
            self.assertEqual(mig.rewrite(ok), {"ASEnvelope": 1})
            self.assertEqual(ok["graph"]["nodes"][0]["dynamicPins"], {knob: {"ref": "c"}})

    def test_keys_the_class_never_read_are_dropped_with_a_note(self):
        # An AREnvelope had no attackMin/attackMax; the ar form reads both.
        doc = graph(env_node("AREnvelope", {"attack": 1.0, "attackMin": 4.0, "attackMax": 4.0}))
        notes = []
        self.assertEqual(mig.rewrite(doc, notes), {"AREnvelope": 1})
        self.assertEqual(doc["graph"]["nodes"][0]["params"], {"preset": "ar", "attack": 1.0})
        self.assertEqual(notes, ["node 'env' (AREnvelope): attackMax, attackMin dropped: the class never read them, the ar form would"])
        self.assertTrue(all(ord(c) < 128 for c in notes[0]))                  # the console may not be UTF-8
        # An ADSREnvelope had no clamps and no timeMode; the adsr form reads all seven.
        doc = graph(env_node("ADSREnvelope", {"attack": 0.1, "release": 0.2, "decayMax": 0.9, "timeMode": "seconds"}))
        notes = []
        mig.rewrite(doc, notes)
        self.assertEqual(doc["graph"]["nodes"][0]["params"], {"preset": "adsr", "attack": 0.1, "release": 0.2})
        self.assertEqual(len(notes), 1)
        self.assertIn("decayMax, timeMode dropped", notes[0])
        # The same keys on a class that read them stay (none of the other four had any).
        doc = graph(env_node("ASREnvelope", {"attack": 0.1, "release": 0.2}))
        notes = []
        mig.rewrite(doc, notes)
        self.assertEqual(notes, [])


def peak(pid, frequency, gain=1.0, width=500.0, power=2.0, **over):
    params = {"frequency": frequency, "gain": gain, "width": width, "power": power}
    params.update(over)
    return {"id": pid, "type": "Formant", "params": {k: v for k, v in params.items() if v is not None}}


def vowel(*peaks, param_map=None):
    """An envelope, the peaks, their spectrum, partials and the additive source
    that reads the spectrum: the shape of patches/library/voice/sing_alto_E."""
    nodes = [env_node("Envelope", {"preset": "as", "attack": 0.03})]
    nodes += list(peaks)
    nodes.append({"id": "spec", "type": "FormantSpectrum",
                  "params": {"formants": [{"ref": p["id"]} for p in peaks]}})
    nodes.append({"id": "fp", "type": "FullPartials", "params": {"maxPartials": 40}})
    nodes.append({"id": "src", "type": "AdditiveSource",
                  "params": {"amplitude": {"ref": "env"}, "formant": {"ref": "spec"}, "partials": {"ref": "fp"}}})
    doc = graph(*nodes)
    doc["instrument"] = {"paramMap": param_map or {"frequency": "src.frequency"}, "polyphony": 1}
    return doc


THREE = (lambda: (peak("f1", 400.0, 1.0, 480.0, 2.0), peak("f2", 1600.0, 0.0631, 640.0, 1.5),
                  peak("f3", 2700, 0.0316, 960.0, 2.0)))


class FormantFoldTests(unittest.TestCase):
    def test_a_three_peak_vowel_folds_to_one_formant_peaks(self):
        doc = vowel(*THREE())
        notes = []
        self.assertEqual(mig.rewrite(doc, notes), {"FormantSpectrum": 1})
        self.assertEqual(notes, [])
        nodes = doc["graph"]["nodes"]
        self.assertEqual([n["id"] for n in nodes], ["env", "spec", "fp", "src"])   # the children gone, the place kept
        self.assertEqual(nodes[1], {"id": "spec", "type": "FormantPeaks", "params": {
            "frequency": [400.0, 1600.0, 2700], "gain": [1.0, 0.0631, 0.0316],
            "width": [480.0, 640.0, 960.0], "power": [2.0, 1.5, 2.0]}})
        self.assertEqual(list(nodes[1]["params"]), ["frequency", "gain", "width", "power"])
        self.assertEqual(nodes[3]["params"]["formant"], {"ref": "spec"})         # the reader unchanged
        self.assertEqual(doc["instrument"]["paramMap"], {"frequency": "src.frequency"})

    def test_a_peak_referenced_twice_is_refused(self):
        doc = vowel(*THREE())
        doc["graph"]["nodes"].append({"id": "other", "type": "AdditiveSource", "params": {"formant": {"ref": "f2"}}})
        before = copy.deepcopy(doc)
        with self.assertRaises(ValueError) as cm:
            mig.rewrite(doc)
        self.assertIn("node 'spec' (FormantSpectrum): peak 'f2' is referenced by another node too", str(cm.exception))
        self.assertEqual(doc, before)                                            # nothing changed
        doc = vowel(*THREE())
        doc["graph"]["nodes"].append({"id": "spec2", "type": "FormantSpectrum",
                                      "params": {"formants": [{"ref": "f1"}]}})
        with self.assertRaises(ValueError) as cm:
            mig.rewrite(doc)
        self.assertIn("peak 'f1' is referenced by another node too", str(cm.exception))

    def test_a_peak_with_a_wired_pin_is_refused(self):
        f1, f2, f3 = THREE()
        f2["params"]["gain"] = {"ref": "env"}
        doc = vowel(f1, f2, f3)
        with self.assertRaises(ValueError) as cm:
            mig.rewrite(doc)
        self.assertIn("node 'spec' (FormantSpectrum): peak 'f2' has its gain wired", str(cm.exception))

    def test_other_spectrum_shapes_are_refused_by_name(self):
        doc = vowel(*THREE())
        doc["graph"]["nodes"][4]["params"]["formants"][1] = {"frequency": 1600.0}  # the legacy inline form
        with self.assertRaisesRegex(ValueError, r"node 'spec' \(FormantSpectrum\): a formants entry is not a"):
            mig.rewrite(doc)
        doc = vowel(*THREE())
        doc["graph"]["nodes"][4]["params"]["formants"][1] = {"ref": "fp"}
        with self.assertRaisesRegex(ValueError, "formants names 'fp', a FullPartials, not a Formant"):
            mig.rewrite(doc)
        doc = vowel(*THREE())
        doc["graph"]["nodes"][4]["params"]["formants"][1] = {"ref": "nobody"}
        with self.assertRaisesRegex(ValueError, "formants names 'nobody', which no node has"):
            mig.rewrite(doc)

    def test_a_ragged_peak_takes_the_default_with_a_note(self):
        doc = vowel(peak("f1", 400.0), peak("f2", None, width=None))
        notes = []
        mig.rewrite(doc, notes)
        rows = doc["graph"]["nodes"][1]["params"]
        self.assertEqual(rows, {"frequency": [400.0, 1000.0], "gain": [1.0, 1.0],
                                "width": [500.0, 500.0], "power": [2.0, 2.0]})
        self.assertEqual(notes, [
            "node 'spec' (FormantSpectrum): peak 'f2' has no frequency; its row takes the default (1000.0)",
            "node 'spec' (FormantSpectrum): peak 'f2' has no width; its row takes the default (500.0)"])

    def test_a_parammap_curve_from_the_constant_folds_to_it(self):
        curve = [[800.0, 800.0], [1100.0, 1100.0]]
        doc = vowel(peak("f1", 800.0), peak("f2", 1150.0),
                    param_map={"frequency": ["src.frequency", {"target": "f1.frequency", "curve": curve}]})
        notes = []
        self.assertEqual(mig.rewrite(doc, notes), {"FormantSpectrum": 1})
        self.assertEqual(doc["instrument"], {"paramMap": {"frequency": "src.frequency"}, "polyphony": 1})
        self.assertEqual(doc["graph"]["nodes"][1]["params"]["frequency"], [800.0, 1150.0])
        self.assertEqual(notes, ["node 'spec' (FormantSpectrum): paramMap target 'f1.frequency' folded to a "
                                 "constant (800.0); the curve that drove it is gone"])
        # The only entry of its key: the key goes, and an empty paramMap goes.
        doc = vowel(peak("f1", 800.0), param_map={"velocity": {"target": "f1.gain", "curve": [[0, 1.0], [1, 0.5]]}})
        mig.rewrite(doc)
        self.assertEqual(doc["instrument"], {"polyphony": 1})

    def test_a_parammap_aim_the_fold_cannot_take_is_refused(self):
        shapes = [
            ({"target": "f1.frequency", "curve": [[800.0, 850.0], [1100.0, 1100.0]]}, "drives 'f1.frequency' from 850.0"),
            ({"target": "f1.frequency", "vcurve": [[0, 800.0]]}, "in a form the fold does not take"),
            ({"target": "f1.frequency", "curve": [[800.0, 800.0]], "vcurve": [[0, 1]]}, "in a form the fold does not take"),
            ("f1.frequency", "in a form the fold does not take"),
            ({"target": "f1.bandwidth", "curve": [[0, 1.0]]}, "'f1.bandwidth', not one of the four pins"),
        ]
        for entry, why in shapes:
            doc = vowel(peak("f1", 800.0), param_map={"frequency": ["src.frequency", entry]})
            before = copy.deepcopy(doc)
            with self.assertRaises(ValueError) as cm:
                mig.rewrite(doc)
            self.assertIn("node 'spec' (FormantSpectrum): paramMap 'frequency'", str(cm.exception))
            self.assertIn(why, str(cm.exception))
            self.assertEqual(doc, before)

    def test_a_formant_sequence_follows_its_spectra(self):
        doc = vowel(*THREE())
        nodes = doc["graph"]["nodes"]
        nodes.insert(5, {"id": "seq", "type": "FormantSequence",
                         "params": {"blend": {"ref": "env"}, "spectra": [{"ref": "spec"}, {"ref": "spec"}]}})
        nodes[-1]["params"]["formant"] = {"ref": "seq"}
        self.assertEqual(mig.rewrite(doc), {"FormantSpectrum": 1})
        self.assertEqual([n["type"] for n in doc["graph"]["nodes"]],
                         ["Envelope", "FormantPeaks", "FormantSequence", "FullPartials", "AdditiveSource"])

    def test_the_editor_positions_of_the_peaks_go(self):
        doc = vowel(*THREE())
        doc["ui"] = {"positions": {"f1": [0, 0], "spec": [1, 1], "src": [2, 2]}}
        mig.rewrite(doc)
        self.assertEqual(doc["ui"], {"positions": {"spec": [1, 1], "src": [2, 2]}})

    def test_bandspectrum_is_renamed_with_params_untouched(self):
        params = {"startFreq": 100, "freqIncrement": 50, "gains": [0.0, 0.5, 1.0]}
        doc = graph({"id": "band", "type": "BandSpectrum", "params": dict(params)})
        self.assertEqual(mig.rewrite(doc), {"BandSpectrum": 1})
        self.assertEqual(doc["graph"]["nodes"][0], {"id": "band", "type": "FormantBands", "params": params})

    def test_fixedspectrum_and_a_stray_formant_are_refused(self):
        doc = graph({"id": "fix", "type": "FixedSpectrum", "params": {}})
        with self.assertRaisesRegex(ValueError, r"node 'fix' \(FixedSpectrum\): deleted; author a FormantBands or FormantPeaks"):
            mig.rewrite(doc)
        doc = graph(peak("lone", 800.0), {"id": "src", "type": "AdditiveSource", "params": {"formant": {"ref": "lone"}}})
        with self.assertRaisesRegex(ValueError, r"node 'lone' \(Formant\): deleted; a peak outside a FormantSpectrum: "
                                                r"author a one-row FormantPeaks"):
            mig.rewrite(doc)

    def test_a_second_run_changes_nothing(self):
        doc = vowel(peak("f1", 800.0), peak("f2", 1150.0),
                    param_map={"frequency": ["src.frequency", {"target": "f1.frequency", "curve": [[800.0, 800.0]]}]})
        mig.rewrite(doc)
        once = copy.deepcopy(doc)
        notes = []
        self.assertEqual(mig.rewrite(doc, notes), {})
        self.assertEqual(doc, once)
        self.assertEqual(notes, [])


class FileTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.dir.cleanup()

    def write(self, name, doc, indent=2):
        path = os.path.join(self.dir.name, name)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(doc, indent=indent) + "\n")
        return path

    def test_a_file_with_nothing_to_rewrite_is_byte_identical(self):
        path = self.write("live.json", graph({"id": "s", "type": "KSString", "params": {"t60": 1.0}}), indent=4)
        before = open(path, "rb").read()
        self.assertEqual(mig.rewrite_file(path), {})
        self.assertEqual(open(path, "rb").read(), before)

    def test_a_rewritten_file_keeps_key_order_and_float_text(self):
        path = self.write("old.json", graph(env_node("ASEnvelope", {"attack": 0.699999988079071, "sustainLevel": 1.0}),
                                            {"id": "osc", "type": "SineSource", "params": {"amplitude": {"ref": "env"}}}))
        self.assertEqual(mig.rewrite_file(path), {"ASEnvelope": 1})
        text = open(path, encoding="utf-8").read()
        self.assertIn('"preset": "as",\n          "attack": 0.699999988079071,\n          "sustainLevel": 1.0', text)
        self.assertTrue(text.endswith("}\n"))
        self.assertEqual(mig.rewrite_file(path), {})                            # a second run changes nothing

    def test_a_rewritten_file_keeps_its_own_indent_step(self):
        for indent in (1, 4):
            path = self.write("old%d.json" % indent, graph({"id": "k", "type": "KSPianoString", "params": {}}), indent=indent)
            self.assertEqual(mig.rewrite_file(path), {"KSPianoString": 1})
            text = open(path, encoding="utf-8").read()
            self.assertIn("\n" + " " * indent + '"graph": {\n', text)
            self.assertEqual(mig.indent_of(text), indent)
        self.assertEqual(mig.indent_of('{"a": 1}'), 2)                          # no indented line: the default

    def test_check_mode_reports_and_writes_nothing(self):
        path = self.write("old.json", graph({"id": "k", "type": "KSPianoString", "params": {}}))
        before = open(path, "rb").read()
        self.assertEqual(mig.rewrite_file(path, check=True), {"KSPianoString": 1})
        self.assertEqual(open(path, "rb").read(), before)
        self.assertEqual(mig.main(["--check", path]), 1)
        self.assertEqual(mig.main([path]), 0)
        self.assertEqual(mig.main(["--check", path]), 0)

    def test_a_refused_type_is_reported_by_name_and_left_unwritten(self):
        for cls in ("AdditiveSource2", "BasicAdditiveSource"):
            doc = graph({"id": "as2", "type": cls, "params": {"frequency": 165.0}})
            with self.assertRaises(ValueError) as cm:
                mig.rewrite(doc)
            self.assertIn("node 'as2' (%s): deleted; author an AdditiveSource" % cls, str(cm.exception))
            self.assertIn("no mechanical rewrite", str(cm.exception))
        path = self.write("as2.json", graph({"id": "as2", "type": "AdditiveSource2", "params": {}},
                                            {"id": "k", "type": "KSPianoString", "params": {}}))
        before = open(path, "rb").read()
        self.assertEqual(mig.main([path]), 1)
        self.assertEqual(open(path, "rb").read(), before)                       # not even the rewritable node

    def test_check_mode_before_and_after_the_fold(self):
        path = self.write("vowel.json", vowel(*THREE()), indent=1)
        before = open(path, "rb").read()
        self.assertEqual(mig.main(["--check", path]), 1)
        self.assertEqual(open(path, "rb").read(), before)
        self.assertEqual(mig.main([path]), 0)
        text = open(path, encoding="utf-8").read()
        self.assertIn('\n     "frequency": [\n      400.0,\n      1600.0,\n      2700\n     ],', text)   # one value per line
        self.assertEqual(mig.main(["--check", path]), 0)
        after = open(path, "rb").read()
        self.assertEqual(mig.main([path]), 0)
        self.assertEqual(open(path, "rb").read(), after)                           # idempotent on disk

    def test_check_mode_exits_1_on_a_refused_type(self):
        path = self.write("as2.json", graph({"id": "as2", "type": "AdditiveSource2", "params": {}}))
        before = open(path, "rb").read()
        self.assertEqual(mig.main(["--check", path]), 1)
        self.assertEqual(open(path, "rb").read(), before)

    def test_a_tree_walk_counts_per_rule_and_a_bad_file_is_reported_not_fatal(self):
        self.write("a.json", graph(env_node("ASREnvelope", {"attack": 0.1}),
                                   {"id": "k", "type": "KSPianoString", "params": {}}))
        os.makedirs(os.path.join(self.dir.name, "sub"))
        self.write(os.path.join("sub", "b.json"), graph(env_node("ADSREnvelope", {"attack": 0.1})))
        with open(os.path.join(self.dir.name, "broken.json"), "w", encoding="utf-8") as f:
            f.write("{not json")
        self.assertEqual(mig.main([self.dir.name]), 1)                           # the broken file is a failure
        for name in ("a.json", os.path.join("sub", "b.json")):
            doc = json.load(open(os.path.join(self.dir.name, name), encoding="utf-8"))
            self.assertNotIn("Envelope", [n["type"] for n in doc["graph"]["nodes"] if n["type"] != "Envelope"])
        self.assertEqual(mig.main(["--check", os.path.join(self.dir.name, "a.json"),
                                   os.path.join(self.dir.name, "sub", "b.json")]), 0)


if __name__ == "__main__":
    unittest.main()
