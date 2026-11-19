"""The built mforce_cli: --check's verdicts and exit code, and the descriptor
dump's enum labels in index order.

The tool is the file the environment variable MFORCE_CLI names (ctest sets it
to the build's own binary), else build/tools/mforce_cli/Release/mforce_cli.exe
under the repository root. With no such file every case fails: a test of the
tool that cannot find the tool has tested nothing.
"""
import json
import os
import subprocess
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.environ.get("MFORCE_CLI") or os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

# A plain sound the codec reads and the builder builds.
PLAIN = {"seconds": 0.1, "graph": {"output": "o", "nodes": [
    {"id": "o", "type": "SineSource", "params": {"frequency": 220}}]}}


def with_unknown_key():
    doc = json.loads(json.dumps(PLAIN))
    doc["graph"]["nodes"][0]["label"] = "L"
    return doc


def without_output():
    """Read whole by the codec (an empty output is a record check() keeps);
    the builder alone refuses it: the patch has no output."""
    doc = json.loads(json.dumps(PLAIN))
    del doc["graph"]["output"]
    return doc


def instrument():
    """An instrument around a MultiplexSource of three saws under one
    Envelope, with a score of one note."""
    return {"sampleRate": 48000, "instrument": {"polyphony": 1, "volume": 0.3},
            "score": [{"time": 0.0, "note": 60, "duration": 0.2, "velocity": 0.8}],
            "graph": {"output": "mux", "nodes": [
                {"id": "pf", "type": "PerformNode", "params": {"field": "frequency"}},
                {"id": "env", "type": "Envelope", "params": {
                    "preset": "adsr", "attack": 0.02, "decay": 0.1, "release": 0.1, "sustainLevel": 0.7}},
                {"id": "R", "type": "SawSource", "params": {"amplitude": {"ref": "env"}, "frequency": {"ref": "pf"}}},
                {"id": "mux", "type": "MultiplexSource", "params": {"count": 3, "source": {"ref": "R"}}}]}}


def with_dynamic_pin_in_template():
    """Read whole by the codec; the builder alone refuses it: the Envelope
    sits inside the Multiplex's template and holds a dynamic pin."""
    doc = instrument()
    nodes = doc["graph"]["nodes"]
    nodes.append({"id": "c", "type": "SineSource", "params": {"amplitude": 0.0, "frequency": 1.0}})
    next(n for n in nodes if n["id"] == "env")["dynamicPins"] = {"timeScale": {"ref": "c"}}
    return doc


class CliTests(unittest.TestCase):
    def setUp(self):
        if not os.path.isfile(CLI):
            self.fail("no mforce_cli at %s: build it, or set MFORCE_CLI to the binary" % CLI)

    def run_cli(self, *args):
        r = subprocess.run([CLI, *args], capture_output=True, text=True, cwd=ROOT)
        return r.returncode, r.stdout

    def write(self, folder, name, doc):
        path = os.path.join(folder, name)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(doc, f)
        return path

    def test_check_reads_and_builds_a_plain_sound(self):
        with tempfile.TemporaryDirectory() as d:
            code, out = self.run_cli("--check", self.write(d, "plain.json", PLAIN))
        self.assertEqual(code, 0, out)
        self.assertEqual(out.splitlines()[-1], "1 files: 0 refused")
        self.assertNotIn("REFUSED", out)

    def test_check_refuses_what_the_reader_refuses(self):
        with tempfile.TemporaryDirectory() as d:
            path = self.write(d, "unknown_key.json", with_unknown_key())
            code, out = self.run_cli("--check", path)
        self.assertEqual(code, 1, out)
        refused = [l for l in out.splitlines() if l.startswith("REFUSED ")]
        self.assertEqual(len(refused), 1, out)
        self.assertTrue(refused[0].endswith("unknown_key.json"), refused[0])
        self.assertIn("unknown key 'label'", out)

    def test_check_refuses_what_the_builder_alone_refuses(self):
        with tempfile.TemporaryDirectory() as d:
            path = self.write(d, "no_output.json", without_output())
            code, out = self.run_cli("--check", path)
        self.assertEqual(code, 1, out)
        self.assertIn("REFUSED ", out)
        self.assertIn("the patch has no output", out)
        self.assertEqual(out.splitlines()[-1], "1 files: 1 refused")

    def test_check_builds_an_instrument(self):
        with tempfile.TemporaryDirectory() as d:
            code, out = self.run_cli("--check", self.write(d, "instrument.json", instrument()))
        self.assertEqual(code, 0, out)
        self.assertEqual(out.splitlines()[-1], "1 files: 0 refused")

    def test_check_refuses_what_an_instruments_builder_refuses(self):
        with tempfile.TemporaryDirectory() as d:
            path = self.write(d, "dynamic_pin.json", with_dynamic_pin_in_template())
            code, out = self.run_cli("--check", path)
        self.assertEqual(code, 1, out)
        self.assertIn("a dynamic pin inside the template of 'mux' is not supported", out)
        self.assertEqual(out.splitlines()[-1], "1 files: 1 refused")

    def test_check_over_a_folder_counts_each_file(self):
        with tempfile.TemporaryDirectory() as d:
            self.write(d, "a_plain.json", PLAIN)
            self.write(d, "b_unknown_key.json", with_unknown_key())
            self.write(d, "c_no_output.json", without_output())
            code, out = self.run_cli("--check", d)
        self.assertEqual(code, 1, out)
        self.assertEqual(out.splitlines()[-1], "3 files: 2 refused")
        self.assertEqual(len([l for l in out.splitlines() if l.startswith("REFUSED ")]), 2, out)

    def test_dump_gives_an_enums_labels_in_index_order(self):
        # The order is pinned: a file that holds an enum as an integer holds an
        # index into this list, and tools/convert_patch.py reads the labels
        # from the dump to rewrite such an integer as its label.
        code, out = self.run_cli("--dump-descriptors")
        self.assertEqual(code, 0)
        settings = {s["name"]: s for s in json.loads(out)["SVFSource"]["settings"]}
        self.assertEqual(settings["mode"]["labels"], ["Lowpass", "Highpass", "Bandpass", "Lowpass1P", "Highpass1P"])


if __name__ == "__main__":
    unittest.main()
