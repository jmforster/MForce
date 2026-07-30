"""Tokenize the Nottingham-Jukedeck folk corpus (comp backlog #3a, G1).

corpus/nottingham-dataset/MIDI/*.mid are melody + chord-accompaniment
tracks merged per file. This script parses PER TRACK, picks the melody
track (most monophonic, tie-break highest mean pitch), and feeds it
through markov_tokenize.tokenize_notes — emitting nottingham_tokens.json
in the exact markov_tokens.json schema, so markov_model / figuregen /
bake_off run on folk unchanged via --tokens.
"""
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from prep_groundtruth import read_vlq                      # noqa: E402
from markov_tokenize import tokenize_notes, MIN_NOTES      # noqa: E402

MIDI_DIR = HERE.parent / "nottingham-dataset" / "MIDI"
OUT = HERE / "nottingham_tokens.json"


def parse_midi_tracks(path):
    """Like prep_groundtruth.parse_midi but returns notes PER TRACK.
    Returns (div, keysig, [track_notes, ...])."""
    b = path.read_bytes()
    assert b[:4] == b"MThd"
    _fmt, _ntr, div = struct.unpack(">HHH", b[8:14])
    keysig = (0, 0)
    tracks = []
    i = 14
    while i < len(b):
        if b[i:i + 4] != b"MTrk":
            break
        tlen = struct.unpack(">I", b[i + 4:i + 8])[0]
        end = i + 8 + tlen
        i += 8
        t = 0
        on = {}
        last_status = 0
        notes = []
        while i < end:
            dt, i = read_vlq(b, i)
            t += dt
            s = b[i]
            if s & 0x80:
                last_status = s
                i += 1
            else:
                s = last_status
            if s == 0xFF:
                meta = b[i]
                i += 1
                ml, i = read_vlq(b, i)
                payload = b[i:i + ml]
                i += ml
                if meta == 0x59 and ml == 2:
                    sf = payload[0] if payload[0] < 128 else payload[0] - 256
                    keysig = (sf, payload[1])
                elif meta == 0x2F:
                    i = end
                    break
            elif s in (0xF0, 0xF7):
                ml, i = read_vlq(b, i)
                i += ml
            else:
                hi = s & 0xF0
                if hi == 0x90:
                    p, v = b[i], b[i + 1]
                    i += 2
                    if v:
                        on[p] = t
                    elif p in on:
                        notes.append((on[p], p, t - on[p]))
                        del on[p]
                elif hi == 0x80:
                    p, v = b[i], b[i + 1]
                    i += 2
                    if p in on:
                        notes.append((on[p], p, t - on[p]))
                        del on[p]
                elif hi in (0xA0, 0xB0, 0xE0):
                    i += 2
                elif hi in (0xC0, 0xD0):
                    i += 1
        i = end
        notes.sort()
        if notes:
            tracks.append(notes)
    return div, keysig, tracks


def onset_share_frac(notes):
    onsets = [n[0] for n in notes]
    return 1.0 - len(set(onsets)) / len(onsets)


def pick_melody(tracks):
    """Melody = least onset-sharing (chords share onsets massively);
    tie-break higher mean pitch. Returns notes or None."""
    cands = [tr for tr in tracks if len(tr) >= MIN_NOTES]
    if not cands:
        return None
    scored = sorted(
        cands,
        key=lambda tr: (round(onset_share_frac(tr), 2),
                        -(sum(n[1] for n in tr) / len(tr))))
    best = scored[0]
    if onset_share_frac(best) > 0.05:
        return None  # even the best track is chordal — skip tune
    # Drop residual same-onset duplicates (keep highest pitch at each onset).
    dedup = {}
    for onset, pitch, dur in best:
        if onset not in dedup or pitch > dedup[onset][1]:
            dedup[onset] = (onset, pitch, dur)
    return sorted(dedup.values())


def main():
    all_streams, all_pulse0, all_starts = [], [], []
    step_set, pulse_set = set(), set()
    n_ok = n_skip = 0
    skip_reasons = {}

    for f in sorted(MIDI_DIR.glob("*.mid")):
        try:
            div, keysig, tracks = parse_midi_tracks(f)
            mel = pick_melody(tracks)
            if mel is None:
                n_skip += 1
                skip_reasons["no melody track"] = skip_reasons.get("no melody track", 0) + 1
                continue
            streams, pulse0, reason = tokenize_notes(mel, keysig, div)
        except Exception as e:  # noqa: BLE001 — malformed midi, count and continue
            n_skip += 1
            key = f"exception: {type(e).__name__}"
            skip_reasons[key] = skip_reasons.get(key, 0) + 1
            continue
        if reason:
            n_skip += 1
            key = reason.split("(")[0].strip()
            skip_reasons[key] = skip_reasons.get(key, 0) + 1
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
