"""Measure same-tick tap reads per patch: checkpoint 2.4's expected-diff list.

Renders every patch of the given set files with MFORCE_TRACE_TAPS=1 and
collects the [taps] line mforce_cli prints at exit:

  same-tick tap read   a tap read a source that had already claimed and
                       finished this tick's compute — the one position where
                       reading A (the present value) and reading B (the value
                       held at the end of the previous tick) differ. These are
                       the patches whose renders may change under reading B.
                       The count is an upper bound: a read of a value that did
                       not change between the two ticks moves nothing.

  python tools/measure_taps.py --set <file> [--set <file> ...] [--jobs 3]
                               [--cli <exe>] --out <document.md>

A set file names one repo-relative patch path per line. A patch named in two
set files is rendered once and listed under each.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
SCRATCH = ROOT / "renders/scratch"
PRIOR = ROOT / "docs/architecture/measurements/2026-10-18-sharing-lag-tree.md"
LINE = re.compile(r"\[taps\] sameTickReads=(\d+)")
PRIOR_LINE = re.compile(r"- `([^`]+)`: (\d+) same-tick tap reads")


def args_of(flag):
    return [sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == flag]


def arg(flag, default):
    v = args_of(flag)
    return v[0] if v else default


def read_set(path):
    return [l.strip() for l in Path(path).read_text(encoding="utf-8").splitlines() if l.strip()]


def measure(cli, key, idx):
    out = SCRATCH / f"measure_taps_{idx}.wav"
    if out.exists():
        out.unlink()
    env = dict(os.environ, MFORCE_TRACE_TAPS="1")
    r = subprocess.run([cli, str(ROOT / key), str(out)],
                       capture_output=True, text=True, env=env, cwd=ROOT)
    if out.exists():
        out.unlink()
    m = LINE.search(r.stderr)
    if r.returncode != 0 or not m:
        return key, None
    return key, int(m.group(1))


def prior_list():
    text = PRIOR.read_text(encoding="utf-8")
    a = text.index("## 47 patches with same-tick tap reads")
    b = text.index("## 157 patches")
    return {k: int(n) for k, n in PRIOR_LINE.findall(text[a:b])}


def main():
    sets = args_of("--set")
    if not sets or "--out" not in sys.argv:
        raise SystemExit(__doc__)
    cli = str(Path(arg("--cli", str(CLI))).resolve())
    jobs = int(arg("--jobs", 3))
    out_path = ROOT / arg("--out", "")
    members = {Path(s).stem: read_set(s) for s in sets}
    keys = sorted({k for ks in members.values() for k in ks})
    SCRATCH.mkdir(parents=True, exist_ok=True)
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        counts = dict(ex.map(lambda ik: measure(cli, ik[1], ik[0]), enumerate(keys)))
    failed = sorted(k for k, n in counts.items() if n is None)
    listed = {k: n for k, n in counts.items() if n}
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                            text=True, cwd=ROOT).stdout.strip()
    today = date.today().isoformat()
    doc = ["# Same-tick tap measurements (for the tap rule, checkpoint 2.4)", "",
           f"Taken {today} at engine commit `{commit}` over the {len(keys)} patches of the set files "
           + ", ".join(Path(s).name for s in sets)
           + f" (`{Path(cli).name}`, `MFORCE_TRACE_TAPS=1`, `python tools/measure_taps.py`).",
           "A *same-tick tap read* is a tap reading a source that had already claimed and finished "
           "this tick's compute: reading A hands it this tick's value, reading B the value held at the "
           "end of the previous tick. The count is an upper bound on the renders that change: a read "
           "of a value that did not move between the two ticks changes nothing.", "",
           f"## {len(listed)} patches with same-tick tap reads (expected to change under reading B)", ""]
    for name, ks in members.items():
        hits = [(k, listed[k]) for k in ks if k in listed]
        doc += [f"### {name} ({len(hits)} of {len(ks)})", ""]
        doc += [f"- `{k}`: {n} same-tick tap reads" for k, n in hits] or ["- none"]
        doc += [""]
    prior = prior_list()
    gone = sorted(k for k in prior if k not in listed)
    new = sorted(k for k in listed if k not in prior)
    changed = sorted(k for k in prior if k in listed and prior[k] != listed[k])
    doc += [f"## Against the 2026-10-18 list ({len(prior)} patches, taken at `587b539` before the memo)", "",
            "In the 2026-10-18 list, not counted now:", ""]
    doc += [f"- `{k}` (was {prior[k]})" for k in gone] or ["- none"]
    doc += ["", "Counted now, not in the 2026-10-18 list:", ""]
    doc += [f"- `{k}`: {listed[k]}" for k in new] or ["- none"]
    doc += ["", "On both lists with a different count:", ""]
    doc += [f"- `{k}`: {prior[k]} → {listed[k]}" for k in changed] or ["- none"]
    doc += ["", f"## {len(failed)} patches that produced no trace line (they do not load)", ""]
    doc += [f"- `{k}`" for k in failed] or ["- none"]
    doc += ["", "## Structural scans", ""]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(doc) + "\n", encoding="utf-8")
    print(f"{len(listed)} listed, {len(failed)} failed, written to {out_path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
