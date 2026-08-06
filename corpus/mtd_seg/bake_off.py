"""FigureGenerator method bake-off (comp backlog #4).

Runs every registered generator (figuregen.build_methods) through the corpus
"ears" (score_generated) and prints a per-method comparison table. Two views:

  pooled JSD   — pool ALL generated intervals/contours per method into one
                 histogram, JSD vs corpus. Stable distributional fit; the core
                 "how corpus-like is this method" number (lower = better).
  composite    — per-melody composite (score_generated.score), aggregated
                 mean / median / p10 over the K melodies. End-to-end filter view.

A `corpus` reference row (real MTD themes, same aggregation) marks the ceiling.

Realization is engine-free: scale-degrees -> semitones in C major via
degree_to_semitone, the same math markov_phrase.predict_relative_semitones uses
(run-3-verified against the mforce_cli engine round-trip). So the bake-off is
pure-Python and freely iterable — no build/render step.

Usage:
  python bake_off.py                         # K=60 melodies, L=16 notes, seed 1234
  python bake_off.py --k 120 --len 16 --seed 7 --csv bakeoff.csv
"""
import argparse
import csv
import pathlib
import random
import sys
from collections import Counter

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from figuregen import load_corpus, build_methods                     # noqa: E402
from score_generated import (corpus_stats, score, interval_hist,     # noqa: E402
                             contour_hist, jsd, melody_from_midi, MIDI_DIR)

MAJOR = [0, 2, 4, 5, 7, 9, 11]


def degree_to_semitone(d):
    octs, deg = divmod(d, 7)
    return 12 * octs + MAJOR[deg]


def realize(steps, pulses, base_midi=60):
    """(steps, pulses) figure -> [(onset_beat, midi, dur), ...]. step[0]==0 stays."""
    deg, onset, mel = 0, 0.0, []
    for s, p in zip(steps, pulses):
        deg += s
        mel.append((onset, base_midi + degree_to_semitone(deg), p))
        onset += p
    return mel


def pctl(xs, q):
    if not xs:
        return 0.0
    s = sorted(xs)
    return s[min(len(s) - 1, int(q * len(s)))]


def eval_method(gen, k, length, cs, rng):
    """Return a row dict: pooled JSDs + composite aggregates over k melodies."""
    pooled_int, pooled_ctr = Counter(), Counter()
    comps, leaps, ranges = [], [], []
    for _ in range(k):
        steps, pulses = gen.figure(length, rng)
        mel = realize(steps, pulses)
        pooled_int.update(interval_hist(mel))
        pooled_ctr.update(contour_hist(mel))
        r = score(mel, cs)
        # score() returns composite=None for an unscorable (near-empty)
        # melody rather than a number that averages in silently (#19).
        if r["scorable"]:
            comps.append(r["composite"])
        leaps.append(r["big_leap"])
        ranges.append(r["range"])
    return {
        "method": gen.name,
        "int_jsd": round(jsd(pooled_int, cs["int_hist"]), 3),
        "ctr_jsd": round(jsd(pooled_ctr, cs["ctr_hist"]), 3),
        "comp_mean": round(sum(comps) / len(comps), 3),
        "comp_med": round(pctl(comps, 0.5), 3),
        "comp_p10": round(pctl(comps, 0.10), 3),
        "big_leap": round(sum(leaps) / len(leaps), 3),
        "range_mean": round(sum(ranges) / len(ranges), 1),
    }


def reference_melodies(corpus, rng):
    """Real melodies from `corpus`, in shuffled order — the ceiling row's feed.

    Non-MTD corpora come through corpus_baseline's windowed feeds so the
    reference melodies are the same length scale as the generated ones (see
    the windowing note in corpus_baseline.py).
    """
    if corpus == "mtd":
        files = sorted(MIDI_DIR.glob("*_score.mid"))
        rng.shuffle(files)
        for f in files:
            try:
                yield melody_from_midi(f)
            except Exception:  # noqa: BLE001
                continue
        return
    import corpus_baseline as cb  # local: keeps the MTD path dependency-free
    cfg = cb.PER_CORPUS[corpus]
    mels = list(cb.windowed(cb.FEEDS[corpus](), cfg["window"]))
    rng.shuffle(mels)
    yield from mels


