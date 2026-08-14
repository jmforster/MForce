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

    # exc_level top-anchor fix (2026-08-13 sweep): the pass-3 CMA-ES
    # "level_top" anchor (2093 Hz -> 1.291) was never scored — the eval set
    # tops at midi 84 — and the curve end-clamp carried 1.291 across all of
    # octave 8, +16..+24 dB attack peaks. New top anchors derived
    # empirically (newLvl = usedLvl * targetPeak/measuredPeak, target =
    # mid-range median 1.34, loop is linear), smoothed across the per-note
    # comb-floor scatter. Endpoints at 20 Hz / 16 kHz per the no-implicit-
    # end-clamp convention (Matt 2026-08-13): a clamp is now an EXPLICIT
    # repeated value, not an accident of where the anchors stop.
    for entry in p["instrument"]["paramMap"]["frequency"]:
        if isinstance(entry, dict) and entry.get("target") == "exc_level.source2":
            entry["curve"] = [
                [20.0,    1.2033117079158895],
                [65.0,    1.2033117079158895],
                [261.63,  0.40203306798048677],
                [1046.5,  0.20629715341244262],
                [1480.0,  0.17],
                [2093.0,  0.19],
                [2960.0,  0.15],
                [3951.0,  0.10],
                [16000.0, 0.10],
            ]

    # Curve-endpoint convention retrofit (Matt 2026-08-13): every curve
    # spans the full domain — 20 Hz..16 kHz for frequency, 0..1 for
    # velocity — with clamps expressed as EXPLICIT repeated endpoint
    # values. Behavior-identical by construction (the interpolator held
    # end values anyway); null-verified by render hash.
    for entry in p["instrument"]["paramMap"]["frequency"]:
        if not isinstance(entry, dict):
            continue
        c = entry.get("curve")
        if c:
            if c[0][0] > 20.0:
                c.insert(0, [20.0, c[0][1]])
            if c[-1][0] < 16000.0:
                c.append([16000.0, c[-1][1]])
        v = entry.get("vcurve")
        if v:
            if v[0][0] > 0.0:
                v.insert(0, [0.0, v[0][1]])
            if v[-1][0] < 1.0:
                v.append([1.0, v[-1][1]])
    json.dump(p, open(DST, "w"), indent=2)
    print("wrote", DST)


if __name__ == "__main__":
    main()
