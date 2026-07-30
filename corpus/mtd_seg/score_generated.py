"""Score generated melodies against MTD corpus statistics (comp backlog #1,
the comp lane's "ears" v1).

Input: piece JSON exported by `mforce_cli --compose` (parts[].events[] with
beat/type/data.noteNumber/duration) or a .mid file. Melody = first part's
note events (monophonic assumption, matching MTD).

Metrics (v1 — plausibility screens, not taste):
  int_jsd      Jensen-Shannon divergence of the pitch-interval distribution
               vs corpus (semitones, clamped to +-12). Lower = more corpus-like.
  ctr_jsd      JSD of contour-transition distribution (up/down/same bigrams).
  rep_LxCount  repetitiveness coverage (score_repetitiveness.repetitiveness
               on quantized IOI pulses) — corpus median ~6-10; near-0 means
               "wandering", huge means "stuck".
  big_leap     fraction of intervals > 7 semitones (corpus is overwhelmingly
               steps; leap-heavy output reads as random).
  range        melodic range in semitones.
  composite    weighted heuristic 0..1 (documented inline) — a FILTER
               threshold, not a quality ranking. Matt's ears stay the judge.

Corpus baseline: computed once over corpus/mtd_full/midi/*_score.mid
(monophonic themes only, same filters as score_repetitiveness) and cached
to corpus/mtd_seg/corpus_stats.json.
"""
import csv
import json
import math
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from prep_groundtruth import parse_midi          # noqa: E402
from score_repetitiveness import repetitiveness  # noqa: E402

CACHE = HERE / "corpus_stats.json"
MIDI_DIR = HERE.parent / "mtd_full" / "midi"
CLAMP = 12
CORPUS_LIMIT = 1200   # themes; plenty for stable histograms


# ---------------------------------------------------------------- melodies
def melody_from_piece_json(path):
    p = json.loads(Path(path).read_text())
    ev = [e for e in p["parts"][0]["events"] if e.get("type") == "note"]
    return [(e["beat"], int(round(e["data"]["noteNumber"])),
             e["data"]["duration"]) for e in ev]


def melody_from_midi(path):
    m = parse_midi(Path(path))
    tpq = m["ticks_per_quarter"]
    return [(t / tpq, p, d / tpq) for t, p, d in m["notes"]]


def load_melody(path):
    s = str(path)
    return melody_from_midi(s) if s.endswith(".mid") \
        else melody_from_piece_json(s)


# ---------------------------------------------------------------- features
def interval_hist(mel):
    h = Counter()
    for a, b in zip(mel, mel[1:]):
        iv = max(-CLAMP, min(CLAMP, b[1] - a[1]))
        h[iv] += 1
    return h


def contour_hist(mel):
    def sgn(x):
        return 0 if x == 0 else (1 if x > 0 else -1)
    steps = [sgn(b[1] - a[1]) for a, b in zip(mel, mel[1:])]
    return Counter(zip(steps, steps[1:]))


def pulses(mel):
    iois = [round(b[0] - a[0], 3) for a, b in zip(mel, mel[1:])]
    return tuple(i for i in iois if i > 0)


def jsd(h1, h2):
    """Jensen-Shannon divergence between two count dicts (base-2, 0..1)."""
    keys = set(h1) | set(h2)
    n1, n2 = sum(h1.values()) or 1, sum(h2.values()) or 1
    d1 = 0.0
    d2 = 0.0
    for k in keys:
        p, q = h1.get(k, 0) / n1, h2.get(k, 0) / n2
        m = (p + q) / 2
        if p:
            d1 += p * math.log2(p / m)
        if q:
            d2 += q * math.log2(q / m)
    return (d1 + d2) / 2


def features(mel):
    ih = interval_hist(mel)
    n_int = sum(ih.values()) or 1
    rep = repetitiveness(pulses(mel))
    pitches = [p for _, p, _ in mel]
    # Monotony screens (#9): first-order distributions can't see a phrase
    # that hammers one note/figure. zero_rate = repeated-note interval
    # fraction; max_run = longest same-pitch run as a fraction of length.
    zero_rate = ih.get(0, 0) / n_int
    max_run = run = 1
    for a, b in zip(pitches, pitches[1:]):
        run = run + 1 if b == a else 1
        max_run = max(max_run, run)
    return {
        "n_notes": len(mel),
        "int_hist": ih,
        "ctr_hist": contour_hist(mel),
        "rep_LxCount": rep["LxCount"],
        "big_leap": sum(c for iv, c in ih.items() if abs(iv) > 7) / n_int,
        "range": (max(pitches) - min(pitches)) if pitches else 0,
        "zero_rate": zero_rate,
        "max_run_frac": max_run / max(1, len(mel)),
    }


