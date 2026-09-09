"""Open-loop hysteresis state-machine test (spec 6.2, plan Task 1).
Curve A = +1 line, curve B = -1 line -> output sign IS the state.
Input: Envelope triangle -1 -> +1 -> -1 over SECONDS, linear, exact.
Asserts transition times for (breakaway, capture) cases against the
analytic ramp. Exit 0 = pass."""
import json
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "scratch", "hyst_openloop")
SECONDS = 2.0
SR = 48000
TOL = 0.01 * SR   # 10 ms slack on each transition


def patch(breakaway, capture):
    return {
        "sampleRate": SR,
        "seconds": SECONDS,
        "instrument": {"polyphony": 1, "volume": 1.0},
        "score": [{"note": 60, "time": 0.0, "duration": SECONDS,
                   "velocity": 1.0}],
        "graph": {
            "output": "Shaper",
            "nodes": [
                {"id": "Ramp", "type": "Envelope", "params": {
                    "minValue": -1.0, "maxValue": 1.0,
                    "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
                    "timeMode": "fraction", "timeScale": 1.0,
                    "stages": [
                        {"type": "Linear", "startVal": 0.0, "endVal": 1.0,
                         "percent": 0.5, "power": 0.0,
                         "minSec": 0.0, "maxSec": 0.0},
                        {"type": "Linear", "startVal": 1.0, "endVal": 0.0,
                         "percent": 0.5, "power": 0.0,
                         "minSec": 0.0, "maxSec": 0.0}]}},
                {"id": "Shaper", "type": "Shaper", "params": {
                    "source": {"ref": "Ramp"},
                    "drive": 1.0, "smoothness": 0.0, "morph": 0.0,
                    "hysteresis": True,
                    "breakaway": breakaway, "capture": capture,
                    "values":  [-2.0, 1.0, 2.0, 1.0],
                    "values2": [-2.0, -1.0, 2.0, -1.0]}},
            ],
        },
    }


def sign_trace(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    mono = [sum(raw[i * ch:(i + 1) * ch]) for i in range(n)]
    return [1 if s > 0 else (-1 if s < 0 else 0) for s in mono]


def transitions(tr):
    out = []
    prev = tr[0]
    for i, s in enumerate(tr):
        if s != 0 and prev != 0 and s != prev:
            out.append((i, prev, s))
        if s != 0:
            prev = s
    return out


def ramp_time(v_from_minus1):
    """Sample index where the triangle (-1 at t=0, +1 at t=1s, -1 at 2s)
    first reaches value v on the way up."""
    return (v_from_minus1 + 1.0) / 2.0 * SR


def expect(breakaway, capture):
    """(sample, newsign) transition list for the analytic ramp.
    The capture band |s|<capture is entered at -capture on the ASCENT and
    at +capture on the DESCENT (first edge reached from that side); with
    capture 0 both collapse to the zero crossing."""
    up_recap = ramp_time(-capture)
    down_recap = 2.0 * SR - ramp_time(capture)
    return [
        (up_recap, +1),                      # slip->stick entering band (up)
        (ramp_time(breakaway), -1),          # stick->slip at +breakaway
        (down_recap, +1),                    # slip->stick entering band (down)
        (2.0 * SR - ramp_time(-breakaway), -1),  # stick->slip at -breakaway
    ]


def run_case(breakaway, capture):
    os.makedirs(OUT, exist_ok=True)
    tag = f"b{int(breakaway*100)}_c{int(capture*100)}"
    pj = os.path.join(OUT, f"case_{tag}.json")
    pw = os.path.join(OUT, f"case_{tag}.wav")
    json.dump(patch(breakaway, capture), open(pj, "w"))
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    if r.returncode != 0:
        print(f"FAIL {tag}: render error:",
              r.stderr.decode(errors="replace")[:200])
        return False
    tr = transitions(sign_trace(pw))
    exp = expect(breakaway, capture)
    ok = True
    if len(tr) != len(exp):
        print(f"FAIL {tag}: {len(tr)} transitions, expected {len(exp)}: "
              f"{[(i, b) for i, _, b in tr]}")
        return False
    for (i, _, newsign), (ei, esign) in zip(tr, exp):
        if newsign != esign or abs(i - ei) > TOL:
            print(f"FAIL {tag}: transition at {i} -> {newsign}, "
                  f"expected ~{int(ei)} -> {esign}")
            ok = False
    if ok:
        print(f"ok {tag}: {len(tr)} transitions at expected times")
    return ok


ok = run_case(0.6, 0.0) and run_case(0.6, 0.3)
sys.exit(0 if ok else 1)
