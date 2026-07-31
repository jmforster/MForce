"""Render a CMA-ES clarinet best patch on a 5-note chalumeau->clarion ladder.

Usage: python render_clarinet_ladder.py [run_name] [out.wav]
Defaults: run clarinet1 -> renders/cmaes_clarinet/best_smoke.wav
Notes: E3 G3 D4 A4 E5 (midi 52 55 62 69 76), 2.2 s each, 2.5 s spacing.
"""
import copy
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

LADDER = [52, 55, 62, 69, 76]   # E3 G3 D4 A4 E5
GAP, DUR = 2.5, 2.2


def main():
    run = sys.argv[1] if len(sys.argv) > 1 else "clarinet1"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        REPO, "renders", "cmaes_clarinet", "best_smoke.wav")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    best = os.path.join(HERE, "cmaes_runs", run, "best_patch.json")
    patch = copy.deepcopy(json.load(open(best)))
    patch["score"] = [{"note": m, "velocity": 0.85,
                      "time": round(i * GAP, 3), "duration": DUR}
                     for i, m in enumerate(LADDER)]
    ppath = os.path.join(HERE, "cmaes_runs", run, "ladder_patch.json")
    json.dump(patch, open(ppath, "w"), indent=1)
    r = subprocess.run([CLI, ppath, out], capture_output=True, text=True)
    if not os.path.exists(out):
        raise SystemExit(f"render failed: {r.stderr.strip()[:300]}")
    print(f"rendered {out}")


if __name__ == "__main__":
    main()
