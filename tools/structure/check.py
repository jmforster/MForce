"""The structure meter: reports the tree, and gates it against the baseline.

  python tools/structure/check.py                 measure the tree, print the report
  python tools/structure/check.py --gate          fail (exit 1) on a violation the baseline does not allow
  python tools/structure/check.py --tighten       lower baseline entries to what is measured; drop cured ones
  python tools/structure/check.py --init-baseline write baseline.json from the tree (only when there is none)
  python tools/structure/check.py --snapshot      also write tools/structure/metrics.json
  python tools/structure/check.py --modules-md    also write docs/architecture/MODULES.md
  python tools/structure/check.py --delta <ref>   compare against metrics.json at a git ref
  python tools/structure/check.py --file <path>   report one file

The gate fails when a file breaks a limit (limits.json) with no baseline
entry for it, or is worse than its entry; a file better than its entry
passes. It measures the tracked sources and the untracked ones git does not
ignore; the report and the snapshot measure the tracked ones. Duplicated
blocks are reported, never gated, and --gate does not run the duplicate
detector. The changelog rule catches dates and tracker ids in comments;
history told in words is a reviewer's to find. An exception is recorded by
editing baseline.json in the commit that needs it.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import baseline                                 # noqa: E402
import measure                                  # noqa: E402
import report                                   # noqa: E402
import rules                                    # noqa: E402
from modules import ROOT, ModuleMap             # noqa: E402

LIMITS = json.loads((HERE / "limits.json").read_text(encoding="utf-8"))
SNAPSHOT = HERE / "metrics.json"
BASELINE = HERE / "baseline.json"
MODULES_MD = ROOT / "docs" / "architecture" / "MODULES.md"
# Git pathspecs: "*" crosses folders; ":(exclude)" drops the vendored libraries.
SOURCE_EXTENSIONS = ("h", "hpp", "inl", "ipp", "cpp", "cc", "cxx")
SOURCE_GLOBS = ([f"{top}/*.{ext}" for top in ("engine", "tools") for ext in SOURCE_EXTENSIONS]
                + [":(exclude)engine/third_party"])


def tracked_sources():
    """The sources git tracks: what the report and the snapshot measure."""
    out = subprocess.check_output(["git", "-C", str(ROOT), "ls-files"] + SOURCE_GLOBS, text=True)
    return [p for p in out.split("\n") if p]


def gated_sources():
    """The tracked sources and the untracked ones git does not ignore: what
    the gate measures, so a file not yet added is gated before its commit."""
    out = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "--others", "--exclude-standard"]
                                  + SOURCE_GLOBS, text=True)
    return sorted(set(tracked_sources()) | {p for p in out.split("\n") if p})


def measure_file(rel):
    text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
    fns = measure.functions(str(ROOT / rel))
    return {
        "path": rel, "lines": len(measure.lines_of(text)),
        "includes": measure.includes(text),
        "functions": [(name, start, length) for name, _, start, length in fns],
        "signatures": fns,
        "mutable_vars": measure.file_level_mutable_vars(text),
        "flags": measure.flag_counts(text),
        "types": measure.type_declarations(text),
    }


def measure_files(paths, module_map):
    files, violations = [], []
    for rel in paths:
        f = measure_file(rel)
        f["module"] = module_map.owner(rel)
        files.append(f)
        violations += rules.include_violations(rel, f["module"], f["includes"], module_map)
    return files, violations


def measure_tree(paths, module_map):
    files, violations = measure_files(paths, module_map)
    dup_blocks = measure.token_duplicates(paths, ROOT)
    return report.snapshot(files, LIMITS, dup_blocks, violations)


def gate(module_map):
    files, incs = measure_files(gated_sources(), module_map)
    failures = baseline.compare(baseline.violations(files, incs, LIMITS), baseline.load(BASELINE), LIMITS)
    for path, rule, key, value, allowed in failures:
        where = f" {key}" if key else ""
        print(f"FAIL {path}: {rule}{where}: measured {value}, allowed {allowed}")
    print(f"structure gate: {len(files)} files, {len(failures)} failures")
    return 1 if failures else 0


def tighten(module_map):
    files, incs = measure_files(tracked_sources(), module_map)
    old = baseline.load(BASELINE)
    new = baseline.tighten(baseline.violations(files, incs, LIMITS), old)
    baseline.save(BASELINE, new)
    print(f"baseline tightened: {len(old)} entries -> {len(new)}")
    return 0


def init_baseline(module_map):
    if BASELINE.exists():
        print(f"{BASELINE.relative_to(ROOT)} exists: lower it with --tighten, raise an entry by editing it")
        return 1
    files, incs = measure_files(tracked_sources(), module_map)
    entries = baseline.violations(files, incs, LIMITS)
    baseline.save(BASELINE, entries)
    print(f"baseline written: {len(entries)} entries {baseline.counts(entries)}")
    return 0


def main(argv):
    module_map = ModuleMap.load()
    if "--gate" in argv:
        return gate(module_map)
    if "--tighten" in argv:
        return tighten(module_map)
    if "--init-baseline" in argv:
        return init_baseline(module_map)
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
