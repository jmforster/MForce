"""Measure shared-source reads per null-gate manifest patch.

Renders every manifest patch with MFORCE_TRACE_SHARING=1 and collects the
[sharing] line mforce_cli prints at exit:

  lag reads       a wrapped consumer (RefSource) read a shared source before
                  its primary consumer advanced it this sample, i.e. it read
                  the previous sample's value. These patches are expected to
                  change when the per-tick memo lands.
  same-tick taps  a guarded tap read a source that had already advanced this
                  sample. These patches are expected to change when taps read
                  the previous sample by rule.

  python tools/measure_sharing.py [--jobs N] [--out docs/.../file.md]
  python tools/measure_sharing.py --consumers
      lists every pin advance in engine/ that bypasses the trace hook
      (a `->next()` outside dsp_value_source.h); the lists are only trusted
      when it prints nothing.
"""
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
MANIFEST = ROOT / "tools/null_gate_manifest.json"
SCRATCH = ROOT / "renders/scratch"
LINE = re.compile(r"\[sharing\] lagReads=(\d+) sameTickTaps=(\d+)")


def arg(flag, default):
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def untraced_consumers():
    r = subprocess.run(["git", "grep", "-n", "->next()", "--", "engine"],
                       capture_output=True, text=True, cwd=ROOT)
    return [l for l in r.stdout.splitlines()
            if "core/dsp_value_source.h" not in l and not l.split(":", 2)[2].lstrip().startswith("//")]


def measure(key, idx):
    out = SCRATCH / f"measure_sharing_{idx}.wav"
    if out.exists():
        out.unlink()
    env = dict(os.environ, MFORCE_TRACE_SHARING="1")
    r = subprocess.run([str(CLI), str(ROOT / key), str(out)],
                       capture_output=True, text=True, env=env, cwd=ROOT)
    if out.exists():
        out.unlink()
    m = LINE.search(r.stderr)
    if r.returncode != 0 or not m:
        return key, None, None
    return key, int(m.group(1)), int(m.group(2))


def main():
    if "--consumers" in sys.argv:
        lines = untraced_consumers()
        print("\n".join(lines) if lines else "every pin advance in engine/ goes through the trace hook")
        return 1 if lines else 0
    jobs = int(arg("--jobs", 3))
    out_path = ROOT / arg("--out", f"docs/architecture/measurements/{date.today().isoformat()}-sharing-lag.md")
    keys = sorted(json.loads(MANIFEST.read_text()))
    SCRATCH.mkdir(parents=True, exist_ok=True)
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        rows = list(ex.map(lambda ik: measure(ik[1], ik[0]), enumerate(keys)))
    failed = [k for k, lag, taps in rows if lag is None]
    lag = [(k, n) for k, n, _ in rows if n]
    taps = [(k, n) for k, _, n in rows if n]
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                            text=True, cwd=ROOT).stdout.strip()
    doc = ["# Sharing measurements (for the per-tick memo and the tap rule)", "",
           f"Taken {date.today().isoformat()} at engine commit `{commit}` over the {len(keys)} null-gate "
           "manifest patches, with `MFORCE_TRACE_SHARING=1` (`python tools/measure_sharing.py`).",
           "A *lag read* is a wrapped consumer reading a shared source before its primary consumer advanced it "
           "that sample (it reads the previous sample's value). A *same-tick tap* is a guarded tap reading a "
           "source that had already advanced that sample.", "",
           f"## {len(lag)} patches with wiring-order lag reads (expected to change under the memo)", ""]
    doc += [f"- `{k}`: {n} lag reads" for k, n in lag] or ["- none"]
    doc += ["", f"## {len(taps)} patches with same-tick tap reads (expected to change under the tap rule)", ""]
    doc += [f"- `{k}`: {n} same-tick tap reads" for k, n in taps] or ["- none"]
    doc += ["", f"## {len(failed)} patches that produced no trace line", ""]
    doc += [f"- `{k}`" for k in failed] or ["- none"]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(doc) + "\n", encoding="utf-8")
    print("\n".join(doc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
