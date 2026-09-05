"""Comp #7 A/B — alternating_figure vs phrase_aware_figure.

Renders the same K467-shaped 12-chord progression (phrase budgets 2+2+4+4
bars) through both chord-driven passage strategies at the same seed, then
measures from the EVENT DUMP, not from the code:

  * how many phrases the passage actually composed;
  * where cadential arrivals land (AFS: one per two chords by construction;
    phrase-aware: one per phrase boundary);
  * whether each phrase's last note is the declared cadence target degree.

Also renders one deliberate norm-breaker (24-bar phrase followed by an 8-bar
one) per the standing outlier rule.

Usage:  python corpus/mtd_seg/phrase_aware_ab.py [--score-only]
"""
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/baselines/Additive1.json"
OUT = REPO / "renders/phrase_aware_ab"

SEED = 4471

# I V | V7 I | I vi ii V | I IV V7 I   — one chord per bar, 4 beats each.
PROG = [
    (0, "Major"), (4, "Major"),
    (4, "7"),     (0, "Major"),
    (0, "Major"), (5, "Minor"), (1, "Minor"), (4, "Major"),
    (0, "Major"), (3, "Major"), (4, "7"),     (0, "Major"),
]
# Phrase boundaries the music actually has, in chord indices.
PHRASE_SPANS = [(0, 2), (2, 4), (4, 8), (8, 12)]
# HC, IAC(full), HC, PAC — the arc the AFS impedance note called for.
CADENCE_TYPES = [1, 2, 1, 2]

BODY_A = {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 5}
BODY_B = {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 5}


def chords_json(prog):
    return [{"degree": d, "quality": q, "beats": 4.0} for d, q in prog]


def base(strategy, phrases, prog=PROG, beats=None):
    beats = beats if beats is not None else 4.0 * len(prog)
    return {
        "keyName": "C",
        "scaleName": "Major",
        "bpm": 100.0,
        "masterSeed": SEED,
        "sections": [{"name": "Main", "beats": beats,
                      "chordProgression": chords_json(prog)}],
        "parts": [{
            "name": "melody",
            "role": "melody",
            "passages": {"Main": {
                "startingPitch": {"octave": 4, "pitch": "C"},
                "strategy": strategy,
                "endGrid": 1.0,
                "phrases": phrases,
            }},
        }],
    }


def afs_arm():
    # AFS reads phrases[0].figures[0..1] and nothing else.
    return base("alternating_figure", [{
        "name": "all",
        "figures": [BODY_A, dict(BODY_B, figureCadenceType=1)],
    }])


def phrase_aware_arm():
    phrases = []
    for i, ((c0, c1), ct) in enumerate(zip(PHRASE_SPANS, CADENCE_TYPES)):
        phrases.append({
            "name": f"p{i}",
            "totalBeats": 4.0 * (c1 - c0),
            "cadenceType": ct,
            "figures": [BODY_A, BODY_B],
        })
    return base("phrase_aware_figure", phrases)


def outlier_arm():
    # Norm-breaker: a 24-bar phrase followed by an 8-bar one, 32 chords.
    prog = [(0, "Major"), (5, "Minor"), (3, "Major"), (4, "Major")] * 8
    phrases = [
        {"name": "long24", "totalBeats": 96.0, "cadenceType": 1,
         "figures": [BODY_A, BODY_B]},
        {"name": "short8", "totalBeats": 32.0, "cadenceType": 2,
         "figures": [BODY_A, BODY_B]},
    ]
    return base("phrase_aware_figure", phrases, prog=prog, beats=128.0)


ARMS = {"afs": afs_arm, "phrase_aware": phrase_aware_arm,
        "outlier_24_8": outlier_arm}


def render(name, template):
    OUT.mkdir(parents=True, exist_ok=True)
    tpath = OUT / f"{name}_tmpl.json"
    tpath.write_text(json.dumps(template, indent=1), encoding="utf-8")
    p = subprocess.run([str(CLI), "--compose", str(PATCH),
                        str(OUT / name), "1", "--template", str(tpath)],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{name}: CLI failed\n{p.stdout}\n{p.stderr}")
    return p.stdout + p.stderr


def melody_events(name):
    ev = json.loads((OUT / f"{name}_1.json").read_text(encoding="utf-8"))
    for part in ev.get("parts", []):
        evs = part.get("events")
        if evs:
            return evs
    return []


DEG_OF_PC = {0: 0, 2: 1, 4: 2, 5: 3, 7: 4, 9: 5, 11: 6}   # C major


def degree(note):
    return DEG_OF_PC.get(int(round(note)) % 12)


def report(name, events, spans, cad_types):
    """Per phrase-span (in BEATS), what the last note of that span is."""
    lines = []
    print(f"\n--- {name}: {len(events)} notes ---")
    for i, (c0, c1) in enumerate(spans):
        b0, b1 = 4.0 * c0, 4.0 * c1
        inspan = [e for e in events
                  if b0 <= float(e["data"].get("startBeat", e.get("beat", -1))) < b1]
        if not inspan:
            lines.append((i, None, None, None))
            print(f"  span {i} [{b0:.0f},{b1:.0f}): NO NOTES")
            continue
        last = inspan[-1]
        nn = float(last["data"].get("noteNumber", last["data"].get("note", 0)))
        d = degree(nn)
        want = 4 if cad_types[i] == 1 else 0
        dur = float(last["data"].get("duration", 0.0))
        ok = "HIT " if d == want else "miss"
        lines.append((i, d, want, dur))
        print(f"  span {i} [{b0:>3.0f},{b1:>3.0f}): {len(inspan):>2} notes, "
              f"last nn={nn:>5.1f} deg={d} want={want} {ok} finalDur={dur:.2f}")
    return lines


def main():
    score_only = "--score-only" in sys.argv
    failed = {}
    for name, fn in ARMS.items():
        if not score_only:
            try:
                out = render(name, fn())
            except RuntimeError as e:
                failed[name] = str(e).strip().splitlines()[-1]
                print(f"!! {name} FAILED: {failed[name]}")
                continue
            for ln in out.splitlines():
                if "ERROR" in ln:
                    print(ln)
    print("=" * 68)
    report("afs (cadence every other chord)", melody_events("afs"),
           PHRASE_SPANS, CADENCE_TYPES)
    report("phrase_aware (cadence per phrase)", melody_events("phrase_aware"),
           PHRASE_SPANS, CADENCE_TYPES)
    if "outlier_24_8" not in failed:
        report("outlier 24+8 bars", melody_events("outlier_24_8"),
               [(0, 24), (24, 32)], [1, 2])


if __name__ == "__main__":
    main()
