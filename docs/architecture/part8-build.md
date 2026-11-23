# Part 8: build and tests

Status: **signed 2026-11-22 as written (Matt: "Signed, proceed").** Parent: `target-architecture.md`, section 2 principle 5, the
"Tools" and "Everywhere" entries of section 5, row 8 of section 7 and item
7 of section 8; `docs/superpowers/specs/2026-10-04-refactor-gates-design.md`
for phase 2. Written under the 2026-11-21 reset, in the shape of Parts 5
and 6. Each change is shown before and after.

Nothing in this part changes what the engine does. Every step ends with
the same gates saying "the same".

## 1. Today, from the code

- **One library holds every module.** `engine/CMakeLists.txt` builds
  `mforce_engine` from 37 `.cpp` files and gives every file every header.
  The module map is checked by the meter, a Python script that reports
  and never fails anything. A forbidden include compiles.
- **A module is in two trees.** Render's headers are in
  `engine/include/mforce/render/` and its sources in `engine/src/render/`.
  Shelved, the newest module, already keeps both under `engine/shelved/`.
- **A fresh clone cannot configure.** The root `CMakeLists.txt` adds
  `engine/third_party/glfw`, `rtaudio` and `rtmidi` as subdirectories.
  `.gitignore` ignores `engine/third_party/`; only `nlohmann/json.hpp` is
  tracked. Nothing has linked those three libraries since the old editor
  was deleted.
- **Compile rules differ by target.** The engine and four tools build at
  `/W4`; `engine_tests` at the default level; `durn_converter` and
  `ppl_to_json` pin C++17 beside a C++20 engine. No target treats a
  warning as an error. A clean build today prints 37 warning lines from
  about ten places: unused local variables, two `getenv` calls and an
  `fopen`, shadowed names, narrowing conversions. The tests have never
  been built at `/W4`, so what it finds there is not known.
- **The tests are one file.** `tools/engine_tests/main.cpp` is 16,399
  lines: 296 test functions, 203 `#include` lines scattered through it,
  two global counters. A throw inside any test ends the program with no
  output. `tools/test_figures/main.cpp` (1,202 lines) is a second program
  with a second harness and a `--render` mode.
- **One gate has no single command.** `tools/gates.py` runs CTest, the
  allocation gate and the null gate. The comp gate
  (`tools/comp_template_null_gate.py`) compares two stages you render by
  hand, so it is not in the list.
