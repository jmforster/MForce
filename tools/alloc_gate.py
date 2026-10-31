#!/usr/bin/env python3
"""The allocation gate over several processes.

alloc_gate.exe arms the allocation probe around every note of every patch it
is given and reports the count and the first allocation's call stack; one
process over library and baselines takes over a minute. This wrapper expands
the folders exactly as the exe does (every *.json below each, sorted), splits
the list into N contiguous slices, runs the exe once per slice, relays each
child's lines, and prints one merged summary line in the exe's own format:

  <n> patches: <a> allocate, <f> failed to load

Usage:
  python tools/alloc_gate.py [--jobs N] [--list] [--verbose] <patch.json | folder>...

Exit: 1 if any child exited 1 (an allocation), else 2 if any exited 2 (a load
failure), else 0.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXE = os.path.join(ROOT, "build", "tools", "alloc_gate", "Release", "alloc_gate.exe")
SUMMARY = re.compile(r"^(\d+) patches: (\d+) allocate, (\d+) failed to load\s*$")


def expand(args):
    patches = []
    for a in args:
        p = os.path.join(ROOT, a) if not os.path.isabs(a) else a
        if os.path.isdir(p):
            for dirpath, _, files in os.walk(p):
                for f in files:
                    if f.lower().endswith(".json"):
                        patches.append(os.path.relpath(os.path.join(dirpath, f), ROOT))
        else:
            patches.append(os.path.relpath(p, ROOT))
    return sorted(set(patches))


def run_slice(flags, paths):
    r = subprocess.run([EXE, *flags, *paths], cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def main(argv):
    jobs, flags, targets = 1, [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--jobs":
            jobs = max(1, int(argv[i + 1])); i += 2; continue
        if a in ("--list", "--verbose"):
            flags.append(a)
        else:
            targets.append(a)
        i += 1
    if not targets:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    patches = expand(targets)
    if not patches:
        print("0 patches: 0 allocate, 0 failed to load")
        return 0
    jobs = min(jobs, len(patches))
    size = -(-len(patches) // jobs)
    slices = [patches[k:k + size] for k in range(0, len(patches), size)]

    total = allocate = failed = 0
    exits = []
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        for code, out, err in ex.map(lambda s: run_slice(flags, s), slices):
            exits.append(code)
            for line in out.splitlines():
                m = SUMMARY.match(line)
                if m:
                    total += int(m.group(1)); allocate += int(m.group(2)); failed += int(m.group(3))
                else:
                    print(line)
            if err.strip():
                print(err.rstrip(), file=sys.stderr)
    print(f"{total} patches: {allocate} allocate, {failed} failed to load")
    if 1 in exits:
        return 1
    if 2 in exits:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
