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
  closure      phrase-ENDING screen (v2, backlog #12): final-note duration
               ratio against the corpus band + where the final note's onset
               sits on the beat grid. Added because run 13 changed every
               ending in a 24-phrase batch and scores.csv came out
               byte-identical — the composite could not see endings at all.
  composite    weighted heuristic 0..1 (documented inline) — a FILTER
               threshold, not a quality ranking. Matt's ears stay the judge.

Batch-level closure (`closure_batch`) is reported separately from the
per-melody screen on purpose: "does this phrase end plausibly" and "does this
BATCH of endings look like the corpus distribution" are different questions,
and the run-13 defect (every phrase ending at exactly ratio 1.0) is only
visible in the second one — 1.0 is MTD's p25, i.e. perfectly legal per melody.

Corpus baseline: computed once over corpus/mtd_full/midi/*_score.mid
(monophonic themes only, same filters as score_repetitiveness) and cached
to corpus/mtd_seg/corpus_stats.json.
"""
import csv
import json
import math
import statistics
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


def self_similarity(mel):
    """Motif-level repetition screen (monotony v2). The first-order screens
    (zero_rate, max_run) only see a single hammered *note*; they miss a whole
    3-6 note figure repeated many times when the individual pitches vary.

    Build the pitch-interval sequence (consecutive semitone deltas). A figure
    of F notes repeated back-to-back makes the interval sequence periodic with
    period F, so we scan motif lengths L=2..6 and, for each, find the single
    L-gram of intervals covering the largest fraction of the sequence:

        cov(L) = min(1, (count of most common L-gram) * L / len(intervals))

    Only grams that actually recur (count >= 2) count as repetition. selfsim is
    the max coverage over L, in 0..1; higher = more hammered/monotonous.
    """
    ivs = [b[1] - a[1] for a, b in zip(mel, mel[1:])]
    best = 0.0
    for L in range(2, 7):
        if len(ivs) - L + 1 < 2:   # need at least two L-grams to call recurrence
            break
        grams = Counter(tuple(ivs[i:i + L]) for i in range(len(ivs) - L + 1))
        top = grams.most_common(1)[0][1]
        if top < 2:
            continue
        best = max(best, min(1.0, top * L / len(ivs)))
    return best


# ------------------------------------------------------------------ closure
GRID_TOL = 1e-3
# Corpus MIDI carries articulation gaps (a notated half note renders as ~0.97
# beats), so an EXACT final >= max(others) test fails on ties that a score
# would call equal: MTD reads 0.115 exact vs 0.351 with any tolerance from 1%
# to 10% — the rate is flat across that whole range, so the jitter is sub-1%
# and there is a clear gap before the next population. 0.98 sits mid-plateau,
# and 0.351 matches what final_note_stats.py gets by snapping to the notated
# grid instead (0.377), by a different route.
LONGEST_TOL = 0.98


def pulse_series(mel):
    """Per-note pulse, corpus convention (final_note_stats.py): IOI for every
    note except the last, which uses its own duration. RAW, not snapped —
    snapping clamps at the 4.0-beat grid top, which would silently compress
    exactly the long-ending tail this screen exists to measure (probed: the
    generated p90 ratio reads 12.0 raw and 12.0 snapped, but a 24-beat final
    note snaps to 4.0 and its phrase-end grid landing flips from true to
    false — an artifact of the grid, not a property of the music)."""
    out = []
    for i, (onset, _p, dur) in enumerate(mel):
        out.append(mel[i + 1][0] - onset if i + 1 < len(mel) else dur)
    return out


def closure_features(mel):
    """How the melody ENDS. All three quantities measured the same way on
    corpus MIDI and on generated piece JSON.

      final_ratio    final pulse / median of the others (corpus median 2.0 in
                     MTD, Nottingham and Essen alike)
      final_longest  final pulse >= every other pulse
      onset_grid     1.0 / 0.5 / 0.0 — the final note's ONSET lands on an
                     integer beat / a half beat / neither, measured relative
                     to the melody's first onset

    onset_grid rather than end-of-phrase grid landing: raw corpus MIDI note
    durations carry articulation gaps, so phrase ENDS land on an integer beat
    in only 1.5% of MTD themes — unusable as a corpus-calibrated screen —
    while final-note ONSETS land in 64% (MTD) / 85% (Nottingham) / 74%
    (Essen). It is also the more diagnostic of the two for generated output:
    run 13's grid completion made phrase ends land 24/24 while the final
    note's onset stayed off the beat in 71% of the same phrases."""
    ps = pulse_series(mel)
    if len(ps) < 4:
        return {"final_ratio": 0.0, "final_longest": 0, "onset_grid": 0.0}
    final, others = ps[-1], ps[:-1]
    med = statistics.median(others)
    onset = mel[-1][0] - mel[0][0]
    if abs(onset - round(onset)) < GRID_TOL:
        grid = 1.0
    elif abs(onset * 2 - round(onset * 2)) < GRID_TOL:
        grid = 0.5
    else:
        grid = 0.0
    return {"final_ratio": (final / med) if med > 0 else 0.0,
            "final_longest": 1 if final >= max(others) * LONGEST_TOL - 1e-9
            else 0,
            "onset_grid": grid}


def closure_score(cf, cs):
    """Per-melody closure screen in 0..1.

    Two terms, weighted 0.6 / 0.4:
      ratio  full credit inside the corpus [p25, p95] band; below p25 it falls
             off linearly to 0 at ratio 0, above p95 linearly to 0 at 3x p95.
             A band, not a target: a ratio of 1.0 IS corpus practice (MTD p25),
             so a per-melody screen must not punish it — that is what
             closure_batch is for.
      grid   1.0 on an integer beat, 0.8 on a half beat, 0.4 otherwise. Mild
             on purpose: the corpus itself is only 64-85% integer, so this
             term separates 29%-on-grid output from 64% without claiming
             off-grid endings are illegal."""
    lo = cs.get("final_ratio_p25", 1.0)
    hi = cs.get("final_ratio_p95", 6.0)
    r = cf["final_ratio"]
    if r < lo:
        ratio_term = max(0.0, r / lo) if lo > 0 else 1.0
    elif r > hi:
        ratio_term = max(0.0, 1.0 - (r - hi) / (2.0 * hi)) if hi > 0 else 0.0
    else:
        ratio_term = 1.0
    grid_term = {1.0: 1.0, 0.5: 0.8}.get(cf["onset_grid"], 0.4)
    return 0.6 * ratio_term + 0.4 * grid_term


def closure_batch(mels, cs):
    """Distributional closure report over a BATCH of melodies vs the corpus.

    The per-melody screen cannot catch "every phrase in this batch ends at
    exactly the same ratio" — each individual value is legal. This is the
    view that caught it: batch quantiles and rates against the corpus ones."""
    cfs = [closure_features(m) for m in mels if len(m) >= 4]
    if not cfs:
        return {}
    rs = sorted(c["final_ratio"] for c in cfs)
    n = len(rs)

    def q(f):
        return rs[min(n - 1, int(f * n))]

    return {"n": n,
            "ratio_p25": round(q(.25), 2), "ratio_med": round(q(.50), 2),
            "ratio_p90": round(q(.90), 2), "ratio_max": round(rs[-1], 2),
            "p_longest": round(sum(c["final_longest"] for c in cfs) / n, 3),
            "p_onset_int": round(sum(1 for c in cfs
                                     if c["onset_grid"] == 1.0) / n, 3),
            "corpus_ratio_p25": cs.get("final_ratio_p25"),
            "corpus_ratio_med": cs.get("final_ratio_p50"),
            "corpus_ratio_p95": cs.get("final_ratio_p95"),
            "corpus_p_longest": cs.get("p_final_longest"),
            "corpus_p_onset_int": cs.get("p_final_onset_int"),
            "over_corpus_p95": round(
                sum(1 for x in rs if x > (cs.get("final_ratio_p95") or 1e9))
                / n, 3)}


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
        "selfsim": self_similarity(mel),
        **closure_features(mel),
    }


# ---------------------------------------------------------------- corpus
def mtd_melodies():
    """Baseline melodies for the MTD anchor (the v1 default)."""
    for f in sorted(MIDI_DIR.glob("*_score.mid")):
        try:
            yield melody_from_midi(f)
        except Exception:  # noqa: BLE001 — malformed midi, skip
            continue


def stats_from_melodies(melodies, limit=CORPUS_LIMIT):
    """Aggregate baseline stats over an iterable of melodies.

    Shared by every corpus anchor (see corpus_baseline.py) so a Nottingham- or
    Essen-anchored baseline is computed with byte-identical feature code — the
    only thing that changes is which melodies go in.
    """
    ih, ch = Counter(), Counter()
    reps, leaps, ranges, zeros, runs, sims = [], [], [], [], [], []
    ratios, longest, onset_int, onset_half = [], 0, 0, 0
    n = 0
    for mel in melodies:
        if n >= limit:
            break
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
        sims.append(ft["selfsim"])
        ratios.append(ft["final_ratio"])
        longest += ft["final_longest"]
        onset_int += 1 if ft["onset_grid"] == 1.0 else 0
        onset_half += 1 if ft["onset_grid"] >= 0.5 else 0
        n += 1
    reps.sort()
    leaps.sort()
    ranges.sort()
    zeros.sort()
    runs.sort()
    sims.sort()
    ratios.sort()
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
        "selfsim_p95": sims[95 * len(sims) // 100],
        # Closure anchors (#12). NOTE for windowed corpora: these are only
        # meaningful over WHOLE tunes — a mid-tune window's last note is not
        # a phrase ending. corpus_baseline.build() therefore recomputes these
        # five keys from the unwindowed feed and overwrites them.
        "final_ratio_p25": round(ratios[len(ratios) // 4], 4),
        "final_ratio_p50": round(ratios[len(ratios) // 2], 4),
        "final_ratio_p95": round(ratios[95 * len(ratios) // 100], 4),
        "p_final_longest": round(longest / max(1, n), 4),
        "p_final_onset_int": round(onset_int / max(1, n), 4),
        "p_final_onset_half": round(onset_half / max(1, n), 4),
    }
    return stats


CLOSURE_KEYS = ("final_ratio_p25", "final_ratio_p50", "final_ratio_p95",
                "p_final_longest", "p_final_onset_int", "p_final_onset_half")


def decode_hists(d):
    """JSON-round-tripped hist keys back to int / (int,int) tuples."""
    d["int_hist"] = {int(k): v for k, v in d["int_hist"].items()}
    d["ctr_hist"] = {tuple(map(int, k.split(","))): v
                     for k, v in d["ctr_hist"].items()}
    return d


def cache_path(corpus="mtd"):
    """MTD keeps the original filename (v1 caches stay valid); other anchors
    get corpus_stats_<name>.json, built by corpus_baseline.py."""
    return CACHE if corpus == "mtd" else HERE / f"corpus_stats_{corpus}.json"


def corpus_stats(corpus="mtd"):
    path = cache_path(corpus)
    if path.exists():
        d = json.loads(path.read_text())
        # Cache auto-upgrade: a missing stat means an older cache predating that
        # screen; fall through to rebuild rather than serve a stale baseline.
        if "selfsim_p95" in d and "final_ratio_p95" in d:
            return decode_hists(d)
    if corpus != "mtd":
        raise SystemExit(
            f"no baseline for corpus '{corpus}' at {path.name} — build it with:"
            f"\n  python corpus/mtd_seg/corpus_baseline.py {corpus}")
    stats = stats_from_melodies(mtd_melodies())
    path.write_text(json.dumps(stats))
    print(f"[corpus] built stats over {stats['n_themes']} themes -> {path}",
          file=sys.stderr)
    return decode_hists(stats)


# ---------------------------------------------------------------- scoring
def score(mel, cs):
    ft = features(mel)
    int_jsd = jsd(ft["int_hist"], cs["int_hist"])
    ctr_jsd = jsd(ft["ctr_hist"], cs["ctr_hist"])
    clo = closure_score(ft, cs)
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
        # Motif-level monotony (v2): penalize self-similarity past corpus p95.
        1.0 if ft["selfsim"] <= cs["selfsim_p95"] else
        max(0.0, 1.0 - (ft["selfsim"] - cs["selfsim_p95"]) * 3),
        # Phrase ending (#12). Before this the composite had NO ending term:
        # run 13 moved every ending in a 24-phrase batch and scores.csv was
        # byte-identical.
        clo,
    ]
    return {
        "n_notes": ft["n_notes"],
        "final_ratio": round(ft["final_ratio"], 3),
        "final_longest": ft["final_longest"],
        "onset_grid": ft["onset_grid"],
        "closure": round(clo, 3),
        "zero_rate": round(ft["zero_rate"], 3),
        "max_run_frac": round(ft["max_run_frac"], 3),
        "selfsim": round(ft["selfsim"], 3),
        "int_jsd": round(int_jsd, 3),
        "ctr_jsd": round(ctr_jsd, 3),
        "rep_LxCount": ft["rep_LxCount"],
        "big_leap": round(ft["big_leap"], 3),
        "range": ft["range"],
        "composite": round(sum(parts) / len(parts), 3),
    }


def main():
    # Hand-rolled parse (no argparse dep): --csv consumes the *next* token as
    # its output filename so it isn't mistaken for an input melody path.
    args = sys.argv[1:]
    paths, csv_out, corpus, i = [], None, "mtd", 0
    while i < len(args):
        a = args[i]
        if a == "--csv":
            if i + 1 < len(args):
                csv_out = args[i + 1]
                i += 2
                continue
        elif a == "--corpus":
            if i + 1 < len(args):
                corpus = args[i + 1]
                i += 2
                continue
        elif not a.startswith("--"):
            paths.append(a)
        i += 1
    if not paths:
        print("usage: score_generated.py <piece.json|x.mid>... "
              "[--csv out] [--corpus mtd|nottingham|essen]")
        sys.exit(1)
    cs = corpus_stats(corpus)
    cols = ["file", "n_notes", "int_jsd", "ctr_jsd", "rep_LxCount",
            "big_leap", "range", "zero_rate", "max_run_frac", "selfsim",
            "final_ratio", "closure", "composite"]
    rows, mels = [], []
    print(f"[corpus:{corpus}] {cs['n_themes']} themes; rep_p10={cs['rep_p10']} "
          f"leap_p90={cs['big_leap_p90']:.3f} "
          f"range=[{cs['range_p10']},{cs['range_p90']}]", file=sys.stderr)
    print("  ".join(f"{c:>11s}" for c in cols))
    for p in paths:
        try:
            mel = load_melody(p)
            r = score(mel, cs)
        except (KeyError, IndexError, AssertionError, FileNotFoundError) as e:
            print(f"{Path(p).name}: not a scorable melody file ({e!r})",
                  file=sys.stderr)
            continue
        r["file"] = Path(p).name
        rows.append(r)
        mels.append(mel)
        print("  ".join(f"{str(r[c]):>11s}" for c in cols))
    if len(mels) > 1:
        cb = closure_batch(mels, cs)
        print(f"\nclosure vs corpus ({corpus}, n={cb['n']}): "
              f"ratio med {cb['ratio_med']} (corpus {cb['corpus_ratio_med']}), "
              f"p90 {cb['ratio_p90']} max {cb['ratio_max']}, "
              f"{cb['over_corpus_p95']:.0%} over corpus p95 "
              f"({cb['corpus_ratio_p95']}); final-is-longest "
              f"{cb['p_longest']} (corpus {cb['corpus_p_longest']}); "
              f"onset-on-beat {cb['p_onset_int']} "
              f"(corpus {cb['corpus_p_onset_int']})")
    if csv_out:
        # score() returns more fields than the console shows (final_longest,
        # onset_grid); the CSV gets all of them.
        csv_cols = cols + [k for k in rows[0] if k not in cols] if rows else cols
        with open(csv_out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=csv_cols)
            w.writeheader()
            w.writerows(rows)


if __name__ == "__main__":
    main()