- **The meter never fails.** Phase 2 of the gates design (a baseline of
  today's violations that may only shrink) was to switch on when surgery
  ends. Today: 10 files over 600 lines, 52 functions over 80, 24
  file-level mutable variables, 54 `dynamic_cast` lines, 122 duplicated
  blocks, 77 changelog comment lines, 0 include edges outside the map.
- **Comments carry history.** The meter's pattern finds 77 lines with a
  date, a backlog id or a REVIEW number. A wider search (phase and stage
  numbers, "pre-refactor", "used to", "no longer") finds about 170 in the
  engine: 72 in source, 57 in compose, 15 in shelved.
- `README.md` is one line.

## 2. A folder and a target per module (proposed)

Before:

```
engine/
  CMakeLists.txt            one library, every header visible to every file
  include/mforce/render/    render's headers
  src/render/               render's sources, in a second tree
  shelved/include/..., shelved/src/
tools/engine_tests/         every module's tests
```

After:

```
engine/
  core/      CMakeLists.txt   include/mforce/core/      src/   tests/
  contract/  CMakeLists.txt   include/mforce/contract/
  source/    CMakeLists.txt   include/mforce/source/    src/   tests/
  patch/  render/  music/  voicing/  compose/  perform/  play/  util/
  shelved/   (its shape today, plus CMakeLists.txt and tests/)
  testing/   include/mforce/testing/     the one test harness
  third_party/nlohmann/
```

**No `#include` line changes.** A header's include string
(`"mforce/render/voice.h"`) is the same; only the folder that holds
`mforce/render/` moves. Files move with `git mv`, so history follows.

Each module's `CMakeLists.txt` says what it is built from and what it may
use:

```cmake
# engine/render/CMakeLists.txt
mforce_module(render
  SOURCES src/mixer.cpp src/voice.cpp src/pitched_instrument.cpp ...
  USES    core source patch contract)
```

`mforce_module` (one function, `cmake/MforceModule.cmake`) makes the
target `mforce_render`, gives it its own `include/` folder and the include
folders of the modules in `USES`, and applies the compile rules of section
3. A module with no sources (contract, voicing, util) is a header-only
target.

What a forbidden include does:

```cpp
// in engine/render/src/voice.cpp
#include "mforce/music/structure.h"
```

| | Result |
|---|---|
| Before | compiles; the meter lists it the next time someone runs the meter |
| After | `fatal error C1083: Cannot open include file`, because render's target was never given music's folder |

- **What the compiler cannot see.** A header includes other headers, so
  a target must be given the folders of everything its `USES` reach. Play
  uses perform, and perform's headers include voicing's, so play is given
  voicing's folder although its own list does not name voicing. A direct
  include of voicing from play therefore compiles, and it is the meter
  that catches it. That is the one such pair in today's map. Every other
  forbidden edge (render to music, music to render, compose to shelved,
  anything to play) is out of reach and fails to compile.
- **`mforce_engine` stays as a name** the tools link: a target with no
  code that uses every module but shelved. A tool's CMake file does not
  change.
- **Two lists that must agree.** `USES` in each CMake file and
  `may_include` in `tools/structure/modules.json` say the same thing. The
  compiler enforces the first; the meter keeps the second because it also
  covers the tools and the rule that only codec files include JSON, which
  a target cannot express (a module's codec `.cpp` and its other files
  are one target). A meter test fails when the two lists differ.
- The alternative is to keep one include root and make the meter's check
  fail the build. It costs nothing to do. It leaves the boundary to a
  script that runs after the compiler has already accepted the code, and
  leaves each module in two trees.

## 3. One set of compile rules (proposed)

Before, per target: `/W4` or nothing; C++20 or C++17; warnings are text.

After, for every MForce target, from the one function:

- C++20, `/W4 /permissive-`, and **`/WX`: a warning fails the build.**
- `nlohmann/json.hpp` reaches a target as a system header, so its own
  warnings are not ours to fail on. Only a target that asks for JSON gets
  it.
- Today's warnings, and whatever `/W4` finds in the tests, are fixed
  first, each by correcting the code (an unused variable removed, a
  shadowing name renamed), not by switching the warning off. The one
  judgement call is C4996 on `getenv` and `fopen`: the engine's two
  `getenv` reads go through one small function that uses the checked
  form, so the engine touches the environment in one place (the two
  variables themselves stay: CLEANUP 133), and the converter's `fopen`
  becomes a stream.
- The flags are written for MSVC. Nothing is claimed for another
  compiler: none has built this tree.

## 4. Third-party sources (proposed)

Before: the root build adds three libraries that exist only on your disk
and that nothing links.

After: the root build names no library but `nlohmann/json.hpp`, which is
tracked. `glfw`, `rtaudio`, `rtmidi`, `imgui` and `imnodes` stay on your
disk, ignored, and out of the build until the editor's design says how it
gets them.

Proof that a fresh clone builds: a clean `git worktree` of the commit,
which has none of the ignored folders, is configured and built, and its
tests run.

## 5. The tests (proposed)

Before:

```
build/tools/engine_tests/Release/engine_tests.exe     12,054 checks, 28 s
build/tools/test_figures/Release/test_figures.exe     its own counts
```

After, one program for each module that has tests, registered with CTest:

```
ctest --test-dir build -C Release
  core_tests  source_tests  patch_tests  render_tests  music_tests
  voicing_tests  compose_tests  perform_tests  play_tests  shelved_tests
```

- Each test function moves, unchanged, to
  `engine/<module>/tests/<subject>_tests.cpp`, where the module is the
  highest one it needs. A file holds one subject and stays under 600
  lines. `test_figures`' assertions go to compose's and shelved's tests;
  its `--render` mode is deleted (CTest never ran it and no script calls
  it).
- **One harness**, `engine/testing/`:

  ```cpp
  // mforce/testing/check.h
  #define CHECK(cond)            // counts; prints file, line and text on failure
  #define CHECK_NEAR(a, b, eps)
  #define REQUIRE(cond)          // as CHECK, and ends this test on failure

  struct Test { const char* name; void (*run)(); };
  int run_tests(std::span<const Test> tests);   // each in a try/catch; prints
                                                // "ALL PASS (N checks)" or the
                                                // failures; returns the exit code
  ```

  Each program's `main` is its explicit list of tests. A throw inside a
  test is caught and reported with the test's name, where today the
  program ends in silence.
- The counting allocator the allocation tests need (`tools/alloc_probe`)
  moves beside the harness. Every test program links it, as
  `engine_tests` does today.
- The harness keeps one process-wide tally, as today's two counters do.
  It is the tests' one file-level mutable variable, recorded as such.
  Passing a tally into 296 functions and their helpers would remove it
  and change every signature for nothing a reader gains.
- **Proof:** the sum of the programs' checks equals today's two programs'
  sum. A script adds them up.
- `tools/engine_tests` and `tools/test_figures` are deleted. The gate
  scripts and `gates.py` call CTest.

## 6. The shelved target (proposed)

Before: shelved code is compiled into the engine and every host registers
it.

After, as the target architecture decided on 2026-10-05:

```
cmake -B build                          shelved is not built
cmake -B build -DMFORCE_SHELVED=ON      mforce_shelved, shelved_tests and
                                        tools/shelved/ppl_to_json are built
```

- With the option on, the command-line tool is compiled with
  `MFORCE_WITH_SHELVED` and calls `register_shelved`; with it off the
  call is not there.
- Your build folder is configured with it on, once. The comp gate needs
  it on (seven of its 21 templates name shelved strategies) and says so
  when it is off.
- With it off, a template naming `period_passage` is refused as any
  unregistered name is: "no passage strategy named 'period_passage' is
  registered".

## 7. Every gate from one command (proposed)

Before: `python tools/gates.py` runs CTest, the allocation gate and the
null gate; the comp gate is two hand-run commands and a stage name.

After:

```
python tools/gates.py --fast    the test programs, the meter, the tool tests
python tools/gates.py           those, and the null, allocation and comp gates
```

- The comp gate gets what the null gate has: a manifest of hashes
  (`tools/comp_gate_manifest.json`: each baseline template's piece JSON
  and WAV), frozen from the last Part 6 stage. `check` composes and
  compares; `freeze` rewrites the manifest when a change is meant.
- The three slow gates are also CTest tests under the label `gate`, so
  `ctest -L gate` runs them.

## 8. Phase 2: the meter can fail (proposed)

Before: the meter prints numbers. Nothing stops a file growing.

After: `tools/structure/baseline.json` lists every violation that exists
today, by file and rule. `check.py --gate` is a CTest test and fails when

- a file not in the baseline breaks a limit (600 lines, 80-line
  functions, a file-level mutable variable, an include outside the map, a
  new `const_cast`, a new `dynamic_cast` in the engine, a changelog
  comment, a new duplicated block), or
- a file in the baseline is worse than its entry.

A file that got better passes, and `check.py --tighten` lowers its entry.

Three things the 2026-10-04 gates design had that this leaves out, under
the reset:

| The gates design | Here | Why |
|---|---|---|
| A value below its baseline fails as "stale" until tightened | passes; tightened at a part's close | a failure for improving a file is ceremony |
| `--raise <file> <rule> --reason` to record an exception | edit `baseline.json` in the commit | the diff shows it, with the commit message as the reason |
| A git pre-commit hook and a Claude Code edit hook, with an install script | neither | the check is in CTest and in `gates.py`; a hook per clone is one more thing to install and to break |

The reset's rule for the new editor (its limits fail from its first
commit, no exemptions) follows from this: a new module has no baseline
entries, so any violation in it fails.

