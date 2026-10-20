"""Measure, for checkpoint 2.5: the patches the loader warns about (unknown
keys), the patches that do not load (by their error), and the patches whose
DelayLine read lands on the slot past the ring.

Renders every patch of the given set files with MFORCE_TRACE_WRAP=1 and keeps
stderr:

  [load] <Type>: unknown param '<key>' ...   a key the loader ignores today and
                                             rejects after the cut (Task 3)
  ERROR: ...                                 the patch did not load or render
  [wrap] delayLineWraps=N                    N reads that hit the slot past the
                                             ring: the render moves after the
                                             wrap fix (Task 4)

  python tools/measure_25.py --set <file> [--set <file> ...] [--jobs 3]
                             [--cli <exe>] --out <document.md>

A set file names one repo-relative patch path per line. A patch named in two
set files is rendered once and listed under each.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys
from collections import defaultdict
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
SCRATCH = ROOT / "renders/scratch"
UNKNOWN = re.compile(r"\[load\] (\S+): unknown param '([^']+)'")
WRAP = re.compile(r"\[wrap\] delayLineWraps=(\d+)")
ERROR = re.compile(r"^ERROR: (.*)$", re.M)


def args_of(flag):
    return [sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == flag]


def arg(flag, default):
    v = args_of(flag)
    return v[0] if v else default


def read_set(path):
    return [l.strip() for l in Path(path).read_text(encoding="utf-8").splitlines() if l.strip()]


def measure(cli, key, idx):
    out = SCRATCH / f"measure_25_{idx}.wav"
    if out.exists():
        out.unlink()
    env = dict(os.environ, MFORCE_TRACE_WRAP="1")
    r = subprocess.run([cli, str(ROOT / key), str(out)],
                       capture_output=True, text=True, env=env, cwd=ROOT)
    rendered = out.exists()
    if rendered:
        out.unlink()
    unknown = sorted(set(UNKNOWN.findall(r.stderr)))
    m = WRAP.search(r.stderr)
    wraps = int(m.group(1)) if m else None
    err = ERROR.search(r.stderr)
    error = err.group(1).strip() if err else ("" if rendered and r.returncode == 0 else "no output and no ERROR line")
    return key, unknown, wraps, (None if rendered and r.returncode == 0 else error)


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
        rows = {k: (u, w, e) for k, u, w, e in ex.map(lambda ik: measure(cli, ik[1], ik[0]), enumerate(keys))}
    unknown = {k: u for k, (u, w, e) in rows.items() if u}
    failed = {k: e for k, (u, w, e) in rows.items() if e is not None}
    wraps = {k: w for k, (u, w, e) in rows.items() if w}
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                            text=True, cwd=ROOT).stdout.strip()
    today = date.today().isoformat()
    doc = ["# No-throw and wire measurements (for checkpoint 2.5)", "",
           f"Taken {today} at engine commit `{commit}` over the {len(keys)} patches of the set files "
           + ", ".join(Path(s).name for s in sets)
           + f" (`{Path(cli).name}`, `MFORCE_TRACE_WRAP=1`, `python tools/measure_25.py`).",
           "An *unknown key* is a param the loader warns about and ignores today; after the cut such a patch "
           "fails to load. A *non-loader* is a patch the CLI could not load or render, with its first ERROR "
           "line. A *wrap* is a DelayLine read that landed on the slot past the ring; after the wrap fix the "
           "read is slot 0 and the render moves.", "",
           f"## {len(unknown)} patches the loader warns about (they stop loading after Task 3)", ""]
    for name, ks in members.items():
        hits = [(k, unknown[k]) for k in ks if k in unknown]
        doc += [f"### {name} ({len(hits)} of {len(ks)})", ""]
        doc += [f"- `{k}`: " + "; ".join(f"`{t}.{key}`" for t, key in u) for k, u in hits] or ["- none"]
        doc += [""]
    by_err = defaultdict(list)
    for k, e in failed.items():
        by_err[" ".join(e.split()[:3])].append(k)
    doc += [f"## {len(failed)} patches that do not load or render, by error", ""]
    for head, ks in sorted(by_err.items(), key=lambda kv: -len(kv[1])):
        doc += [f"### `{head}` ({len(ks)})", ""]
        doc += [f"- `{k}`: {failed[k]}" for k in sorted(ks)]
        doc += [""]
    doc += [f"## {len(wraps)} patches whose DelayLine read hits the slot past the ring (expected to change under the wrap fix)", ""]
    for name, ks in members.items():
        hits = [(k, wraps[k]) for k in ks if k in wraps]
        doc += [f"### {name} ({len(hits)} of {len(ks)})", ""]
        doc += [f"- `{k}`: {n} reads" for k, n in hits] or ["- none"]
        doc += [""]
    doc += ["## Lint cross-check", ""]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(doc) + "\n", encoding="utf-8")
    print(f"{len(unknown)} with unknown keys, {len(failed)} failed, {len(wraps)} with wraps; written to "
          f"{out_path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
