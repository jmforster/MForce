"""Every test and gate, from the one list CMakeLists.txt registers.

  python tools/gates.py --fast    the tests without the label `gate`: the test
                                  programs, the structure gate and its unit tests,
                                  the tool tests
  python tools/gates.py           those, then the tests labelled `gate`: the null
                                  gate, the allocation gate and the comp gate
  --build DIR                     the build folder (default: build)

It runs CTest in the build folder and does not build: a registered test whose
program is missing is named, and the run fails. It prints one PASS or FAIL
line per test with its seconds (and a test program's "ALL PASS (N checks)"
line), the sum of those checks, and ALL GATES PASS or the failed names. The
comp gate is registered only on a build configured with -DMFORCE_SHELVED=ON;
without it the full run says so and does not print ALL GATES PASS.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CTEST = shutil.which("ctest") or "ctest"
RESULT = re.compile(r"Test\s+#(\d+): (\S+) \.+[ *]*(Passed|\*\*\*Failed|Failed|\*\*\*Exception.*|Not Run.*)\s+([\d.]+) sec")
ALL_PASS = re.compile(r"^(\d+): (ALL PASS \((\d+) checks\))")


def registered(build, label_args):
    """[(name, command)] of the tests CTest would run with these label args."""
    out = subprocess.run([CTEST, "--test-dir", str(build), "-C", "Release", "-N", "--show-only=json-v1"]
                         + label_args, capture_output=True, text=True, cwd=ROOT)
    tests = json.loads(out.stdout).get("tests", []) if out.returncode == 0 and out.stdout.strip() else []
    return [(t["name"], t.get("command", [])) for t in tests]


def missing_programs(tests):
    """The tests whose program does not exist: CTest lists no command for a
    target whose binary is not built."""
    return [name for name, cmd in tests if not cmd or not Path(cmd[0]).exists()]


def run(build, label_args):
    """[(name, passed, seconds, all_pass_line, checks)] for one CTest run."""
    r = subprocess.run([CTEST, "--test-dir", str(build), "-C", "Release", "-V"] + label_args,
                       capture_output=True, text=True, cwd=ROOT, errors="replace")
    numbers, rows = {}, []
    for line in r.stdout.splitlines():
        m = ALL_PASS.match(line)
        if m:
            numbers[m.group(1)] = (m.group(2), int(m.group(3)))
    for m in RESULT.finditer(r.stdout):
        line, checks = numbers.get(m.group(1), ("", 0))
        rows.append((m.group(2), m.group(3) == "Passed", float(m.group(4)), line, checks))
    return rows


def main(argv):
    build = ROOT / (argv[argv.index("--build") + 1] if "--build" in argv else "build")
    fast = "--fast" in argv
    sets = [["-LE", "gate"]] + ([] if fast else [["-L", "gate"]])
    tests = [t for s in sets for t in registered(build, s)]
    if not tests:
        print(f"no tests registered in {build}: configure and build it first")
        return 1
    missing = missing_programs(tests)
    for name in missing:
        print(f"MISSING {name}: its program is not built (this script does not build)")
    rows = [row for s in sets for row in run(build, s)]
    for name, ok, secs, line, _ in rows:
        print(f"[{'PASS' if ok else 'FAIL'}] {name} ({secs:.1f}s){'  ' + line if line else ''}")
    print(f"checks sum {sum(r[4] for r in rows)}")
    failed = [r[0] for r in rows if not r[1] and r[0] not in missing] + missing
    comp_registered = any(name == "comp_gate" for name, _ in tests)
    if not fast and not comp_registered:
        print(f"the comp gate is not registered: {build.name} is configured without -DMFORCE_SHELVED=ON")
    if failed:
        print("FAILED: " + ", ".join(failed))
        return 1
    if not fast and not comp_registered:
        print("NOT ALL GATES RAN")
        return 1
    print("ALL GATES PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
