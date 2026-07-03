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
    (Tasks 5-8).
"""
import argparse, json, pathlib, random, struct, sys

ROOT      = pathlib.Path(__file__).resolve().parent
POOL_PATH = ROOT / "pools" / "paired.json"
OUT_DIR   = ROOT / "auditions"
OUT_DIR.mkdir(parents=True, exist_ok=True)

DEFAULT_BPM         = 120
DEFAULT_ANCHOR_MIDI = 60   # C4
TICKS_PER_QUARTER   = 480

# Major scale semitone offsets from tonic for degrees 0..6
MAJOR_SEMITONES = [0, 2, 4, 5, 7, 9, 11]


def load_pool(path=POOL_PATH):
    path = pathlib.Path(path)
    if not path.exists():
        print(f"missing pool: {path} (run build_pools.py / markov_generate.py first)", file=sys.stderr)
        sys.exit(1)
    return json.loads(path.read_text(encoding="utf-8"))


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
    if v == 0:
        return b"\x00"
    out = bytearray()
    out.append(v & 0x7F)
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def step_to_midi(anchor_midi, cumulative_step):
    """Walk `cumulative_step` scale degrees (positive=up) from `anchor_midi` in
    a major scale rooted at the same pitch class as `anchor_midi`."""
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
    ap.add_argument("--pool", default=str(POOL_PATH),
                    help="pool JSON to read (default pools/paired.json)")
    args = ap.parse_args()

    pool = load_pool(args.pool)
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
