"""The deleted-type migration: each rule, the adsr release default, key order,
idempotence, the refusal, check mode and the tree walk."""
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

    def test_a_dynamic_pin_on_a_layout_knob_is_refused_by_name(self):
        doc = graph(env_node("ASEnvelope", {"attack": 0.3}, dynamicPins={"attack": {"ref": "c"}}))
        with self.assertRaises(ValueError) as cm:
            mig.rewrite(doc)
        self.assertIn("node 'env' (ASEnvelope): dynamicPins names attack", str(cm.exception))
        ok = graph(env_node("ASEnvelope", {"attack": 0.3}, dynamicPins={"sustainLevel": {"ref": "c"}}))
        self.assertEqual(mig.rewrite(ok), {"ASEnvelope": 1})
        self.assertEqual(ok["graph"]["nodes"][0]["dynamicPins"], {"sustainLevel": {"ref": "c"}})


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
