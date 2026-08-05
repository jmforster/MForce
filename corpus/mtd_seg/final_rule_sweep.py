"""Final-note rule calibration harness (backlog #16).

Replays the markov_phrase --v3 AFTER draw loop WITHOUT rendering, so a rule
arm can be measured over hundreds of phrases in seconds instead of a 24-WAV
batch. The rng streams are constructed exactly as main_v3() constructs them
(structure / figure / build / xform / final), so a batch measured here has
the same phrases the renderer would produce at the same --seed.

Reports the closure distribution against the corpus profile measured by
final_note_stats.py (MTD n=1632 / Nottingham n=1024).

  python final_rule_sweep.py --n 200 --rules longest,corpus,old
  python final_rule_sweep.py --n 200 --rules longest --breakdown
"""
import argparse
import json
import pathlib
import random
import statistics as _stats
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import markov_phrase as mp          # noqa: E402
from markov_model import MarkovModel  # noqa: E402


def corpus_targets(corpus="mtd"):
    """Percentiles of the empirical ratio histogram + P(final >= max)."""
    prof = json.loads((HERE / "final_note_profile.json")
                      .read_text(encoding="utf-8"))
    p = prof.get(corpus) or prof["mtd"]
    vals = []
    for k, w in p["hist"].items():
        vals.extend([float(k)] * int(w))
    vals.sort()
    def pct(q):
        return vals[min(len(vals) - 1, int(q * len(vals)))]
    return {"p25": pct(0.25), "p50": pct(0.50), "p95": pct(0.95),
            "p_ge_max": p["p_ge_max"], "n": p["n"]}


def phrase_batch(model, n, seed, rule, corpus="mtd", range_cap=19,
                 max_tries=24, repeat_xform_prob=0.3):
    """One --v3 AFTER batch, unrendered. Returns per-phrase closure rows."""
    srng = random.Random(seed ^ 0x5EED)
    rows = []
    for i in range(n):
        pattern = srng.choice(mp.V3_PATTERNS)
        transform = (srng.choice(mp.V3_TRANSFORMS) if "'" in pattern
                     else "none")
        contour = srng.choice(list(mp.CONTOURS))
        use_ctr = (i % 2 == 1)
        frng = random.Random((seed ^ 0xF16E) + i * 7919)
        brng = random.Random((seed ^ 0xB01D) + i * 15485863)
        xrng = random.Random((seed ^ 0x0F0F) + i * 60013)
        tries = 0
        while True:
            tries += 1
            kmin = 4 if (transform in mp.INDEPENDENT_OPS
                         or transform == "mixed") else 3
            figA = mp._sample_figure_sized(model, frng, kmin=kmin)
            if use_ctr:
                figB, _fit = mp.sample_contrast_figB(figA, model, frng)
            else:
                figB = mp._sample_figure_sized(model, frng)
            motif_list, refs, conns, info = mp.build_phrase_v3(
                figA, figB, pattern, transform, contour, model, brng,
                corpus=corpus, final_rule=rule,
                final_rng=random.Random((seed ^ 0xF1A1) + i * 104729),
                repeat_xform_prob=repeat_xform_prob, xform_rng=xrng,
                range_cap=range_cap)
            mdict = dict(motif_list)
            names = [nm for nm, _ in motif_list]
            span = mp.predicted_range(mdict, names, conns)
            if span <= range_cap or tries >= max_tries:
                break
        durs = [u["duration"] for _, f in motif_list for u in f["units"]]
        rest = durs[:-1] or durs
        med = _stats.median(rest)
        gp = info.get("grid_pad", 0.0)
        rows.append({"i": i, "ratio": durs[-1] / med if med else 0.0,
                     "ratio_pre_grid": (durs[-1] - gp) / med if med else 0.0,
                     "grid_pad": gp,
                     "ge_max": 1 if durs[-1] >= max(rest) - 1e-9 else 0,
                     "final": durs[-1], "median_pulse": med,
                     "max_rest": max(rest), "n_notes": len(durs),
                     "total": round(sum(durs), 6), "span": span,
                     "final_ext": info["final_ext"]})
    return rows


def summarize(label, rows, tgt):
    rs = sorted(r["ratio"] for r in rows)
    n = len(rs)
    def pct(q):
        return rs[min(n - 1, int(q * n))]
    over = sum(1 for x in rs if x > tgt["p95"] + 1e-9) / n
    gemax = sum(r["ge_max"] for r in rows) / n
    print(f"--- {label} (n={n}) ---")
    print(f"  ratio p25/p50/p95   {pct(.25):.2f} / {pct(.50):.2f} / "
          f"{pct(.95):.2f}   [corpus {tgt['p25']:.2f} / {tgt['p50']:.2f} / "
          f"{tgt['p95']:.2f}]")
    print(f"  mean ratio          {sum(rs)/n:.2f}   max {rs[-1]:.2f}")
    print(f"  over corpus p95     {over:.3f}   [corpus 0.050 by definition]")
    print(f"  final is longest    {gemax:.3f}   [corpus {tgt['p_ge_max']:.3f}]")
    print(f"  ends on int beat    "
          f"{sum(1 for r in rows if abs(r['total']-round(r['total']))<1e-3)/n:.3f}")
    pg = sorted(r.get("ratio_pre_grid", r["ratio"]) for r in rows)
    print(f"  ratio BEFORE grid   p50 {pg[n//2]:.2f}  p95 "
          f"{pg[min(n-1,int(.95*n))]:.2f}   (grid completion adds the rest)")
    return {"p25": pct(.25), "p50": pct(.50), "p95": pct(.95),
            "mean": sum(rs) / n, "over_p95": over, "ge_max": gemax}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--corpus", default="mtd")
    ap.add_argument("--rules", default="longest",
                    help="comma-separated final-rule arms to measure")
    ap.add_argument("--breakdown", action="store_true",
                    help="print per-phrase rows for the first arm")
    args = ap.parse_args()

    import markov_model as mm
    tok = (mm.TOKENS if args.corpus.split("_")[0] == "mtd"
           else mm.ROOT / f"{args.corpus.split('_')[0]}_tokens.json")
    model = MarkovModel.load(tok)
    tgt = corpus_targets(args.corpus)
    print(f"corpus target profile: {args.corpus} (n={tgt['n']})\n")
    out = {}
    for rule in args.rules.split(","):
        rows = phrase_batch(model, args.n, args.seed, rule, corpus=args.corpus)
        out[rule] = summarize(rule, rows, tgt)
        if args.breakdown:
            for r in rows[:24]:
                print(f"    p{r['i']:02d} ratio={r['ratio']:.2f} "
                      f"final={r['final']:.2f} med={r['median_pulse']:.2f} "
                      f"maxrest={r['max_rest']:.2f} ext={r['final_ext']}")
        print()
    return out


if __name__ == "__main__":
    main()
