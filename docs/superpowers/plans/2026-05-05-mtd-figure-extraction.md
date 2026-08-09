# MTD Figure Extraction Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the manual-segmentation pipeline (extract → pool → audition) and wire a `RealMotifSlotFiller` into `LibraryPassageStrategy` so Pattern slots can be filled with real MTD-derived figures instead of RFB-generated ones.

**Architecture:** Three Python utilities (stdlib only) + one C++ header + targeted edits to two existing engine headers. Per-theme JSON output keeps the data human-readable and traceable. Three flat pool files (paired / pulse-only / step-only) decorate each entry with provenance so strategies can filter by composer/era/instrument without joining tables.

**Tech Stack:** Python 3 (stdlib only), C++20 header-only engine, MSVC via CMake at `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

**CWD assumption:** all `mforce_cli` and `test_figures` runs assume CWD = `C:/@dev/repos/mforce` (the engine reads `corpus/mtd_seg/pools/paired.json` via that relative path). Commands that need this prepend `cd C:/@dev/repos/mforce &&` explicitly.

**Spec:** `docs/superpowers/specs/2026-05-05-mtd-figure-extraction-design.md`

**Predecessor commits already on `main`:**
- Pattern Library + LibraryPassageStrategy through `0017f2d`
- Curation tooling through this session: `pick_curated.py`, `prep_groundtruth.py`, `parse_groundtruth.py`, `curated.json`, `ground_truth/MTD####.txt` for the 20 selected themes plus their `_score.mid` siblings.

---

## Pending decisions (from spec, locked-in defaults)

| ID | Topic | Default this plan executes against |
|---|---|---|
| D1 | Chromatic encoding | Sidecar (`chromatic[]` parallel to `step[]`). Slot-filler IGNORES chromatic in Phase 1; sidecar is preserved in the pool data for later. |
| D2 | Figure-too-long warn threshold | `> 8 notes` → emit but warn |
| D3 | Pulse quantization tolerance | `ε = 0.05 beats` |
| D4 | Last-note-of-theme pulse fallback | Played duration |
| D5 | `extract.py` re-run policy | Always re-extract (idempotent) |
| D6 | Audition output | `.mid` only Phase 1 |

If any decision changes before execution: D1 chromatic flip would touch Tasks 1, 6, 7. D2/D3 are constants in extract.py only. D5 changing to caching changes Task 1 step 3 only. D6 changing to .wav adds renderer integration to Task 3.

---

## File Structure

| Path | Action | Responsibility |
|---|---|---|
| `corpus/mtd_seg/extract.py` | NEW | Per-theme MIDI + boundary list → `figures/MTD####.json` |
| `corpus/mtd_seg/build_pools.py` | NEW | Aggregate `figures/*.json` → `pools/{pulse,step,paired}.json` |
| `corpus/mtd_seg/audition.py` | NEW | One pool entry → short `.mid` for ear-checking |
| `corpus/mtd_seg/figures/MTD####.json` | GENERATED | Per-theme figures (committed for traceability) |
| `corpus/mtd_seg/pools/{pulse,step,paired}.json` | GENERATED | Flat libraries (committed) |
| `corpus/mtd_seg/auditions/*.mid` | GENERATED | Ad-hoc; gitignored |
| `engine/include/mforce/music/templates.h` | MODIFY | Add `LibraryPassageConfig::motifSource` enum + field |
| `engine/include/mforce/music/templates_json.h` | MODIFY | JSON round-trip for the new enum |
| `engine/include/mforce/music/real_motif_slot_filler.h` | NEW | Pool loader + filtered random pick → `MelodicFigure` |
| `engine/include/mforce/music/library_passage_strategy.h` | MODIFY | Switch slot fill on `motifSource` |
| `tools/test_figures/main.cpp` | MODIFY | Integration tests for the real-motif slot-fill path |
| `corpus/mtd_seg/.gitignore` | NEW | Ignore `auditions/` |

---

## Task 1: Create `extract.py`

**Files:**
- Create: `corpus/mtd_seg/extract.py`
- Read: `corpus/mtd_seg/ground_truth_json/MTD####.json`, `corpus/mtd_seg/ground_truth/MTD####_score.mid` (or `corpus/mtd_full/midi/MTD####_score.mid` as fallback), `corpus/mtd_full/manifest.csv`
- Write: `corpus/mtd_seg/figures/MTD####.json`

- [ ] **Step 1: Create `extract.py` with the full content below.**

```python
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
```

- [ ] **Step 2: Smoke-test on a synthetic ground-truth (no real hand-marks yet).**

Generate a temporary ground-truth JSON for MTD5755 with a hand-crafted boundary, run extract, verify the output structure, then clean up.

```bash
python -c "
import json, pathlib
gt = pathlib.Path('C:/@dev/repos/mforce/corpus/mtd_seg/ground_truth_json')
gt.mkdir(parents=True, exist_ok=True)
(gt / 'MTD5755.json').write_text(json.dumps({
    'mtdid': '5755',
    'note_count': 11,
    'boundaries': [5, 9],
    'figure_count': 3,
    'issues': []
}) + '\n')
"
python C:/@dev/repos/mforce/corpus/mtd_seg/extract.py 5755
```

Expected: prints `MTD5755: 3 figures (...)` and writes `corpus/mtd_seg/figures/MTD5755.json`.

- [ ] **Step 3: Inspect the output.**

```bash
cat C:/@dev/repos/mforce/corpus/mtd_seg/figures/MTD5755.json | python -m json.tool | head -80
```