def corpus_reference(k, cs, rng, corpus="mtd"):
    """Score k real corpus melodies through the same aggregation — the ceiling."""
    pooled_int, pooled_ctr = Counter(), Counter()
    comps, leaps, ranges = [], [], []
    for mel in reference_melodies(corpus, rng):
        if len(comps) >= k:
            break
        if len(mel) < 8:
            continue
        pooled_int.update(interval_hist(mel))
        pooled_ctr.update(contour_hist(mel))
        r = score(mel, cs)
        # score() returns composite=None for an unscorable (near-empty)
        # melody rather than a number that averages in silently (#19).
        if r["scorable"]:
            comps.append(r["composite"])
        leaps.append(r["big_leap"])
        ranges.append(r["range"])
    return {
        "method": "corpus",
        "int_jsd": round(jsd(pooled_int, cs["int_hist"]), 3),
        "ctr_jsd": round(jsd(pooled_ctr, cs["ctr_hist"]), 3),
        "comp_mean": round(sum(comps) / len(comps), 3),
        "comp_med": round(pctl(comps, 0.5), 3),
        "comp_p10": round(pctl(comps, 0.10), 3),
        "big_leap": round(sum(leaps) / len(leaps), 3),
        "range_mean": round(sum(ranges) / len(ranges), 1),
    }


COLS = ["method", "int_jsd", "ctr_jsd", "comp_mean", "comp_med", "comp_p10",
        "big_leap", "range_mean"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--k", type=int, default=60, help="melodies per method")
    ap.add_argument("--len", type=int, default=16, dest="length",
                    help="notes per generated melody")
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--tokens", default=None,
                    help="path to a markov_tokens.json (default: the --corpus "
                         "token file). Point at another corpus's token file to "
                         "train on one corpus and score against another.")
    ap.add_argument("--corpus", default="mtd",
                    choices=["mtd", "nottingham", "essen",
                             "essen_europa", "essen_asia"],
                    help="scoring ANCHOR: whose baseline stats + ceiling row. "
                         "Also selects the default training tokens, so "
                         "`--corpus essen` = Essen-trained, Essen-anchored. "
                         "Build anchors with corpus_baseline.py.")
    ap.add_argument("--csv", default=None)
    args = ap.parse_args()

    tokens = args.tokens
    if tokens is None and args.corpus != "mtd":
        # essen_europa / essen_asia are anchor subsets of one token file.
        tokens = str(HERE / f"{args.corpus.split('_')[0]}_tokens.json")
    streams, meta = load_corpus(tokens) if tokens else load_corpus()
    cs = corpus_stats(args.corpus)
    methods = build_methods(streams, meta)

    print(f"[bake-off] {len(methods)} methods x {args.k} melodies x {args.length} "
          f"notes, seed={args.seed}; anchor={args.corpus} "
          f"({cs['n_themes']} segments)", file=sys.stderr)

    rows = []
    # corpus ceiling first (its own rng so method seeds are stable if --k changes)
    rows.append(corpus_reference(args.k, cs, random.Random(args.seed ^ 0x9E3779B9),
                                 args.corpus))
    for gen in methods:
        rows.append(eval_method(gen, args.k, args.length, cs,
                                random.Random(args.seed)))

    print("  ".join(f"{c:>10s}" for c in COLS))
    for r in rows:
        print("  ".join(f"{str(r[c]):>10s}" for c in COLS))

    if args.csv:
        pathlib.Path(args.csv).parent.mkdir(parents=True, exist_ok=True)
        with open(args.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=COLS)
            w.writeheader()
            w.writerows(rows)
        print(f"wrote {args.csv}", file=sys.stderr)


if __name__ == "__main__":
    main()