## 9. The comment purge (proposed)

Before:

```cpp
// ---- Pre-refactor port: ClassicalComposer::setup_piece -----------------
// Clamp a note that would ring past the section end (#18). The loop
// above stops STARTING notes at the boundary, but the last one to get
// in kept its full duration and overhung ...
```

After:

```cpp
// ---- Builds the piece's sections and parts, and realizes the motifs ----
// A note that would ring past the section's end is cut to end on it.
```

Every comment in `engine/` and in the C++ under `tools/` says what the
code does now: no date, no backlog, CLEANUP or REVIEW id, no plan, part,
phase, stage or task number, no "was", "used to", "no longer". Git holds
the history. A comment that explains WHY the code is as it is stays, in
the present tense. The meter's changelog count goes to 0.

No code line changes in this step: the proof is that the compiled
programs' outputs are the same and that a diff with comments stripped is
empty.

## 10. The rulings the signature accepts

1. **A folder and a target per module, so the compiler refuses a
   forbidden include** (section 2). The move touches every engine file's
   path and no `#include` line.
2. **`/WX` on every MForce target**, after today's warnings are fixed by
   correcting the code (section 3).
3. **The three unused libraries leave the build**; `imgui` and the rest
   wait for the editor's design (section 4).
4. **One test program per module, one harness, tests moved unchanged**;
   `engine_tests`, `test_figures` and `test_figures --render` go
   (section 5).
