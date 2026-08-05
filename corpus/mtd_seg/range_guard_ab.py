"""Range-guard reversion A/B (backlog #17).

When a stack of repeat-transforms pushes a phrase past the range cap, the
guard reverts transforms until it fits. v1 reverted LAST-APPLIED first, which
punishes an innocent late transform for an earlier one's overshoot (run 16's
p18: an ornament died so that an invert three occurrences earlier could stay).
v2 reverts the OFFENDER — the transform whose removal recovers the most span.

Both paths consume no rng, so the arms are like-for-like by construction; this
script measures how often they differ and whether v2 keeps more transforms.

  python range_guard_ab.py --n 400 --range-cap 12 --prob 1.0
"""
import argparse
import pathlib
import random
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import markov_phrase as mp                        # noqa: E402
from markov_model import MarkovModel              # noqa: E402


def one_phrase(model, seed, i, rule, cap, prob, offender_first):
    """One phrase draw, no rendering. Returns the guard bookkeeping."""
    # Structure stream replayed exactly as main_v3 draws it — the transform
    # roll is CONDITIONAL on the pattern carrying a prime, so an unconditional
    # draw would desync every later index from the rendered batch.
    srng = random.Random(seed ^ 0x5EED)
    for _ in range(i + 1):
        pattern = srng.choice(mp.V3_PATTERNS)
        transform = (srng.choice(mp.V3_TRANSFORMS) if "'" in pattern
                     else "none")
        contour = srng.choice(list(mp.CONTOURS))
    frng = random.Random((seed ^ 0xF16E) + i * 7919)
    brng = random.Random((seed ^ 0xB01D) + i * 15485863)
    xrng = random.Random((seed ^ 0x0F0F) + i * 60013)
    kmin = 4 if (transform in mp.INDEPENDENT_OPS or transform == "mixed") else 3
    figA = mp._sample_figure_sized(model, frng, kmin=kmin)
    figB = (mp.sample_contrast_figB(figA, model, frng)[0] if i % 2
            else mp._sample_figure_sized(model, frng))
    mp.RANGE_GUARD_OFFENDER_FIRST = offender_first
    motif_list, refs, conns, info = mp.build_phrase_v3(
        figA, figB, pattern, transform, contour, model, brng,
        corpus="mtd", final_rule=rule,
        final_rng=random.Random((seed ^ 0xF1A1) + i * 104729),
        repeat_xform_prob=prob, xform_rng=xrng, range_cap=cap)
    mdict = dict(motif_list)
    span = mp.predicted_range(mdict, [n for n, _ in motif_list], conns)
    return {"kept": [(o["idx"], o["op"]) for o in info["repeat_ops"]],
            "reverted": [(o["idx"], o["op"]) for o in info["reverted_ops"]],
            "span": span, "pattern": pattern}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--range-cap", type=int, default=12, dest="cap")
    ap.add_argument("--prob", type=float, default=1.0)
    ap.add_argument("--rule", default="calib")
    args = ap.parse_args()

    import markov_model as mm
    model = MarkovModel.load(mm.TOKENS)
    fired = diff = kept_v1 = kept_v2 = 0
    examples = []
    for i in range(args.n):
        a = one_phrase(model, args.seed, i, args.rule, args.cap, args.prob,
                       False)
        b = one_phrase(model, args.seed, i, args.rule, args.cap, args.prob,
                       True)
        assert a["span"] <= args.cap or not a["reverted"], "guard escaped"
        if a["reverted"] or b["reverted"]:
            fired += 1
            kept_v1 += len(a["kept"])
            kept_v2 += len(b["kept"])
            if a["kept"] != b["kept"] or a["reverted"] != b["reverted"]:
                diff += 1
                if len(examples) < 8:
                    examples.append((i, a, b))
    print(f"phrases                {args.n} (cap={args.cap}, "
          f"repeat_xform_prob={args.prob})")
    print(f"guard fired            {fired}")
    print(f"arms differ            {diff}")
    print(f"transforms kept  v1    {kept_v1}")
    print(f"transforms kept  v2    {kept_v2}  "
          f"(offender-first keeps {kept_v2 - kept_v1:+d})")
    for i, a, b in examples:
        print(f"\n  p{i:02d} {a['pattern']}")
        print(f"    v1 kept={a['kept']} reverted={a['reverted']} "
              f"span={a['span']}")
        print(f"    v2 kept={b['kept']} reverted={b['reverted']} "
              f"span={b['span']}")


if __name__ == "__main__":
    main()
