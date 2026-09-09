"""Loader warnings for hysteresis Shapers (plan Task 2). Renders three
tiny patches and asserts on CLI stderr. Exit 0 = pass."""
import copy
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "scratch", "hyst_loader")

BASE = {
    "sampleRate": 48000, "seconds": 0.2,
    "instrument": {"polyphony": 1, "volume": 1.0},
    "score": [{"note": 60, "time": 0.0, "duration": 0.2, "velocity": 1.0}],
    "graph": {"output": "Shaper", "nodes": [
        {"id": "Shaper", "type": "Shaper", "params": {
            "drive": 1.0, "smoothness": 0.0, "hysteresis": True,
            "values":  [-2.0, 1.0, 2.0, 1.0],
            "values2": [-2.0, -1.0, 2.0, -1.0]}},
    ]},
}


def run(mutate):
    p = copy.deepcopy(BASE)
    mutate(p["graph"]["nodes"][0]["params"])
    os.makedirs(OUT, exist_ok=True)
    pj = os.path.join(OUT, "case.json")
    json.dump(p, open(pj, "w"))
    r = subprocess.run([CLI, pj, os.path.join(OUT, "case.wav")],
                       capture_output=True, timeout=60)
    return r.returncode, r.stderr.decode(errors="replace")


def check(name, cond):
    print(("ok " if cond else "FAIL ") + name)
    return cond


ok = True
rc, err = run(lambda q: q.update({"morph": 0.5}))
ok &= check("morph+hysteresis warns", rc == 0 and "ignored" in err)
rc, err = run(lambda q: q.pop("values2"))
ok &= check("missing values2 warns inert", rc == 0 and "inert" in err)
rc, err = run(lambda q: q.update(
    {"values2": [-2.0, -1.0, 0.0, 0.0, 2.0, -1.0]}))
ok &= check("length mismatch still errors", rc != 0)
sys.exit(0 if ok else 1)
