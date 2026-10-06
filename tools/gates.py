"""One command that runs every behaviour gate and the structure meter.

  python tools/gates.py            run everything (15-30 minutes)
  python tools/gates.py --fast     the CTest set, the UI stamp and the fast
                                   roundtrip smoke only

The CTest set (engine_tests, test_figures, the meter and its unit tests)
is run through ctest, so CMakeLists.txt stays the one list of tests. The
slow gates are the repo's own scripts. A gate passes when its exit code
is 0; the summary lists PASS or FAIL per gate and the exit code is 1 if
any failed. Build first; this does not build.
"""
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY = sys.executable
CTEST = shutil.which("ctest") or "ctest"
UI = ROOT / "build/tools/mforce_ui/Release/mforce_ui.exe"

FAST = [
    ("ctest (engine_tests, test_figures, structure meter)", [CTEST, "--test-dir", str(ROOT / "build"), "-C", "Release"]),
    ("mforce_ui --stamp (engine build is current)", [str(UI), "--stamp"]),
    ("roundtrip smoke (one patch per shape)", [PY, "tools/rt_smoke.py"]),
]
SLOW = [
    ("null gate (patches)", [PY, "tools/null_gate_perform_source.py", "--jobs", "3"]),
    ("roundtrip (UI save/load, whole corpus)", [PY, "tools/test_stable_roundtrip.py", "patches/baselines", "patches/library", "--jobs", "3"]),
    ("wiring null gate (paramMap conversion)", [PY, "tools/null_gate_wiring.py"]),
]
# Not run here: tools/comp_template_null_gate.py needs a render/compare pair
# of stages rather than one command; it joins this list when its driver is
# reshaped.


def run(cmd):
    t0 = time.time()
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    tail = "\n".join((r.stdout + r.stderr).strip().split("\n")[-3:])
    return r.returncode, time.time() - t0, tail


def main(argv):
    gates = FAST if "--fast" in argv else FAST + SLOW
    failed = []
    for name, cmd in gates:
        code, secs, tail = run(cmd)
        if code != 0:
            failed.append(name)
        print(f"[{'PASS' if code == 0 else 'FAIL'}] {name} ({secs:.0f}s)")
        print("    " + tail.replace("\n", "\n    "), flush=True)
    print("\n" + ("ALL GATES PASS" if not failed else "FAILED: " + ", ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
