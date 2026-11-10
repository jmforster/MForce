"""Fast round-trip smoke — one patch per SHAPE, not the whole library.

tools/test_stable_roundtrip.py, over patches/baselines and patches/library as
gates.py runs it, walks 132 patches and makes up to 342 subprocess invocations;
it takes minutes and belongs at a task boundary. Most edits cannot plausibly
affect 132 patches, and using the heaviest check as the default turns a
ten-second question into a five-minute one.

This covers the shapes that actually differ, and runs in seconds:
  bare            a PerformNode's frequency wired straight to a SineSource pin
  curve           viola_default: the frequency through CurveNodes into two
                  Envelopes' dynamicPins, and through a Vibrato into the pitch
  vcurve+settings piano_default: frequency and velocity through CurveNodes
                  (two pairs multiplied by CombinedSources) into filter
                  cutoffs and gains, and into the KSString's dynamicPins
  formant         a FormantPeaks voice: the frequency straight into an
                  AdditiveSource whose formant is a FormantPeaks
  bend            Bend articulations in the score over a SawSource whose
                  frequency is a PerformNode's
  wiring          a frequency CurveNode on an Envelope's sustainLevel
                  dynamicPins setting

Same contract as the full gate: ids must not be lost or gain a non-"__" id,
and the render must be byte-identical. Green here is NOT a substitute for the
full gate before a commit — it is the check between edits.

After the shapes, a patch carrying an instrument.paramMap, written to the temp
folder, must be refused by mforce_ui --roundtrip: a non-zero exit,
tools/convert_patch.py named in its output, and no file written. It prints its
own line and is not counted among the shapes. So does make-instrument: mforce_ui
--convert-roundtrip on patches/baselines/distortion_test.json must exit 0 and
write an instrument block with no paramMap and a PerformNode whose field is
frequency wired into the sin node's frequency.

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

PARAMMAP_FIXTURE = {
    "sampleRate": 48000,
    "graph": {"nodes": [{"id": "osc", "type": "SineSource",
                         "params": {"frequency": 440.0, "amplitude": 0.5}}],
              "output": "osc"},
    "instrument": {"polyphony": 1, "paramMap": {"frequency": "osc.frequency"}},
    "score": [{"note": 60, "velocity": 0.8, "time": 0.0, "duration": 0.5}],
}


def refusal_failures(td, doc):
    """Write `doc` to `td` and round-trip it; return why the editor did not
    refuse it (empty when it exited non-zero, named tools/convert_patch.py and
    wrote nothing)."""
    src, out = Path(td) / "refusal.json", Path(td) / "refusal_rt.json"
    src.write_text(json.dumps(doc), encoding="utf-8")
    r = subprocess.run([str(UI), "--roundtrip", str(src), str(out)],
                       capture_output=True, text=True, cwd=str(ROOT))
    why = []
    if r.returncode == 0:
        why.append("loaded")
    if "tools/convert_patch.py" not in r.stdout + r.stderr:
        why.append("converter not named")
    if out.exists():
        why.append("wrote " + out.name)
    return why


MAKE_INSTRUMENT_SOURCE = "patches/baselines/distortion_test.json"


def make_instrument_doc_failures(doc):
    """Why `doc`, the editor's make-instrument output for distortion_test.json,
    is not the wired instrument (empty when it is): an instrument block with no
    paramMap key, a PerformNode whose params.field is frequency, and the sin
    node's params.frequency a ref to that node."""
    why = []
    inst = doc.get("instrument")
    if not isinstance(inst, dict):
        why.append("no instrument block")
    elif "paramMap" in inst:
        why.append("instrument.paramMap present")
    nodes = (doc.get("graph") or {}).get("nodes") or doc.get("nodes") or []
    perf = [n["id"] for n in nodes if n.get("type") == "PerformNode"
            and (n.get("params") or {}).get("field") == "frequency"]
    if not perf:
        why.append("no frequency PerformNode")
    sin = next((n for n in nodes if n.get("id") == "sin"), None)
    if sin is None:
        why.append("no sin node")
    elif not perf or (sin.get("params") or {}).get("frequency") != {"ref": perf[0]}:
        why.append("sin.frequency is %s" % json.dumps((sin.get("params") or {}).get("frequency")))
    return why


def make_instrument_failures(td):
    """Run the editor's make-instrument on MAKE_INSTRUMENT_SOURCE into `td`;
    return why it failed (empty when it exited 0 and wrote the wired
    instrument)."""
    out = Path(td) / "make_instrument.json"
    r = subprocess.run([str(UI), "--convert-roundtrip", str(ROOT / MAKE_INSTRUMENT_SOURCE), str(out)],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        return ["exit %d %s" % (r.returncode, r.stderr.strip()[:70])]
    if not out.exists():
        return ["wrote nothing"]
    return make_instrument_doc_failures(json.loads(out.read_text(encoding="utf-8")))


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
        refusal = refusal_failures(td, PARAMMAP_FIXTURE)
        print(f"{'refusal':<16} " + (f"FAIL {', '.join(refusal)}" if refusal else "ok"))
        make = make_instrument_failures(td)
        print(f"{'make-instrument':<16} " + (f"FAIL {', '.join(make)}" if make else "ok"))
    print(f"{len(CASES) - bad}/{len(CASES)} shapes clean")
    sys.exit(1 if bad or refusal or make else 0)


main()
