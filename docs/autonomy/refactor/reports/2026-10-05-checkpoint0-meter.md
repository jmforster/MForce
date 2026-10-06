# Architecture report: checkpoint 0, the structure meter

Commits `637207a` (the build) and its fix-up after the independent review
(see section 8), 2026-10-05. The first checkpoint of the refactor, and the
first report in the agreed format (protocol section 3.9 of the gates
spec). Nothing in the engine changed.

## 1. Summary

The meter exists: one command measures every tracked C++ file against
the budgets and the module map and prints a report, in phase 1 without
ever blocking. The existing test programs and the meter are registered
with CTest and run from the repo root. One command runs every behaviour
gate. The meter's first snapshot is tracked, so later checkpoints can
show their delta against it.

## 2. Types and files

```
NEW  tools/structure/measure.py  (86 lines)
  Does: turns source text into numbers. Pure functions over text, plus two
        that call lizard: function lengths, and token-based duplicate blocks.
  Used by: check.py; the unit tests.
  Depends on: lizard, the standard library. Nothing in the repo.
  Design choice: functions, not a class. Nothing here holds state.
  Alternative: a hand-written C++ function finder. Rejected: it is the
    fragile code this effort removes; lizard's numbers match the audit's.
  Name: "measure" because that is all it does.
  Read: lines 14-18 (the regexes that define every text-based metric; each
    number the meter prints means exactly what these say) and 63-86
    (token_duplicates: lizard's output parsed into blocks).

NEW  tools/structure/modules.py  (58 lines)  — class ModuleMap
  Does: loads modules.json and answers two questions: which module owns a
        file, and may module A include module or library B. Renders the map
        as Markdown.
  Used by: check.py.
  Depends on: the standard library.
  Design choice: one small class holding the parsed map; the two queries
    are its methods. A class because the map is state several queries share.
  Name: "modules", "ModuleMap".
  Read: lines 22-33 (owner: longest matching path wins) and 35-44
    (include_target: how an #include line is classified).

NEW  tools/structure/report.py  (95 lines)
  Does: builds the JSON snapshot from measurements, renders it as the
        Markdown report, and renders the delta between two snapshots.
  Used by: check.py.
  Depends on: the standard library.
  Design choice: functions over plain dicts; the snapshot is data that is
    also the file format of metrics.json.
  Read: lines 8-32 (snapshot: the one definition of what is recorded).

NEW  tools/structure/check.py  (86 lines)
  Does: the command line. Lists tracked sources through git, measures
        each, runs the duplicate detector over all, prints, and optionally
        writes the snapshot and MODULES.md or a delta against a git ref.
  Depends on: measure, modules, report, git.
  Read: lines 31-57 (measure_file / measure_tree: the whole pipeline).

NEW  tools/structure/rules.py  (22 lines)
  Does: the rules, in one place: which includes are outside the map, and
        what exceeds a limit. Added after the review (item 1).
NEW  tools/structure/modules.json   the phase-1 module map: 11 modules, 5
     third-party families, and a file_allowed list (JSON only in the four
     serializer files, the patch loader and the tools), describing today's
     layout, not the destination.
NEW  tools/structure/limits.json    the budgets for new code (600 / 80 / 0).
NEW  tools/structure/metrics.json   the first snapshot (tracked; 3,773 lines).
NEW  tools/structure/tests/test_structure.py  (75 lines)  6 unit tests.
NEW  tools/gates.py  (50 lines)
  Does: runs every behaviour gate and the meter as subprocesses from the
        repo root and prints PASS/FAIL per gate. --fast skips the two
        ten-minute gates.
  Read: lines 17-24 (the gate list: this is the definition of "the gates").
NEW  docs/architecture/MODULES.md   generated from the map; do not edit.
CHANGED  CMakeLists.txt  (+20)  enable_testing() and four add_test()s with
     the repo root as working directory; Python taken from PATH, not the
     Windows registry.
```

## 3. Interfaces changed

None in the engine. New command lines: `python tools/structure/check.py
[--snapshot] [--modules-md] [--delta <ref>] [--file <path>]`,
`python tools/gates.py [--fast]`, and `ctest --test-dir build -C Release`.

## 4. Duplicates collapsed

None; this checkpoint adds, it does not cut.

## 5. Metrics

The baseline, as the meter printed it on `637207a`:

| Measure | Value | Limit for new code |
|---|---|---|
| Files | 164 | |
| Lines | 57,873 | |
| Files over the file limit | 21 | 600 lines |
| Functions over the function limit | 100 | 80 lines |
| File-level mutable variables | 112 | 0 |
| `const_cast` lines | 10 | 0 new |
| `dynamic_cast` lines | 51 | 0 new in engine |
| Changelog comment lines | 270 | 0 new |
| Duplicated blocks (token comparison) | 208 | 0 new |
| Include edges outside the map | 13 | 0 |
| Files on no module | 0 | 0 |

The thirteen include edges outside the map: render including music's two
pitch headers and the patch loader including five music headers (both
known; the destination removes them), plus six music headers that include
JSON without being serializers (`melody_profile`, `pattern_library`,
`pool_figure_builder`, `style_table`, `templates`,
`voicing_profile_selector`), which the review's JSON rule surfaced.

The meter's own files: about 560 lines in eight files, longest file 105
lines, longest function under 40, no file-level mutable variables.

Two known-answer checks: `main()` 1,711 lines and `draw_properties_panel`
1,035, as the audit measured by hand; the ring-out copy in
`instrument.h` (lines 631-649 against 686-698) is found by the token
comparison and missed by a line comparison, which is why the duplicate
rule is token-based.

## 6. Decisions and rejected alternatives

