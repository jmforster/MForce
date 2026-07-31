"""Tokenize the Essen Folksong Collection (Humdrum **kern) for the Markov bake-off.

corpus/essen-folksong-collection holds ~8.5k monophonic **kern folk melodies
(europa/deutschl dominates, plus asia/china etc.). This script parses each
.krn file directly (no music21 dependency): kern note tokens -> notes =
[(onset, midi, dur)] in tick units (DIV=480 per quarter), key from the *X:
tonic interpretation (uppercase = major, lowercase = minor) converted to a
MIDI-style (sf, mi) pair, then fed through markov_tokenize.tokenize_notes —
emitting essen_tokens.json in the exact markov_tokens.json schema so
markov_model / figuregen / bake_off run unchanged via --tokens.

Kern subset handled (verified by corpus-wide vocabulary scan):
  durations : integer N = whole/N (4=quarter, 8=eighth, 12=triplet-8th, ...),
              0 = breve, 00 = longa, dots multiply (1.5, 1.75, ...)
  pitches   : letter case/repetition octaves (c=C4, cc=C5, C=C3, CC=C2),
              accidentals # / - (cumulative), n = natural
  rests     : any token whose pitch field is r — advances time only
  ties      : [ open, _ continue, ] close — merged into one note
  ignored   : barlines (=..), phrase/slur marks {}(), fermatas etc.
Multi-spine files (3 in corpus) and files without a tonic are skipped.

Usage:
  python essen_tokenize.py            # -> essen_tokens.json
  python essen_tokenize.py --spot 3   # print kern source next to decoded notes
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from markov_tokenize import tokenize_notes, MIN_NOTES      # noqa: E402

KERN_ROOT = HERE.parent / "essen-folksong-collection"
OUT = HERE / "essen_tokens.json"
DIV = 480  # ticks per quarter fed to tokenize_notes

# Tonic name -> sf (circle-of-fifths key signature), per prep_groundtruth's
# KEYNAME_MAJOR / KEYNAME_MINOR tables (kern uses '-' for flat).
MAJ_SF = {"C-": -7, "G-": -6, "D-": -5, "A-": -4, "E-": -3, "B-": -2, "F": -1,
          "C": 0, "G": 1, "D": 2, "A": 3, "E": 4, "B": 5, "F#": 6, "C#": 7}
MIN_SF = {"A-": -7, "E-": -6, "B-": -5, "F": -4, "C": -3, "G": -2, "D": -1,
          "A": 0, "E": 1, "B": 2, "F#": 3, "C#": 4, "G#": 5, "D#": 6, "A#": 7}

LETTER_PC = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}

TONIC_RE = re.compile(r"^\*([A-Ga-g])([#-]?):")
DUR_RE = re.compile(r"(\d+)(\.*)")
PITCH_RE = re.compile(r"([a-gA-G])\1*|r")


class KernError(Exception):
    pass


def tonic_to_keysig(letter, acc):
    """Kern tonic (letter, accidental) -> (sf, mi) as MIDI keysig."""
    name = letter.upper() + acc
    if letter.isupper():
        sf = MAJ_SF.get(name)
        mi = 0
    else:
        sf = MIN_SF.get(name)
        mi = 1
    if sf is None:
        raise KernError(f"unknown tonic {letter}{acc}")
    return sf, mi


def parse_token(tok):
    """One kern note/rest token -> (dur_quarters, midi_or_None, tie_open, tie_mid, tie_close).
    midi is None for rests. Raises KernError on anything unrecognized."""
    m = DUR_RE.search(tok)
    if not m:
        raise KernError(f"no duration in {tok!r}")
    n, dots = m.group(1), len(m.group(2))
    if n == "0":
        dur = 8.0
    elif n == "00":
        dur = 16.0
    else:
        dur = 4.0 / int(n)
    dur *= 2.0 - 0.5 ** dots

    pm = PITCH_RE.search(tok)
    if not pm:
        raise KernError(f"no pitch in {tok!r}")
    if pm.group(0).startswith("r"):
        midi = None
    else:
        letters = pm.group(0)
        pc = LETTER_PC[letters[0].lower()]
        n_letters = len(letters)
        if letters[0].islower():          # c = C4 (60), cc = C5 ...
            midi = 60 + 12 * (n_letters - 1) + pc
        else:                             # C = C3 (48), CC = C2 ...
            midi = 60 - 12 * n_letters + pc
        rest = tok[pm.end():]
        midi += rest.count("#") - rest.count("-")   # 'n' natural adds 0
    return dur, midi, "[" in tok, "_" in tok, "]" in tok


def parse_kern(path):
    """One .krn file -> (notes, keysig). notes = [(onset_ticks, midi, dur_ticks)].
    Raises KernError with a categorizable message on unsupported files."""
    text = path.read_text(encoding="latin-1")
    keysig = None
    notes = []
    t = 0.0           # quarters
    tie = None        # (index into notes) while a tie is open
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("!"):
            continue
        if "\t" in line:
            raise KernError("multi-spine")
        if line.startswith("*"):
            tm = TONIC_RE.match(line)
            if tm and keysig is None:
                keysig = tonic_to_keysig(tm.group(1), tm.group(2))
            continue
        if line.startswith("="):
            continue
        if line == ".":
            continue
        dur, midi, t_open, t_mid, t_close = parse_token(line)
        if midi is None:                       # rest: advance clock, break any tie
            t += dur
            tie = None
            continue
        if (t_mid or t_close) and tie is not None and notes[tie][1] == midi:
            o, p, d = notes[tie]
            notes[tie] = (o, p, d + dur * DIV)
            if t_close and not t_open:
                tie = None
        else:
            notes.append((t * DIV, midi, dur * DIV))
            tie = len(notes) - 1 if (t_open or t_mid) else None
        if t_open:
            tie = len(notes) - 1
        t += dur
    if keysig is None:
        raise KernError("no tonic")
    return notes, keysig


def decode_for_spotcheck(path):
    notes, keysig = parse_kern(path)
    print(f"--- {path.name}  keysig(sf,mi)={keysig}")
    print(path.read_text(encoding="latin-1"))
    names = "C C# D D# E F F# G G# A A# B".split()
    for onset, midi, dur in notes:
        print(f"  onset={onset / DIV:7.3f}q  {names[midi % 12]}{midi // 12 - 1}"
              f" (midi {midi})  dur={dur / DIV:.4g}q")


def main():
    if "--spot" in sys.argv:
        k = int(sys.argv[sys.argv.index("--spot") + 1])
        picks = [
            KERN_ROOT / "europa" / "deutschl" / "allerkbd" / "deut3663.krn",
            KERN_ROOT / "europa" / "deutschl" / "erk" / "deut0567.krn",
            KERN_ROOT / "asia" / "china" / "han" / "han0330.krn",
        ][:k]
        for p in picks:
            decode_for_spotcheck(p)
        return

    all_streams, all_pulse0, all_starts = [], [], []
    step_set, pulse_set = set(), set()
    n_ok = n_skip = 0
    skip_reasons = {}

    def skip(key):
        nonlocal n_skip
        n_skip += 1
        skip_reasons[key] = skip_reasons.get(key, 0) + 1

    for f in sorted(KERN_ROOT.rglob("*.krn")):
        try:
            notes, keysig = parse_kern(f)
            if len(notes) < MIN_NOTES:
                skip("too few notes")
                continue
            streams, pulse0, reason = tokenize_notes(notes, keysig, DIV)
        except KernError as e:
            skip(str(e).split("(")[0].strip()[:40])
            continue
        except Exception as e:  # noqa: BLE001 — malformed file, count and continue
            skip(f"exception: {type(e).__name__}")
            continue
        if reason:
            skip(reason.split("(")[0].strip())
            continue
        n_ok += 1
        all_streams.extend(streams)
        all_pulse0.extend(pulse0)
        for s in streams:
            all_starts.append(s[1])
            pulse_set.add(s[0][1])
            for (d, p) in s[1:]:
                step_set.add(d)
                pulse_set.add(p)

    n_tokens = sum(len(s) - 1 for s in all_streams)
    out = {
        "alphabet": {"steps": sorted(step_set), "pulses": sorted(pulse_set)},
        "streams": all_streams,
        "starts": all_starts,
        "pulse0": sorted(set(all_pulse0)),
        "pulse0_all": all_pulse0,
        "stats": {
            "tunes_ok": n_ok, "tunes_skipped": n_skip,
            "streams": len(all_streams), "transition_tokens": n_tokens,
            "skip_reasons": skip_reasons,
        },
    }
    OUT.write_text(json.dumps(out))
    print(f"ok={n_ok} skip={n_skip} streams={len(all_streams)} "
          f"tokens={n_tokens} -> {OUT.name}")
    print("skip reasons:", skip_reasons)


if __name__ == "__main__":
    main()
