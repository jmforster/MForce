"""Extract Figures (PulseSequence + StepSequence + chromatic sidecar) from
hand-segmented MTD themes.

Reads:
  ground_truth_json/MTD####.json   (boundary indices)
  ground_truth/MTD####_score.mid   (or corpus/mtd_full/midi/ as fallback)
  ../mtd_full/manifest.csv         (one row per theme)

Writes:
  figures/MTD####.json             (one file per theme)

Usage:
  python extract.py                 # all themes with ground_truth_json/ entries
  python extract.py 5755 0389 ...   # specific MTDIDs
"""
import csv, json, sys, pathlib

from prep_groundtruth import (
    parse_midi, MAJOR_PC, MINOR_PC, TONIC_PC_MAJOR, KEYNAME_MAJOR, KEYNAME_MINOR,
)

ROOT      = pathlib.Path(__file__).resolve().parent
GT_JSON   = ROOT / "ground_truth_json"
GT_MIDI   = ROOT / "ground_truth"
FULL_MIDI = ROOT.parent / "mtd_full" / "midi"
MANIFEST  = ROOT.parent / "mtd_full" / "manifest.csv"
OUT_DIR   = ROOT / "figures"
OUT_DIR.mkdir(parents=True, exist_ok=True)

EXTRACTION_VERSION = "1.0"

# Pulse quantization grid
GRID = sorted([
    4.0, 2.0, 1.0, 0.5, 0.25, 0.125,                # plain
    3.0, 1.5, 0.75, 0.375,                          # dotted
    2.0/3, 1.0/3, 1.0/6,                            # triplets
])
GRID_TOL = 0.05  # beats

WARN_FIGURE_NOTE_COUNT = 8


def snap_pulse(beats):
    """Return (snapped_value, off_grid_distance). off_grid_distance > GRID_TOL means warn."""
    nearest = min(GRID, key=lambda g: abs(g - beats))
    return nearest, abs(nearest - beats)


def degree_and_accidental(pc, tonic_pc, mode):
    """Return (diatonic_degree 1-7, accidental ∈ {-1, 0, +1}) — raise-first."""
    scale = MAJOR_PC if mode == 0 else MINOR_PC
    rel = (pc - tonic_pc) % 12
    if rel in scale:
        return scale.index(rel) + 1, 0
    if (rel - 1) % 12 in scale:
        return scale.index((rel - 1) % 12) + 1, +1
    if (rel + 1) % 12 in scale:
        return scale.index((rel + 1) % 12) + 1, -1
    return None, None


def scale_step_index(midi_pitch, tonic_pc, mode, accidental):
    """Convert MIDI pitch to absolute scale-degree-step index (7 per octave above
    MIDI-0 tonic)."""
    deg, _ = degree_and_accidental(midi_pitch % 12, tonic_pc, mode)
    if deg is None:
        return None
    rel_pc = (midi_pitch % 12 - tonic_pc - accidental) % 12
    tonic_midi_pos = midi_pitch - rel_pc
    octaves_above_minus1 = (tonic_midi_pos - tonic_pc) // 12
    return 7 * octaves_above_minus1 + (deg - 1)


def load_manifest_row(mtdid):
    if not MANIFEST.exists():
        return {}
    with MANIFEST.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("MTDID") == mtdid:
                return row
    return {}


def find_midi(mtdid):
    """Prefer the copy sitting next to the markup; fall back to mtd_full/midi/."""
    p = GT_MIDI / f"MTD{mtdid}_score.mid"
    if p.exists():
        return p
    p = FULL_MIDI / f"MTD{mtdid}_score.mid"
    if p.exists():
        return p
    p = FULL_MIDI / f"MTD{mtdid}_edm-corr.mid"
    if p.exists():
        return p
    return None


