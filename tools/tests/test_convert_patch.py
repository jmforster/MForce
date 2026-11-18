"""The driver: --check's exit codes, the rule order on a document that needs
two rules, the not-written rule for an unchanged and a refused file, the
file's indent step and line endings, idempotence on disk, the folder walk and
its sorted order, a paramMap curve leaving as columns, a non-ASCII character
and a byte-order mark kept, a node with no id, a failed descriptor dump, a
paramMap entry on a VarSource's val reaching the old-defaults rule wired,
one document holding the five old forms of the five rules before the
spelling rules, and one that needs the spelling rules after the others."""
import codecs
import contextlib
import io
import json
import os
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import convert_patch as drv  # noqa: E402
import convert_rules  # noqa: E402
from convert_rules import Context  # noqa: E402

DESC = {
    "KSString": {"params": ["frequency", "amplitude"], "inputs": ["source"], "settings": [{"name": "t60"}]},
    "SineSource": {"params": ["amplitude", "frequency", "phase"], "inputs": [], "settings": []},
    "WhiteNoiseSource": {"params": ["amplitude"], "inputs": [], "settings": []},
    "VarSource": {"params": ["val", "var", "varPct"], "inputs": [], "settings": [{"name": "absolute"}]},
}


def ks_piano(param_map=None):
    """A retired type with a bare paramMap entry on it: the paramMap rule finds
    the type in the registry only because the deleted-type rule renamed it."""
    return {"sampleRate": 48000,
            "instrument": {"polyphony": 1, "paramMap": param_map or {"frequency": "string.frequency"}},
            "graph": {"nodes": [{"id": "exc", "type": "WhiteNoiseSource", "params": {}},
                                {"id": "string", "type": "KSPianoString",
                                 "params": {"source": {"ref": "exc"}, "frequency": 110.0}}],
                      "output": "string"}}


