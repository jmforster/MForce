"""The driver: --check's exit codes, the rule order on a document that needs
two rules, the not-written rule for an unchanged and a refused file, the
file's indent step and line endings, idempotence on disk, the folder walk."""
import contextlib
import io
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import convert_patch as drv  # noqa: E402
from convert_rules import Context  # noqa: E402

DESC = {
    "KSString": {"params": ["frequency", "amplitude"], "settings": [{"name": "t60"}]},
    "SineSource": {"params": ["amplitude", "frequency", "phase"], "settings": []},
    "WhiteNoiseSource": {"params": ["amplitude"], "settings": []},
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


if __name__ == "__main__":
    unittest.main()
