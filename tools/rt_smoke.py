"""Fast round-trip smoke — one patch per SHAPE, not the whole library.

tools/test_stable_roundtrip.py, over patches/baselines and patches/library as
gates.py runs it, walks every patch in both folders and makes up to three
subprocess invocations for each (the roundtrip and two renders); it takes
minutes and belongs at a task boundary. Most edits cannot plausibly affect
every patch, and using the heaviest check as the default turns a
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

After the shapes, sixteen cases, each on its own line and not counted among
the shapes. Six fixtures written to the temp folder must be refused by
mforce_ui --roundtrip, each with a non-zero exit, tools/convert_patch.py named
in its output, and no file written: a patch carrying an instrument.paramMap
(refusal), an Envelope holding a stage list (retired-key), a CurveNode
holding a knot list (retired-knots), a MultiSource with a weight on a feed
(retired-feed), a BWLowpassFilter holding cutoff (retired-rename) and a
FullPartials holding an inline expandRule (retired-inline). A NameGate's
name must survive a round trip (string-setting), and a MultiSource's weights
array must survive one with the round trip rendering the original's bytes
(list-weights). A MultiSource holding a number in its list must be
refused the same way, its pin named in place of the converter, and must load
in the command-line tool (list-number). An Envelope whose stage table lacks a
column must be refused the same way, with the engine's table sentence named
in place of the converter (ragged-table). make-instrument: mforce_ui
--convert-roundtrip on patches/baselines/distortion_test.json must exit 0 and
write an instrument block with no paramMap and a PerformNode whose field is
frequency wired into the sin node's frequency. Envelopes must round-trip
through mforce_ui --roundtrip as the file gave them: the type column as its
labels and timeMode as its label (table-labels); eight empty columns as eight
empty columns (empty-table); four sustain slots at 0.9 beside a sustainLevel
of 0.3, which the load applies before the table (settings-order); and an asr
preset as that preset's three rows in columns, with no preset key
(preset-form). apply-preset: mforce_ui --apply-preset lays a table-form
Envelope out as the adsr preset, and the saved node's eight columns are that
preset's defaults.

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

STAGE_COLUMNS = ("startVal", "endVal", "power", "type", "percent", "minSec", "maxSec", "nominal")


def envelope_doc(env_params):
    """An instrument patch whose sine's amplitude is the Envelope `env` with
    `env_params`."""
    return {
        "sampleRate": 48000,
        "graph": {"nodes": [{"id": "env", "type": "Envelope", "params": env_params},
                            {"id": "osc", "type": "SineSource",
                             "params": {"frequency": 440.0, "amplitude": {"ref": "env"}}}],
                  "output": "osc"},
        "instrument": {"polyphony": 1},
        "score": [{"note": 60, "velocity": 0.8, "time": 0.0, "duration": 0.5}],
    }


RETIRED_FIXTURE = envelope_doc({"stages": [
    {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.1},
    {"startVal": 1.0, "endVal": 0.0, "type": "Expo", "power": 2.0, "percent": 0.0}]})

LABELS_FIXTURE = envelope_doc({
    "timeMode": "seconds",
    "startVal": [0.0, 1.0], "endVal": [1.0, 0.0], "power": [0.0, 2.0],
    "type": ["Linear", "Expo"], "percent": [0.01, 0.0], "minSec": [0.0, 0.0],
    "maxSec": [0.0, 0.0], "nominal": [0.0, 0.0]})

EMPTY_TABLE_FIXTURE = envelope_doc({c: [] for c in STAGE_COLUMNS})

RETIRED_KNOTS_FIXTURE = {
    "sampleRate": 48000,
    "graph": {"nodes": [{"id": "pf", "type": "PerformNode", "params": {"field": "frequency"}},
                        {"id": "cv", "type": "CurveNode",
                         "params": {"source": {"ref": "pf"}, "knots": [[100.0, 0.2], [1000.0, 0.8]]}},
                        {"id": "osc", "type": "SineSource",
                         "params": {"frequency": 440.0, "amplitude": {"ref": "cv"}}}],
              "output": "osc"},
    "instrument": {"polyphony": 1},
    "score": [{"note": 60, "velocity": 0.8, "time": 0.0, "duration": 0.5}],
}

RAGGED_FIXTURE = envelope_doc({
    "startVal": [0.0, 1.0], "endVal": [1.0, 0.0], "power": [0.0, 0.0],
    "type": ["Linear", "Sine"], "percent": [0.2, 0.0], "minSec": [0.0, 0.0],
    "maxSec": [1.0, 0.0]})
RAGGED_SENTENCE = "node 'env' (Envelope): table 'stages' takes its columns at one length"

SETTINGS_ORDER_FIXTURE = envelope_doc({
    "sustainLevel": 0.3,
    "startVal": [0.0, 1.0, 0.9, 0.9], "endVal": [1.0, 0.9, 0.9, 0.0], "power": [0.0, 0.0, 0.0, 0.0],
    "type": ["Linear", "Linear", "Linear", "Sine"], "percent": [0.2, 0.1, 0.0, 0.2],
    "minSec": [0.0, 0.0, 0.0, 0.0], "maxSec": [1.0, 0.5, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0, 0.0]})

PRESET_FORM_FIXTURE = envelope_doc({"preset": "asr", "attack": 0.05, "sustainLevel": 0.4, "release": 0.3})

# make_asr(0.05, 0.4, 0.3): the attack 0 -> 0.4 over 0.05 (max 1), the
# expand at 0.4, the release 0.4 -> 0 (Sine) over 0.3.
ASR_COLUMNS = {
    "startVal": [0.0, 0.4, 0.4], "endVal": [0.4, 0.4, 0.0], "power": [0.0, 0.0, 0.0],
    "type": ["Linear", "Linear", "Sine"], "percent": [0.05, 0.0, 0.3], "minSec": [0.0, 0.0, 0.0],
    "maxSec": [1.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0]}

APPLY_PRESET_FIXTURE = envelope_doc({
    "startVal": [0.0, 0.5, 0.25], "endVal": [0.5, 0.25, 0.0], "power": [2.0, 0.0, 0.0],
    "type": ["Expo", "Hold", "Linear"], "percent": [0.3, 0.3, 0.0], "minSec": [0.01, 0.0, 0.0],
    "maxSec": [0.2, 0.0, 0.0], "nominal": [0.0, 0.1, 0.0]})

# make_adsr's defaults, the adsr preset with no knob given: the attack 0 -> 1
# over 0.2 (max 1), the decay 1 -> 0.7 over 0.1 (max 0.5), the expand at 0.7,
# the release 0.7 -> 0 (Sine) at 0.
ADSR_COLUMNS = {
    "startVal": [0.0, 1.0, 0.7, 0.7], "endVal": [1.0, 0.7, 0.7, 0.0], "power": [0.0, 0.0, 0.0, 0.0],
    "type": ["Linear", "Linear", "Linear", "Sine"], "percent": [0.2, 0.1, 0.0, 0.0],
    "minSec": [0.0, 0.0, 0.0, 0.0], "maxSec": [1.0, 0.5, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0, 0.0]}


def instrument_doc(nodes, output, **instrument):
    """An instrument patch of `nodes` whose output is `output`, with one note
    and its length given, as the editor writes one."""
    return {
        "sampleRate": 48000,
        "seconds": 1.0,
        "graph": {"nodes": nodes, "output": output},
        "instrument": dict({"polyphony": 1}, **instrument),
        "score": [{"note": 60, "velocity": 0.8, "time": 0.0, "duration": 0.5}],
    }


PERFORM_FREQUENCY = {"id": "pf", "type": "PerformNode", "params": {"field": "frequency"}}
TWO_SINES = [PERFORM_FREQUENCY,
             {"id": "s1", "type": "SineSource", "params": {"frequency": {"ref": "pf"}, "amplitude": 0.5}},
             {"id": "s2", "type": "SawSource", "params": {"frequency": {"ref": "pf"}, "amplitude": 0.5}}]

RETIRED_FEED_FIXTURE = instrument_doc(TWO_SINES + [
    {"id": "mix", "type": "MultiSource",
     "params": {"source": [{"ref": "s1", "weight": 0.5}, {"ref": "s2"}]}}], "mix")

LIST_NUMBER_FIXTURE = instrument_doc(TWO_SINES + [
    {"id": "mix", "type": "MultiSource", "params": {"source": [{"ref": "s1"}, 0.5]}}], "mix")
LIST_NUMBER_SENTENCE = "'source' holds a value that is not a link"

RETIRED_RENAME_FIXTURE = instrument_doc(TWO_SINES + [
    {"id": "lp", "type": "BWLowpassFilter", "params": {"source": {"ref": "s2"}, "cutoff": 800.0}}], "lp")

RETIRED_INLINE_FIXTURE = instrument_doc([
    PERFORM_FREQUENCY,
    {"id": "parts", "type": "FullPartials",
     "params": {"maxPartials": 8, "expandRule": {"count": 2, "spacing1": 0.3}}},
    {"id": "add", "type": "AdditiveSource",
     "params": {"frequency": {"ref": "pf"}, "amplitude": 0.5, "partials": {"ref": "parts"}}}], "add")

STRING_SETTING_FIXTURE = instrument_doc([
    PERFORM_FREQUENCY,
    {"id": "on", "type": "PerformNode", "params": {"field": "onset"}},
    {"id": "gate", "type": "NameGate", "params": {"name": "tongue", "in": {"ref": "on"}}},
    {"id": "osc", "type": "SineSource", "params": {"frequency": {"ref": "pf"}, "amplitude": {"ref": "gate"}}}],
    "osc", onsets=["tongue", "slur"])

LIST_WEIGHTS = [0.75, 0.25]
LIST_WEIGHTS_FIXTURE = instrument_doc(TWO_SINES + [
    {"id": "mix", "type": "MultiSource",
     "params": {"source": [{"ref": "s1"}, {"ref": "s2"}], "weights": LIST_WEIGHTS}}], "mix")


def refusal_failures(td, doc, name="refusal", needle="tools/convert_patch.py"):
    """Write `doc` to `td` and round-trip it; return why the editor did not
    refuse it (empty when it exited non-zero, named `needle` and wrote
    nothing)."""
    src, out = Path(td) / (name + ".json"), Path(td) / (name + "_rt.json")
    src.write_text(json.dumps(doc), encoding="utf-8")
    r = subprocess.run([str(UI), "--roundtrip", str(src), str(out)],
                       capture_output=True, text=True, cwd=str(ROOT))
    why = []
    if r.returncode == 0:
        why.append("loaded")
    if needle not in r.stdout + r.stderr:
        why.append("'%s' not named" % needle)
    if out.exists():
        why.append("wrote " + out.name)
    return why


def roundtrip_env_params(td, doc, name):
    """Write `doc` to `td` and round-trip it; return (why it failed, the saved
    Envelope's params)."""
    src, out = Path(td) / (name + ".json"), Path(td) / (name + "_rt.json")
    src.write_text(json.dumps(doc), encoding="utf-8")
    r = subprocess.run([str(UI), "--roundtrip", str(src), str(out)],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        return ["exit %d %s" % (r.returncode, r.stderr.strip()[:70])], None
    if not out.exists():
        return ["wrote nothing"], None
    nodes = json.loads(out.read_text(encoding="utf-8"))["graph"]["nodes"]
    env = next((n for n in nodes if n.get("id") == "env"), None)
    if env is None:
        return ["no env node"], None
    return [], env.get("params") or {}


def roundtrip_node_params(td, doc, name, node_id):
    """Write `doc` to `td` and round-trip it; return (why it failed, the source
    path, the output path, the saved params of node `node_id`)."""
    src, out = Path(td) / (name + ".json"), Path(td) / (name + "_rt.json")
    src.write_text(json.dumps(doc), encoding="utf-8")
    r = subprocess.run([str(UI), "--roundtrip", str(src), str(out)],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        return ["exit %d %s" % (r.returncode, r.stderr.strip()[:70])], src, out, None
    if not out.exists():
        return ["wrote nothing"], src, out, None
    nodes = json.loads(out.read_text(encoding="utf-8"))["graph"]["nodes"]
    node = next((n for n in nodes if n.get("id") == node_id), None)
    if node is None:
        return ["no %s node" % node_id], src, out, None
    return [], src, out, node.get("params") or {}


def string_setting_failures(td):
    """Round-trip STRING_SETTING_FIXTURE; return why its NameGate's name is not
    the file's."""
    why, _, _, params = roundtrip_node_params(td, STRING_SETTING_FIXTURE, "string_setting", "gate")
    if why:
        return why
    return [] if params.get("name") == "tongue" else ["name is %s" % json.dumps(params.get("name"))]


def list_weights_failures(td):
    """Round-trip LIST_WEIGHTS_FIXTURE; return why its MultiSource does not keep
    its weights or the round trip does not render the original's bytes."""
    why, src, out, params = roundtrip_node_params(td, LIST_WEIGHTS_FIXTURE, "list_weights", "mix")
    if why:
        return why
    got = params.get("weights")
    if not (isinstance(got, list) and len(got) == len(LIST_WEIGHTS)
            and all(isinstance(g, (int, float)) and abs(g - w) <= 1e-6 for g, w in zip(got, LIST_WEIGHTS))):
        why.append("weights is %s" % json.dumps(got))
    wa, wb = Path(td) / "list_weights_a.wav", Path(td) / "list_weights_b.wav"
    if not (render(src, wa) and render(out, wb)):
        why.append("a render failed")
    elif sha(wa) != sha(wb):
        why.append("render differs")
    return why


def list_number_failures(td):
    """LIST_NUMBER_FIXTURE: the editor refuses it naming the pin and writes
    nothing; the command-line tool loads it. Return why not."""
    why = refusal_failures(td, LIST_NUMBER_FIXTURE, "list_number", LIST_NUMBER_SENTENCE)
    if not render(Path(td) / "list_number.json", Path(td) / "list_number.wav"):
        why.append("the CLI refused it")
    return why


def table_labels_failures(td):
    """Round-trip LABELS_FIXTURE; return why its Envelope did not keep the type
    column as its labels and timeMode as its label."""
    why, params = roundtrip_env_params(td, LABELS_FIXTURE, "table_labels")
    if why:
        return why
    if params.get("type") != ["Linear", "Expo"]:
        why.append("type is %s" % json.dumps(params.get("type")))
    if params.get("timeMode") != "seconds":
        why.append("timeMode is %s" % json.dumps(params.get("timeMode")))
    return why


def columns_failures(params, want):
    """Why the Envelope `params` do not hold the columns `want` (empty when
    they do): the type column word for word, a number column within 1e-6, as
    the editor writes a float cell as the double nearest it."""
    why = []
    for c, w in want.items():
        got = params.get(c)
        if c == "type":
            ok = got == w
        else:
            ok = (isinstance(got, list) and len(got) == len(w)
                  and all(isinstance(g, (int, float)) and abs(g - x) <= 1e-6 for g, x in zip(got, w)))
        if not ok:
            why.append("%s is %s" % (c, json.dumps(got)))
    return why


def settings_order_failures(td):
    """Round-trip SETTINGS_ORDER_FIXTURE; return why its four sustain slots
    are not the table's 0.9."""
    why, params = roundtrip_env_params(td, SETTINGS_ORDER_FIXTURE, "settings_order")
    if why:
        return why
    s, e = params.get("startVal") or [], params.get("endVal") or []
    if len(s) != 4 or len(e) != 4:
        return ["startVal is %s, endVal is %s" % (json.dumps(s), json.dumps(e))]
    return ["%s is %r" % (n, v) for n, v in (("endVal[1]", e[1]), ("startVal[2]", s[2]),
                                            ("endVal[2]", e[2]), ("startVal[3]", s[3]))
            if abs(v - 0.9) > 1e-6]


def preset_form_failures(td):
    """Round-trip PRESET_FORM_FIXTURE; return why its Envelope is not the asr
    preset's three rows as columns, with no preset key."""
    why, params = roundtrip_env_params(td, PRESET_FORM_FIXTURE, "preset_form")
    if why:
        return why
    if "preset" in params:
        why.append("preset is %s" % json.dumps(params["preset"]))
    return why + columns_failures(params, ASR_COLUMNS)


def apply_preset_failures(td):
    """Lay APPLY_PRESET_FIXTURE's Envelope out as the adsr preset through
    mforce_ui --apply-preset; return why the saved node's eight columns are
    not the preset's defaults."""
    src, out = Path(td) / "apply_preset.json", Path(td) / "apply_preset_out.json"
    src.write_text(json.dumps(APPLY_PRESET_FIXTURE), encoding="utf-8")
    r = subprocess.run([str(UI), "--apply-preset", str(src), "env", "adsr", str(out)],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        return ["exit %d %s" % (r.returncode, r.stderr.strip()[:70])]
    if not out.exists():
        return ["wrote nothing"]
    nodes = json.loads(out.read_text(encoding="utf-8"))["graph"]["nodes"]
    env = next((n for n in nodes if n.get("id") == "env"), None)
    if env is None:
        return ["no env node"]
    return columns_failures(env.get("params") or {}, ADSR_COLUMNS)


def empty_table_failures(td):
    """Round-trip EMPTY_TABLE_FIXTURE; return why its Envelope did not keep
    eight empty columns."""
    why, params = roundtrip_env_params(td, EMPTY_TABLE_FIXTURE, "empty_table")
    if why:
        return why
    return ["%s is %s" % (c, json.dumps(params.get(c))) for c in STAGE_COLUMNS if params.get(c) != []]


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
        failed = False
        for label, why in (("refusal", lambda: refusal_failures(td, PARAMMAP_FIXTURE)),
                           ("retired-key", lambda: refusal_failures(td, RETIRED_FIXTURE, "retired_key")),
                           ("retired-knots", lambda: refusal_failures(td, RETIRED_KNOTS_FIXTURE, "retired_knots")),
                           ("retired-feed", lambda: refusal_failures(td, RETIRED_FEED_FIXTURE, "retired_feed")),
                           ("retired-rename", lambda: refusal_failures(td, RETIRED_RENAME_FIXTURE, "retired_rename")),
                           ("retired-inline", lambda: refusal_failures(td, RETIRED_INLINE_FIXTURE, "retired_inline")),
                           ("string-setting", lambda: string_setting_failures(td)),
                           ("list-weights", lambda: list_weights_failures(td)),
                           ("list-number", lambda: list_number_failures(td)),
                           ("ragged-table", lambda: refusal_failures(td, RAGGED_FIXTURE, "ragged_table",
                                                                     RAGGED_SENTENCE)),
                           ("make-instrument", lambda: make_instrument_failures(td)),
                           ("table-labels", lambda: table_labels_failures(td)),
                           ("empty-table", lambda: empty_table_failures(td)),
                           ("settings-order", lambda: settings_order_failures(td)),
                           ("preset-form", lambda: preset_form_failures(td)),
                           ("apply-preset", lambda: apply_preset_failures(td))):
            reasons = why()
            failed = failed or bool(reasons)
            print(f"{label:<16} " + (f"FAIL {', '.join(reasons)}" if reasons else "ok"))
    print(f"{len(CASES) - bad}/{len(CASES)} shapes clean")
    sys.exit(1 if bad or failed else 0)


main()
