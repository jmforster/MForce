"""Comp walk round 1 batch — seeded siblings over the Mary harmony.

Renders template_mary_walk.json at N masterSeeds and validates Matt's
three anchor rules on every render from the piece JSON (spec
docs/superpowers/specs/2026-09-21-comp-walk1-design.md §4-5):
  R1  phrase-opening downbeats (beats 0 and 16) are chord tones
  R2  last note of each phrase is a chord tone of its bar
  R3  the passage's last note is scale degree 1 (pitch class C)
A validator failure is a BUILD BUG — the batch does not queue.

Walk2 (spec 2026-09-21-comp-walk2 §5) additions: renders to walk2/ (the
walk1 audio stays put — Matt's annotations point at it), captures the
MFORCE_ANCHOR_LOG decision log per seed, and self-checks the annotation
rules: no sub-sixteenth durations, no sub-beat repeated pitches, no
sixteenths in a bar without eighths, passage-final pitch previously
visited.

Usage:  python tools/comp_walk1_batch.py [count] [outdir_name]
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
TEMPLATE = REPO / "scores/baselines/template_mary_walk.json"
OUT = REPO / "renders/comp/audition" / (
    sys.argv[2] if len(sys.argv) > 2 else "walk2")
# Fallback CLI patch (parts carry their own instrumentPatch).
PATCH = REPO / "patches/library/keys/acoustic_piano/piano_default.json"

# Chord pitch classes per 4-beat bar of the Mary progression (C major).
C_PCS, G7_PCS = {0, 4, 7}, {7, 11, 2, 5}
BAR_PCS = [C_PCS, C_PCS, G7_PCS, C_PCS, C_PCS, C_PCS, G7_PCS, C_PCS]

# The walk template's phrase rhythm is fixed (transforms preserve length):
# phrase 1 = beats [0,16), phrase 2 = [16,32).
PHRASE_STARTS = [0.0, 16.0]
PHRASE_ENDS = [16.0, 32.0]


def melody_events(piece_json):
    part = {p["name"]: p for p in piece_json["parts"]}["melody"]
    return [(e["beat"], int(e["data"]["noteNumber"]),
             float(e["data"]["duration"])) for e in part["events"]]


# ---------------------------------------------------------------------------
# Passage-string emission (STANDING ORDER, Matt 2026-09-22): every batch
# writes its melodies as passage strings — the vocabulary Matt authors and
# annotates in — both next to the renders and into docs/matt/ for him.
# ---------------------------------------------------------------------------
NOTE_NAMES = {0: "C", 1: "C#", 2: "D", 3: "Eb", 4: "E", 5: "F", 6: "F#",
              7: "G", 8: "Ab", 9: "A", 10: "Bb", 11: "B"}
DUR_TOKENS = {4.0: "w", 3.0: "h.", 2.0: "h", 1.5: "q.", 1.0: "q",
              0.75: "e.", 0.5: "e", 0.375: "s.", 0.25: "s", 0.125: "t"}


def passage_string(events):
    toks, octv = [], 5
    for i, (beat, nn, dur) in enumerate(events):
        if i > 0 and beat >= PHRASE_STARTS[1] and events[i - 1][0] < PHRASE_STARTS[1]:
            toks.append("|")
        o = nn // 12
        while o > octv:
            toks.append("O+"); octv += 1
        while o < octv:
            toks.append("O-"); octv -= 1
        toks.append(NOTE_NAMES[nn % 12]
                    + DUR_TOKENS.get(round(dur, 3), f"({dur})"))
    return " ".join(toks)


def validate(events, label):
    problems = []
    for start in PHRASE_STARTS:                      # R1
        hits = [nn for b, nn, d in events if b == start]
        if not hits:
            problems.append(f"{label}: no note at phrase start beat {start}")
        elif hits[0] % 12 not in BAR_PCS[int(start // 4)]:
            problems.append(
                f"{label}: R1 violated at beat {start} (nn {hits[0]})")
    for start, end in zip(PHRASE_STARTS, PHRASE_ENDS):  # R2
        phrase = [(b, nn) for b, nn, d in events if start <= b < end]
        if not phrase:
            problems.append(f"{label}: empty phrase [{start},{end})")
            continue
        b, nn = phrase[-1]
        if nn % 12 not in BAR_PCS[int(b // 4)]:
            problems.append(f"{label}: R2 violated at beat {b} (nn {nn})")
    if events and events[-1][1] % 12 != 0:               # R3
        problems.append(f"{label}: R3 violated (final nn {events[-1][1]})")

    # ---- Walk2 annotation-rule checks (spec 2026-09-21-comp-walk2 §5) ----
    if events and events[-1][1] not in [nn for b, nn, d in events[:-1]]:
        problems.append(                                  # R4
            f"{label}: R4 violated (final nn {events[-1][1]} never visited)")
    bars = {}
    for i, (b, nn, d) in enumerate(events):
        if d < 0.25 - 1e-6:
            problems.append(f"{label}: sub-sixteenth duration {d} at beat {b}")
        if (i > 0 and d < 1.0 - 1e-6 and events[i - 1][2] < 1.0 - 1e-6
                and nn == events[i - 1][1]):
            problems.append(
                f"{label}: sub-beat repeated pitch (nn {nn}) at beat {b}")
        bars.setdefault(int(b // 4), []).append(d)
    for bar, ds in bars.items():
        has16 = any(dd <= 0.25 + 1e-6 for dd in ds)
        has8 = any(0.25 + 1e-6 < dd < 1.0 - 1e-6 for dd in ds)
        if has16 and not has8:
            problems.append(
                f"{label}: sixteenths in bar {bar} with no eighths")
    return problems


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    OUT.mkdir(parents=True, exist_ok=True)
    base = json.loads(TEMPLATE.read_text(encoding="utf-8"))
    all_problems = []
    with tempfile.TemporaryDirectory() as td:
        for i in range(count):
            seed = 100 + i
            t = dict(base)
            t["masterSeed"] = seed
            tpath = Path(td) / f"walk_{seed}.json"
            tpath.write_text(json.dumps(t), encoding="utf-8")
            prefix = OUT / f"mary_walk_s{seed}"
            env = dict(os.environ, MFORCE_ANCHOR_LOG="1")
            p = subprocess.run(
                [str(CLI), "--compose", str(PATCH), str(prefix), "1",
                 "--template", str(tpath)],
                capture_output=True, text=True, cwd=str(REPO), env=env)
            anchor_lines = [ln for ln in (p.stderr or "").splitlines()
                            if ln.startswith("[anchor]")]
            if anchor_lines:
                (OUT / f"mary_walk_s{seed}.log").write_text(
                    "\n".join(anchor_lines) + "\n", encoding="utf-8")
            if p.returncode != 0:
                all_problems.append(
                    f"s{seed}: render failed rc={p.returncode}: "
                    + (p.stderr or p.stdout).strip().splitlines()[-1])
                continue
            pj = json.loads(
                Path(str(prefix) + "_1.json").read_text(encoding="utf-8"))
            events = melody_events(pj)
            probs = validate(events, f"s{seed}")
            all_problems.extend(probs)
            print(f"s{seed}: {len(events)} melody events, "
                  f"{'OK' if not probs else 'RULE VIOLATIONS'}")
    if all_problems:
        print("\nVALIDATOR FAILURES (build bug — do not queue):")
        for pr in all_problems:
            print(" ", pr)
        sys.exit(1)
    print(f"\n{count}/{count} renders pass R1-R4 + rhythm rules.")

    # Standing order: passage strings beside the renders AND in docs/matt/.
    lines = []
    for i in range(count):
        seed = 100 + i
        pj = json.loads(
            (OUT / f"mary_walk_s{seed}_1.json").read_text(encoding="utf-8"))
        lines.append(f"s{seed}: " + passage_string(melody_events(pj)))
    text = "\n\n".join(lines) + "\n"
    (OUT / f"{OUT.name}_passages.txt").write_text(text, encoding="utf-8")
    matt = REPO / "docs/matt" / f"Comp_{OUT.name}_for_annotation.txt"
    matt.parent.mkdir(parents=True, exist_ok=True)
    matt.write_text(text, encoding="utf-8")
    print(f"passage strings -> {matt.relative_to(REPO).as_posix()}")


if __name__ == "__main__":
    main()
