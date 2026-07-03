"""Batch-render the model and random pools to MIDI for A/B listening.

Reads:
  pools/generated.json

Writes:
  auditions/markov/model/<id>.mid

Usage:
  python markov_audition.py            # render both pools at C4, 120 BPM
  python markov_audition.py --anchor 67 --bpm 100
"""
import argparse, json, pathlib
from collections import defaultdict

from audition import build_midi, DEFAULT_ANCHOR_MIDI, DEFAULT_BPM

ROOT     = pathlib.Path(__file__).resolve().parent
POOL_DIR = ROOT / "pools"
OUT_DIR  = ROOT / "auditions" / "markov"


def render_pool(name, anchor, bpm):
    pool = json.loads((POOL_DIR / f"{name}.json").read_text(encoding="utf-8"))
    sub = OUT_DIR / ("model" if name == "generated" else "random")
    sub.mkdir(parents=True, exist_ok=True)
    by_len = defaultdict(int)
    for e in pool:
        (sub / f"{e['id']}.mid").write_bytes(build_midi(e, anchor_midi=anchor, bpm=bpm))
        by_len[e["num_notes"]] += 1
    print(f"{name:9s} -> {sub.relative_to(ROOT)}  "
          f"({len(pool)} cells, lengths { {k: by_len[k] for k in sorted(by_len)} })")
    return sub


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--anchor", type=int, default=DEFAULT_ANCHOR_MIDI)
    ap.add_argument("--bpm", type=int, default=DEFAULT_BPM)
    args = ap.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    render_pool("generated", args.anchor, args.bpm)
    print(f"\nrendered to {OUT_DIR.relative_to(ROOT.parent.parent)}/model")


if __name__ == "__main__":
    main()
