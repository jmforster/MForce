#!/usr/bin/env python3
"""A/B render-time comparison between two mforce_cli builds.

Alternates the two binaries round by round so machine drift (this box shares
cores with mforce_ui) hits both equally, and reports MIN over rounds — the
least-contended sample is the truest compute cost.

Usage:
  python tools/ab_render_time.py <base_exe> <new_exe> [rounds]
"""
import os
import re
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
WAV = os.path.join(ROOT, "renders", "nulltest", "_ab.wav")

PATCHES = [
    ("prof 032p",      "patches/prof_additive/add_032p.json"),
    ("prof 096p",      "patches/prof_additive/add_096p.json"),
    ("prof 200p",      "patches/prof_additive/add_200p.json"),
    ("viola_default",  "patches/viola_default.json"),
    ("v6_cmaes_best",  "patches/fable1_v6/v6_05_cmaes_best.json"),
    ("vowel_liar1320", "patches/vowel_baseline/liar_1320.json"),
    ("expand_control", "patches/expand_sweep/control_noexpand.json"),
]

RE_RENDER = re.compile(r"render=([\d.]+)ms")


def once(exe, patch):
    """Return (wall_ms, render_ms).

    Wall clock is the honest number: instrument+score patches PRE-RENDER their
    voices inside load_patch_file, so for those the `render=` line only times
    the mixing pass and understates the synthesis cost by ~100x. Wall also
    works against builds predating the load= timer.
    """
    t0 = time.perf_counter()
    r = subprocess.run([exe, os.path.join(ROOT, patch), WAV],
                       capture_output=True, text=True)
    wall = (time.perf_counter() - t0) * 1000.0
    m = RE_RENDER.search(r.stderr)
    return wall, (float(m.group(1)) if m else None)


def resolve(p):
    p = p if os.path.isabs(p) else os.path.join(ROOT, p)
    if not os.path.exists(p):
        sys.exit(f"no such exe: {p}")
    return os.path.normpath(p)


def main():
    base, new = resolve(sys.argv[1]), resolve(sys.argv[2])
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 5

    res = {tag: {"base": [], "new": []} for tag, _ in PATCHES}
    for _ in range(rounds):
        for tag, patch in PATCHES:
            for which, exe in (("base", base), ("new", new)):
                res[tag][which].append(once(exe, patch))

    print(f"{'patch':16s} {'base wall':>10} {'new wall':>10} {'speedup':>9}"
          f" {'base rndr':>10} {'new rndr':>10}")
    print("-" * 70)
    for tag, _ in PATCHES:
        b, n = res[tag]["base"], res[tag]["new"]
        bw, nw = min(x[0] for x in b), min(x[0] for x in n)
        br = min((x[1] for x in b if x[1] is not None), default=float("nan"))
        nr = min((x[1] for x in n if x[1] is not None), default=float("nan"))
        print(f"{tag:16s} {bw:>10.1f} {nw:>10.1f} {bw/nw:>8.2f}x"
              f" {br:>10.1f} {nr:>10.1f}")


if __name__ == "__main__":
    main()