def extract_theme(mtdid):
    """Return dict ready for JSON serialization, or raise on hard error."""
    gt_path = GT_JSON / f"MTD{mtdid}.json"
    if not gt_path.exists():
        raise FileNotFoundError(f"missing ground truth: {gt_path}")
    midi_path = find_midi(mtdid)
    if midi_path is None:
        raise FileNotFoundError(f"missing MIDI for MTD{mtdid}")

    gt = json.loads(gt_path.read_text(encoding="utf-8"))
    boundaries = gt["boundaries"]   # list of "first-note-of-figure-i+1" indices
    note_count_gt = gt["note_count"]

    mid = parse_midi(midi_path)
    notes = mid["notes"]
    if len(notes) != note_count_gt:
        raise ValueError(
            f"MTD{mtdid}: ground truth note_count={note_count_gt} but MIDI has {len(notes)}"
        )
    if len(mid["timesig_changes"]) > 1:
        raise ValueError(f"MTD{mtdid}: multi-timesig themes are out of scope")

    sf, mi = mid["keysig"]
    tonic_pc = TONIC_PC_MAJOR.get(sf, 0)
    if mi == 1:
        tonic_pc = (tonic_pc - 3) % 12
        mode_name = "minor"
        root_name = KEYNAME_MINOR.get(sf, "?")
    else:
        mode_name = "major"
        root_name = KEYNAME_MAJOR.get(sf, "?")

    div = mid["ticks_per_quarter"]
    num, den = mid["timesig"]
    beats_per_bar = num * (4.0 / den)

    row = load_manifest_row(mtdid)

    # Validate boundaries
    for b in boundaries:
        if b <= 0 or b >= len(notes):
            raise ValueError(f"MTD{mtdid}: boundary {b} out of range (1..{len(notes)-1})")
    if len(set(boundaries)) != len(boundaries):
        raise ValueError(f"MTD{mtdid}: duplicate boundaries: {boundaries}")

    # Segment indices
    fig_starts = [0] + sorted(boundaries)
    fig_ends   = sorted(boundaries) + [len(notes)]   # exclusive

    # Pre-compute scale-step + accidental for each note
    note_steps = []
    for (_, p, _) in notes:
        deg, acc = degree_and_accidental(p % 12, tonic_pc, mi)
        if deg is None:
            note_steps.append((None, 0))
        else:
            note_steps.append((scale_step_index(p, tonic_pc, mi, acc), acc))

    figures_out = []
    prev_fig_last_diatonic_step = None

    for fi, (start, end) in enumerate(zip(fig_starts, fig_ends)):
        n = end - start
        warnings = []
        if n > WARN_FIGURE_NOTE_COUNT:
            warnings.append(f"figure has {n} notes — likely a phrase, not a figure")

        pulse = []
        step_seq = []
        chromatic = []
        first_note_diatonic_step = None
        for j, idx in enumerate(range(start, end)):
            onset_tick, pitch, dur_tick = notes[idx]

            # Pulse: IOI to next note (any figure), or duration if last note in theme.
            if idx + 1 < len(notes):
                ioi = (notes[idx + 1][0] - onset_tick) / div
            else:
                ioi = dur_tick / div
            snapped, off = snap_pulse(ioi)
            if off > GRID_TOL:
                warnings.append(f"pulse[{j}] off-grid by {off:.3f} (raw {ioi:.3f})")
            pulse.append(snapped)

            # Step + chromatic
            this_diatonic_step, this_acc = note_steps[idx]
            if this_diatonic_step is None:
                warnings.append(f"step[{j}] off-scale by ≥2 semitones; encoded as 0")
                step_val = 0
                acc_val = 0
            else:
                if j == 0:
                    step_val = 0
                    first_note_diatonic_step = this_diatonic_step
                else:
                    prev_diatonic_step, _ = note_steps[idx - 1]
                    if prev_diatonic_step is None:
                        step_val = 0
                    else:
                        step_val = this_diatonic_step - prev_diatonic_step
                acc_val = this_acc
            step_seq.append(step_val)
            chromatic.append(acc_val)

        # lead_step_from_prev: previous figure's last diatonic step → this figure's first
        if fi == 0 or prev_fig_last_diatonic_step is None or first_note_diatonic_step is None:
            lead_step = None
        else:
            lead_step = first_note_diatonic_step - prev_fig_last_diatonic_step

        # Update prev_fig_last_diatonic_step for the next iteration
        last_idx_in_fig = end - 1
        last_diatonic, _ = note_steps[last_idx_in_fig]
        prev_fig_last_diatonic_step = last_diatonic

        first_onset_beats = notes[start][0] / div
        bar = int(first_onset_beats // beats_per_bar) + 1
        beat_in_bar = first_onset_beats - (bar - 1) * beats_per_bar + 1.0

        total_beats = sum(pulse)

        figures_out.append({
            "id":                  f"MTD{mtdid}_f{fi}",
            "source_index":        fi,
            "num_notes":           n,
            "pulse":               [round(p, 6) for p in pulse],
            "step":                step_seq,
            "chromatic":           chromatic,
            "lead_step_from_prev": lead_step,
            "first_note_midi":     notes[start][1],
            "first_note_bar_beat": [bar, round(beat_in_bar, 4)],
            "total_beats":         round(total_beats, 6),
            "warnings":            warnings,
        })

    out = {
        "mtdid":              mtdid,
        "composer":           row.get("ComposerID", "?"),
        "work":               row.get("WorkID", "?"),
        "work_title":         row.get("WorkTitle", ""),
        "instrument":         row.get("ThemeInstruments", ""),
        "polyphony":          row.get("Polyphony", ""),
        "key":                {"root_name": root_name, "mode": mode_name, "sharps_flats": sf},
        "time_sig":           [num, den],
        "extraction_version": EXTRACTION_VERSION,
        "source": {
            "midi": str(midi_path.relative_to(ROOT.parent.parent).as_posix()),
            "html": f"corpus/mtd_full/html/MTD{mtdid}.html",
        },
        "total_notes":        len(notes),
        "num_figures":        len(figures_out),
        "figures":            figures_out,
    }
    return out


def main():
    targets = sys.argv[1:]
    if not targets:
        targets = sorted(p.stem.replace("MTD", "") for p in GT_JSON.glob("MTD*.json"))
    if not targets:
        print(f"no ground-truth JSON files in {GT_JSON}", file=sys.stderr)
        sys.exit(1)

    ok = err = 0
    for arg in targets:
        mtdid = f"{int(arg):04d}" if arg.isdigit() else arg.replace("MTD", "").rstrip(".json")
        try:
            data = extract_theme(mtdid)
        except Exception as e:
            print(f"  ERROR MTD{mtdid}: {e}", file=sys.stderr)
            err += 1
            continue
        out_path = OUT_DIR / f"MTD{mtdid}.json"
        out_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        n_warn = sum(1 for f in data["figures"] if f["warnings"])
        print(f"  MTD{mtdid}: {data['num_figures']} figures ({n_warn} with warnings)")
        ok += 1

    print(f"\nextracted {ok} themes, {err} errors")
    if err:
        sys.exit(1)


if __name__ == "__main__":
    main()