def plain():
    return {"sampleRate": 48000, "graph": {"nodes": [{"id": "osc", "type": "SineSource",
                                                      "params": {"frequency": 440.0}}], "output": "osc"}}


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.ctx = Context(descriptors=DESC)

    def tearDown(self):
        self.dir.cleanup()

    def write(self, name, doc, indent=2, newline="\n", final=True):
        path = os.path.join(self.dir.name, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        text = json.dumps(doc, indent=indent).replace("\n", newline) + (newline if final else "")
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        return path

    def run_main(self, argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = drv.main(argv, self.ctx)
        return rc, out.getvalue()

    def test_check_exits_1_with_a_rewrite_pending_and_writes_nothing(self):
        path = self.write("p.json", ks_piano())
        before = open(path, "rb").read()
        rc, out = self.run_main(["--check", path])
        self.assertEqual(rc, 1)
        self.assertEqual(open(path, "rb").read(), before)
        self.assertIn("1 file(s) pending", out)
        rc, _ = self.run_main([path])
        self.assertEqual(rc, 0)
        rc, out = self.run_main(["--check", path])
        self.assertEqual(rc, 0)
        self.assertIn("0 file(s) pending", out)

    def test_no_arguments_prints_the_docstring_and_exits_2(self):
        rc, out = self.run_main([])
        self.assertEqual(rc, 2)
        self.assertIn("convert_patch.py", out)

    def test_two_rules_in_one_pass_and_one_write(self):
        path = self.write("p.json", ks_piano())
        changed = drv.convert_file(path, ctx=self.ctx)
        self.assertEqual(list(changed), ["deleted_types", "parammap"])          # the rule order
        self.assertEqual(changed["deleted_types"], {"KSPianoString": 1})
        self.assertEqual(changed["parammap"]["paramMap entry"], 1)
        doc = json.load(open(path, encoding="utf-8"))
        self.assertNotIn("paramMap", doc["instrument"])
        nodes = {n["id"]: n for n in doc["graph"]["nodes"]}
        self.assertEqual(nodes["string"]["type"], "KSString")
        self.assertEqual(nodes["string"]["params"]["frequency"], {"ref": "__perf_freq"})
        self.assertEqual(doc["graph"]["nodes"][0]["id"], "__perf_freq")
        self.assertEqual(drv.convert_file(path, ctx=self.ctx), {})              # a second run: nothing

    def test_a_file_no_rule_changes_is_not_written(self):
        path = self.write("live.json", plain(), indent=4)
        before = open(path, "rb").read()
        old = time.time() - 1000
        os.utime(path, (old, old))
        mtime = os.stat(path).st_mtime_ns
        rc, out = self.run_main([path])
        self.assertEqual(rc, 0)
        self.assertEqual(open(path, "rb").read(), before)
        self.assertEqual(os.stat(path).st_mtime_ns, mtime)
        self.assertNotIn("live.json:", out)

    def test_a_refused_file_is_not_written_even_after_an_earlier_rule_changed_it(self):
        # The deleted-type rule renames the KSPianoString; the paramMap rule then
        # refuses the entry naming no node. Nothing reaches the disk.
        path = self.write("p.json", ks_piano({"frequency": ["string.frequency", "nobody.frequency"]}))
        before = open(path, "rb").read()
        rc, out = self.run_main([path])
        self.assertEqual(rc, 1)
        self.assertEqual(open(path, "rb").read(), before)
        self.assertIn("NOT rewritten", out)
        self.assertIn("no node 'nobody'", out)
        rc, _ = self.run_main(["--check", path])
        self.assertEqual(rc, 1)

    def test_a_json_file_that_is_not_an_object_is_left_alone(self):
        # A folder's manifest is a list: not a patch, so not a refusal either.
        path = self.write("_manifest.json", [{"file": "a.json"}, {"file": "b.json"}])
        before = open(path, "rb").read()
        good = self.write("b.json", ks_piano())
        rc, out = self.run_main([self.dir.name])
        self.assertEqual(rc, 0)
        self.assertEqual(open(path, "rb").read(), before)
        self.assertIn("1 file(s) rewritten, 0 refused", out)
        self.assertNotIn("paramMap", json.load(open(good, encoding="utf-8"))["instrument"])
        self.assertEqual(self.run_main(["--check", self.dir.name])[0], 0)

    def test_a_refusal_does_not_stop_the_run(self):
        bad = self.write("a.json", ks_piano({"frequency": "nobody.frequency"}))
        good = self.write("b.json", ks_piano())
        rc, out = self.run_main([bad, good])
        self.assertEqual(rc, 1)
        self.assertNotIn("paramMap", json.load(open(good, encoding="utf-8"))["instrument"])
        self.assertIn("1 refused", out)

    def test_the_indent_step_is_kept(self):
        for indent in (1, 2):
            path = self.write("i%d.json" % indent, ks_piano(), indent=indent)
            drv.convert_file(path, ctx=self.ctx)
            text = open(path, encoding="utf-8").read()
            self.assertIn("\n" + " " * indent + '"graph": {\n', text)
            self.assertEqual(drv.indent_of(text), indent)

    def test_a_crlf_file_without_a_final_newline_comes_out_lf_with_one(self):
        path = self.write("crlf.json", ks_piano(), newline="\r\n", final=False)
        raw = open(path, "rb").read()
        self.assertIn(b"\r\n", raw)
        self.assertFalse(raw.endswith(b"\n"))
        drv.convert_file(path, ctx=self.ctx)
        data = open(path, "rb").read()
        self.assertNotIn(b"\r", data)
        self.assertTrue(data.endswith(b"}\n"))
        self.assertFalse(data.endswith(b"\n\n"))

    def test_a_second_run_writes_nothing(self):
        path = self.write("p.json", ks_piano(), indent=1)
        self.assertEqual(self.run_main([path])[0], 0)
        after = open(path, "rb").read()
        old = time.time() - 1000
        os.utime(path, (old, old))
        mtime = os.stat(path).st_mtime_ns
        self.assertEqual(self.run_main([path])[0], 0)
        self.assertEqual(open(path, "rb").read(), after)
        self.assertEqual(os.stat(path).st_mtime_ns, mtime)

    def test_a_folder_is_walked_recursively(self):
        a = self.write("a.json", ks_piano())
        b = self.write(os.path.join("sub", "deeper", "b.json"), ks_piano())
        c = self.write(os.path.join("sub", "c.json"), plain())
        with open(os.path.join(self.dir.name, "sub", "notes.txt"), "w") as f:
            f.write("not json")
        rc, out = self.run_main([self.dir.name])
        self.assertEqual(rc, 0)
        for p in (a, b):
            self.assertNotIn("paramMap", json.load(open(p, encoding="utf-8"))["instrument"])
        self.assertIn("2 file(s) rewritten", out)
        self.assertEqual(self.run_main(["--check", self.dir.name])[0], 0)
        self.assertEqual(json.load(open(c, encoding="utf-8")), plain())

    def test_sub_folders_are_visited_in_sorted_order(self):
        # The walk below hands the folders back reversed, as a file system
        # that keeps no order may; the driver's own sort must restore it.
        self.write(os.path.join("b", "p.json"), ks_piano())
        self.write(os.path.join("a", "p.json"), ks_piano())
        real_walk = os.walk

        def reversed_walk(top, *args, **kwargs):
            for dirpath, dirs, names in real_walk(top, *args, **kwargs):
                dirs.reverse()
                yield dirpath, dirs, names

        with mock.patch.object(drv.os, "walk", reversed_walk):
            rc, out = self.run_main([self.dir.name])
        self.assertEqual(rc, 0)
        visited = [os.path.basename(os.path.dirname(line.split(": ", 1)[0]))
                   for line in out.splitlines() if line.split(": ", 1)[0].endswith("p.json")]
        self.assertEqual(visited, ["a", "b"])

    def test_a_parammap_curve_leaves_the_driver_as_columns(self):
        # The paramMap rule writes the curve as a CurveNode with a knot list;
        # the curve-table rule, after it, rewrites that list as x and y.
        doc = {"sampleRate": 48000,
               "instrument": {"polyphony": 1,
                              "paramMap": {"frequency": {"target": "osc.frequency",
                                                         "curve": [[100, 200], [1000, 2000]]}}},
               "graph": {"nodes": [{"id": "osc", "type": "SineSource", "params": {"amplitude": 0.5}}],
                         "output": "osc"}}
        path = self.write("p.json", doc)
        changed = drv.convert_file(path, ctx=self.ctx)
        self.assertEqual(list(changed), ["parammap", "curve_table"])
        self.assertEqual(changed["curve_table"], {"knots": 1})
        nodes = {n["id"]: n for n in json.load(open(path, encoding="utf-8"))["graph"]["nodes"]}
        self.assertEqual(nodes["__curve_1"]["params"], {"interp": "logx", "x": [100.0, 1000.0],
                                                        "y": [200.0, 2000.0],
                                                        "source": {"ref": "__perf_freq"}})
        self.assertEqual(drv.convert_file(path, ctx=self.ctx), {})

    def raw_file(self, name, data):
        path = os.path.join(self.dir.name, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def test_a_non_ascii_character_survives_a_rewrite_byte_for_byte(self):
        doc = ks_piano()
        doc["comment"] = "wiring \u2014 caf\u00e9"
        path = self.raw_file("u.json", json.dumps(doc, indent=2, ensure_ascii=False).encode("utf-8") + b"\n")
        self.assertEqual(self.run_main([path])[0], 0)
        data = open(path, "rb").read()
        self.assertIn('"comment": "wiring \u2014 caf\u00e9"'.encode("utf-8"), data)
        self.assertNotIn(b"\\u", data)
        self.assertFalse(data.startswith(codecs.BOM_UTF8))                      # none added
        self.assertNotIn("paramMap", json.loads(data.decode("utf-8"))["instrument"])

    def test_a_byte_order_mark_is_read_and_kept(self):
        path = self.raw_file("bom.json", codecs.BOM_UTF8 + json.dumps(ks_piano(), indent=2).encode("utf-8"))
        rc, out = self.run_main([path])
        self.assertEqual(rc, 0)
        self.assertIn("1 file(s) rewritten, 0 refused", out)
        data = open(path, "rb").read()
        self.assertTrue(data.startswith(codecs.BOM_UTF8))
        self.assertFalse(data[len(codecs.BOM_UTF8):].startswith(codecs.BOM_UTF8))
        self.assertNotIn("paramMap", json.loads(data.decode("utf-8-sig"))["instrument"])
        self.assertEqual(self.run_main(["--check", path])[0], 0)

    def test_a_node_with_no_id_is_named_in_a_refusal(self):
        bad = self.write("a.json", {"graph": {"nodes": [{"type": "Envelope", "params": {"stages": "x"}}],
                                              "output": "e"}})
        no_id = ks_piano()
        no_id["graph"]["nodes"].append({"type": "SineSource", "params": {}})   # beside a paramMap
        good = self.write("b.json", no_id)
        rc, out = self.run_main([bad, good])
        self.assertEqual(rc, 1)
        self.assertIn("a.json: NOT rewritten: node <no id> (Envelope)", out)
        self.assertIn("1 file(s) rewritten, 1 refused", out)
        self.assertNotIn("paramMap", json.load(open(good, encoding="utf-8"))["instrument"])

    def test_a_failed_descriptor_dump_is_a_refusal(self):
        needs = self.write("a.json", ks_piano())                                 # the paramMap rule asks
        # No node holds a value an enum could be, so the enum-label rule does
        # not ask either.
        free = self.write("b.json", {"graph": {"nodes": [{"id": "c", "type": "SineSource",
                                                          "params": {"phase": {"ref": "c"}}}],
                                               "output": "c"},
                                     "ui": {"positions": {"c": [1.0, 2.0]}}})
        before = open(needs, "rb").read()
        failure = RuntimeError("mforce_cli --dump-descriptors failed: no exe")
        with mock.patch.object(convert_rules, "descriptors", side_effect=failure):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = drv.main([needs, free], Context())
        out = out.getvalue()
        self.assertEqual(rc, 1)
        self.assertIn("a.json: NOT rewritten: mforce_cli --dump-descriptors failed: no exe", out)
        self.assertIn("1 file(s) rewritten, 1 refused", out)
        self.assertEqual(open(needs, "rb").read(), before)
        self.assertEqual(json.load(open(free, encoding="utf-8"))["layout"], {"positions": {"c": [1.0, 2.0]}})

    def test_a_parammap_entry_on_a_var_sources_val_leaves_it_wired(self):
        # The paramMap rule wires val; the old-defaults rule, after it, then
        # finds val present and writes no 0.0.
        doc = {"sampleRate": 48000,
               "instrument": {"polyphony": 1, "paramMap": {"frequency": "v.val"}},
               "graph": {"nodes": [{"id": "v", "type": "VarSource", "params": {}}], "output": "v"}}
        path = self.write("p.json", doc)
        changed = drv.convert_file(path, ctx=self.ctx)
        self.assertEqual(list(changed), ["parammap"])
        nodes = {n["id"]: n for n in json.load(open(path, encoding="utf-8"))["graph"]["nodes"]}
        self.assertEqual(nodes["v"]["params"], {"val": {"ref": "__perf_freq"}})
        self.assertEqual(drv.convert_file(path, ctx=self.ctx), {})

    def test_the_five_old_forms_in_one_pass(self):
        doc = {"sampleRate": 48000,
               "graph": {"nodes": [
                   {"id": "v", "type": "VarSource", "params": {"var": 0.1}},
                   {"id": "lp", "type": "BWLowpassFilter", "params": {"cutoff": 900.0, "source": {"ref": "v"}}},
                   {"id": "rep", "type": "RepeatingSource", "params": {"gap": 0.3, "source": {"ref": "lp"}}},
                   {"id": "mix", "type": "MultiSource",
                    "params": {"source": [{"ref": "lp", "weight": 0.5}, {"ref": "rep"}]}},
                   {"id": "ph", "type": "PhasedValueSource",
                    "params": {"stages": [{"source": {"ref": "mix"}, "percent": 0.5, "gain": 2.0}],
                               "overlap": 0.1}},
                   {"id": "c", "type": "CombinedSource",
                    "params": {"source1": {"ref": "ph"}, "source2": 1.0, "operation": "Add"}},
                   {"id": "fp", "type": "FullPartials", "params": {"expandRule": {"count": 3}}}],
                   "output": "c"}}
        path = self.write("p.json", doc)
        rc, out = self.run_main([path])
        self.assertEqual(rc, 0)
        self.assertIn("1 file(s) rewritten, 0 refused", out)
        got = json.load(open(path, encoding="utf-8"))
        nodes = got["graph"]["nodes"]
        self.assertEqual([n["id"] for n in nodes], ["v", "lp", "rep", "mix", "ph", "c", "fp_expand", "fp"])
        by_id = {n["id"]: n["params"] for n in nodes}
        self.assertEqual(by_id["v"], {"val": 0.0, "var": 0.1})
        self.assertEqual(by_id["lp"], {"cutoffFreq": 900.0, "source": {"ref": "v"}})
        self.assertEqual(by_id["rep"], {"gapDuration": 0.3, "source": {"ref": "lp"}})
        self.assertEqual(by_id["mix"], {"source": [{"ref": "lp"}, {"ref": "rep"}], "weights": [0.5, 1.0]})
        self.assertEqual(by_id["ph"], {"sources": [{"ref": "mix"}], "percent": [0.5], "minSec": [0.0],
                                       "maxSec": [0.0], "gain": [2.0], "overlap": 0.1})
        self.assertEqual(by_id["c"]["operation"], "Mix")
        self.assertEqual(by_id["fp"], {"expandRule": {"ref": "fp_expand"}})
        self.assertEqual(by_id["fp_expand"]["count"], 3)
        changed = drv.convert_file(path, check=True, ctx=self.ctx)
        self.assertEqual(changed, {})
        after = open(path, "rb").read()
        old = time.time() - 1000
        os.utime(path, (old, old))
        mtime = os.stat(path).st_mtime_ns
        rc, out = self.run_main([path])
        self.assertEqual(rc, 0)
        self.assertIn("0 file(s) rewritten, 0 refused", out)
        self.assertEqual(open(path, "rb").read(), after)
        self.assertEqual(os.stat(path).st_mtime_ns, mtime)

    def test_the_rules_run_in_their_order(self):
        names = [convert_rules.rule_name(r) for r in convert_rules.RULES]
        self.assertEqual(names, ["deleted_types", "string_evolution", "parammap", "envelope_table",
                                 "curve_table", "key_renames", "multi_weights", "phased_table",
                                 "old_defaults", "expand_rule_node", "terminal_inputs", "enum_labels",
                                 "dead_keys", "comment", "layout"])

    def test_the_spelling_rules_read_what_the_earlier_rules_wrote(self):
        # The paramMap rule writes "multiply" and a node __perf_freq; the
        # old-defaults rule writes "Mix" for "add", which the enum-label rule
        # would refuse; the expand-rule rule adds fp_expand. The enum-label
        # rule respells the one and accepts the other, and the layout rule
        # keeps the positions of the two new nodes.
        self.ctx = Context(descriptors=dict(DESC, CombinedSource={
            "params": [], "inputs": ["source1", "source2"], "arrays": [],
            "settings": [{"name": "operation", "isEnum": True, "labels": ["Mix", "Multiply", "Fade", "Sum"]}]}))
        doc = {"sampleRate": 48000,
               "instrument": {"polyphony": 1, "release": 0.4,
                              "paramMap": {"frequency": {"target": "osc.frequency", "vcurve": [[0, 0], [1, 1]]}}},
               "graph": {"nodes": [
                   {"id": "osc", "type": "SineSource", "params": {"amplitude": 0.5}},
                   {"id": "c", "type": "CombinedSource",
                    "params": {"source1": {"ref": "osc"}, "source2": 1.0, "operation": "add"}},
                   {"id": "fp", "type": "FullPartials", "params": {"expandRule": {"count": 3}}}],
                   "output": "c"},
               "ui": {"positions": {"osc": [0, 0], "__perf_freq": [1, 1], "fp_expand": [2, 2],
                                    "__output": [3, 3]}},
               "_comment": "order"}
        path = self.write("p.json", doc)
        rc, out = self.run_main([path])
        self.assertEqual(rc, 0, out)
        self.assertNotIn("dropped:", out)
        got = json.load(open(path, encoding="utf-8"))
        self.assertEqual(list(got), ["sampleRate", "instrument", "graph", "layout", "comment"])
        self.assertEqual(got["instrument"], {"polyphony": 1})
        self.assertEqual(got["comment"], "order")
        nodes = {n["id"]: n for n in got["graph"]["nodes"]}
        self.assertEqual(nodes["c"]["params"]["operation"], "Mix")
        muls = [n for n in nodes.values() if n["id"].startswith("__mul_")]
        self.assertEqual([n["params"]["operation"] for n in muls], ["Multiply"])
        self.assertEqual(got["layout"], {"positions": {"osc": [0, 0], "__perf_freq": [1, 1], "fp_expand": [2, 2]},
                                         "output": [3, 3]})
        self.assertEqual(self.run_main(["--check", path])[0], 0)


if __name__ == "__main__":
    unittest.main()
