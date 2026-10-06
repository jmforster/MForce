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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import measure                                  # noqa: E402
import report                                   # noqa: E402
from modules import ROOT, ModuleMap             # noqa: E402

LIMITS = json.loads((Path(__file__).resolve().parent / "limits.json").read_text(encoding="utf-8"))
SNAPSHOT = Path(__file__).resolve().parent / "metrics.json"
MODULES_MD = ROOT / "docs" / "architecture" / "MODULES.md"
SOURCE_GLOBS = ["engine/include/*.h", "engine/src/*.cpp", "tools/*.cpp", "tools/*.h"]


def tracked_sources():
    out = subprocess.check_output(["git", "-C", str(ROOT), "ls-files"] + SOURCE_GLOBS, text=True)
    return [p for p in out.split("\n") if p]


def measure_file(rel, mmap):
    text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
    module = mmap.owner(rel)
    incs = measure.includes(text)
    violations = []
    for inc in incs:
        target = mmap.include_target(inc)
        if target and not mmap.may_include(module, target):
            violations.append({"file": rel, "module": module, "include": inc, "target": target})
    return {
        "path": rel, "module": module, "lines": len(measure.lines_of(text)),
        "functions": measure.function_lengths(str(ROOT / rel)),
        "mutable_vars": measure.file_level_mutable_vars(text),
        "flags": measure.flagged_lines(text),
        "types": measure.type_declarations(text),
    }, violations


def measure_tree(paths, mmap):
    files, violations = [], []
    for rel in paths:
        f, v = measure_file(rel, mmap)
        files.append(f)
        violations += v
    dup_blocks = measure.token_duplicates(paths, ROOT)
    return report.snapshot(files, LIMITS, dup_blocks, violations)


def main(argv):
    mmap = ModuleMap.load()
    if "--file" in argv:
        rel = argv[argv.index("--file") + 1].replace("\\", "/")
        snap = measure_tree([rel], mmap)
        print(report.render(snap, LIMITS))
        return 0
    snap = measure_tree(tracked_sources(), mmap)
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
        MODULES_MD.write_text(mmap.render_markdown(), encoding="utf-8")
        print(f"module map written: {MODULES_MD.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