5. **The harness keeps one process-wide tally** (section 5).
6. **Shelved is off by default**, as decided on 2026-10-05; your build
   folder has it on (section 6).
7. **The comp gate gets a frozen manifest**, and `gates.py` runs every
   gate (section 7).
8. **The meter fails on new violations, with the three omissions of
   section 8's table**: no "stale" failure, no `--raise`, no hooks.
9. **The comment purge covers the engine and the tools' C++**, and
   changes no code line (section 9).
10. **A short `README.md`**: what MForce is in three lines, the
    prerequisites (CMake 3.20, Visual Studio 2022, Python 3 with `lizard`
    for the meter), and the configure, build and test commands.
11. **No continuous-integration workflow in this part.** A GitHub Actions
    file is a few lines once a fresh clone builds, but whether the byte
    gates hold on a machine that is not yours is unknown, and it runs on
    your public repository. Say so if you want it.

## 11. Build order and proof

Each step is one commit or a few. Each ends with the build, every test,
the null gate at 77 of 77, the allocation gate at 0 of 131 and the comp
gate at 21 same. No step is expected to move anything.

1. **Rules and third-party.** `cmake/MforceModule.cmake`; today's
   warnings fixed; `/WX`; the three libraries out of the root build; the
   two C++17 tools on C++20. The fresh-clone proof of section 4.
2. **Folders and targets.** Section 2. The module map's paths, the
   meter's globs, the test that `USES` equals `may_include`.
3. **The tests.** Section 5.
4. **The shelved target.** Section 6.
5. **The comment purge.** Section 9.
6. **Phase 2 and the gates.** Sections 7 and 8; the README.

Expected on the meter at the close: files over 600 lines 10 to 8 (the two
test programs go; `templates_json.h`, `figures.h`, `music_json.h`,
`envelope.h`, the command-line tool's `main.cpp` and three converter files
stay); changelog comment lines 77 to 0;
the other counts as they are, now as a baseline that can only shrink.

One independent review and one short report at the end of the part.

## 12. Not in this part

- The files and functions still over the limits. They are the baseline;
  each shrinks when its code is next worked on.
- A workflow file for continuous integration (ruling 11).
- The 139 Python scripts under `tools/`. Some drive things that no
  longer exist (`gen_trombone2.py` and the old editor). A pass over them
  is its own job.
- `tools/stk_ref`, which builds against a checkout of STK beside the
  repo and is not in the root build.
- The converters' shared intermediate form and the broken MusicXML path
  (target section 5): corpus work, stalled.
- Bodies out of headers into `.cpp` files; precompiled headers.
- The workaround cleanup the refactor backlog books for after this part
  (row 8).

## 13. For the C# reader

