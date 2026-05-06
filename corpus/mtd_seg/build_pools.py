"""Aggregate per-theme figure JSON files into three flat pool files.

Reads:
  figures/MTD####.json (one entry per theme)

Writes:
  pools/pulse.json    — flat list, each entry has pulse + total_beats + num_notes + source
  pools/step.json     — flat list, each entry has step + chromatic + num_notes + source
  pools/paired.json   — flat list, full Figure data + source

Each entry's `id` is stable across the three pools so they can be cross-referenced.

Usage:
  python build_pools.py
"""
import json, pathlib

ROOT     = pathlib.Path(__file__).resolve().parent
FIG_DIR  = ROOT / "figures"
POOL_DIR = ROOT / "pools"
POOL_DIR.mkdir(parents=True, exist_ok=True)


def source_block(theme):
    return {
        "mtdid":      theme["mtdid"],
        "composer":   theme["composer"],
        "work":       theme["work"],
        "work_title": theme["work_title"],
        "key_mode":   theme["key"]["mode"],
        "instrument": theme["instrument"],
        "polyphony":  theme["polyphony"],
    }


def main():
    pulse_pool  = []
    step_pool   = []
    paired_pool = []

    files = sorted(FIG_DIR.glob("MTD*.json"))
    if not files:
        print(f"no figure files in {FIG_DIR}")
        return

    for fp in files:
        theme = json.loads(fp.read_text(encoding="utf-8"))
        src = source_block(theme)
        for fig in theme["figures"]:
            common = {
                "id":          fig["id"],
                "num_notes":   fig["num_notes"],
                "total_beats": fig["total_beats"],
                "source":      src,
            }
            pulse_pool.append({**common, "pulse": fig["pulse"]})
            step_pool.append({**common, "step": fig["step"], "chromatic": fig["chromatic"]})
            paired_pool.append({
                **common,
                "pulse":     fig["pulse"],
                "step":      fig["step"],
                "chromatic": fig["chromatic"],
            })

    (POOL_DIR / "pulse.json").write_text(json.dumps(pulse_pool, indent=2) + "\n", encoding="utf-8")
    (POOL_DIR / "step.json").write_text(json.dumps(step_pool, indent=2) + "\n", encoding="utf-8")
    (POOL_DIR / "paired.json").write_text(json.dumps(paired_pool, indent=2) + "\n", encoding="utf-8")

    print(f"wrote pools/pulse.json   ({len(pulse_pool)} entries)")
    print(f"wrote pools/step.json    ({len(step_pool)} entries)")
    print(f"wrote pools/paired.json  ({len(paired_pool)} entries)")


if __name__ == "__main__":
    main()
