#!/usr/bin/env python3
"""v6m = v6l_cmaes3 restated under note-contained sound (2026-08-13 spec):
verb node removed (string -> output), env_damper (preset damper, pct 0.5 /
max 0.25 s) wired to string.damper, instrument.release removed. Everything
else byte-identical to v6l. Volume left unfolded — render_ks_piano_v6.py
recalibrates to 0.85 peak anyway.
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
        "params": {"preset": "damper", "release": 0.5, "releaseMax": 0.25},
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
