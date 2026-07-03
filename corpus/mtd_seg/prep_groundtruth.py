"""Generate ground-truth markup template for MTD themes.

Reads MIDI + manifest metadata, emits a text file ready for hand-marking
figure boundaries. Mark a boundary by inserting a line that starts with `|`
between two note lines.

Usage:
    python prep_groundtruth.py <MTDID> [<MTDID>...]

e.g.:
    python prep_groundtruth.py 5753 5754 5755 0389
"""
import csv, struct, sys, pathlib

ROOT      = pathlib.Path(__file__).resolve().parent
MTD_FULL  = ROOT.parent / "mtd_full"
MIDI_DIR  = MTD_FULL / "midi"
MANIFEST  = MTD_FULL / "manifest.csv"
OUT_DIR   = ROOT / "ground_truth"
OUT_DIR.mkdir(parents=True, exist_ok=True)

NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
MAJOR_PC   = [0, 2, 4, 5, 7, 9, 11]
MINOR_PC   = [0, 2, 3, 5, 7, 8, 10]  # natural minor

KEYNAME_MAJOR = {-7:"Cb",-6:"Gb",-5:"Db",-4:"Ab",-3:"Eb",-2:"Bb",-1:"F",
                  0:"C", 1:"G", 2:"D", 3:"A", 4:"E", 5:"B", 6:"F#",7:"C#"}
TONIC_PC_MAJOR = {-7:11,-6:6,-5:1,-4:8,-3:3,-2:10,-1:5,
                   0:0, 1:7, 2:2, 3:9, 4:4, 5:11, 6:6, 7:1}
KEYNAME_MINOR = {-7:"Ab",-6:"Eb",-5:"Bb",-4:"F",-3:"C",-2:"G",-1:"D",
                  0:"A", 1:"E", 2:"B", 3:"F#",4:"C#",5:"G#",6:"D#",7:"A#"}

def pitch_name(midi):
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"

