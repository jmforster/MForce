"""Tagged, weighted Markov atom pool for the constrained-atom supply layer.

Samples the Phase-1 Markov model across note lengths, leap-caps, tags each atom with
measured noteCount / totalBeats / contour and a generative count (weight), and writes
lib/figures/markov_pool.json. The C++ PoolFigureBuilder selects from it by constraint.
"""
import argparse, json, pathlib, random
from collections import Counter

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
OUT  = REPO / "lib" / "figures" / "markov_pool.json"
LEAP_CAP = 7


# --------------------------------------------------------------------------- #
# Task 1: contour classifier (5-class, R=2)
# --------------------------------------------------------------------------- #
def classify_contour(steps, R=2):
    d, c = [], 0
    for s in steps:
        c += s; d.append(c)
    e, hi, lo = d[-1], max(d), min(d)
    upExc   = hi - max(0, e)
    downExc = min(0, e) - lo
    if hi - lo <= 1 and abs(e) <= 1:
        return "Level"
    if upExc >= R and upExc >= downExc:
        return "Arch"
    if downExc >= R and downExc > upExc:
        return "Valley"
    if e > 0:
        return "Up"
    if e < 0:
        return "Down"
    return "Level"


# --------------------------------------------------------------------------- #
# Task 2: leap-cap, tag, weighted dedupe, emit
# --------------------------------------------------------------------------- #
def within_leap_cap(steps):
    return all(abs(s) <= LEAP_CAP for s in steps)


def tag_atom(steps, pulses):
    return {
        "units": [{"duration": float(pulses[i]), "step": int(steps[i])}
                  for i in range(len(steps))],
        "noteCount": len(steps),
        "totalBeats": round(sum(pulses), 6),
        "contour": classify_contour(steps),
        "count": 1,
    }


def _key(steps, pulses):
    return (tuple(int(s) for s in steps), tuple(round(float(p), 6) for p in pulses))


def accumulate(pool, steps, pulses):
    k = _key(steps, pulses)
    if k in pool:
        pool[k]["count"] += 1
    else:
        pool[k] = tag_atom(steps, pulses)


def generate_pool(model, rng, per_k=5000, kmin=2, kmax=9):
    from markov_generate import gen_model
    pool = {}
    for k in range(kmin, kmax + 1):
        for _ in range(per_k):
            step, pulse = gen_model(model, k, rng)
            if within_leap_cap(step):
                accumulate(pool, step, pulse)
    return list(pool.values())


def main():
    from markov_model import MarkovModel
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-k", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    atoms = generate_pool(model, rng, per_k=args.per_k)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "version": 1, "source": "mtd_markov_order2", "leapCapDegrees": LEAP_CAP,
        "atoms": atoms,
    }), encoding="utf-8")

    cov = Counter((a["noteCount"], a["contour"]) for a in atoms)
    print(f"wrote {OUT.relative_to(REPO)}  ({len(atoms)} unique atoms)")
    print("coverage (noteCount x contour) — unique atoms:")
    for k in range(2, 10):
        row = {c: cov.get((k, c), 0) for c in ["Up", "Down", "Arch", "Valley", "Level"]}
        empties = [c for c, n in row.items() if n == 0]
        print(f"  k={k}: {row}" + (f"  EMPTY: {empties}" if empties else ""))


if __name__ == "__main__":
    main()
