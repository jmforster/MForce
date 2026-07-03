"""Generate figure cells from the Markov model, in the audition.py pool schema.
Also dumps the model's most-probable paths (the figures the corpus "recognizes").

Reads:
  markov_tokens.json  (via markov_model.MarkovModel.load)

Writes:
  pools/generated.json   model-sampled cells
  markov_top_paths.txt   N most-probable k-note paths per length

Usage:
  python markov_generate.py                 # default N=5 per length 2..9, seed 1234
  python markov_generate.py --n 8 --seed 7
"""
import argparse, json, pathlib, random

from markov_model import MarkovModel

ROOT     = pathlib.Path(__file__).resolve().parent
POOL_DIR = ROOT / "pools"
POOL_DIR.mkdir(parents=True, exist_ok=True)
LENGTHS  = range(2, 10)   # 2..9 notes


def make_entry(eid, step, pulse, kind, seed):
    return {
        "id": eid,
        "num_notes": len(step),
        "total_beats": round(sum(pulse), 6),
        "pulse": [round(p, 6) for p in pulse],
        "step": step,
        "chromatic": [0] * len(step),
        "source": {"composer": kind, "work": f"k{len(step)}",
                   "instrument": "generated", "mtdid": "-",
                   "work_title": f"{kind} cell", "key_mode": "major",
                   "polyphony": "Monophon", "seed": seed},
    }


def gen_model(model, k, rng):
    """k-note figure: anchor step[0]=0 with a real opening duration, then (k-1)
    transitions sampled from the chain — note 1 is conditioned on the opening."""
    bos = rng.choice(model.bos_tokens)              # (BOS_STEP, note-0 duration)
    pulse0 = bos[1]
    history = [bos]
    transitions = []
    for _ in range(k - 1):
        t = model.next_token(history, rng)
        transitions.append(t)
        history.append(t)
    step  = [0] + [t[0] for t in transitions]
    pulse = [pulse0] + [t[1] for t in transitions]
    return step, pulse


def dump_top_paths(model, path, lengths=(3, 4, 5, 6), n=8):
    lines = ["MTD Markov — most-probable paths (the 'recognized' figures)\n"]
    for k in lengths:
        lines.append(f"=== {k}-note figures (top {n}) ===")
        for lp, seq in model.top_paths(k, n=n):
            steps = [0] + [t[0] for t in seq]
            pulses = [seq[0][1]] + [t[1] for t in seq]  # note0 pulse ~ first token's
            cum, acc = [0], 0
            for d in steps[1:]:
                acc += d; cum.append(acc)
            steps_s  = " ".join(f"{d:+d}" if i else "0" for i, d in enumerate(steps))
            pulses_s = " ".join(f"{p:g}" for p in pulses)
            lines.append(f"  logp={lp:7.2f}  deg={cum}  step=[{steps_s}]  pulse=[{pulses_s}]")
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=5, help="cells per length")
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()

    gen_pool = []
    for k in LENGTHS:
        for i in range(args.n):
            st, pu = gen_model(model, k, rng)
            gen_pool.append(make_entry(f"gen_k{k}_{i}", st, pu, "model", args.seed))

    (POOL_DIR / "generated.json").write_text(json.dumps(gen_pool, indent=2) + "\n", encoding="utf-8")
    print(f"wrote pools/generated.json ({len(gen_pool)} cells)  seed={args.seed}\n")

    dump_top_paths(model, ROOT / "markov_top_paths.txt")


if __name__ == "__main__":
    main()
