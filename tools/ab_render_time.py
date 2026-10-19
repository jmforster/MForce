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

# The three library patches the refactor reports time, and the nine micro
# patches that isolate one mechanism each (renders/scratch/ab_tree/micro,
# kept from report R2's render-time investigation).
PATCHES = [
    ("oboe1",          "patches/library/winds/oboe1.json"),
    ("piano_default",  "patches/library/keys/acoustic_piano/piano_default.json"),
    ("viola_default",  "patches/library/strings/viola_default.json"),
    ("A sine",         "renders/scratch/ab_tree/micro/A_sine_unwired.json"),
    ("B sine+env",     "renders/scratch/ab_tree/micro/B_sine_env.json"),
    ("C filter x8",    "renders/scratch/ab_tree/micro/C_filter_chain8.json"),
    ("D 8 sines",      "renders/scratch/ab_tree/micro/D_8sines_2wired_each.json"),
    ("E shared ref",   "renders/scratch/ab_tree/micro/E_shared_env_ref.json"),
    ("F 40 sines",     "renders/scratch/ab_tree/micro/F_40_unwired_sines.json"),
    ("G 40 own env",   "renders/scratch/ab_tree/micro/G_40_sines_own_env.json"),
    ("H 40 shared",    "renders/scratch/ab_tree/micro/H_40_sines_shared_env.json"),
    ("I 40 envs",      "renders/scratch/ab_tree/micro/I_40_envelopes.json"),
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
