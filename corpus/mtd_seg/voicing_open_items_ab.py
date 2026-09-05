"""Comp #8 A/B — the three musical open items from the voicing framework.

Matt's three observations from 2026-04-20, each now a VoicingProfile term
that defaults to off:

  1. repeatPenalty — a repeated chord makes voice-leading distance 0 for
     the identical voicing, so it wins by a mile and the shape repeats.
  2. cadential     — the final chord of a resolution should land in root
     position, not merely smoothly.

The third (register drift, "voicings trend into the stratosphere") has NO
term: two were written and both measured worse than nothing, and the symptom
itself does not reproduce at HEAD — the base arm's top voice drifts 0
semitones over 16 chords. See backlog #8.

Measured from the rendered chord events, per arm:
  * top-voice drift and span across the passage (the register symptom, kept
    as a standing measurement even though no term acts on it);
  * consecutive identical voicings (the boring-repeat count);
  * how many chords are in root position (bass pitch-class = chord root).

The base patch is test_jazz_turnaround_smooth (Dm7 G7 Cmaj7 A7 x4), which is
the patch the original observation was made on.

Usage:  python corpus/mtd_seg/voicing_open_items_ab.py
"""
import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/baselines/Additive1.json"
BASE = REPO / "patches/test_jazz_turnaround_smooth.json"
OUT = REPO / "renders/voicing_open_items"

ARMS = {
    "base":       {},
    "repeat":     {"repeatPenalty": 0.5},
    "cadential":  {"cadential": True},
    "both":       {"repeatPenalty": 0.5, "cadential": True},
}


def build(profile_extra):
    j = json.loads(BASE.read_text(encoding="utf-8"))
    for part in j.get("parts", []):
        for passage in part.get("passages", {}).values():
            vp = passage.setdefault("voicingProfile", {})
            vp.update(profile_extra)
    return j


def render(name, template):
    OUT.mkdir(parents=True, exist_ok=True)
    tpath = OUT / f"{name}_tmpl.json"
    tpath.write_text(json.dumps(template, indent=1), encoding="utf-8")
    p = subprocess.run([str(CLI), "--compose", str(PATCH), str(OUT / name),
                        "1", "--template", str(tpath)],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{name}: CLI failed\n{p.stdout}\n{p.stderr}")


def chord_voicings(name):
    """[[noteNumbers...], ...] for the harmony part, in beat order."""
    j = json.loads((OUT / f"{name}_1.json").read_text(encoding="utf-8"))
    out = []
    for part in j.get("parts", []):
        for e in (part.get("events") or []):
            if e.get("type") == "chord":
                out.append((float(e["beat"]),
                            sorted(float(x) for x in e["data"]["pitches"])))
    return [v for _, v in sorted(out)]


# Dm7 G7 Cmaj7 A7 in C major — scale degree -> root pitch class.
ROOT_PC = {1: 2, 4: 7, 0: 0, 5: 9}
SEQ = [1, 4, 0, 5] * 4


def measure(name):
    vs = chord_voicings(name)
    if not vs:
        return None
    tops = [v[-1] for v in vs]
    basses = [v[0] for v in vs]
    repeats = sum(1 for i in range(1, len(vs)) if vs[i] == vs[i - 1])
    rootpos = sum(1 for i, v in enumerate(vs)
                  if int(round(v[0])) % 12 == ROOT_PC[SEQ[i % len(SEQ)]])
    return {
        "chords": len(vs),
        "topDrift": tops[-1] - tops[0],
        "topSpan": max(tops) - min(tops),
        "bassSpan": max(basses) - min(basses),
        "repeats": repeats,
        "rootPos": rootpos,
        "finalVoicing": vs[-1],
    }


def main():
    rows = []
    for name, extra in ARMS.items():
        render(name, build(extra))
        rows.append((name, measure(name)))

    print(f"{'arm':<12}{'chords':>7}{'topDrift':>10}{'topSpan':>9}"
          f"{'bassSpan':>10}{'repeats':>9}{'rootPos':>10}")
    for name, m in rows:
        if m is None:
            print(f"{name:<12}  NO CHORD EVENTS")
            continue
        print(f"{name:<12}{m['chords']:>7}{m['topDrift']:>+10.0f}"
              f"{m['topSpan']:>9.0f}{m['bassSpan']:>10.0f}"
              f"{m['repeats']:>9}{m['rootPos']:>7}/{m['chords']}")
    print()
    for name, m in rows:
        if m:
            print(f"  {name:<12} final voicing {[int(x) for x in m['finalVoicing']]}")


if __name__ == "__main__":
    main()