Verify:
- `mtdid == "5755"`, `composer == "Mozart"`, `key.root_name == "F"`, `key.mode == "major"`.
- `num_figures == 3`, `total_notes == 11`.
- Each figure has `id` like `MTD5755_f0`, has `pulse`/`step`/`chromatic` arrays of equal length matching `num_notes`.
- `figures[0].step[0] == 0`.
- `figures[0].lead_step_from_prev` is `null`; `figures[1].lead_step_from_prev` is an integer.
- The chromatic note (F♯ at theme position 9 — index 9 maps into figure 2 at local index 0 because boundaries=[5,9]) has `chromatic` entry `+1`.

If any of those fail: read the failing-case output, fix the bug in `extract.py`, re-run.

- [ ] **Step 4: Clean up the synthetic ground-truth file.**

```bash
rm C:/@dev/repos/mforce/corpus/mtd_seg/ground_truth_json/MTD5755.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/figures/MTD5755.json
```

(We'll regenerate once Matt does the real hand-marking.)

- [ ] **Step 5: Commit.**

```bash
git -C C:/@dev/repos/mforce add corpus/mtd_seg/extract.py
git -C C:/@dev/repos/mforce commit -m "feat(mtd_seg): add extract.py — segment MTD themes into figures"
```

---

## Task 2: Create `build_pools.py`

**Files:**
- Create: `corpus/mtd_seg/build_pools.py`
- Read: `corpus/mtd_seg/figures/*.json`
- Write: `corpus/mtd_seg/pools/{pulse,step,paired}.json`

- [ ] **Step 1: Create `build_pools.py` with the full content below.**

```python
"""Aggregate per-theme figure JSON files into three flat pool files.

Reads:
  figures/MTD####.json (one entry per theme)

Writes:
  pools/pulse.json    — flat list, each entry has pulse + total_beats + num_notes + source
  pools/step.json     — flat list, each entry has step + chromatic + num_notes + source
  pools/paired.json   — flat list, full Figure data + source

Each entry's `id` is stable across the three pools so they can be cross-referenced.

Usage:
  python build_pools.py
"""
import json, pathlib

ROOT     = pathlib.Path(__file__).resolve().parent
FIG_DIR  = ROOT / "figures"
POOL_DIR = ROOT / "pools"
POOL_DIR.mkdir(parents=True, exist_ok=True)


def source_block(theme):
    return {
        "mtdid":      theme["mtdid"],
        "composer":   theme["composer"],
        "work":       theme["work"],
        "work_title": theme["work_title"],
        "key_mode":   theme["key"]["mode"],
        "instrument": theme["instrument"],
        "polyphony":  theme["polyphony"],
    }


def main():
    pulse_pool  = []
    step_pool   = []
    paired_pool = []

    files = sorted(FIG_DIR.glob("MTD*.json"))
    if not files:
        print(f"no figure files in {FIG_DIR}")
        return

    for fp in files:
        theme = json.loads(fp.read_text(encoding="utf-8"))
        src = source_block(theme)
        for fig in theme["figures"]:
            common = {
                "id":          fig["id"],
                "num_notes":   fig["num_notes"],
                "total_beats": fig["total_beats"],
                "source":      src,
            }
            pulse_pool.append({**common, "pulse": fig["pulse"]})
            step_pool.append({**common, "step": fig["step"], "chromatic": fig["chromatic"]})
            paired_pool.append({
                **common,
                "pulse":     fig["pulse"],
                "step":      fig["step"],
                "chromatic": fig["chromatic"],
            })

    (POOL_DIR / "pulse.json").write_text(json.dumps(pulse_pool, indent=2) + "\n", encoding="utf-8")
    (POOL_DIR / "step.json").write_text(json.dumps(step_pool, indent=2) + "\n", encoding="utf-8")
    (POOL_DIR / "paired.json").write_text(json.dumps(paired_pool, indent=2) + "\n", encoding="utf-8")

    print(f"wrote pools/pulse.json   ({len(pulse_pool)} entries)")
    print(f"wrote pools/step.json    ({len(step_pool)} entries)")
    print(f"wrote pools/paired.json  ({len(paired_pool)} entries)")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Smoke-test by re-running the synthetic extract from Task 1 then build_pools.**

```bash
python -c "
import json, pathlib
gt = pathlib.Path('C:/@dev/repos/mforce/corpus/mtd_seg/ground_truth_json')
gt.mkdir(parents=True, exist_ok=True)
(gt / 'MTD5755.json').write_text(json.dumps({
    'mtdid': '5755',
    'note_count': 11,
    'boundaries': [5, 9],
    'figure_count': 3,
    'issues': []
}) + '\n')
"
python C:/@dev/repos/mforce/corpus/mtd_seg/extract.py 5755
python C:/@dev/repos/mforce/corpus/mtd_seg/build_pools.py
```

Expected: prints `wrote pools/pulse.json (3 entries)` etc. for all three pools.

- [ ] **Step 3: Inspect pool output.**

```bash
cat C:/@dev/repos/mforce/corpus/mtd_seg/pools/paired.json | python -m json.tool | head -30
```

Verify:
- 3 entries.
- All three have `source.composer == "Mozart"` and `source.work == "KV467-02"`.
- Each entry has `pulse`, `step`, `chromatic`, `num_notes`, `total_beats`, `source`, `id`.

- [ ] **Step 4: Clean up the synthetic data.**

```bash
rm C:/@dev/repos/mforce/corpus/mtd_seg/ground_truth_json/MTD5755.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/figures/MTD5755.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/pools/pulse.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/pools/step.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/pools/paired.json
```

- [ ] **Step 5: Commit.**

```bash
git -C C:/@dev/repos/mforce add corpus/mtd_seg/build_pools.py
git -C C:/@dev/repos/mforce commit -m "feat(mtd_seg): add build_pools.py — aggregate figures into three flat pools"
```

---

## Task 3: Create `audition.py`

**Files:**
- Create: `corpus/mtd_seg/audition.py`
- Create: `corpus/mtd_seg/.gitignore` (ignores `auditions/`)
- Read: `corpus/mtd_seg/pools/paired.json`
- Write: `corpus/mtd_seg/auditions/<id>.mid`

- [ ] **Step 1: Create `audition.py` with the full content below.**

```python
"""Audition a single Figure from the pool by emitting a short .mid file.

Reads:
  pools/paired.json

Writes:
  auditions/<id>.mid   (single-track monophonic MIDI)

Usage:
  python audition.py MTD5755_f0          # specific figure
  python audition.py --random            # random pick from pool
  python audition.py --composer Mozart   # random Mozart figure
  python audition.py --instrument Piano  # random piano figure

Notes:
  - Tempo is fixed at 120 BPM.
  - Anchor pitch is fixed at C4 (MIDI 60). The figure's `step[0]` is 0 by
    convention; subsequent steps walk a major scale relative to the anchor.
  - Phase 1: chromatic sidecar is IGNORED. The audition shows the diatonic
    skeleton only — chromatic round-trip is part of the engine integration
    (Tasks 5–8).
"""
import argparse, json, pathlib, random, struct, sys

ROOT      = pathlib.Path(__file__).resolve().parent
POOL_PATH = ROOT / "pools" / "paired.json"
OUT_DIR   = ROOT / "auditions"
OUT_DIR.mkdir(parents=True, exist_ok=True)

DEFAULT_BPM   = 120
DEFAULT_ANCHOR_MIDI = 60   # C4
TICKS_PER_QUARTER   = 480

# Major scale semitone offsets from tonic for degrees 0..6
MAJOR_SEMITONES = [0, 2, 4, 5, 7, 9, 11]


def load_pool():
    if not POOL_PATH.exists():
        print(f"missing pool: {POOL_PATH} (run build_pools.py first)", file=sys.stderr)
        sys.exit(1)
    return json.loads(POOL_PATH.read_text(encoding="utf-8"))


def find_entries(pool, args):
    if args.id:
        return [e for e in pool if e["id"] == args.id]
    candidates = pool
    if args.composer:
        candidates = [e for e in candidates if e["source"]["composer"] == args.composer]
    if args.instrument:
        candidates = [e for e in candidates if e["source"]["instrument"] == args.instrument]
    return candidates


def vlq_bytes(v):
    """Variable-length quantity encoding for MIDI delta times."""
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def step_to_midi(anchor_midi, cumulative_step):
    """Walk `cumulative_step` scale degrees (positive=up) from `anchor_midi` in
    a major scale rooted at the same pitch class as `anchor_midi`."""
    # Octave + degree decomposition
    if cumulative_step >= 0:
        octs, deg = divmod(cumulative_step, 7)
    else:
        # divmod handles negatives correctly: -8 → (-2, 6) so degree wraps
        octs, deg = divmod(cumulative_step, 7)
    return anchor_midi + 12 * octs + MAJOR_SEMITONES[deg]


def build_midi(figure, anchor_midi=DEFAULT_ANCHOR_MIDI, bpm=DEFAULT_BPM):
    """Return a complete MIDI file (format 1, 2 tracks) as bytes."""
    div = TICKS_PER_QUARTER

    pulses = figure["pulse"]
    steps  = figure["step"]
    if len(pulses) != len(steps):
        raise ValueError(f"pulse/step length mismatch in {figure['id']}")

    # Track 1 (meta): tempo + timesig + endtrack
    us_per_qn = int(60_000_000 / bpm)
    meta = bytearray()
    meta += b"\x00" + b"\xFF\x51\x03" + us_per_qn.to_bytes(3, "big")    # tempo
    meta += b"\x00" + b"\xFF\x58\x04\x04\x02\x18\x08"                    # 4/4
    meta += b"\x01" + b"\xFF\x2F\x00"                                    # endtrack
    meta_chunk = b"MTrk" + len(meta).to_bytes(4, "big") + bytes(meta)

    # Track 2 (notes): walk steps, emit note-on / note-off pairs
    notes_track = bytearray()
    cumulative_step = 0
    notes_track += b"\x00" + b"\xC0\x00"  # program change to 0 (acoustic grand)
    for k, (pulse, step) in enumerate(zip(pulses, steps)):
        cumulative_step += step
        pitch = step_to_midi(anchor_midi, cumulative_step)
        dur_ticks = max(1, int(round(pulse * div)) - 1)
        # Note on at delta 0 (relative to previous event)
        notes_track += vlq_bytes(0) + bytes([0x90, pitch & 0x7F, 80])
        # Note off after dur_ticks
        notes_track += vlq_bytes(dur_ticks) + bytes([0x80, pitch & 0x7F, 0])
    notes_track += b"\x01" + b"\xFF\x2F\x00"
    notes_chunk = b"MTrk" + len(notes_track).to_bytes(4, "big") + bytes(notes_track)

    # Header: format 1, 2 tracks, ticks/quarter
    header_body = struct.pack(">HHH", 1, 2, div)
    header = b"MThd" + len(header_body).to_bytes(4, "big") + header_body

    return header + meta_chunk + notes_chunk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id", nargs="?", help="figure id like MTD5755_f0")
    ap.add_argument("--random", action="store_true", help="random pick from pool")
    ap.add_argument("--composer", help="filter by composer")
    ap.add_argument("--instrument", help="filter by instrument")
    ap.add_argument("--anchor", type=int, default=DEFAULT_ANCHOR_MIDI,
                    help="anchor MIDI pitch (default 60 = C4)")
    ap.add_argument("--bpm", type=int, default=DEFAULT_BPM)
    args = ap.parse_args()

    pool = load_pool()
    candidates = find_entries(pool, args)
    if not candidates:
        print("no matching pool entries", file=sys.stderr)
        sys.exit(1)

    if args.id:
        entry = candidates[0]
    else:
        entry = random.choice(candidates)

    midi_bytes = build_midi(entry, anchor_midi=args.anchor, bpm=args.bpm)
    out_path = OUT_DIR / f"{entry['id']}.mid"
    out_path.write_bytes(midi_bytes)
    print(f"wrote {out_path.relative_to(ROOT)}  "
          f"({entry['num_notes']} notes, {entry['total_beats']} beats, "
          f"source={entry['source']['composer']} {entry['source']['work']})")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Create the .gitignore so audition output doesn't pollute git.**

Create `corpus/mtd_seg/.gitignore` with:

```
auditions/
```

- [ ] **Step 3: Smoke-test using the synthetic flow from Tasks 1-2.**

```bash
python -c "
import json, pathlib
gt = pathlib.Path('C:/@dev/repos/mforce/corpus/mtd_seg/ground_truth_json')
gt.mkdir(parents=True, exist_ok=True)
(gt / 'MTD5755.json').write_text(json.dumps({
    'mtdid': '5755',
    'note_count': 11,
    'boundaries': [5, 9],
    'figure_count': 3,
    'issues': []
}) + '\n')
"
python C:/@dev/repos/mforce/corpus/mtd_seg/extract.py 5755
python C:/@dev/repos/mforce/corpus/mtd_seg/build_pools.py
python C:/@dev/repos/mforce/corpus/mtd_seg/audition.py MTD5755_f0
```

Expected: prints `wrote auditions/MTD5755_f0.mid (5 notes, ... source=Mozart KV467-02)`.

- [ ] **Step 4: Verify the .mid is well-formed.**

Use the existing `corpus/mtd_sample/dump.py` to parse the audition output:

```bash
python C:/@dev/repos/mforce/corpus/mtd_sample/dump.py C:/@dev/repos/mforce/corpus/mtd_seg/auditions/MTD5755_f0.mid
```

Expected: clean `dump.py` output showing 5 notes at quarter-note intervals starting at C4 (MIDI 60), tempo 120 bpm, 4/4. No parse errors.

- [ ] **Step 5: Clean up synthetic flow output.**

```bash
rm C:/@dev/repos/mforce/corpus/mtd_seg/ground_truth_json/MTD5755.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/figures/MTD5755.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/pools/pulse.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/pools/step.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/pools/paired.json
rm C:/@dev/repos/mforce/corpus/mtd_seg/auditions/MTD5755_f0.mid
```

- [ ] **Step 6: Commit.**

```bash
git -C C:/@dev/repos/mforce add corpus/mtd_seg/audition.py corpus/mtd_seg/.gitignore
git -C C:/@dev/repos/mforce commit -m "feat(mtd_seg): add audition.py — emit MIDI from pool entries"
```

---

## Human checkpoint: Hand-marking

**Not code to write.** Matt opens each `corpus/mtd_seg/ground_truth/MTD####.txt` against its `.html` (sheet music) and `_score.mid` (audio reference), inserts `|` lines at figure boundaries, saves. Tasks 4 onward depend on this being done for at least some themes. Partial completion is fine — the pipeline runs against whatever ground-truth JSONs exist.

---

## Task 4: Run the pipeline on real ground-truth data

**Files:**
- Read: `corpus/mtd_seg/ground_truth/MTD####.txt` (Matt's marks)
- Write/Generate (and commit): `corpus/mtd_seg/ground_truth_json/*.json`, `corpus/mtd_seg/figures/*.json`, `corpus/mtd_seg/pools/*.json`

- [ ] **Step 1: Parse Matt's hand-marks into JSON boundary lists.**

```bash
python C:/@dev/repos/mforce/corpus/mtd_seg/parse_groundtruth.py
```

Expected: one line per marked theme, e.g. `MTD5755.txt: 11 notes, 3 figures`. If any theme reports `(N issues)`, read the `!`-prefixed lines and re-mark.

- [ ] **Step 2: Run extract on the parsed themes.**

```bash
python C:/@dev/repos/mforce/corpus/mtd_seg/extract.py
```

Expected: one line per extracted theme. Themes with `(N with warnings)` indicate quantization or off-scale issues — note them but don't block.

- [ ] **Step 3: Build the three pools.**

```bash
python C:/@dev/repos/mforce/corpus/mtd_seg/build_pools.py
```

Expected: `wrote pools/pulse.json (N entries)` etc. with N matching total figures across all themes (60–100 expected for 20 themes × ~3-5 figures each).

- [ ] **Step 4: Spot-check by ear.**

```bash
python C:/@dev/repos/mforce/corpus/mtd_seg/audition.py --random
python C:/@dev/repos/mforce/corpus/mtd_seg/audition.py --composer Mozart
python C:/@dev/repos/mforce/corpus/mtd_seg/audition.py --composer Bach
python C:/@dev/repos/mforce/corpus/mtd_seg/audition.py --composer Beethoven
```

Each writes a .mid in `corpus/mtd_seg/auditions/`. Open in any MIDI player. Listen for "does this sound like a coherent figure" — diatonic-only at C4 anchor.

- [ ] **Step 5: Commit ground-truth JSON, figures, and pools.**

```bash
git -C C:/@dev/repos/mforce add corpus/mtd_seg/ground_truth/ corpus/mtd_seg/ground_truth_json/ corpus/mtd_seg/figures/ corpus/mtd_seg/pools/
git -C C:/@dev/repos/mforce commit -m "data(mtd_seg): hand-marked figure boundaries + extracted pools (N themes)"
```

(Replace N in the commit message with the actual theme count.)

---

## Task 5: Add `LibraryPassageConfig::motifSource` field

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/templates_json.h`

- [ ] **Step 1: Locate the existing `LibraryPassageConfig`.**

```bash
grep -n "LibraryPassageConfig" C:/@dev/repos/mforce/engine/include/mforce/music/templates.h
```

Expected: 2-3 matches around the struct definition.

- [ ] **Step 2: Add the `MotifSource` enum + field to `LibraryPassageConfig`.**

In `templates.h`, find the existing `struct LibraryPassageConfig` and modify it to look like:

```cpp
struct LibraryPassageConfig {
    // Either a specific pattern name to use, or empty to pick by length.
    std::string patternName;

    // Length budget if pattern not specified by name.
    int barsHint{0};

    // Seed for slot motif generation. 0 = derive from masterSeed + locus.
    uint32_t seed{0};

    // Phase 2: how to fill each slot's motif content.
    //   Generated  → existing RFB-build_by_length path
    //   Pool       → RealMotifSlotFiller.pickFromPool (real MTD figures)
    enum class MotifSource { Generated, Pool };
    MotifSource motifSource{MotifSource::Generated};

    // Filter knobs applied when motifSource == Pool.
    // Empty string = no filter on that field.
    std::string poolFilterComposer;     // e.g. "Mozart"
    std::string poolFilterInstrument;   // e.g. "Piano"
    std::string poolFilterKeyMode;      // "major" / "minor" / ""
};
```

If the existing struct already has some of these fields, integrate without duplicating. Preserve any field ordering that round-trips to existing JSON patches.

- [ ] **Step 3: Build the engine to verify nothing broke.**

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target mforce_engine
```

Expected: clean build.

- [ ] **Step 4: Add JSON round-trip for the enum + new fields in `templates_json.h`.**

Find the existing `to_json` / `from_json` for `LibraryPassageConfig`. Add support for the new fields. Below the existing config block, add:

```cpp
inline void to_json(json& j, LibraryPassageConfig::MotifSource ms) {
    switch (ms) {
        case LibraryPassageConfig::MotifSource::Generated: j = "generated"; break;
        case LibraryPassageConfig::MotifSource::Pool:      j = "pool";      break;
    }
}

inline void from_json(const json& j, LibraryPassageConfig::MotifSource& ms) {
    const std::string s = j.get<std::string>();
    if      (s == "generated") ms = LibraryPassageConfig::MotifSource::Generated;
    else if (s == "pool")      ms = LibraryPassageConfig::MotifSource::Pool;
    else throw std::runtime_error("Unknown LibraryPassageConfig::MotifSource: " + s);
}
```

Then in the `LibraryPassageConfig` `to_json` body, append:

```cpp
    j["motifSource"]         = c.motifSource;
    j["poolFilterComposer"]  = c.poolFilterComposer;
    j["poolFilterInstrument"]= c.poolFilterInstrument;
    j["poolFilterKeyMode"]   = c.poolFilterKeyMode;
```

In the `from_json` body, append:

```cpp
    if (j.contains("motifSource"))          c.motifSource          = j.at("motifSource").get<LibraryPassageConfig::MotifSource>();
    if (j.contains("poolFilterComposer"))   c.poolFilterComposer   = j.at("poolFilterComposer").get<std::string>();
    if (j.contains("poolFilterInstrument")) c.poolFilterInstrument = j.at("poolFilterInstrument").get<std::string>();
    if (j.contains("poolFilterKeyMode"))    c.poolFilterKeyMode    = j.at("poolFilterKeyMode").get<std::string>();
```

- [ ] **Step 5: Build engine to verify clean.**

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target mforce_engine
```

Expected: clean build.

- [ ] **Step 6: Commit.**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git -C C:/@dev/repos/mforce commit -m "feat(templates): LibraryPassageConfig.motifSource + pool filter fields"
```

---

## Task 6: Implement `RealMotifSlotFiller`

**Files:**
- Create: `engine/include/mforce/music/real_motif_slot_filler.h`

- [ ] **Step 1: Create the header with the full content below.**

```cpp
#pragma once
//
// RealMotifSlotFiller — pool-loader + filtered-random-pick component used by
// LibraryPassageStrategy when LibraryPassageConfig::motifSource == Pool.
//
// Loads corpus/mtd_seg/pools/paired.json once at first use. For each Pattern
// slot needing a motif of `totalBeats = T`, filters by length match (within
// epsilon) plus optional composer/instrument/key-mode filters, picks one at
// random, and emits a MelodicFigure.
//
// Phase 1 chromatic handling: IGNORED. The emitted figure uses only the
// diatonic step[] from the pool entry. The chromatic[] sidecar in pool data
// is preserved (so we don't lose it on disk) but not honored at slot-fill
// time. Plumbing chromatic through to the realize loop is a follow-on spec.
//
// Spec: docs/superpowers/specs/2026-05-05-mtd-figure-extraction-design.md
//

#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace mforce {

struct PoolEntry {
    std::string             id;
    std::vector<float>      pulse;
    std::vector<int>        step;
    std::vector<int>        chromatic;
    int                     numNotes{0};
    float                   totalBeats{0.0f};
    // Source metadata
    std::string             composer;
    std::string             work;
    std::string             keyMode;     // "major" / "minor"
    std::string             instrument;
    std::string             polyphony;
};

class RealMotifSlotFiller {
public:
    // Loads pool from `corpus/mtd_seg/pools/paired.json` relative to CWD on
    // first call. Subsequent calls reuse the cache.
    static const std::vector<PoolEntry>& pool() {
        static std::vector<PoolEntry> cached = load_pool_();
        return cached;
    }

    // Pick a random pool entry matching the constraints, build a MelodicFigure.
    // Returns std::nullopt if no entries match the filter.
    //
    // Length match: pool entry's totalBeats must be within `lengthEps` of
    // target. Default eps = 0.01 (effectively exact for snapped pulses).
    static std::optional<MelodicFigure> pickFigure(
        float                        targetBeats,
        const std::string&           filterComposer,
        const std::string&           filterInstrument,
        const std::string&           filterKeyMode,
        Randomizer&                  rng,
        float                        lengthEps = 0.01f)
    {
        const auto& p = pool();
        std::vector<const PoolEntry*> candidates;
        candidates.reserve(p.size() / 4);
        for (const auto& e : p) {
            if (std::abs(e.totalBeats - targetBeats) > lengthEps) continue;
            if (!filterComposer.empty()   && e.composer != filterComposer)     continue;
            if (!filterInstrument.empty() && e.instrument != filterInstrument) continue;
            if (!filterKeyMode.empty()    && e.keyMode != filterKeyMode)       continue;
            candidates.push_back(&e);
        }
        if (candidates.empty()) {
            return std::nullopt;
        }

        const PoolEntry* chosen = candidates[rng.int_range(0, int(candidates.size() - 1))];

        MelodicFigure fig;
        fig.units.reserve(chosen->numNotes);
        for (int i = 0; i < chosen->numNotes; ++i) {
            FigureUnit u;
            u.duration = chosen->pulse[size_t(i)];
            // step[0]=0 by convention (already so in pool data, but enforce)
            u.step = (i == 0) ? 0 : chosen->step[size_t(i)];
            fig.units.push_back(u);
        }
        return fig;
    }

private:
    static std::vector<PoolEntry> load_pool_() {
        std::vector<PoolEntry> out;
        const std::string path = "corpus/mtd_seg/pools/paired.json";
        std::ifstream in(path);
        if (!in) {
            std::cerr << "RealMotifSlotFiller: cannot open " << path
                      << " (CWD must be repo root)\n";
            return out;
        }
        nlohmann::json j;
        try { in >> j; }
        catch (const std::exception& e) {
            std::cerr << "RealMotifSlotFiller: JSON parse error in " << path
                      << ": " << e.what() << "\n";
            return out;
        }
        if (!j.is_array()) {
            std::cerr << "RealMotifSlotFiller: " << path << " is not a JSON array\n";
            return out;
        }
        out.reserve(j.size());
        for (const auto& je : j) {
            try {
                PoolEntry e;
                e.id          = je.at("id").get<std::string>();
                e.pulse       = je.at("pulse").get<std::vector<float>>();
                e.step        = je.at("step").get<std::vector<int>>();
                e.chromatic   = je.at("chromatic").get<std::vector<int>>();
                e.numNotes    = je.at("num_notes").get<int>();
                e.totalBeats  = je.at("total_beats").get<float>();
                const auto& src = je.at("source");
                e.composer    = src.value("composer",   "");
                e.work        = src.value("work",       "");
                e.keyMode     = src.value("key_mode",   "");
                e.instrument  = src.value("instrument", "");
                e.polyphony   = src.value("polyphony",  "");
                out.push_back(std::move(e));
            } catch (const std::exception& ex) {
                std::cerr << "RealMotifSlotFiller: skipping pool entry: "
                          << ex.what() << "\n";
            }
        }
        std::cerr << "RealMotifSlotFiller: loaded " << out.size()
                  << " entries from " << path << "\n";
        return out;
    }
};

} // namespace mforce
```

- [ ] **Step 2: Build engine.**

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target mforce_engine
```

Expected: clean build. (No consumer yet, so no behavior change.)

- [ ] **Step 3: Commit.**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/music/real_motif_slot_filler.h
git -C C:/@dev/repos/mforce commit -m "feat(music): RealMotifSlotFiller — load pool + filtered random pick"
```

---

## Task 7: Wire `RealMotifSlotFiller` into `LibraryPassageStrategy`

**Files:**
- Modify: `engine/include/mforce/music/library_passage_strategy.h`

- [ ] **Step 1: Add the include for `real_motif_slot_filler.h`.**

In `library_passage_strategy.h`, immediately after the line `#include "mforce/music/pattern_library.h"`, add:

```cpp
#include "mforce/music/real_motif_slot_filler.h"
```

- [ ] **Step 2: Locate the per-slot motif generation block.**

Find the block at `library_passage_strategy.h:141-175` (the `for (char s : slots)` loop that calls `rfb.build_by_length`). Replace just the body of that loop (NOT the loop itself or the surrounding code) so that it switches on `cfg.motifSource`.

The current body looks like:

```cpp
        std::string motifName = pattern->name + "_" + std::string(1, s);
        if (locus.pieceTemplate->realizedMotifs.count(motifName)) continue;

        uint32_t motifSeed = slotRng.rng();
        Randomizer perMotifRng(motifSeed);
        float pulseHint = PULSE_HINTS[perMotifRng.int_range(0, 2)];

        Constraints constraints;
        constraints.defaultPulse = pulseHint;

        RandomFigureBuilder rfb(perMotifRng.rng());
        float length = float(slotUnits[s]) * beat_per_unit;
        MelodicFigure fig;
        try {
            fig = rfb.build_by_length(length, constraints);
        } catch (const std::exception& e) {
            std::cerr << "LibraryPassageStrategy: RFB build_by_length failed for slot "
                      << s << " (length=" << length << ", pulse=" << pulseHint
                      << "): " << e.what() << "\n";
            continue;
        }
        if (!fig.units.empty()) fig.units[0].step = 0;

        Motif m;
        m.name = motifName;
        m.userProvided = false;
        m.content = fig;
        m.origin = MotifOrigin::Generated;
        m.generationSeed = motifSeed;
        locus.pieceTemplate->add_motif(std::move(m));
```

Replace it with:

```cpp
        std::string motifName = pattern->name + "_" + std::string(1, s);
        if (locus.pieceTemplate->realizedMotifs.count(motifName)) continue;

        uint32_t motifSeed = slotRng.rng();
        Randomizer perMotifRng(motifSeed);
        const float length = float(slotUnits[s]) * beat_per_unit;

        MelodicFigure fig;
        MotifOrigin origin = MotifOrigin::Generated;

        if (cfg.motifSource == LibraryPassageConfig::MotifSource::Pool) {
            auto picked = RealMotifSlotFiller::pickFigure(
                length,
                cfg.poolFilterComposer,
                cfg.poolFilterInstrument,
                cfg.poolFilterKeyMode,
                perMotifRng);
            if (picked) {
                fig = std::move(*picked);
                origin = MotifOrigin::FromCorpus;
            } else {
                std::cerr << "LibraryPassageStrategy: pool fill empty for slot " << s
                          << " (length=" << length << "); falling back to RFB\n";
            }
        }

        if (fig.units.empty()) {  // either Generated mode or Pool fell back
            float pulseHint = PULSE_HINTS[perMotifRng.int_range(0, 2)];
            Constraints constraints;
            constraints.defaultPulse = pulseHint;
            RandomFigureBuilder rfb(perMotifRng.rng());
            try {
                fig = rfb.build_by_length(length, constraints);
            } catch (const std::exception& e) {
                std::cerr << "LibraryPassageStrategy: RFB build_by_length failed for slot "
                          << s << " (length=" << length << ", pulse=" << pulseHint
                          << "): " << e.what() << "\n";
                continue;
            }
        }

        if (!fig.units.empty()) fig.units[0].step = 0;

        Motif m;
        m.name = motifName;
        m.userProvided = false;
        m.content = fig;
        m.origin = origin;
        m.generationSeed = motifSeed;
        locus.pieceTemplate->add_motif(std::move(m));
```

- [ ] **Step 3: Add the `FromCorpus` value to `MotifOrigin` if it doesn't already exist.**

```bash
grep -rn "enum class MotifOrigin" C:/@dev/repos/mforce/engine/include/mforce/music/
```

Expected: one match (likely in `templates.h`). Open the file and check the enum body. If it's:

```cpp
enum class MotifOrigin { Generated, UserProvided };
```

Add a new value:

```cpp
enum class MotifOrigin { Generated, UserProvided, FromCorpus };
```

If `MotifOrigin` already has `FromCorpus`, no change needed.

If `MotifOrigin` has JSON round-trip (look for `to_json`/`from_json` near it), add string mapping for `FromCorpus`:
- `to_json`: case `FromCorpus`: `j = "fromCorpus"; break;`
- `from_json`: `else if (s == "fromCorpus") m = MotifOrigin::FromCorpus;`

- [ ] **Step 4: Build engine.**

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target mforce_engine
```

Expected: clean build.

- [ ] **Step 5: Run baseline tests to confirm no regressions.**

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target test_figures
C:/@dev/repos/mforce/build/Debug/test_figures.exe
```

Expected: all existing tests pass. The Generated path is the default and unchanged.

- [ ] **Step 6: Commit.**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/music/library_passage_strategy.h engine/include/mforce/music/templates.h
git -C C:/@dev/repos/mforce commit -m "feat(library_passage): switch on motifSource — RFB or RealMotifSlotFiller"
```

---

## Task 8: Integration test + audition deliverable

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Add the include.**

In `tools/test_figures/main.cpp`, find the existing `#include "mforce/music/library_passage_strategy.h"` (or near other passage-strategy includes). Immediately after, add:

```cpp
#include "mforce/music/real_motif_slot_filler.h"
```

- [ ] **Step 2: Add a pool-load test.**

After the last existing `integ_*` test in `main.cpp`, add:

```cpp
// ----------------------------------------------------------------------------
// RealMotifSlotFiller integration tests
// ----------------------------------------------------------------------------

int integ_real_motif_pool_loads() {
    const auto& pool = RealMotifSlotFiller::pool();
    if (pool.empty()) {
        std::cerr << "  FAIL: pool is empty (run extract.py + build_pools.py first, "
                     "and run from repo root)\n";
        return 1;
    }
    // Sanity: every entry has matching pulse/step/chromatic lengths.
    for (const auto& e : pool) {
        EXPECT_EQ(int(e.pulse.size()), e.numNotes, "pulse size matches numNotes");
        EXPECT_EQ(int(e.step.size()),  e.numNotes, "step size matches numNotes");
        EXPECT_EQ(int(e.chromatic.size()), e.numNotes, "chromatic size matches numNotes");
        EXPECT_EQ(e.step.empty() ? 0 : e.step[0], 0, "step[0] is 0");
        if (e.id.empty()) {
            std::cerr << "  FAIL: empty id\n";
            return 1;
        }
    }
    return 0;
}

int integ_real_motif_pick_by_length() {
    const auto& pool = RealMotifSlotFiller::pool();
    if (pool.empty()) {
        std::cerr << "  SKIP: pool empty\n";
        return 0;
    }
    // Find a length that exists in the pool, then pick.
    float target = pool[0].totalBeats;
    Randomizer rng(0x12345);
    auto fig = RealMotifSlotFiller::pickFigure(target, "", "", "", rng);
    if (!fig) {
        std::cerr << "  FAIL: pickFigure returned nullopt for target=" << target << "\n";
        return 1;
    }
    EXPECT_EQ(int(fig->units.size()) > 0, 1, "figure has at least one unit");
    EXPECT_EQ(fig->units[0].step, 0, "step[0] is 0");
    return 0;
}

int integ_real_motif_pick_filter_composer() {
    const auto& pool = RealMotifSlotFiller::pool();
    if (pool.empty()) {
        std::cerr << "  SKIP: pool empty\n";
        return 0;
    }
    // Find a (composer, totalBeats) that exists.
    std::string comp = pool[0].composer;
    float target = pool[0].totalBeats;
    Randomizer rng(0x22222);
    auto fig = RealMotifSlotFiller::pickFigure(target, comp, "", "", rng);
    if (!fig) {
        std::cerr << "  FAIL: pickFigure with composer filter returned nullopt for "
                  << comp << " @ " << target << "\n";
        return 1;
    }
    return 0;
}

int integ_real_motif_pick_no_match() {
    Randomizer rng(0x33333);
    // Length nothing should match.
    auto fig = RealMotifSlotFiller::pickFigure(999.0f, "", "", "", rng);
    if (fig) {
        std::cerr << "  FAIL: pickFigure returned a figure for impossible target\n";
        return 1;
    }
    return 0;
}
```

- [ ] **Step 3: Register the new tests in `main()`.**

Find the existing block of `RUN_TEST(integ_*)` calls in `main()`. After the last existing test, add:

```cpp
    RUN_TEST(integ_real_motif_pool_loads);
    RUN_TEST(integ_real_motif_pick_by_length);
    RUN_TEST(integ_real_motif_pick_filter_composer);
    RUN_TEST(integ_real_motif_pick_no_match);
```

- [ ] **Step 4: Build test_figures.**

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target test_figures
```

Expected: clean build.

- [ ] **Step 5: Run tests from repo root (so the relative pool path resolves).**

```bash
cd C:/@dev/repos/mforce && build/Debug/test_figures.exe
```

Expected:
- All baseline tests pass.
- New `integ_real_motif_*` tests pass (assuming `pools/paired.json` exists from Task 4).
- If pools missing: `integ_real_motif_pool_loads` fails with the diagnostic message; rerun `build_pools.py` and re-test.

- [ ] **Step 6: Audition deliverable — render a Pattern with real motifs.**

Locate or create a simple test patch that drives `LibraryPassageStrategy` with `motifSource=Pool`. Pattern: pick BillyLife from `lib/json/Pop1.json`. Construct a piece template via `mforce_cli` using JSON config:

```bash
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:/@dev/repos/mforce/build --config Debug --target mforce_cli
```

Then construct a `corpus/mtd_seg/audition_pattern.json` with:

```json
{
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 100.0,
  "masterSeed": 305419896,
  "sections": [{ "name": "Main", "beats": 256.0 }],
  "parts": [{
    "name": "melody",
    "role": "Melody",
    "passages": {
      "Main": {
        "name": "Main",
        "strategy": "library_passage",
        "startingPitch": {"name": "C", "octave": 4},
        "libraryConfig": {
          "patternName": "BillyLife",
          "motifSource": "pool"
        }
      }
    }
  }]
}
```

Run:

```bash
cd C:/@dev/repos/mforce && build/Debug/mforce_cli.exe --compose corpus/mtd_seg/audition_pattern.json --render renders/billylife_real_motifs.wav
```

Expected: produces `renders/billylife_real_motifs.wav`. For comparison, produce the RFB version:

Edit `corpus/mtd_seg/audition_pattern.json` to swap `"motifSource": "pool"` → `"motifSource": "generated"`, save, then:

```bash
cd C:/@dev/repos/mforce && build/Debug/mforce_cli.exe --compose corpus/mtd_seg/audition_pattern.json --render renders/billylife_rfb.wav
```

Listen to both. If `billylife_real_motifs.wav` reads as more "musical" than `billylife_rfb.wav` (Matt's ear judges), Phase 1 is validated. If not, the data shape is wrong somewhere; debug from extract.py outputs back.

- [ ] **Step 7: Commit the integration tests + audition harness.**

```bash
git -C C:/@dev/repos/mforce add tools/test_figures/main.cpp corpus/mtd_seg/audition_pattern.json
git -C C:/@dev/repos/mforce commit -m "test(figures): integration tests for RealMotifSlotFiller; audition harness"
```

- [ ] **Step 8: Save the audition .wavs as evidence.**

```bash
git -C C:/@dev/repos/mforce add renders/billylife_real_motifs.wav renders/billylife_rfb.wav
git -C C:/@dev/repos/mforce commit -m "audition: real-motif Pattern fill vs RFB baseline"
```

---

## Verification

After all 8 tasks plus the human checkpoint:

- [ ] `python C:/@dev/repos/mforce/corpus/mtd_seg/extract.py` runs cleanly against all hand-marked themes.
- [ ] `python C:/@dev/repos/mforce/corpus/mtd_seg/build_pools.py` produces three pool files with matching entry counts and `id` consistency across them.
- [ ] `python C:/@dev/repos/mforce/corpus/mtd_seg/audition.py --random` produces a valid .mid audible in any player.
- [ ] `cd C:/@dev/repos/mforce && build/Debug/test_figures.exe` passes all tests including the four new `integ_real_motif_*`.
- [ ] `billylife_real_motifs.wav` exists in `renders/` and Matt judges it audibly more musical than `billylife_rfb.wav`.

If verification fails at any step, the diagnostic message points to the relevant task to revisit.

---

## Out of scope (per spec)

- Auto-segmentation algorithm (separate spec, after Phase 1 produces ground truth).
- Plumbing chromatic offset through `MelodicFigure` / realize loop. Phase 1 audition ignores chromatic at slot-fill time; the data is preserved in pools for the future fix.
- Modal scales beyond major / natural minor.
- Articulation / ornament extraction beyond literal notes.
- `mforce_cli` rendering integration in `audition.py` (.mid only Phase 1).
- Extending the corpus beyond the 20 curated themes (Phase 2 will run the auto-segmenter against the full 1638).
