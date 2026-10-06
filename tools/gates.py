"""One command that runs every behaviour gate and the structure meter.

  python tools/gates.py            run everything (10-20 minutes)
  python tools/gates.py --fast     the test programs and the meter only

Each gate is a subprocess run from the repo root; a gate passes when its
exit code is 0. The summary lists every gate with PASS or FAIL and the
exit code is 1 if any gate failed. Build first; this does not build.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY = sys.executable
GATES = [
    ("engine_tests",   ["build/tools/engine_tests/Release/engine_tests.exe"], True),
    ("test_figures",   ["build/tools/test_figures/Release/test_figures.exe"], True),
    ("structure meter unit tests", [PY, "-m", "unittest", "discover", "-s", "tools/structure/tests", "-p", "test_*.py"], True),
    ("structure meter", [PY, "tools/structure/check.py"], True),
    ("null gate (patches)", [PY, "tools/null_gate_perform_source.py", "--jobs", "3"], False),
    ("roundtrip (UI save/load)", [PY, "tools/test_stable_roundtrip.py", "patches/baselines", "patches/library", "--jobs", "3"], False),
]


def run(name, cmd):
    t0 = time.time()
    cmd = [str(ROOT / cmd[0]) if cmd[0].startswith("build/") else cmd[0]] + cmd[1:]   # Windows resolves a relative exe against the parent cwd
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    tail = "\n".join((r.stdout + r.stderr).strip().split("\n")[-3:])
    return r.returncode, time.time() - t0, tail


def main(argv):
    fast = "--fast" in argv
    results = []
    for name, cmd, is_fast in GATES:
        if fast and not is_fast:
            continue
        code, secs, tail = run(name, cmd)
        results.append((name, code, secs, tail))
        print(f"[{'PASS' if code == 0 else 'FAIL'}] {name} ({secs:.0f}s)\n    {tail.replace(chr(10), chr(10) + '    ')}", flush=True)
    failed = [n for n, c, _, _ in results if c != 0]
    print("\n" + ("ALL GATES PASS" if not failed else "FAILED: " + ", ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
