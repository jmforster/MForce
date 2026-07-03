"""Tokenize the full MTD corpus into (step-delta, pulse) streams — NO boundaries.

Walks each monophonic theme end-to-end and emits one token per note transition:
  token = (delta_scale_step, snapped_pulse_beats)
where delta is the diatonic scale-step change from the previous note and pulse is
the snapped inter-onset interval (last note: its duration).

Non-monophonic themes (any two notes sharing an onset tick = a chord) are skipped and
logged. Notes that fall >=2 semitones off the inferred scale break the stream (no bogus
transition is created across them).

Reads:
  ../mtd_full/manifest.csv   (one row per theme; MTDID column)
  ../mtd_full/midi/MTD####_score.mid  (fallback _edm-corr.mid)

Writes:
  markov_tokens.json   {alphabet:{steps,pulses}, streams:[[ [dstep,pulse], ... ], ...],
                        starts:[[dstep,pulse], ...], pulse0:[float,...], stats:{...}}

Usage:
  python markov_tokenize.py            # tokenize whole corpus, write markov_tokens.json
  python markov_tokenize.py --limit 50 # first 50 themes (smoke test)
"""
import csv, json, sys, pathlib

from prep_groundtruth import (
    parse_midi, degree_and_accidental,
    MAJOR_PC, MINOR_PC, TONIC_PC_MAJOR,
)

ROOT      = pathlib.Path(__file__).resolve().parent
MTD_FULL  = ROOT.parent / "mtd_full"
MIDI_DIR  = MTD_FULL / "midi"
MANIFEST  = MTD_FULL / "manifest.csv"
OUT_PATH  = ROOT / "markov_tokens.json"

# Pulse quantization grid (matches extract.py)
GRID = sorted([
    4.0, 2.0, 1.0, 0.5, 0.25, 0.125,        # plain
    3.0, 1.5, 0.75, 0.375,                   # dotted
    2.0/3, 1.0/3, 1.0/6,                     # triplets
])
MIN_NOTES = 3   # need at least two transitions to be useful
BOS_STEP  = -999  # sentinel "start-of-figure" step; carries note-0's pulse, never emitted


def snap_pulse(beats):
    return min(GRID, key=lambda g: abs(g - beats))


def scale_step_index(midi_pitch, tonic_pc, mode, accidental):
    """Absolute scale-degree-step index (7 per octave above MIDI-0 tonic)."""
    deg, _ = degree_and_accidental(midi_pitch % 12, tonic_pc, mode)
    if deg is None:
        return None
    rel_pc = (midi_pitch % 12 - tonic_pc - accidental) % 12
    tonic_midi_pos = midi_pitch - rel_pc
    octaves_above_minus1 = (tonic_midi_pos - tonic_pc) // 12
    return 7 * octaves_above_minus1 + (deg - 1)


def find_midi(mtdid):
    p = MIDI_DIR / f"MTD{mtdid}_score.mid"
    if p.exists():
        return p
    p = MIDI_DIR / f"MTD{mtdid}_edm-corr.mid"
    if p.exists():
        return p
    return None


def is_monophonic(notes):
    """True if no two notes share an onset tick (a chord)."""
    onsets = [n[0] for n in notes]
    return len(set(onsets)) == len(onsets)


