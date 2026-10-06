"""The structure meter (phase 1: reports, never blocks).

  python tools/structure/check.py                 measure the tree, print the report
  python tools/structure/check.py --snapshot      also write tools/structure/metrics.json
  python tools/structure/check.py --modules-md    also write docs/architecture/MODULES.md
  python tools/structure/check.py --delta <ref>   compare against metrics.json at a git ref
  python tools/structure/check.py --file <path>   report one file

Exit code is 0 unless the meter itself fails. Phase 2 adds the ratchet.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import measure                                  # noqa: E402
import report                                   # noqa: E402
import rules                                    # noqa: E402
from modules import ROOT, ModuleMap             # noqa: E402

LIMITS = json.loads((HERE / "limits.json").read_text(encoding="utf-8"))
SNAPSHOT = HERE / "metrics.json"
MODULES_MD = ROOT / "docs" / "architecture" / "MODULES.md"
SOURCE_GLOBS = ["engine/include/*.h", "engine/src/*.cpp", "tools/*.cpp", "tools/*.h"]


def tracked_sources():
    out = subprocess.check_output(["git", "-C", str(ROOT), "ls-files"] + SOURCE_GLOBS, text=True)
    return [p for p in out.split("\n") if p]


def measure_file(rel):
    text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
    return {
        "path": rel, "lines": len(measure.lines_of(text)),
        "includes": measure.includes(text),
        "functions": measure.function_lengths(str(ROOT / rel)),
        "mutable_vars": measure.file_level_mutable_vars(text),
        "flags": measure.flag_counts(text),
        "types": measure.type_declarations(text),
    }


def measure_tree(paths, module_map):
    files, violations = [], []
    for rel in paths:
        f = measure_file(rel)
        f["module"] = module_map.owner(rel)
        files.append(f)
        violations += rules.include_violations(rel, f["module"], f["includes"], module_map)
    dup_blocks = measure.token_duplicates(paths, ROOT)
    return report.snapshot(files, LIMITS, dup_blocks, violations)


def main(argv):
    module_map = ModuleMap.load()
    if "--file" in argv:
        rel = argv[argv.index("--file") + 1].replace("\\", "/")
        print(report.render(measure_tree([rel], module_map), LIMITS))
        return 0
    snap = measure_tree(tracked_sources(), module_map)
    if "--delta" in argv:
        ref = argv[argv.index("--delta") + 1]
        old = json.loads(subprocess.check_output(
            ["git", "-C", str(ROOT), "show", f"{ref}:tools/structure/metrics.json"], text=True))
        print(report.delta(old, snap))
        return 0
    print(report.render(snap, LIMITS))
    if "--snapshot" in argv:
        SNAPSHOT.write_text(report.dump(snap), encoding="utf-8")
        print(f"snapshot written: {SNAPSHOT.relative_to(ROOT)}")
    if "--modules-md" in argv:
        MODULES_MD.parent.mkdir(parents=True, exist_ok=True)
        MODULES_MD.write_text(report.render_modules(module_map), encoding="utf-8")
        print(f"module map written: {MODULES_MD.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
