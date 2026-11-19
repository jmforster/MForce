#!/usr/bin/env python3
"""The twin gate over several processes.

twin_gate.exe renders each patch file it is given through the loader and
through the module and compares the two sample for sample; one process over
library and baselines takes minutes. This wrapper expands the folders as the
exe does (every *.json below each, sorted), splits the list into N contiguous
slices, runs the exe once per slice with the same mode options, relays the
children's lines in path order, and prints one merged summary in the exe's
own form:

  <n> files: <s> same, <d> differ, ... (the gate's counts, summed)
  time: old <ms> ms, new <ms> ms, new/old <r>   (--time: recomputed from the TIME lines)

Usage:
  python tools/twin_gate.py [--jobs N] [--self | --order reversed|tails-kept |
      --resaved DIR | --sparse DIR | --time ROUNDS | --warnings]
      [--expect WHAT.txt] <patch.json | folder>...

Exit: 3 if a child exited other than 0 or 1 (a crash) or the children's
summaries do not account for every file, else 1 if any child exited 1, else 0.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXE = os.path.join(ROOT, "build", "tools", "twin_gate", "Release", "twin_gate.exe")
SUMMARY = re.compile(r"^(\d+) files: (.*)$")
COUNT = re.compile(r"^(\d+) (.+)$")
TIME_LINE = re.compile(r"^TIME (.*)\t([0-9.]+)\t([0-9.]+)\s*$")
TIME_TOTAL = re.compile(r"^time: ")
VALUED = ("--order", "--resaved", "--sparse", "--time", "--expect")
FLAGS = ("--self", "--warnings")


def expand(args):
    patches = []
    for a in args:
        p = os.path.join(ROOT, a) if not os.path.isabs(a) else a
        if os.path.isdir(p):
            found = []
            for dirpath, _, files in os.walk(p):
                for f in files:
                    if f.lower().endswith(".json"):
                        found.append(os.path.relpath(os.path.join(dirpath, f), ROOT).replace("\\", "/"))
            patches.extend(sorted(found))
        else:
            patches.append(os.path.relpath(p, ROOT).replace("\\", "/"))
    seen, out = set(), []
    for p in patches:
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


def run_slice(flags, paths):
    r = subprocess.run([EXE, *flags, *paths], cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def merge(results, expected):
    """Fold the children's (code, stdout, stderr) into (lines, errs, summary lines, exit)."""
    total, counts, order = 0, {}, []
    told = tnew = 0.0
    timed = False
    lines, errs, exits = [], [], []
    for code, out, err in results:
        exits.append(code)
        for line in out.splitlines():
            m = SUMMARY.match(line)
            if m:
                total += int(m.group(1))
                for part in m.group(2).split(", "):
                    c = COUNT.match(part)
                    if c:
                        if c.group(2) not in counts:
                            order.append(c.group(2))
                        counts[c.group(2)] = counts.get(c.group(2), 0) + int(c.group(1))
                continue
            if TIME_TOTAL.match(line):
                timed = True
                continue
            t = TIME_LINE.match(line)
            if t:
                told += float(t.group(2)); tnew += float(t.group(3))
            lines.append(line)
        if err.strip():
            errs.append(err.rstrip())
    summary = []
    if timed:
        summary.append("time: old %.3f ms, new %.3f ms, new/old %.4f" % (told, tnew, tnew / told if told else 0.0))
    summary.append("%d files: %s" % (total, ", ".join("%d %s" % (counts[k], k) for k in order)))
    bad = [c for c in exits if c not in (0, 1)]
    if bad:
        errs.append("a gate process crashed (exit %s): its files were not checked" % bad)
        return lines, errs, summary, 3
    if total != expected:
        errs.append("the children reported %d files of %d" % (total, expected))
        return lines, errs, summary, 3
    return lines, errs, summary, 1 if 1 in exits else 0


def main(argv):
    jobs, flags, targets = 1, [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--jobs":
            jobs = max(1, int(argv[i + 1])); i += 2; continue
        if a in VALUED:
            flags += [a, argv[i + 1]]; i += 2; continue
        if a in FLAGS:
            flags.append(a)
        else:
            targets.append(a)
        i += 1
    if not targets:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    patches = expand(targets)
    if not patches:
        print("0 files:")
        return 0
    jobs = min(jobs, len(patches))
    size = -(-len(patches) // jobs)
    slices = [patches[k:k + size] for k in range(0, len(patches), size)]

    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda s: run_slice(flags, s), slices))
    lines, errs, summary, code = merge(results, len(patches))
    for line in lines:
        print(line)
    for err in errs:
        print(err, file=sys.stderr)
    for line in summary:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