def tokenize_theme(midi_path):
    """Return (streams, pulse0_list, skip_reason).

    streams: list of token-lists, each token = [dstep, pulse]. A theme yields >1
    stream only if an off-scale note breaks it. skip_reason is set (str) when the
    theme is unusable, in which case streams is empty.
    """
    mid = parse_midi(midi_path)
    notes = mid["notes"]
    if len(notes) < MIN_NOTES:
        return [], [], f"too few notes ({len(notes)})"
    if not is_monophonic(notes):
        return [], [], "polyphonic (shared onsets)"

    sf, mi = mid["keysig"]
    tonic_pc = TONIC_PC_MAJOR.get(sf, 0)
    if mi == 1:
        tonic_pc = (tonic_pc - 3) % 12
    div = mid["ticks_per_quarter"]

    # Per-note: absolute scale step (or None if >=2 semitones off scale) + snapped pulse.
    steps = []
    pulses = []
    for idx, (onset, pitch, dur) in enumerate(notes):
        deg, acc = degree_and_accidental(pitch % 12, tonic_pc, mi)
        if deg is None:
            steps.append(None)
        else:
            steps.append(scale_step_index(pitch, tonic_pc, mi, acc))
        ioi = (notes[idx + 1][0] - onset) / div if idx + 1 < len(notes) else dur / div
        pulses.append(snap_pulse(ioi))

    # Build streams, breaking on off-scale notes.
    streams, pulse0 = [], []
    cur = []
    cur_started = False
    for idx in range(len(notes)):
        if steps[idx] is None:
            if cur:
                streams.append(cur)
                cur = []
            cur_started = False
            continue
        if not cur_started:
            # First note of a (sub)stream: the start-of-figure token. Its step is a
            # sentinel (BOS); its pulse IS note-0's duration, so note-1 will be
            # conditioned on the opening duration during modeling.
            cur.append([BOS_STEP, pulses[idx]])
            pulse0.append(pulses[idx])
            cur_started = True
            prev_step = steps[idx]
            continue
        dstep = steps[idx] - prev_step
        cur.append([dstep, pulses[idx]])
        prev_step = steps[idx]
    if cur:
        streams.append(cur)

    streams = [s for s in streams if len(s) >= 2]  # need BOS + >=1 transition
    if not streams:
        return [], [], "no usable transitions"
    return streams, pulse0, None


def main():
    limit = None
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])

    mtdids = []
    with MANIFEST.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            mid = row.get("MTDID")
            if mid:
                mtdids.append(mid)
    if limit:
        mtdids = mtdids[:limit]

    all_streams, all_pulse0, all_starts = [], [], []
    step_set, pulse_set = set(), set()
    n_ok = n_skip = n_nomidi = 0
    skip_reasons = {}

    for mtdid in mtdids:
        path = find_midi(mtdid)
        if path is None:
            n_nomidi += 1
            continue
        try:
            streams, pulse0, reason = tokenize_theme(path)
        except Exception as e:
            n_skip += 1
            skip_reasons[f"exception: {type(e).__name__}"] = skip_reasons.get(
                f"exception: {type(e).__name__}", 0) + 1
            continue
        if reason:
            n_skip += 1
            skip_reasons[reason.split("(")[0].strip()] = (
                skip_reasons.get(reason.split("(")[0].strip(), 0) + 1)
            continue
        n_ok += 1
        all_streams.extend(streams)
        all_pulse0.extend(pulse0)
        for s in streams:
            all_starts.append(s[1])          # first REAL token (note 1) of each stream
            pulse_set.add(s[0][1])           # BOS pulse is a valid duration
            for (d, p) in s[1:]:             # skip BOS step sentinel
                step_set.add(d)
                pulse_set.add(p)

    n_tokens = sum(len(s) - 1 for s in all_streams)  # exclude BOS
    out = {
        "alphabet": {
            "steps":  sorted(step_set),
            "pulses": sorted(pulse_set),
        },
        "streams": all_streams,
        "starts":  all_starts,
        "pulse0":  sorted(set(all_pulse0)),
        "pulse0_all": all_pulse0,
        "stats": {
            "themes_ok": n_ok, "themes_skipped": n_skip, "themes_no_midi": n_nomidi,
            "streams": len(all_streams), "transition_tokens": n_tokens,
            "step_alphabet_size": len(step_set), "pulse_alphabet_size": len(pulse_set),
            "skip_reasons": skip_reasons,
        },
    }
    OUT_PATH.write_text(json.dumps(out) + "\n", encoding="utf-8")

    s = out["stats"]
    print(f"themes: ok={s['themes_ok']} skipped={s['themes_skipped']} no_midi={s['themes_no_midi']}")
    print(f"streams={s['streams']}  transition tokens={s['transition_tokens']}")
    print(f"step alphabet={s['step_alphabet_size']}  pulse alphabet={s['pulse_alphabet_size']}")
    print(f"pulses seen: {out['alphabet']['pulses']}")
    print(f"steps seen:  {out['alphabet']['steps']}")
    print(f"skip reasons: {s['skip_reasons']}")
    print(f"wrote {OUT_PATH.relative_to(ROOT.parent.parent)}")


if __name__ == "__main__":
    main()
