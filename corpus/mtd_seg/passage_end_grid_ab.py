"""Three-arm endGrid A/B for the engine passage strategies (comp REVIEW 17).

Re-creates run 18's `renders/passage_end_grid/{off,beat,bar}` arms, which were
purged 2026-08-08 before Matt could verdict them (backlog #22). The original
arm driver was a one-off and never kept; this is it, kept.

Same 21 passages as engine_passage_strategies.py (5 strategies x 3 takes at
seed 7311 + 1000*k, plus the 6 outliers at seed 7388), with every passage's
`endGrid` forced per arm:

  off   endGrid = 0    (no quantization — what run 18 replaced)
  beat  endGrid = 1.0  (integer beat, the shipped default)
  bar   endGrid = 4.0  (4/4 barline)

  python passage_end_grid_ab.py            # renders all three arms + table
  python passage_end_grid_ab.py --outroot renders/comp/audition/passage_end_grid
"""
import argparse
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import engine_passage_strategies as eps                            # noqa: E402

REPO = HERE.parent.parent
ARMS = [("off", 0.0), ("beat", 1.0), ("bar", 4.0)]
EPS_TOL = 1e-3


def inject_end_grid(template, value):
    for part in template["parts"]:
        for passage in part["passages"].values():
            passage["endGrid"] = value
    return template


def melody_end_beat(out_prefix):
    """End of the FIRST part carrying events — the strategy's melody. The max
    over all parts would let a 4-beat-unit pedal (always barline-aligned) mask
    an off-grid melody ending, which is exactly the defect under audit."""
    ev = json.loads((REPO / (out_prefix + "_1.json")).read_text(encoding="utf-8"))
    for part in ev.get("parts", []):
        evs = part.get("events", [])
        if evs:
            return max(float(e["beat"]) + float(e["data"].get("duration", 0.0))
                       for e in evs)
    return 0.0


def on_grid(t, grid):
    return abs(t - grid * round(t / grid)) < EPS_TOL


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--takes", type=int, default=3)
    ap.add_argument("--seed", type=int, default=7311)
    ap.add_argument("--bpm", type=float, default=92.0)
    ap.add_argument("--outroot", default="renders/comp/audition/passage_end_grid")
    args = ap.parse_args()

    jobs = []
    for name in eps.STRATEGIES:
        for k in range(args.takes):
            jobs.append((f"{name}_{k}", name, {}, args.seed + 1000 * k))
    for label, name, kw in eps.OUTLIERS:
        jobs.append((label, name, kw, args.seed + 77))

    table = {}
    for arm, grid in ARMS:
        armdir = f"{args.outroot}/{arm}"
        (REPO / armdir).mkdir(parents=True, exist_ok=True)
        rows = []
        for label, name, kw, seed in jobs:
            t = inject_end_grid(eps.STRATEGIES[name](seed, args.bpm, **kw), grid)
            prefix = f"{armdir}/{label}"
            eps.render(t, prefix)
            end = melody_end_beat(prefix)
            rows.append((label, end))
            print(f"{arm}/{label}: ends at {end:.3f}")
        table[arm] = rows

    print(f"\n=== ending grid, {len(jobs)} passages per arm ===")
    print(f"{'arm':6} {'on integer beat':>16} {'on 4/4 barline':>16}")
    for arm, rows in table.items():
        beat = sum(1 for _, e in rows if on_grid(e, 1.0))
        bar = sum(1 for _, e in rows if on_grid(e, 4.0))
        print(f"{arm:6} {beat:>13}/{len(rows)} {bar:>13}/{len(rows)}")


if __name__ == "__main__":
    main()
