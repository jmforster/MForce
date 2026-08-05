"""Bruckner pedal v2 — render + verify (Matt's run-15 verdict, item 6).

Two cadence flavors (Ger6 / Neapolitan) x two pacings (4 and 8 beats per
chord). Verifies, from the rendered event dumps, the three things Matt's
point (d) demands:

  1. the pedal holds G (43) under EVERY chord including the plain G triad,
     and moves to C (36) only on the final chord's downbeat;
  2. the pinned cadence seam voices as authored: root-position bVI
     (bass Ab4=56, top Gb5=66 for the Ger6) -> bass-G tonic 6/4 with the G
     doubled on top (55 ... 67); note numbers are octave*12 + pc;
  3. every WAV is non-silent (peak > 0.1).

Usage:  python corpus/mtd_seg/render_bruckner2.py
"""
import json
import pathlib
import struct
import sys
import wave

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from markov_phrase import REPO, render_template            # noqa: E402
from passage_strategies import bruckner2                   # noqa: E402

OUTROOT = "renders/passage_bruckner2"


def wav_peak(path):
    with wave.open(str(path), "rb") as w:
        n = w.getnframes()
        width = w.getsampwidth()
        raw = w.readframes(n)
    if width == 2:
        vals = struct.unpack(f"<{len(raw) // 2}h", raw)
        return max(abs(v) for v in vals) / 32768.0
    if width == 4:
        vals = struct.unpack(f"<{len(raw) // 4}f", raw)
        return max(abs(v) for v in vals)
    raise ValueError(f"unhandled sample width {width}")


def verify(prefix, per_chord, cadence):
    ev = json.loads((REPO / (prefix + "_1.json")).read_text(encoding="utf-8"))
    parts = {p["name"]: p["events"] for p in ev["parts"]}
    ok = True

    ped = [(e["beat"], int(e["data"]["noteNumber"])) for e in parts["pedal"]]
    final_beat = 9 * per_chord
    for beat, nn in ped:
        want = 36 if beat >= final_beat else 43
        if nn != want:
            ok = False
            print(f"  PEDAL FAIL at beat {beat}: {nn} (want {want})")
    print(f"  pedal: {[nn for _b, nn in ped]}  "
          f"(G until beat {final_beat}, then C) "
          + ("OK" if ok else "FAIL"))

    chords = [(e["beat"], e["data"].get("pitches", []))
              for e in parts["chords"]]
    pre, cad64 = chords[6][1], chords[7][1]   # bVI -> I(6/4) seam
    # MForce note numbers are octave*12 + pc: Ab4=56, G4=55, Gb5=66, G5=67.
    seam_ok = (bool(pre) and bool(cad64)
               and pre[0] == 56 and cad64[0] == 55       # bass Ab4 -> G4
               and cad64[-1] % 12 == 7                   # top is G
               and cad64[-1] == cad64[0] + 12)           # doubled bass on top
    if cadence == "ger6":
        seam_ok = seam_ok and pre[-1] % 12 == 6          # Ger6 top is Gb
    if not seam_ok:
        ok = False
    print(f"  seam: bVI {pre} -> I(6/4) {cad64}  "
          + ("OK" if seam_ok else "FAIL"))

    peak = wav_peak(REPO / (prefix + "_1.wav"))
    if peak <= 0.1:
        ok = False
    print(f"  peak: {peak:.3f}  " + ("OK" if peak > 0.1 else "FAIL (silent?)"))
    return ok, chords


def main():
    all_ok = True
    for cadence in ("ger6", "neap"):
        for k, per_chord in enumerate((4.0, 8.0)):
            t, meta = bruckner2(None, None, seed=4242 + 1000 * k,
                                cadence=cadence, per_chord=per_chord)
            prefix = f"{OUTROOT}/bruckner2_{cadence}_{k}"
            render_template(t, prefix)
            print(f"{prefix}  per_chord={per_chord} beats={meta['beats']} "
                  f"({meta['progression']})")
            ok, chords = verify(prefix, per_chord, cadence)
            all_ok = all_ok and ok
            for beat, pitches in chords:
                print(f"    beat {beat:5.1f}: {pitches}")
    print("ALL OK" if all_ok else "FAILURES — see above")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