# ---------------------------------------------------------------- corpus
def corpus_stats():
    if CACHE.exists():
        d = json.loads(CACHE.read_text())
        d["int_hist"] = {int(k): v for k, v in d["int_hist"].items()}
        d["ctr_hist"] = {tuple(map(int, k.split(","))): v
                         for k, v in d["ctr_hist"].items()}
        return d
    ih, ch = Counter(), Counter()
    reps, leaps, ranges, zeros, runs = [], [], [], [], []
    n = 0
    for f in sorted(MIDI_DIR.glob("*_score.mid")):
        if n >= CORPUS_LIMIT:
            break
        try:
            mel = melody_from_midi(f)
        except Exception:  # noqa: BLE001 — malformed midi, skip
            continue
        if len(mel) < 8:
            continue
        ft = features(mel)
        ih.update(ft["int_hist"])
        ch.update(ft["ctr_hist"])
        reps.append(ft["rep_LxCount"])
        leaps.append(ft["big_leap"])
        ranges.append(ft["range"])
        zeros.append(ft["zero_rate"])
        runs.append(ft["max_run_frac"])
        n += 1
    reps.sort()
    leaps.sort()
    ranges.sort()
    zeros.sort()
    runs.sort()
    stats = {
        "n_themes": n,
        "int_hist": dict(ih),
        "ctr_hist": {f"{a},{b}": v for (a, b), v in ch.items()},
        "rep_median": reps[len(reps) // 2],
        "rep_p10": reps[len(reps) // 10],
        "big_leap_p90": leaps[9 * len(leaps) // 10],
        "range_p10": ranges[len(ranges) // 10],
        "range_p90": ranges[9 * len(ranges) // 10],
        "zero_rate_p95": zeros[95 * len(zeros) // 100],
        "max_run_frac_p95": runs[95 * len(runs) // 100],
    }
    CACHE.write_text(json.dumps(stats))
    print(f"[corpus] built stats over {n} themes -> {CACHE}", file=sys.stderr)
    stats["int_hist"] = {int(k): v for k, v in stats["int_hist"].items()}
    stats["ctr_hist"] = {tuple(map(int, k.split(","))): v
                         for k, v in stats["ctr_hist"].items()}
    return stats


# ---------------------------------------------------------------- scoring
def score(mel, cs):
    ft = features(mel)
    int_jsd = jsd(ft["int_hist"], cs["int_hist"])
    ctr_jsd = jsd(ft["ctr_hist"], cs["ctr_hist"])
    # Composite: 1 = plausible on every screen. Weights are v1 heuristics.
    parts = [
        max(0.0, 1.0 - int_jsd / 0.5),                      # interval shape
        max(0.0, 1.0 - ctr_jsd / 0.5),                      # contour shape
        1.0 if ft["rep_LxCount"] >= cs["rep_p10"] else
        ft["rep_LxCount"] / max(1, cs["rep_p10"]),          # enough repetition
        1.0 if ft["big_leap"] <= cs["big_leap_p90"] else
        max(0.0, 1.0 - (ft["big_leap"] - cs["big_leap_p90"]) * 4),  # leaps
        1.0 if cs["range_p10"] <= ft["range"] <= cs["range_p90"] else 0.5,
        # Monotony screens (#9): penalize past corpus p95, floor at 0.
        1.0 if ft["zero_rate"] <= cs["zero_rate_p95"] else
        max(0.0, 1.0 - (ft["zero_rate"] - cs["zero_rate_p95"]) * 3),
        1.0 if ft["max_run_frac"] <= cs["max_run_frac_p95"] else
        max(0.0, 1.0 - (ft["max_run_frac"] - cs["max_run_frac_p95"]) * 3),
    ]
    return {
        "n_notes": ft["n_notes"],
        "zero_rate": round(ft["zero_rate"], 3),
        "max_run_frac": round(ft["max_run_frac"], 3),
        "int_jsd": round(int_jsd, 3),
        "ctr_jsd": round(ctr_jsd, 3),
        "rep_LxCount": ft["rep_LxCount"],
        "big_leap": round(ft["big_leap"], 3),
        "range": ft["range"],
        "composite": round(sum(parts) / len(parts), 3),
    }


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    csv_out = None
    if "--csv" in sys.argv:
        csv_out = sys.argv[sys.argv.index("--csv") + 1]
    if not paths:
        print("usage: score_generated.py <piece.json|x.mid>... [--csv out]")
        sys.exit(1)
    cs = corpus_stats()
    cols = ["file", "n_notes", "int_jsd", "ctr_jsd", "rep_LxCount",
            "big_leap", "range", "zero_rate", "max_run_frac", "composite"]
    rows = []
    print(f"[corpus] {cs['n_themes']} themes; rep_p10={cs['rep_p10']} "
          f"leap_p90={cs['big_leap_p90']:.3f} "
          f"range=[{cs['range_p10']},{cs['range_p90']}]", file=sys.stderr)
    print("  ".join(f"{c:>11s}" for c in cols))
    for p in paths:
        try:
            r = score(load_melody(p), cs)
        except (KeyError, IndexError, AssertionError, FileNotFoundError) as e:
            print(f"{Path(p).name}: not a scorable melody file ({e!r})",
                  file=sys.stderr)
            continue
        r["file"] = Path(p).name
        rows.append(r)
        print("  ".join(f"{str(r[c]):>11s}" for c in cols))
    if csv_out:
        with open(csv_out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            w.writerows(rows)


if __name__ == "__main__":
    main()