def read_vlq(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not (c & 0x80):
            return v, i

def parse_midi(path):
    """Returns dict: ticks_per_quarter, keysig=(sf,mi), timesig=(num,den),
    timesig_changes=[(tick,num,den)], notes=[(onset_tick, pitch, dur_tick)]."""
    b = path.read_bytes()
    assert b[:4] == b"MThd"
    fmt, ntr, div = struct.unpack(">HHH", b[8:14])
    out = {"ticks_per_quarter": div, "keysig": (0, 0),
           "timesig": (4, 4), "timesig_changes": [], "notes": []}
    i = 14
    while i < len(b):
        if b[i:i+4] != b"MTrk":
            break
        tlen = struct.unpack(">I", b[i+4:i+8])[0]
        end = i + 8 + tlen
        i += 8
        t = 0; on = {}; last_status = 0
        while i < end:
            dt, i = read_vlq(b, i); t += dt
            s = b[i]
            if s & 0x80:
                last_status = s; i += 1
            else:
                s = last_status
            if s == 0xFF:
                meta = b[i]; i += 1
                ml, i = read_vlq(b, i)
                payload = b[i:i+ml]; i += ml
                if meta == 0x58 and ml >= 2:
                    num, den = payload[0], 2 ** payload[1]
                    out["timesig_changes"].append((t, num, den))
                    if t == 0:
                        out["timesig"] = (num, den)
                elif meta == 0x59 and ml == 2:
                    sf = payload[0] if payload[0] < 128 else payload[0] - 256
                    out["keysig"] = (sf, payload[1])
                elif meta == 0x2F:
                    break
            elif s in (0xF0, 0xF7):
                ml, i = read_vlq(b, i); i += ml
            else:
                hi = s & 0xF0
                if hi == 0x90:
                    p, v = b[i], b[i+1]; i += 2
                    if v:
                        on[p] = t
                    else:
                        if p in on:
                            out["notes"].append((on[p], p, t - on[p]))
                            del on[p]
                elif hi == 0x80:
                    p, v = b[i], b[i+1]; i += 2
                    if p in on:
                        out["notes"].append((on[p], p, t - on[p]))
                        del on[p]
                elif hi in (0xA0, 0xB0, 0xE0):
                    i += 2
                elif hi in (0xC0, 0xD0):
                    i += 1
        i = end
    out["notes"].sort()
    return out

def degree_and_accidental(pc, tonic_pc, mode):
    """Return (degree 1-7, accidental in {-1, 0, +1}). Raise-first heuristic
    (C# = #1, F# = #4, etc.)."""
    scale = MAJOR_PC if mode == 0 else MINOR_PC
    rel = (pc - tonic_pc) % 12
    if rel in scale:
        return scale.index(rel) + 1, 0
    if (rel - 1) % 12 in scale:
        return scale.index((rel - 1) % 12) + 1, +1
    if (rel + 1) % 12 in scale:
        return scale.index((rel + 1) % 12) + 1, -1
    return None, None

def load_manifest_row(mtdid):
    if not MANIFEST.exists():
        return None
    with MANIFEST.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("MTDID") == mtdid:
                return row
    return None

def build_markup(mtdid):
    midi_path = MIDI_DIR / f"MTD{mtdid}_score.mid"
    if not midi_path.exists():
        midi_path = MIDI_DIR / f"MTD{mtdid}_edm-corr.mid"
    if not midi_path.exists():
        return f"# ERROR: no MIDI for MTD{mtdid}\n"

    mid = parse_midi(midi_path)
    row = load_manifest_row(mtdid) or {}

    sf, mi = mid["keysig"]
    if mi == 0:
        tonic_pc = TONIC_PC_MAJOR.get(sf, 0)
        keyname  = f"{KEYNAME_MAJOR.get(sf, '?')} major"
    else:
        tonic_pc = (TONIC_PC_MAJOR.get(sf, 0) - 3) % 12
        keyname  = f"{KEYNAME_MINOR.get(sf, '?')} minor"

    num, den = mid["timesig"]
    div = mid["ticks_per_quarter"]
    multi_ts = len(mid["timesig_changes"]) > 1

    composer  = row.get("ComposerID", "?")
    work      = row.get("WorkID", "?")
    title     = row.get("WorkTitle", "?")
    polyphony = row.get("Polyphony", "?")
    instr     = row.get("ThemeInstruments", "?")

    lines = []
    lines.append(f"# MTD{mtdid}  {composer}  {work}  {title}")
    lines.append(f"# {keyname}  {num}/{den}  {polyphony}  {instr}")
    if multi_ts:
        lines.append(f"# WARNING: multiple timesig changes — bar:beat may drift")
    lines.append(f"# SVG/score: corpus/mtd_full/html/MTD{mtdid}.html")
    lines.append(f"# MIDI: corpus/mtd_full/midi/{midi_path.name}")
    lines.append(f"#")
    lines.append(f"# Mark figure boundaries: insert a line starting with | between two note lines.")
    lines.append(f"# Comments after | are fine, e.g.: | end of bar 1")
    lines.append(f"# Pickup note: if bar:beat doesn't match the SVG, MTD may have placed the")
    lines.append(f"# pickup at t>0; cross-reference with the score image.")
    lines.append(f"# Columns: idx  pitch  dur(beats)  ioi(beats)  step-from-prev(acc)  beat   bar:beat")
    lines.append(f"# (IOI = inter-onset interval to next note. Reflects felt rhythm even when")
    lines.append(f"#  notes are played detached/staccato. Last note's IOI = its duration.)")
    lines.append(f"#")

    beats_per_bar = num * (4.0 / den)  # in quarter-note beats
    prev_step = None  # absolute scale-step index from MIDI-0 tonic
    notes = mid["notes"]

    for idx, (onset_tick, pitch, dur_tick) in enumerate(notes):
        onset_beats = onset_tick / div
        dur_beats   = dur_tick   / div
        if idx + 1 < len(notes):
            ioi_beats = (notes[idx + 1][0] - onset_tick) / div
        else:
            ioi_beats = dur_beats
        deg, acc = degree_and_accidental(pitch % 12, tonic_pc, mi)
        if deg is None:
            scale_step = None
            acc_str = "?"
        else:
            tonic_midi_octave = (pitch - ((pitch - tonic_pc - acc) % 12)) // 12
            scale_step = 7 * tonic_midi_octave + (deg - 1)
            acc_str = {-1: "b", 0: " ", 1: "#"}[acc]

        if scale_step is None or prev_step is None:
            step_str = "  - "
        else:
            d = scale_step - prev_step
            step_str = f"{d:+3d} "
        if scale_step is not None:
            prev_step = scale_step

        bar = int(onset_beats // beats_per_bar) + 1
        beat_in_bar = onset_beats - (bar - 1) * beats_per_bar + 1.0

        pn = pitch_name(pitch)
        lines.append(f"  {idx:3d}  {pn:4s}  {dur_beats:5.2f}  {ioi_beats:5.2f}   {step_str}{acc_str}   {onset_beats:6.2f}   {bar}:{beat_in_bar:.2f}")

    return "\n".join(lines) + "\n"

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for arg in sys.argv[1:]:
        mtdid = f"{int(arg):04d}"
        out = OUT_DIR / f"MTD{mtdid}.txt"
        text = build_markup(mtdid)
        out.write_text(text, encoding="utf-8")
        print(f"wrote {out.relative_to(ROOT)}")

if __name__ == "__main__":
    main()
