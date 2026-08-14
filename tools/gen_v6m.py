#!/usr/bin/env python3
"""v6m = v6l_cmaes3 restated under note-contained sound (2026-08-13 spec):
verb node removed (string -> output), env_damper wired to string.damper,
instrument.release removed. Everything else byte-identical to v6l. Volume
left unfolded — render_ks_piano_v6.py recalibrates to 0.85 peak anyway.

The damper envelope is a PLAIN 3-stage Envelope in stage-list form (no
preset — presets are UI conveniences, per Matt 2026-08-13): hold-open
(expand at 0), fast 0->1 felt drop (min=max pins ~40 ms), hold-closed
choke window (pct 0.5 capped at 0.25 s) in which the string physics kills
the ring.
"""
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "patches", "pending", "ks_piano_v6", "v6l_cmaes3.json")
DST = os.path.join(ROOT, "patches", "pending", "ks_piano_v6", "v6m.json")


def main():
    p = json.load(open(SRC))
    damper = {
        "id": "env_damper",
        "type": "Envelope",
        "params": {"stages": [
            {"startVal": 0.0, "endVal": 0.0, "type": "Linear",
             "percent": 0.0, "minSec": 0.0,  "maxSec": 0.0},   # open (expand)
            {"startVal": 0.0, "endVal": 1.0, "type": "Linear",
             "percent": 0.5, "minSec": 0.04, "maxSec": 0.04},  # felt drop
            {"startVal": 1.0, "endVal": 1.0, "type": "Linear",
             "percent": 0.5, "minSec": 0.0,  "maxSec": 0.25},  # choke window
        ]},
    }
    # The loader resolves refs in file order, so env_damper must precede
    # the string node that references it.
    nodes = []
    for n in p["graph"]["nodes"]:
        if n["id"] == "verb":
            continue
        if n["id"] == "string":
            nodes.append(damper)
            n["params"]["damper"] = {"ref": "env_damper"}
        nodes.append(n)
    p["graph"]["nodes"] = nodes
    p["graph"]["output"] = "string"
    p["instrument"].pop("release", None)
    json.dump(p, open(DST, "w"), indent=2)
    print("wrote", DST)


if __name__ == "__main__":
    main()
