"""Probe: are section keyContexts read during melody realization? (#6 stage 3)

Run 12 established they were NOT: keyContexts were parsed
(PieceTemplate::SectionTemplate -> Section::keyContexts) and
Section::active_scale_at() existed, with ZERO callers, so a passage rendered
the same notes no matter what key it claimed to be in. This is that probe,
kept as the regression test for the stage-3 fix.

Two cases, both one section with three restatements of one motif:
  no-keycontexts   negative control. All three restatements MUST render
                   identically — that is the safety claim for the change
                   (strict no-op for every patch that has no keyContexts).
  keycontexts      keyContexts at beats 0 / 8 / 16 (C / G / D major). All
                   three restatements MUST differ.

Finding worth recording (checked, not assumed): keyContexts are the ONLY
in-piece key-change mechanism. PieceTemplate::SectionTemplate has no
`keyName` field — only `scaleOverride`, which swaps the MODE against the
piece's tonic. A per-section keyName in a template JSON is silently ignored
by from_json. So before this change there was no way to modulate at all.

  python probe_key_contexts.py
"""
import json
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/library/keys/acoustic_piano/piano_default.json"  # comp render vehicle unless otherwise indicated (Matt 2026-09-05)
OUT = "renders/probe_keyctx"

# Traverses degrees 0-3-6-7 and back. Degree 6 is where the C/G/D signatures
# diverge, so a figure that reaches it is the discriminating one.
MOTIF = {"units": [{"duration": 1.0, "step": 0}, {"duration": 1.0, "step": 3},
                   {"duration": 1.0, "step": 3}, {"duration": 1.0, "step": 1},
                   {"duration": 2.0, "step": -7}, {"duration": 2.0, "step": 0}]}
KEYS = ["C", "G", "D"]
NOTES_PER = len(MOTIF["units"])


def template(with_contexts):
    section = {"name": "Main", "beats": 24}
    if with_contexts:
        section["keyContexts"] = [{"beat": 8.0 * i, "key": f"{k} Major"}
                                  for i, k in enumerate(KEYS)]
    return {
        "keyName": "C", "scaleName": "Major", "bpm": 100.0, "masterSeed": 11,
        "motifs": [{"name": "M", "figure": MOTIF, "userProvided": True}],
        "sections": [section],
        "parts": [{"name": "melody", "role": "melody", "passages": {
            "Main": {"startingPitch": {"octave": 4, "pitch": "C"},
                     "phrases": [
                         {"name": f"P{i}",
                          "startingPitch": {"octave": 4, "pitch": "C"},
                          "figures": [{"source": "reference",
                                       "motifName": "M"}],
                          "connectors": [None]}
                         for i in range(len(KEYS))]}}}]}


def render(with_contexts, label):
    prefix = f"{OUT}/{label}"
    tpath = REPO / (prefix + "_tmpl.json")
    tpath.parent.mkdir(parents=True, exist_ok=True)
    tpath.write_text(json.dumps(template(with_contexts), indent=1),
                     encoding="utf-8")
    p = subprocess.run([str(CLI), "--compose", str(PATCH), str(REPO / prefix),
                        "1", "--template", str(tpath)],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{label}: CLI failed\n{p.stdout}\n{p.stderr}")
    ev = json.loads((REPO / (prefix + "_1.json")).read_text(encoding="utf-8"))
    notes = [e for e in ev["parts"][0]["events"] if e.get("type") == "note"]
    ns = [int(e["data"]["noteNumber"]) for e in notes]
    return [ns[i * NOTES_PER:(i + 1) * NOTES_PER] for i in range(len(KEYS))]


def report(label, segs, expect_distinct):
    print(f"\n{label}:")
    for k, seg in zip(KEYS, segs):
        print(f"  restatement in {k:>2} major -> {seg}")
    distinct = len({tuple(s) for s in segs})
    ok = (distinct == len(segs)) if expect_distinct else (distinct == 1)
    want = "all differ" if expect_distinct else "all identical"
    print(f"  distinct: {distinct}/{len(segs)}  expected {want}  "
          f"=> {'OK' if ok else 'FAIL'}")
    return ok


def main():
    ctrl = render(False, "no_keycontexts")
    ok_ctrl = report("no keyContexts (negative control — the no-op claim)",
                     ctrl, expect_distinct=False)
    kc = render(True, "keycontexts")
    ok_kc = report("keyContexts at beats 0/8/16 (the stage-3 case)",
                   kc, expect_distinct=True)

    # The C restatement must be untouched by the change: with keyContexts, the
    # first context IS C major, so segment 0 has to equal the control.
    ok_first = kc[0] == ctrl[0]
    print(f"\n  C-major restatement matches the no-context render: {ok_first}")

    allok = ok_ctrl and ok_kc and ok_first
    print("\nRESULT: " + ("all checks OK" if allok else "FAIL"))
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
