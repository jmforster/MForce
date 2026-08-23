"""Fast round-trip smoke — one patch per SHAPE, not the whole library.

tools/test_stable_roundtrip.py walks 199 patches and does ~600 subprocess
invocations; it takes minutes and belongs at a task boundary. Most edits cannot
plausibly affect 199 patches, and using the heaviest check as the default turns
a ten-second question into a five-minute one.

This covers the shapes that actually differ, and runs in seconds:
  bare            paramMap target with no curve
  curve           frequency->curve
  vcurve+settings piano_default (was Piano_bright until the 2026-08-22 library
                  curation): vcurve legs as CurveNode chains, dynamicPins
  formant         voice patch whose paramMap targets an OWNED Formant child
                  (the unconvertible carve-out)
  bend            real Bend articulation in the score — graft membership
  wiring          already in wiring format, incl. a dynamicPins setting

Same contract as the full gate: ids must not be lost or gain a non-"__" id,
and the render must be byte-identical. Green here is NOT a substitute for the
full gate before a commit — it is the check between edits.

Usage: python tools/rt_smoke.py
"""
import hashlib, json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "build/tools/mforce_ui/Release/mforce_ui.exe"
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

CASES = [
    ("bare",            "patches/baselines/BaselineSIN.json"),
    ("curve",           "patches/library/strings/viola_default.json"),
    ("vcurve+settings", "patches/library/keys/acoustic_piano/piano_default.json"),
    ("formant",         "patches/library/voice/sing_soprano_A.json"),
    ("bend",            "patches/baselines/bend_test.json"),
    ("wiring",          "patches/baselines/perform/wiring_setting.json"),
]


def ids_of(path):
    j = json.loads(Path(path).read_text(encoding="utf-8"))
    nodes = j.get("graph", {}).get("nodes", [])
    types = {n["id"]: n.get("type") for n in nodes}
    children = set()
    for n in nodes:
        for f in n.get("params", {}).get("formants", []) or []:
            if isinstance(f, dict) and types.get(f.get("ref")) == "Formant":
                children.add(f["ref"])
    return sorted(n["id"] for n in nodes
                  if n["id"] not in children and "__f" not in n["id"])


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def render(patch, wav):
    r = subprocess.run([str(CLI), str(patch), str(wav)],
                       capture_output=True, text=True, cwd=str(ROOT))
    return r.returncode == 0


def main():
    bad = 0
    with tempfile.TemporaryDirectory() as td:
        for label, rel in CASES:
            src = ROOT / rel
            if not src.exists():
                print(f"{label:<16} MISSING {rel}"); bad += 1; continue
            rt = Path(td) / (src.stem + "_rt.json")
            r = subprocess.run([str(UI), "--roundtrip", str(src), str(rt)],
                               capture_output=True, text=True, cwd=str(ROOT))
            if r.returncode != 0:
                print(f"{label:<16} ROUNDTRIP FAIL {r.stderr.strip()[:70]}"); bad += 1; continue
            before, after = set(ids_of(src)), set(ids_of(rt))
            lost = before - after
            added = {i for i in (after - before) if not i.startswith("__")}
            if lost or added:
                print(f"{label:<16} ID CHANGE lost={sorted(lost)} new={sorted(added)}")
                bad += 1
                continue
            wa, wb = Path(td) / "a.wav", Path(td) / "b.wav"
            ra, rb = render(src, wa), render(rt, wb)
            if not ra and not rb:
                print(f"{label:<16} skip (unrenderable both forms)"); continue
            if ra != rb or sha(wa) != sha(wb):
                print(f"{label:<16} RENDER DIFF"); bad += 1; continue
            print(f"{label:<16} ok")
    print(f"{len(CASES) - bad}/{len(CASES)} shapes clean")
    sys.exit(1 if bad else 0)


main()
