"""How long is the LAST note, really? — corpus measurement for the final-note rule.

Matt's run-10 verdict was implemented as "final note >= phrase median pulse"
(markov_phrase.build_phrase_v3). His run-12 verdict on the result: "Still need
more weighting of final note being longer/longest duration." Rather than pick a
multiplier by taste, measure what real themes do and reproduce that
distribution.

For every usable theme/tune, using the SAME pulse convention as
markov_tokenize (snapped IOI per note; the last note uses its own duration):

  ratio_med   final / median(all pulses)
  ratio_max   final / max(other pulses)
  ge_max      final >= max(other pulses)   (is the last note the longest?)
  pct_rank    fraction of notes strictly shorter than the final note

Writes final_note_profile.json — {corpus: {ratios: [...], p_ge_max, ...}} —
which markov_phrase samples at generation time.

Usage:
  python final_note_stats.py [--limit N]        # mtd + nottingham
"""
import csv
import json
import pathlib
import statistics
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from prep_groundtruth import parse_midi                      # noqa: E402
from markov_tokenize import (MANIFEST, find_midi, is_monophonic,  # noqa: E402
                             snap_pulse, MIN_NOTES)

OUT_PATH = HERE / "final_note_profile.json"


def pulses_from_notes(notes, div):
    """Snapped per-note pulses, markov_tokenize convention: IOI for every note
    except the last, which uses its own duration."""
    out = []
    for i, (onset, _pitch, dur) in enumerate(notes):
        ioi = (notes[i + 1][0] - onset) / div if i + 1 < len(notes) else dur / div
        out.append(snap_pulse(ioi))
    return out


def measure(pulses):
    """Final-note stats for one melody. None if degenerate."""
    if len(pulses) < MIN_NOTES:
        return None
    final, others = pulses[-1], pulses[:-1]
    med = statistics.median(others)
    mx = max(others)
    if med <= 0 or mx <= 0:
        return None
    shorter = sum(1 for p in others if p < final)
    return {
        "ratio_med": final / med,
        "ratio_max": final / mx,
        "ge_max": final >= mx - 1e-9,
        "strict_max": final > mx + 1e-9,
        "pct_rank": shorter / len(others),
        "final": final,
        "median": med,
    }


def collect_mtd(limit=None):
    ids = []
    with MANIFEST.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("MTDID"):
                ids.append(row["MTDID"])
    if limit:
        ids = ids[:limit]
    rows = []
    for mtdid in ids:
        path = find_midi(mtdid)
        if path is None:
            continue
        try:
            mid = parse_midi(path)
        except Exception:
            continue
        notes = mid["notes"]
        if len(notes) < MIN_NOTES or not is_monophonic(notes):
            continue
        r = measure(pulses_from_notes(notes, mid["ticks_per_quarter"]))
        if r:
            rows.append(r)
    return rows


def collect_nottingham(limit=None):
    import nottingham_tokenize as nt
    files = sorted(nt.MIDI_DIR.glob("*.mid"))
    if limit:
        files = files[:limit]
    rows = []
    for path in files:
        try:
            div, _ks, tracks = nt.parse_midi_tracks(path)
        except Exception:
            continue
        mel = nt.pick_melody(tracks)
        if not mel or len(mel) < MIN_NOTES or not is_monophonic(mel):
            continue
        r = measure(pulses_from_notes(mel, div))
        if r:
            rows.append(r)
    return rows


def summarize(name, rows):
    if not rows:
        print(f"{name}: no data")
        return None
    rm = sorted(r["ratio_med"] for r in rows)
    n = len(rm)
    ge = sum(1 for r in rows if r["ge_max"]) / n
    st = sum(1 for r in rows if r["strict_max"]) / n
    pr = sum(r["pct_rank"] for r in rows) / n

    def q(f):
        return rm[min(n - 1, int(f * n))]

    print(f"--- {name} (n={n}) ---")
    print(f"  final/median   p10={q(.10):.2f} p25={q(.25):.2f} "
          f"median={q(.50):.2f} p75={q(.75):.2f} p90={q(.90):.2f} "
          f"max={rm[-1]:.2f}")
    print(f"  final >= max of the rest   {ge:.3f}")
    print(f"  final strictly longest     {st:.3f}")
    print(f"  mean pct-rank of final     {pr:.3f}")
    print(f"  ratio >= 1                 "
          f"{sum(1 for x in rm if x >= 1 - 1e-9) / n:.3f}")
    print(f"  ratio >= 2                 "
          f"{sum(1 for x in rm if x >= 2 - 1e-9) / n:.3f}")
    # Store the distribution as a weighted histogram, not 1600 near-duplicate
    # floats — ratios cluster hard on the notated values (1.0, 2.0, 4.0).
    hist = {}
    for x in rm:
        k = f"{round(x, 3):g}"
        hist[k] = hist.get(k, 0) + 1
    return {"n": n, "hist": hist,
            "p_ge_max": round(ge, 4), "p_strict_max": round(st, 4),
            "mean_pct_rank": round(pr, 4),
            "median_ratio": round(q(.50), 4)}


def main():
    limit = None
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    prof = {}
    print("=== final-note duration profile ===")
    for name, fn in (("mtd", collect_mtd), ("nottingham", collect_nottingham)):
        try:
            rows = fn(limit)
        except Exception as e:
            print(f"{name}: skipped ({type(e).__name__}: {e})")
            continue
        s = summarize(name, rows)
        if s:
            prof[name] = s
    OUT_PATH.write_text(json.dumps(prof), encoding="utf-8")
    print(f"\nwrote {OUT_PATH}")


if __name__ == "__main__":
    main()