- **Token-based duplicate detection (lizard) instead of the line-window
  rule in the gates spec.** The spec said the rule would move to tokens
  if the known copy differed by renamed variables; it does. Cost: the
  whole-tree duplicate pass takes 12 seconds.
- **The file-level-variable count is a documented heuristic** (regexes
  at measure.py:11-16), and it reads 116 for the tree where the audit
  read 349 for the UI file alone by a different rule. The meter's rule is
  the one that counts from now on; the number is only meaningful against
  itself.
- **Python from PATH, not the registry.** CMake found Python 3.13 in the
  registry, which has no lizard; the interpreter on PATH is 3.11, which
  does. Two lines in CMakeLists.txt.
- **An absolute path for the test programs in gates.py:** on Windows a
  relative executable path is resolved against the parent's directory,
  not the `cwd` given to the subprocess.

## 7. Debt knowingly left

- The meter is phase 1: it reports and never fails. The ratchet,
  `--tighten`, `--raise`, the edit hook and the pre-commit hook are phase
  2, after surgery, as decided.
- `gates.py` does not run the comp-lane template null gate
  (`tools/comp_template_null_gate.py` needs a render/compare pair of
  stages rather than one command); it is added when that gate's driver is
  reshaped.
- `tools/rt_smoke.py` (the fast roundtrip smoke, now in `gates.py --fast`)
  fails on its `wiring` shape, `patches/baselines/perform/wiring_setting.json`.
  That is one of the seven pre-existing roundtrip render differences of
  backlog 79, reproduced on the committed tree before any of this
  checkpoint's changes; it stays visible rather than being allow-listed,
  and goes away with the single patch codec (Part 4).
- Three measurement heuristics have documented misses (measure.py
  docstrings): a pointer-to-const static, an unprefixed non-static
  global, a nested type. The number each produces is only meaningful
  against itself, which is all the ratchet needs.
- `metrics.json` is 3,773 lines of tracked JSON that will churn with
  every checkpoint. If that becomes noisy in diffs, the snapshot moves to
  a summary-only form.

## 8. Independent review

A reviewer with a fresh context, told to refute, checked twelve items
against the commit. Seven FAIL, four PASS, one not applicable. Its
findings and what was done about each:

1. **Rule evaluation spread over three files: FAIL.** Right. The include
   rule was evaluated in `check.py`, the limits in `report.py`, and the
   map rendered Markdown in `modules.py`. Fixed: `rules.py` holds the two
   rules; `report.py` renders everything including the map.
2. **Functions needing "and": FAIL.** Right for `measure_file` (measure and
   detect) and `token_duplicates` (spawn, parse, sort). Fixed: detection
   moved to `rules.py`; `parse_duplicates` is its own pure function with
   its own test. `main` still dispatches four modes; that is what a
   command line does.
3. **Dependency direction: PASS.** The reviewer noted `measure.py` is not
   pure as the spec said; its docstring now says which two functions do
   I/O and why.
4. **Duplication: FAIL.** Right: `gates.py` restated the four CTest
   commands. Fixed: the fast set is one `ctest` invocation, so
   `CMakeLists.txt` is the only list of tests.
5. **Measurement errors: FAIL.** All verified and fixed: every file's
   line count was one too high (a trailing newline counted as a line;
   the total dropped from 58,037 to 57,873); a wrapped function return
   type (`patch_loader.cpp:597`) counted as a mutable variable; `backlog
   #7` comments were missed. Two misses stay as documented heuristics
   (pointer-to-const statics, unprefixed globals); nested types stay
   unlisted. The duplicate parser was confirmed correct.
6. **Module map wrong in three places: FAIL.** Right. `source_registry.cpp`
   now sits under core with its header; `source_registrations.cpp` under
   source, because it is the list of node types; the invented
   `source_impl` module is gone; and JSON is allowed only in the four
   serializer files, the patch loader and the tools, which exposed six
   non-serializer music headers that include it.
7. **CTest registration: PASS.**
8. **Gates missing from the runner: FAIL.** Right. Added `mforce_ui
   --stamp`, the fast roundtrip smoke (to `--fast`) and the wiring null
   gate. The comp template null gate still needs its driver reshaped
   (section 7). Adding the smoke exposed the pre-existing backlog-79
   failure noted in section 7.
9. **Tests: FAIL.** Right. Added tests for `lines_of`, `function_lengths`,
   `parse_duplicates`, the rules, snapshot and delta rendering, and the
   three known-answer tests the spec asked for (`main` 1,711,
   `draw_properties_panel` 1,035, the ring-out duplicate at
   `instrument.h` 631-649 / 686-698) plus one that every tracked source
   file is on a module. Six tests became thirteen.
10. **Names: PASS**, with two renames taken (`flagged_lines` to
    `flag_counts`, `mmap` to `module_map`).
11. **Real-time rule: N/A.** No engine code in the diff.
12. **Author's claims: PASS**, except "every gate", which item 8 corrected.

## 9. Reading guide

Ten minutes: `tools/structure/measure.py` lines 11-18 (what each number
means), `tools/structure/modules.json` (the map), the gate list in
`tools/gates.py` lines 17-24.

## 10. C++ lessons

Two, both from the tooling rather than the engine, since no C++ changed:

- **CTest and working directories.** A C++ test program has no notion of
  "the project root"; it sees whatever directory it was launched from.
  `add_test(... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})` is how the build
  system supplies that fact, the way a Java test runner supplies the
  classpath. Before this checkpoint both test programs silently required
  being run from the root.
- **Finding tools at configure time.** `find_package(Python3)` is CMake's
  equivalent of a dependency declaration; where it looks is policy
  (`Python3_FIND_STRATEGY`, `Python3_FIND_REGISTRY`). On Windows the
  registry can answer with a different interpreter than the shell's
  PATH, which is exactly what happened here.