**A CMake target is a project; `USES` is its project references.** In a
C# solution, `Render.csproj` references `Core.csproj`, and a `using` of a
namespace from a project it does not reference fails to compile. C++ has
no such thing in the language: `#include` pastes any file the compiler
can find on its search path. The build system supplies it.
`target_include_directories` is the search path, and
`target_link_libraries(mforce_render PUBLIC mforce_core)` both links core
and passes core's search path on. Give each module its own folder and
only the folders it may use, and the search path becomes the reference
list. That is all section 2 does.

**`PUBLIC`, `PRIVATE`, `INTERFACE`.** `PUBLIC` is a reference that flows
to whoever references you (C#'s default for a project reference).
`PRIVATE` is one that stops at you (`PrivateAssets="all"`). `INTERFACE`
is "not for me, only for those who reference me", which is what a
header-only module is: it has nothing to compile and exists to hand its
folder on.

**`/WX` is `TreatWarningsAsErrors`.** The reason is the same: a warning
nobody has to fix is read once and never again, and the tenth one hides
the first.

## 14. Amendments made while writing the briefs, before any code (2026-11-22)

Each of these came from running a section above against the code. Where
one disagrees with an earlier section, it wins.

1. **JSON's folder passes on (section 3).** "Only a target that asks for
   JSON gets it" is true of the asking, not of the reach. Patch, music,
   voicing, compose and shelved declare their codecs in headers that
   include JSON, so a module that uses one of them must be able to open
   `json.hpp` as well. A module's CMake file names `JSON` when its own
   files include it, and the folder then passes to the modules that use
   it. Core, contract, source and util are never given it: an include of
   JSON from one of them fails to compile. For the rest, "only codec
   files include JSON" stays the meter's rule, as section 2 said.
2. **Tests no one module can reach (section 5).** "The highest module it
   needs" has no answer for a test that needs two modules no module may
   use together. Compose with perform is the known case: a composed piece,
   performed. Such tests go to one more program, `integration_tests`, in
   `engine/tests/`, which may use every module, as the command-line tool
   may. A test there that needs shelved sits in a file built only when
   the option is on. The report says what ended up in it.
3. **A declaration lives with its definition (section 2).**
   `register_all_sources()` is declared in core's `source_registry.h` and
   defined in source. With a library per module, that is a header
   promising what its own library does not hold. The declaration moves to
   a header in source, and the one file that calls it
   (`patch/type_declarations.cpp`) changes one `#include` line: the
   exception to "no `#include` line changes". Any other such pair that
   linking the per-module test programs turns up is treated the same way
   and listed in the report.
4. **The harness tests itself (section 5).** A small program,
   `testing_tests`, proves that a failed `CHECK` fails the run, that a
   throw inside a test is caught and named, and that `REQUIRE` ends its
   test. Every other test's pass rests on those three. The allocation
   probe's header is included as `mforce/testing/alloc_probe.h`.
5. **Shelved between steps 2 and 4.** Step 2 makes `mforce_shelved` a
   target that every host links, so nothing changes for a template that
   names a shelved strategy. Step 4 puts it behind the option.
6. **`test_figures` counts tests, not checks (section 5).** Its 43 tests
   use a second set of macros (`EXPECT_EQ`, `EXPECT_NEAR`) that leave the
   test at the first failure, and it reports "43 passed". So its tests
   cannot move unchanged, and "the sum equals today's two programs' sum"
   has no second number to add. Each of the 43 moves by name, its
   `EXPECT_EQ(a, b, msg)` becoming `REQUIRE(a == b)` (the same control
   flow: the test ends there). The proof is then: the checks that came
   from `engine_tests` still sum to 12,054, all 43 figure tests are
   present by name, and the report gives the number of checks they add.
7. **A header is checked by its own module (section 2).** The compiler
   reads a header with the folders of whichever file includes it, not
   with its own module's. So a forbidden include written in a header is
   refused only if a `.cpp` of that header's module happens to include
   it, and most of this engine is headers: voicing, contract and util
   have no `.cpp` at all, and source has five for about ninety headers.
   Each module therefore also compiles one generated file that includes
   every one of its headers, with the module's own folders and nothing
   else. That is what makes section 2's table true for a header as well
   as for a `.cpp`, and it puts every header under `/W4 /WX` whether or
   not anything includes it yet.
