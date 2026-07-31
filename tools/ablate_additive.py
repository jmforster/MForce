#!/usr/bin/env python3
"""Ablation profile of the additive partial body (dsp item 8, G4).

Answers "where is the remaining time actually going", which is the input to
deciding stage 2c (explicit SIMD vs iFFT overlap-add vs neither). It edits a
COPY of partials.h to stub out one stage, rebuilds mforce_cli, measures the
marginal ns/sample/partial from the 32p/200p profiling patches, and restores
the file afterwards.

The marginal figure (cost difference between 200 and 32 partials, divided by
the extra partials) isolates the per-partial loop from all fixed per-sample
overhead, so it is the number to optimize against.

Variants:
  full         the committed body
  no_sin       fast_sin_turns replaced by a passthrough — what the polynomial
               sin still costs once it is no longer a libm call
  loads_only   body short-circuited to two array loads and a multiply — the
               memory-bound floor for the current data layout, i.e. the best
               any restructuring could reach without changing the layout

Run from the repo root. Requires a writable build/ and leaves partials.h
byte-identical to how it found it.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HDR = os.path.join(REPO, "engine", "include", "mforce", "source", "additive",
                   "partials.h")
CMAKE = ("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/"
         "CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
SCRATCH_WAV = os.path.join(REPO, "renders", "nulltest", "_ablate.wav")

SIN_LINE = "    return fast_sin_turns(partialPos_[index]) * pampl;"
BODY_HEAD = ("  inline float partial_value_impl(float amplitude, float frequency, "
             "float phaseDiff,\n                                  int index, "
             "IFormant* formant, float fmtWt,\n                                  "
             "float fmtFloor)\n  {")

VARIANTS = {
    "full": lambda s: s,
    "no_sin": lambda s: s.replace(SIN_LINE,
                                  "    return partialPos_[index] * pampl;"),
    "loads_only": lambda s: s.replace(
        BODY_HEAD, BODY_HEAD + "\n    return ampl1_[index] * amplitude;"),
}

RE = re.compile(r"render=([\d.]+)ms for (\d+) frames")


def render_ms(patch, reps=5):
    best, frames = None, None
    for _ in range(reps):
        r = subprocess.run([CLI, os.path.join(REPO, patch), SCRATCH_WAV],
                           capture_output=True, text=True)
        m = RE.search(r.stderr)
        if not m:
            sys.exit("no timing in stderr:\n" + r.stderr[:400])
        t, frames = float(m.group(1)), int(m.group(2))
        best = t if best is None else min(best, t)
    return best, frames


def main():
    os.makedirs(os.path.dirname(SCRATCH_WAV), exist_ok=True)
    saved = os.path.join(tempfile.gettempdir(), "partials_ablate_backup.h")
    shutil.copy(HDR, saved)
    src = open(HDR, encoding="utf-8").read()
    try:
        print(f"{'variant':12s} {'32p ms':>9} {'200p ms':>9} {'marginal ns':>12}")
        print("-" * 46)
        for name, fn in VARIANTS.items():
            out = fn(src)
            if out == src and name != "full":
                print(f"{name:12s} SKIP — stub pattern no longer matches "
                      f"partials.h (update it)")
                continue
            open(HDR, "w", encoding="utf-8").write(out)
            b = subprocess.run([CMAKE, "--build", os.path.join(REPO, "build"),
                                "--config", "Release", "--target", "mforce_cli"],
                               capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name:12s} BUILD FAILED\n{b.stdout[-1200:]}")
                continue
            t32, frames = render_ms("patches/prof_additive/add_032p.json")
            t200, _ = render_ms("patches/prof_additive/add_200p.json")
            ns = ((t200 - t32) / 1000.0) / frames / (200 - 32) * 1e9
            print(f"{name:12s} {t32:>9.1f} {t200:>9.1f} {ns:>12.2f}")
    finally:
        shutil.copy(saved, HDR)
        subprocess.run([CMAKE, "--build", os.path.join(REPO, "build"),
                        "--config", "Release", "--target", "mforce_cli"],
                       capture_output=True, text=True)
        print("\nrestored partials.h and rebuilt")


if __name__ == "__main__":
    main()
