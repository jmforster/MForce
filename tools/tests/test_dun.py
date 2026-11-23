"""The built mforce_cli's --dun mode: a parsed DUN score plays.

The tool is the file the environment variable MFORCE_CLI names (ctest sets it
to the build's own binary), else build/tools/mforce_cli/Release/mforce_cli.exe
under the repository root. With no such file every case fails: a test of the
tool that cannot find the tool has tested nothing.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.environ.get("MFORCE_CLI") or os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

# Four quarter notes in C major from C4.
DUN = "key:C scale:Major meter:4/4 bpm:120 start:0:4\nqn qu1 qu1 qd2\n"


def instrument():
    """A one-voice saw under an Envelope, its pitch from the performed note."""
    return {"sampleRate": 48000, "instrument": {"polyphony": 1, "volume": 0.3},
            "graph": {"output": "R", "nodes": [
                {"id": "pf", "type": "PerformNode", "params": {"field": "frequency"}},
                {"id": "env", "type": "Envelope", "params": {
                    "preset": "adsr", "attack": 0.02, "decay": 0.1, "release": 0.1, "sustainLevel": 0.7}},
                {"id": "R", "type": "SawSource", "params": {"amplitude": {"ref": "env"}, "frequency": {"ref": "pf"}}}]}}


class DunTests(unittest.TestCase):
    def test_dun_plays(self):
        self.assertTrue(os.path.isfile(CLI), f"no mforce_cli at {CLI}")
        with tempfile.TemporaryDirectory() as tmp:
            dun = os.path.join(tmp, "four.dun")
            patch = os.path.join(tmp, "saw.json")
            wav = os.path.join(tmp, "four.wav")
            with open(dun, "w") as f:
                f.write(DUN)
            with open(patch, "w") as f:
                json.dump(instrument(), f)
            r = subprocess.run([CLI, "--dun", dun, patch, wav], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            m = re.search(r"peak=([0-9.eE+-]+)\)", r.stdout)
            self.assertIsNotNone(m, r.stdout)
            self.assertGreater(float(m.group(1)), 0.0, r.stdout)


if __name__ == "__main__":
    unittest.main()
