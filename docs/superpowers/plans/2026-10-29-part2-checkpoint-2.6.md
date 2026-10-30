# Part 2, checkpoint 2.6: the allocation test over every baseline patch — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `start()` and `next()` (and `step()`) allocate nothing, proven by a gate that arms a global allocation probe around every note-on and every sample of every patch under `patches/library` and `patches/baselines`, fails on the first allocation naming the patch and the call stack, and runs with no tolerance list; every allocation it finds today moves to `prepare()`.

**Architecture:** A small static library `mforce_alloc_probe` replaces the global `operator new`/`delete` family in the executables that link it (never the CLI or the UI), counts allocations while armed, and records the first armed allocation's call stack. A gate executable `alloc_gate` loads each patch, prepares it unarmed, then arms the probe around the score's note-ons and the render — the loader's embedded-score delivery is extracted into one function so the gate drives an instrument exactly as `load_patch_file` does, without the unarmed pre-render that `load_patch_file` performs at load. `engine_tests` links the probe too and pins each node family's fix with a one-note test. The fixes follow DelayLine's 2.2 pattern: size for the floor once in `on_prepare`, fit within capacity at `start`.

**Tech Stack:** C++20 (MSVC, CMake 3.20+), `dbghelp` for symbolizing the first stack (Windows; raw addresses elsewhere), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/structure/check.py`.

**Spec:** `docs/architecture/part2-valuesource-contract.md` section 4 ("`prepare`: reset versus note length" — decided 2026-10-05: `prepare(ctx)` may allocate, `start(frames)` may not; "The allocation test asserts both: no allocation in `start` or in `next`, over every baseline patch … the test lands with no tolerance list"; the work list is the 2.2 report's section 7) and section 7's row 2.6 ("none; the test is the new gate"). Matt, 2026-10-29 (brainstorm): the detector is a tool first — one executable that takes a patch or a folder and reports the count and the first allocation's call stack — and the gate is that tool run over library and baselines; nothing runs inside the UI or the CLI render.

## Global Constraints

- No heap allocation, lock or throw in any `next()`, `step()`, `compute()`, `start()`, `on_start()` or `play_note()` path (CLAUDE.md non-negotiables). This checkpoint makes the first of those checkable by a running test; a `prepare()`/`on_prepare()` may allocate.
- The probe's `operator new` replacement is linked only into `alloc_gate` and `engine_tests`. `mforce_engine`, `mforce_cli` and `mforce_ui` never link `mforce_alloc_probe` (Matt's decision above; a replaced allocator has no business in a production binary).
- Explicit registries, no reflection (CLAUDE.md).
- Byte-identity: `python tools/null_gate_perform_source.py --jobs 3` reports `78/78 manifest entries identical` after EVERY task. Part 2 section 7 row 2.6 says "none": moving a sizing from the first `start` to `prepare` changes no value. A render that does change is a finding, named in the Ledger with its cause, and decided with Matt before the manifest moves (2.2's DelayLine move exposed an out-of-bounds read this way).
- No new file over 600 lines, no new function over 80 (`tools/structure/limits.json`); `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report.
- No dated changelog, backlog id or REVIEW number in code comments.
- Commit after every task; `python tools/gates.py --fast` before every commit (its roundtrip smoke fails on `perform/wiring_setting.json` for the pre-existing backlog-79 reason; every other gate must pass). Once Task 9 registers `alloc_gate` in ctest, it is part of `--fast` and must pass before every later commit.
- Work on main, no worktree (ruling 1 of checkpoint 2.1 stands). The in-tree build is `build/`.
- The A/B base is `renders/scratch/ab_tree/cli_pre26.exe` (Task 0 step 0: the build at `1481d15`, checkpoint 2.5's seal); the eight set files of `renders/scratch/ab25_tree/*.txt` are reused as `renders/scratch/ab26_tree/*.txt` with the `t5` arms copied as `old`. The A/B runs at the close (Task 9) over the whole sample, expected 0 DIFFER in every set.
- Dates: `date` first (the machine is HST); this plan is dated 2026-10-29.

## Review Focus

The five input classes the spec implies but no existing test exercises, most likely to bite first. Each line names the task whose tests pin it.

1. **A table whose length depends on the note's frequency** (WavetableSource, HybridKSSource: `len = rate / frequency`): today `fill_table` resizes at the first sample of every note whose length differs. Tasks 4 and 5 pin it: the table is reserved once in `on_prepare` for a stated floor frequency (`kMinTableHz`, 20 Hz as DelayLine's ring; the pin's own declared minimum is 0.01 Hz, a 19 MB table per voice, no floor at all), and a note at the floor, one at the pin's maximum and two notes of different pitch in one pool allocate nothing; a note below the floor gets a table of the floor's length — a pitch change for that note, which the A/B lists if any gated patch has one, and REVIEW R7 item (3) decides the floor's value from Task 2's survey of the lowest wavetable and KS frequencies in use.
2. **A note-on on a pooled voice** (`PitchedInstrument::play_note` → voice `start`; the live keyboard's path, on the audio thread): Task 2's gate arms around `play_note`, not only around `render`, so an allocation in voice acquisition or in a push-binding chain's `step()` at Setup is a failure; Task 3 pins Partials' `partials_start` through a real note.
3. **A `RepeatingSource` boundary inside `next()`** calls `start(repFrames)` on its child (legal by Part 2 section 4): the child's `start` must not allocate. Task 8 pins it: a RepeatingSource over a Partials-bearing child renders across two repetitions armed, count 0.
4. **A phrased score** (`hold`, `gate_release` re-laying the release at line end, `finish_open_lines`): Task 2's gate drives the embedded score through the extracted delivery, so every baseline with a phrased score (the `perform/` and articulation baselines) is covered; Task 8 adds a two-note held line through `load_instrument_patch_json` armed, count 0.
5. **A graph edited after `prepare`** (a partial count or stage list grown by the UI without a re-prepare): sizes fixed at `prepare` would be too small at `start`. Not pinned by a test: Part 2 section 4 and the 2.2 report's section 7 make a graph edited after `prepare` stale until the next `prepare`, and the UI re-prepares at every play (2.2 ruling 6). Each sizing added by Tasks 3–8 reads its count from a value fixed at `prepare` and `assign`s within that capacity at `start`; the Ledger records the rule, Part 7 owns the live-edit case.

Also noted, not a Review Focus line: `DrumKit` has no patch under library or baselines, so the gate never drives it; Task 8 adds one armed drum hit through `DrumKit::render` in `engine_tests`.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `tools/alloc_probe/alloc_probe.h` (new) | `namespace alloc_probe`: `arm()`, `disarm()`, `armed()`, `count()`, `bytes()`, `first_stack(int& depth)`, `reset()`. |
| `tools/alloc_probe/alloc_probe.cpp` (new) | the global `operator new`/`delete` family (plain, array, nothrow, aligned; sized deletes) forwarding to `malloc`/`free` (`_aligned_malloc`/`_aligned_free` for the aligned forms on MSVC); while armed, counts and bytes; on the first armed allocation captures up to 32 return addresses (`CaptureStackBackTrace` on Windows). Nothing in this file allocates. |
| `tools/alloc_probe/CMakeLists.txt` (new) | `add_library(mforce_alloc_probe STATIC alloc_probe.cpp)`; include dir `tools/alloc_probe`. |
| `tools/alloc_gate/main.cpp` (new) | the gate: patches and folders on the command line; per patch, load unarmed, arm around note-ons and render, report `ALLOC <patch>: <count> allocations, <bytes> bytes` plus the first stack symbolized, or `ok <patch>`; `--list` prints every patch's count; exit 1 if any allocation. |
| `tools/alloc_gate/CMakeLists.txt` (new) | links `mforce_engine`, `mforce_alloc_probe`, `dbghelp` (MSVC). |
| `CMakeLists.txt` (modify) | `add_subdirectory(tools/alloc_probe)`, `add_subdirectory(tools/alloc_gate)`; Task 9: `add_test(NAME alloc_gate COMMAND alloc_gate patches/library patches/baselines WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`. |
| `tools/engine_tests/CMakeLists.txt` (modify) | links `mforce_alloc_probe`. |
| `engine/include/mforce/render/patch_loader.h`, `engine/src/patch_loader.cpp` (modify, Task 1) | `void perform_embedded_score(PitchedInstrument&, const std::string& patchJsonText)`: the loader's score walk (NotePerformer, bpm 60, hold/onset/articulation/ornament, `finish_open_lines`, `conclude`) as one function with two callers — the loader's pre-render and the gate. |
| `engine/include/mforce/source/additive/partials.h` (modify, Task 3) | every array `partials_start` sizes is sized in `on_prepare` from the partial count; `partials_start` fills within capacity. |
| `engine/src/wavetable_source.cpp`, `engine/include/mforce/source/wavetable_source.h`, `engine/include/mforce/source/wave_evolution.h` (modify, Task 4) | `values_` reserved in `on_prepare` for `kMinTableHz`; `WaveEvolution::prepare(const RenderContext&, int maxLen)` lets each implementation reserve its buffers (Target, Bowed, Brass, ReactionDiffusion, CellularAutomaton, HistogramEqualize, Bezier). |
| `engine/src/hybrid_ks_source.cpp`, `engine/include/mforce/source/hybrid_ks_source.h` (modify, Task 5) | `table_` reserved in `on_prepare` for the same floor; `extract_partials`' three arrays sized with `size_targets`. |
| `engine/include/mforce/source/phased_value_source.h` (modify, Task 6) | `stageCounts_`/`boundaries_` sized in `on_prepare`. |
| `engine/include/mforce/source/segment_source.h` (modify, Task 6) | `currVals_` reserved in `on_prepare` for the largest shape it can be asked for; `update_segments` pushes within capacity. |
| `engine/include/mforce/source/additive/basic_additive_source.h` (modify, Task 6) | the five per-partial arrays sized in `on_prepare`. |
| whatever Task 2 finds beyond the 2.2 list (modify, Task 7) | one family per Ledger line. |
| `tools/engine_tests/main.cpp` (modify, Tasks 0, 3–8) | `run_alloc_probe_tests` (the probe itself), `run_allocation_tests` (one note per family through `load_instrument_patch_json`, armed, count 0; the Review Focus cases). |
| `docs/architecture/measurements/2026-10-29-allocations.md` (new, Task 2) | the gate's `--list` over library, baselines (and, for information, audition and pending): count and first stack per allocating patch, grouped by node family; the gate's run time. |
| `docs/autonomy/refactor/reports/2026-10-29-checkpoint-2.6.md` (new, Task 9), `REVIEW.md`, `BACKLOG.md`, `docs/autonomy/STATUS.md`, `tools/structure/metrics.json`, `docs/architecture/part2-valuesource-contract.md` (section 4 decided line, section 7 row 2.6) (modify, Task 9) | the seal. |

---

### Task 0: The probe

**Files:**
- Create: `tools/alloc_probe/alloc_probe.h`, `tools/alloc_probe/alloc_probe.cpp`, `tools/alloc_probe/CMakeLists.txt`
- Modify: `CMakeLists.txt` (`add_subdirectory(tools/alloc_probe)` after `tools/engine_tests`), `tools/engine_tests/CMakeLists.txt` (`target_link_libraries(engine_tests PRIVATE mforce_engine mforce_alloc_probe)`)
- Test: `tools/engine_tests/main.cpp` — `run_alloc_probe_tests()`

**Interfaces:**
- Produces, in `namespace alloc_probe` (`alloc_probe.h`):
  ```cpp
  void arm();                         // count from here
  void disarm();                      // stop counting; the count and the first stack stay
  bool armed();
  std::size_t count();                // armed allocations since the last reset
  std::size_t bytes();                // their sizes summed
  const void* const* first_stack(int& depth);   // return addresses of the first armed allocation; depth 0 when none
  void reset();                       // count 0, bytes 0, no stack
  ```
  State is `thread_local`. `operator new` increments only when `armed()` on the calling thread; the first armed allocation captures its stack (Windows: `CaptureStackBackTrace(1, 32, …)`; elsewhere depth stays 0). The replacement forwards to `std::malloc`/`std::free` and throws `std::bad_alloc` on a null `malloc` exactly as the default does; it never allocates itself.

- [ ] **Step 0: Date and base** — `date`; `git rev-parse --short HEAD` is `1481d15` or a docs-only descendant (`git diff 1481d15 HEAD --stat -- engine tools` empty); copy `build/tools/mforce_cli/Release/mforce_cli.exe` to `renders/scratch/ab_tree/cli_pre26.exe`; `mkdir renders/scratch/ab26_tree`, copy `renders/scratch/ab25_tree/*.txt` there and each set's `t5/hashes.txt` to `renders/scratch/ab26_tree/<set>/old/hashes.txt`.
- [ ] **Step 1: Write the failing test** — in `tools/engine_tests/main.cpp`, before `main()`:
  ```cpp
  static void run_alloc_probe_tests() {
      alloc_probe::reset();
      { std::vector<int> v(100); (void)v; }                 // unarmed: not counted
      CHECK(alloc_probe::count() == 0);
      alloc_probe::arm();
      { std::vector<int> v(100); (void)v; }                 // one allocation of 400 bytes
      alloc_probe::disarm();
      CHECK(alloc_probe::count() == 1);
      CHECK(alloc_probe::bytes() >= 400);
      int depth = 0; const void* const* stack = alloc_probe::first_stack(depth);
      CHECK(stack != nullptr);
  #ifdef _WIN32
      CHECK(depth > 0);
  #endif
      { std::vector<int> w(10); (void)w; }                  // disarmed again: still 1
      CHECK(alloc_probe::count() == 1);
      alloc_probe::reset();
      CHECK(alloc_probe::count() == 0 && alloc_probe::bytes() == 0);
      alloc_probe::first_stack(depth); CHECK(depth == 0);
  }
  ```
  and `run_alloc_probe_tests();` first in `main()`'s list.
- [ ] **Step 2: Run it to see it fail** — `cmake --build build --config Release --target engine_tests`: fails to compile (`alloc_probe` undeclared).
- [ ] **Step 3: Implement the probe** — `alloc_probe.h` with the seven functions above; `alloc_probe.cpp` with the state (`thread_local std::size_t count_, bytes_; thread_local bool armed_; thread_local void* stack_[32]; thread_local int depth_;`), the seven functions, and the replacement set: `operator new(size_t)`, `operator new[](size_t)`, both `nothrow` forms, `operator new(size_t, std::align_val_t)` and its array and nothrow forms, and the matching `operator delete` forms including the sized ones. One static `record(std::size_t)` does the counting and the first-stack capture; every `new` form calls it before returning. `CMakeLists.txt` for the library; the two CMake edits. The `.cpp` holds both the API and the replacements so that pulling `arm()` from the static library pulls the replacements into the executable.
- [ ] **Step 4: Run the tests** — rebuild `engine_tests`; `build/tools/engine_tests/Release/engine_tests.exe` → `ALL PASS`, 6486 + the new checks. `python tools/gates.py --fast`: ctest PASS, stamp PASS, roundtrip smoke the known wiring diff.
- [ ] **Step 5: Commit** — `git add tools/alloc_probe CMakeLists.txt tools/engine_tests/CMakeLists.txt tools/engine_tests/main.cpp && git commit -m "tools: the allocation probe — a counting operator new for the test executables, armed and disarmed by the test"`.

---

### Task 1: One score delivery, two callers

**Files:**
- Modify: `engine/include/mforce/render/patch_loader.h` (declare), `engine/src/patch_loader.cpp:1462-1529` (extract)
- Test: `tools/engine_tests/main.cpp` — one case in `run_allocation_tests()` (added here, grown by Tasks 3–8)

**Interfaces:**
- Produces: `void perform_embedded_score(PitchedInstrument& inst, const std::string& patchJsonText);` — parses the text; if it has a `score` array, delivers it exactly as the loader's pre-render does (a `NotePerformer`, bpm 60, the v2 keys `hold`/`onset`, articulation/ornament on unphrased notes, `finish_open_lines()`, `performer.conclude()`); if it has none, does nothing. The loader keeps an internal `perform_score_json(PitchedInstrument&, const json& root)` that both the pre-render and the string overload call, so the walk exists once.

- [ ] **Step 1: Write the failing test** — in `run_allocation_tests()` (new, called from `main()` after `run_alloc_probe_tests()`):
  ```cpp
  static void run_allocation_tests() {
      // The extracted delivery renders what the loader's pre-render rendered.
      const char* scored = R"({"sampleRate":48000,"seconds":1,"instrument":{"polyphony":2},
        "score":[{"note":60,"velocity":0.8,"duration":0.25},{"note":64,"duration":0.25}],
        "graph":{"output":"osc","nodes":[{"id":"osc","type":"SineSource","params":{"frequency":440.0}}]}})";
      auto viaLoader = load_patch_file_from_json_for_test(scored);     // see Step 3
      auto ip = load_instrument_patch_json(scored);
      perform_embedded_score(*ip.instrument, scored);
      std::vector<float> mono(48000, 0.0f);
      ip.instrument->render(mono.data(), 48000);
      CHECK(same_bits(viaLoader, mono));
  }
  ```
  where `viaLoader` is the left channel of a `load_patch_file` render of the same text written to a temp file (`std::filesystem::temp_directory_path() / "alloc_gate_scored.json"`), rendered through `p.mixer->render` for `p.frames` and de-interleaved; the helper `load_patch_file_from_json_for_test` lives in the test file. (`same_bits` exists in the file.)
- [ ] **Step 2: Run it to see it fail** — compile error: `perform_embedded_score` undeclared.
- [ ] **Step 3: Extract** — move the body of `if (root.contains("score")) { … }` at `patch_loader.cpp:1462-1529` into `static void perform_score_json(PitchedInstrument& inst, const json& root)` (the `NotePerformer`, `bpm`, the loop, `finish_open_lines`, `conclude`); the loader calls it; `perform_embedded_score(inst, text)` parses and calls it. Declare the string overload in `patch_loader.h` under `load_instrument_patch_json`.
- [ ] **Step 4: Run the tests and the gates** — `engine_tests` ALL PASS; `python tools/null_gate_perform_source.py --jobs 3` → `78/78`; `python tools/gates.py --fast` PASS/PASS/known.
- [ ] **Step 5: Commit** — `git commit -m "loader: the embedded score's delivery is one function with two callers — the pre-render and a host that drives the instrument itself"`.

---

### Task 2: The gate, and the measurement

**Files:**
- Create: `tools/alloc_gate/main.cpp`, `tools/alloc_gate/CMakeLists.txt`, `docs/architecture/measurements/2026-10-29-allocations.md`
- Modify: `CMakeLists.txt` (`add_subdirectory(tools/alloc_gate)`)

**Interfaces:**
- Consumes: `alloc_probe` (Task 0), `perform_embedded_score` (Task 1), `load_patch_file`, `load_instrument_patch`.
- Produces: `alloc_gate [--list] [--verbose] <patch.json | folder>...` — exit 0 when no patch allocated, 1 otherwise, 2 on a load failure (reported as `LOAD FAIL <patch>: <what>`, not counted as an allocation).

- [ ] **Step 1: Write the gate** — `main.cpp` (under 300 lines, every function under 80):
  - `collect(paths) → std::vector<std::filesystem::path>`: files as given, folders recursed for `*.json`, sorted.
  - `gate_one(path) → Result{count, bytes, stack}`: `Patch p = load_patch_file(path)` (unarmed; gives `frames` and the mixer-mode graph); read the file's text; if the JSON has an `instrument` block: `auto ip = load_instrument_patch(path);` then `alloc_probe::reset(); alloc_probe::arm(); perform_embedded_score(*ip.instrument, text); if (text has no "score") ip.instrument->play_note({60.0f, 0.8f, 0.5f, 0.0f, false}, 0.0f); ip.instrument->render(mono.data(), p.frames); alloc_probe::disarm();` — else: `alloc_probe::reset(); alloc_probe::arm(); p.mixer->render(outLR.data(), p.frames); alloc_probe::disarm();`. Buffers are allocated before arming.
  - `report(path, result)`: `ok <path>` (only with `--verbose`, or always with `--list` as `<count>\t<path>`), or `ALLOC <path>: <count> allocations, <bytes> bytes` followed by the first stack, one frame per line, `  <function> (<file>:<line>)` when `SymFromAddr`/`SymGetLineFromAddr64` resolve it, `  0x<address>` otherwise. `SymInitialize(GetCurrentProcess(), nullptr, TRUE)` once at start, `SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME)`. Guarded by `#ifdef _WIN32`.
  - `main`: options, collect, loop, summary line `<n> patches: <k> allocate`, exit code.
  - `CMakeLists.txt`: `add_executable(alloc_gate main.cpp)`, `target_link_libraries(alloc_gate PRIVATE mforce_engine mforce_alloc_probe)`, `if (MSVC) target_link_libraries(alloc_gate PRIVATE dbghelp) target_compile_options(alloc_gate PRIVATE /W4 /permissive-) endif()`.
- [ ] **Step 2: Build and prove it on the zero-allocation and one-allocation cases** — `cmake --build build --config Release --target alloc_gate`; `build/tools/alloc_gate/Release/alloc_gate.exe renders/scratch/ab25_tree/zero_freq.json` → `ok` or `ALLOC` (record which; a SineSource with a constant frequency should be `ok`); `alloc_gate.exe patches/library/winds/oboe1.json` → expected `ALLOC` if oboe1's graph holds any family on the 2.2 list, else `ok`; the first stack must name an engine function in a `start`/`compute` path (the symbolization works) — if it prints only addresses, fix the `SymInitialize` call before going on.
- [ ] **Step 3: Measure** — `alloc_gate.exe --list patches/library patches/baselines > renders/scratch/alloc26_baseline.txt` (count per patch) and `alloc_gate.exe patches/library patches/baselines > renders/scratch/alloc26_baseline_stacks.txt` (the stacks); the same for `patches/audition patches/pending` into `alloc26_queues*.txt` (information only — not the gate's set). Time the baseline run (`Measure-Command` or `time`): the number decides Task 9's placement (ctest if under 60 seconds, else `gates.py`'s slow list). Also survey the floor: a script over `patches/library`, `patches/baselines`, `patches/audition`, `patches/pending` and `patches/sweep` that lists every `WavetableSource` and `HybridKSSource` node's `frequency` (a constant, or the lowest value a wired pin's declared range can reach) — the lowest value per folder goes in the document, and decides whether 20 Hz is a floor no gated patch crosses.
- [ ] **Step 4: Write the measurement document** — `docs/architecture/measurements/2026-10-29-allocations.md`: the command, the date and sha, the run time; a table of every allocating patch in library and baselines with its count, bytes and the first stack's top engine frame; the same patches grouped by node family (the frame's class); the list compared with the 2.2 report's section 7 list (families confirmed, families absent, families new — each new one becomes a Task 7 line in the Ledger); the queue folders' counts in one paragraph.
- [ ] **Step 5: Commit** — `git add tools/alloc_gate CMakeLists.txt docs/architecture/measurements/2026-10-29-allocations.md && git commit -m "tools: alloc_gate — a patch or a folder, the allocations between note-on and the last sample, and where the first one came from; measure: the allocating baselines, by family"`.

---

### Task 3: Partials

**Files:**
- Modify: `engine/include/mforce/source/additive/partials.h:215-370` (`on_start`/`partials_start`), `:700-720` (`update_arrays`/`init_detune_values`), the three `init_arrays` (`:902-904`, `:958-960`, `:1039-1040`) and `init_arrays_defaults` (`:1050-1052`)
- Test: `tools/engine_tests/main.cpp` — `run_allocation_tests()`

**Interfaces:**
- Consumes: `alloc_probe`, `load_instrument_patch_json`.
- Produces: a helper the later tasks reuse —
  ```cpp
  // One note through the real host, armed from note-on to the last sample.
  static std::size_t allocations_in_one_note(const char* patchJson, float noteHz = 60.0f, int frames = 4800);
  ```
  (loads, `play_note({noteHz, 0.8f, 0.1f, 0.0f, false}, 0.0f)` armed, `render(frames)` armed, returns `alloc_probe::count()`; prints the first stack's depth on failure for the reader).

- [ ] **Step 1: Write the failing test** — in `run_allocation_tests()`:
  ```cpp
  const char* additive = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"add","nodes":[
    {"id":"p","type":"Partials","params":{"count":16}},
    {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"partials":{"ref":"p"}}}]}})";
  CHECK(allocations_in_one_note(additive) == 0);
  CHECK(allocations_in_one_note(additive, 72.0f) == 0);     // a second pitch on the pool's other voice
  ```
  (The Partials `count` setting is declared `{2, 0, 16}`; use the `partials` wiring form the library's additive patches use — read one, e.g. `patches/library/strings/viola_default.json`, and copy its node keys.)
- [ ] **Step 2: Run it to see it fail** — `engine_tests` → `FAIL … allocations_in_one_note(additive) == 0` with a count ≥ 20 (the `assign`s and `resize`s of the first `partials_start`).
- [ ] **Step 3: Move the sizing to `on_prepare`** — `Partials::on_prepare(const RenderContext&)` (new override, or extend the existing one) computes `n` as `partials_start` does today (`mult1_.size()` after `update_arrays`/`init_arrays`, which must therefore run in `on_prepare` when `arrayUpdateReq_` is set; `count` is a `Setting<int>` declared `{2, 0, 16}`, so `n <= 16` always — sizing for `count.value` at `prepare` is the rule, the declared maximum the fallback if a subclass can grow `n` per note) and `assign`s every array `partials_start` assigns, to the same values; `partials_start` keeps its `assign`s (a fill within capacity, no allocation when `size() == n`) and its `expandRng_`/`apply_expand_rule` work, which must not grow the arrays — if `apply_expand_rule` can change `n` per note, size for `recurse`'s maximum in `on_prepare` and record it in the Ledger. `init_detune_values`' `dtVals_.resize(n)` and `onsetDelay_.resize(n)` become fills.
- [ ] **Step 4: Run the tests and the gates** — `engine_tests` ALL PASS; null gate `78/78`; `alloc_gate.exe --list patches/library patches/baselines`: every additive patch's count is now 0 or attributable to another family (record the new totals in the Ledger); `gates.py --fast`.
- [ ] **Step 5: Commit** — `git commit -m "partials: every per-partial array is sized once in on_prepare; start fills within capacity"`.

---

### Task 4: WavetableSource and its evolutions

**Files:**
- Modify: `engine/src/wavetable_source.cpp:12-35` (`on_prepare`, `fill_table`), `engine/include/mforce/source/wavetable_source.h`, `engine/include/mforce/source/wave_evolution.h:30` (the `prepare` virtual) and the seven implementations' buffers (`:194`, `:453-454`, `:579`, `:852-855`, `:1018-1029`, `:1100/1126`, `:1199`)

**Interfaces:**
- Produces: `virtual void WaveEvolution::prepare(const RenderContext& ctx, int maxLen)` — `maxLen` is the longest table the owner can ask for (the owner's `sample_rate() / frequency.min`, rounded up, plus one); each implementation reserves its length-dependent buffers to `maxLen` here and `assign`s within that capacity where it sized before. The old one-argument `prepare` is removed; its one caller is `WavetableSource::on_prepare` (`wavetable_source.cpp:16`; HybridKSSource has no evolution — `git grep "evolution_->prepare" -- engine` lists the one site).

- [ ] **Step 1: Write the failing tests** — in `run_allocation_tests()`, one JSON per evolution that the library uses (read `patches/library` for the `evolution` values in use; at least `target`, `bowed`, `brass`, and the plain table), each `CHECK(allocations_in_one_note(json) == 0)`, plus one at the pin's minimum frequency (`noteHz` chosen so the frequency pin reads its minimum) and one at its maximum.
- [ ] **Step 2: Run them to see them fail** — counts ≥ 1 (`values_.resize` at the first sample; the evolution's buffers).
- [ ] **Step 3: Reserve at prepare** — `WavetableSource::on_prepare`: `static constexpr float kMinTableHz = 20.0f;` (one constant, shared with HybridKSSource through the header both include, `core/dsp_wave_source.h`); `const int maxLen = int(std::ceil(float(sample_rate()) / kMinTableHz)) + 1; values_.reserve(size_t(maxLen));` then `evolution_->prepare(ctx, maxLen)`; `fill_table` computes `len` as today and then `len = std::min(len, maxLen)` — within capacity by construction; a note whose frequency is below the floor gets the floor's length (the pitch change the Review Focus names; the A/B lists any gated patch that has one). Each evolution: `reserve`/`assign` in `prepare(ctx, maxLen)` for `resampledTarget_`, `bridgeLine_`/`neckLine_` (sized `2*Lb`/`2*Ln` from the frequency — reserve `2*maxLen`), `rawLoop_`, `u_`/`v_`/`uPrev_`/`vPrev_`, `state_`/`snapshot_`, `cdf_` (`bins`), `target_`.
- [ ] **Step 4: Run the tests and the gates** — `engine_tests` ALL PASS; null gate `78/78` (a changed render here means a length was clamped where it was not before — stop, Ledger, Matt); `alloc_gate --list` totals recorded.
- [ ] **Step 5: Commit** — `git commit -m "wavetable: the table and every evolution's buffers are reserved once in prepare for the pin's lowest frequency; a note fits within that capacity"`.

---

### Task 5: HybridKSSource

**Files:**
- Modify: `engine/src/hybrid_ks_source.cpp:15-25` (`size_targets`, `on_prepare`), `:43-70` (`fill_table`, `extract_partials`), `engine/include/mforce/source/hybrid_ks_source.h`

- [ ] **Step 1: Write the failing test** — one `allocations_in_one_note` on a HybridKS patch (copy the node from a library or baselines patch that uses `HybridKSSource`; if none, the registry name and its required params) at a mid pitch and at the pin's minimum: `== 0`.
- [ ] **Step 2: Run it to see it fail** — count ≥ 1 (`table_.resize` in `fill_table`; the three `resize`s in `extract_partials`).
- [ ] **Step 3: Reserve at prepare** — `on_prepare`: `table_.reserve(maxLen)` with Task 4's `kMinTableHz` and formula, and `fill_table`'s `len` clamped to `maxLen` the same way; `size_targets()` also sizes `partialAmpl_`, `partialPhase_`, `startAmpl_` to `numPartials.value` (it already sizes `targetAmpl_`); `extract_partials` fills within those sizes.
- [ ] **Step 4: Run the tests and the gates** — ALL PASS; `78/78`; totals.
- [ ] **Step 5: Commit** — `git commit -m "hybrid ks: the table is reserved and the partial arrays sized in prepare; start and the transition fill within capacity"`.

---

### Task 6: PhasedValueSource, SegmentSource, BasicAdditiveSource

**Files:**
- Modify: `engine/include/mforce/source/phased_value_source.h:57, 90`, `engine/include/mforce/source/segment_source.h:117-175`, `engine/include/mforce/source/additive/basic_additive_source.h:62-66`

- [ ] **Step 1: Write the failing tests** — three `allocations_in_one_note` cases, one per node, each built from a baselines or library patch that carries the node (or the registry's minimal form): `== 0`.
- [ ] **Step 2: Run them to see them fail** — PhasedValueSource: 2 (`stageCounts_`, `boundaries_` at the first `on_start`); SegmentSource: ≥ 1 (`currVals_` at the first `compute`); BasicAdditive: 5 (`wave()`'s first sample).
- [ ] **Step 3: Size at prepare** — PhasedValueSource: `on_prepare` sizes both vectors to `stages_.size()` (the stage list is fixed before `prepare`); SegmentSource: `on_prepare` reserves `currVals_` to `(maxPairs + 1) * 2` where `maxPairs` is the largest `shape` it can be given — the shape's declared size in its descriptor, or the current `shape.size() / 2` if the shape cannot grow after `prepare` (read the setter; record which in the Ledger); `update_segments` does `clear()` then `push_back` within capacity; BasicAdditiveSource: `on_prepare` sizes the five arrays to the partial count.
- [ ] **Step 4: Run the tests and the gates** — ALL PASS; `78/78`; totals.
- [ ] **Step 5: Commit** — `git commit -m "phased, segment, basic additive: per-note arrays sized once in prepare"`.

---

### Task 7: What the measurement found beyond the list

**Files:**
- Modify: one family per Ledger line, named by Task 2's measurement document.

- [ ] **Step 1: For each family Task 2's document lists that Tasks 3–6 do not cover** (candidates from the code survey, to be confirmed or cleared by the measurement: `Envelope::replace_stages` if `gate_release` reaches it at line end; `LayeredRedNoiseSource::rebuild_layers_` if `layersDirty_` is set after `prepare`; `FMSource::build_decimation` if a setting changes it after `prepare`; `AdditiveSource2`'s note arrays if a note exceeds the reserved capacity; `play_note`'s voice acquisition; a push-binding chain's `step()` at Setup): write the one-note test (`== 0`), see it fail, move the allocation to `prepare` or size for the maximum there, see it pass, null gate `78/78`, commit — one commit per family, `"<node>: <what moved> to prepare"`. A family the measurement does not list is not touched.
- [ ] **Step 2: When `alloc_gate.exe patches/library patches/baselines` exits 0** — record the run in the Ledger (`<n> patches: 0 allocate`, the run time) and go on; if a family cannot move its allocation (a size that is genuinely unknown before the note), STOP: write the case in the Ledger with the size's source and ask Matt — the spec admits no tolerance list, so the answer is a design decision, not a plan step.

---

### Task 8: The Review Focus cases

**Files:**
- Modify: `tools/engine_tests/main.cpp` — `run_allocation_tests()`

- [ ] **Step 1: Write the tests** — (a) a `RepeatingSource` whose child carries Partials, rendered armed across two repetition boundaries (`frames` long enough for two repeats): count 0; (b) a phrased score through `load_instrument_patch_json` + `perform_embedded_score` on a `sustaining` instrument — two notes with `hold` then a release at line end — armed: count 0; (c) one `DrumKit` hit through `DrumKit::render` armed: count 0 (build the kit as the drum tests in this file do); (d) `play_note` alone, armed, on a prepared pool: count 0 (the live note-on).
- [ ] **Step 2: Run them** — each must pass against the tree after Task 7; a failure here is a Task 7 family that the baseline set did not exercise: fix it the same way, Ledger line, commit.
- [ ] **Step 3: Commit** — `git commit -m "tests: a repetition boundary, a held line's release, a drum hit and a bare note-on allocate nothing"`.

---

### Task 9: Close the checkpoint

**Files:**
- Modify: `CMakeLists.txt` (the `add_test` for `alloc_gate`), `tools/gates.py` (nothing if ctest carries it; else the slow list), `docs/architecture/part2-valuesource-contract.md` (section 4: "Decided 2026-…: landed as checkpoint 2.6 …"; section 7 row 2.6's cell), `REVIEW.md`, `BACKLOG.md`, `docs/autonomy/STATUS.md`, `tools/structure/metrics.json`, this plan's Ledger
- Create: `docs/autonomy/refactor/reports/2026-10-29-checkpoint-2.6.md`

- [ ] **Step 1: The gate joins the gates** — `add_test(NAME alloc_gate COMMAND alloc_gate patches/library patches/baselines WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})` in the root `CMakeLists.txt` (ctest, hence `gates.py --fast`) if Task 2's timing was under 60 seconds; otherwise a line in `gates.py`'s `SLOW` list. `python tools/gates.py` (all): everything passes except the two roundtrip gates on the known six.
- [ ] **Step 2: Nothing old remains** — `git grep -n "resize\|assign\|push_back\|reserve" -- engine/include/mforce/source engine/include/mforce/filter engine/include/mforce/core engine/src` lists no call inside an `on_start`, `start`, `compute`, `next`, `step`, `wave`, `fill_table`, `update_segments`, `adjust`, `shape_excitation` or `evolve` body except a `resize`/`assign`/`push_back` within a capacity reserved in `prepare` (each such line carries a comment saying so, or sits in a function whose header does).
- [ ] **Step 3: The A/B** — `python tools/ab_render_set.py render renders/scratch/ab26_tree/<set>.txt renders/scratch/ab26_tree/<set> new` for the eight sets; `compare <set> old new`: DIFFER 0 in every set, "present in one arm only" unchanged from 2.5's Appendix A. A DIFFER here is a Ledger entry and Matt's call.
- [ ] **Step 4: Timing** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre26.exe build/tools/mforce_cli/Release/mforce_cli.exe 7` on an idle machine (a process sample first; the IntelliSense server idle); expected within noise — a `reserve` at prepare costs nothing per sample, and a `resize` within capacity is a loop over the same elements it filled before.
- [ ] **Step 5: Meter and delta** — `python tools/structure/check.py --snapshot`; `--delta <Task 0 sha>`; `tools/alloc_probe` and `tools/alloc_gate` need entries in `tools/structure/modules.json` (the map; `unmapped_files` must stay 0).
- [ ] **Step 6: Independent review** — a fresh Fable reviewer, read-only, with `REVIEW_BRIEF.md`, the diff, `MODULES.md`, Part 2 section 4 as the signed design and this plan's Review Focus and Ledger, told to refute; the probe's replacement set (every `new` form, the aligned ones), the gate's arming points, each family's "fits within capacity" claim, and the per-note tests' non-vacuity (does each RED count come from the node under test?) are the places to look. Fix wave; the same reviewer re-reviews.
- [ ] **Step 7: Architecture report R7** — the ten-section format: section 2 entries for the probe (design choice: a replaced global `operator new` in the test executables only; alternatives: `_CrtSetAllocHook` (Debug CRT only), an allocator template threaded through every node (the whole tree), instrumenting `ValueSource::start`/`next` with a hook (a per-sample cost in production)), `perform_embedded_score`, `WaveEvolution::prepare(ctx, maxLen)`, each family's sizing; section 3 the interfaces; section 4 duplicates (the loader's score walk, now one; the three `init_arrays` if they collapsed); section 5 the meter delta, the measurement against the gate's final run (every allocating baseline, by family, each now 0), the A/B (0 moved), the timing; section 6 the Ledger; section 7 the debt (the queue folders' allocations, by family, not gated; `WaveEvolution` still a part, not a Component — Part 3; the UI's live-edit case — Part 7; anything Task 7 left); section 9 the reading guide; section 10 three lessons: (1) replacing `operator new` — why a definition in any translation unit replaces the global one, why the aligned and nothrow forms must come too, and what the C# analogue is not (there is none: the GC owns allocation; `GC.GetAllocatedBytesForCurrentThread` is the nearest read-only cousin); (2) `reserve` versus `resize` versus `assign`, and why "within capacity" is a contract the code must state rather than a fact the reader can see; (3) `thread_local` — one copy of the probe's state per thread, why the audio thread and the UI thread never see each other's count, and the C# `[ThreadStatic]`. REVIEW R7 awaiting with its verdict items: (0) the gate as a `--fast` gate (ctest) or a slow one, from the measured run time; (1) any render the A/B moved (expected none); (2) the queue folders' allocating patches (information: left as they are, or a sweep's node family worth fixing now); (3) `kMinTableHz` = 20 Hz as the floor for every frequency-dependent table, against Task 2's survey of the lowest wavetable and KS frequencies in library, baselines and the queues — keep, lower it, or make it a setting. BACKLOG row 1: 2.6 DONE, Part 2 complete; row 2 (Part 3) next. STATUS handoff.
- [ ] **Step 8: Commit** — `git commit -m "docs: checkpoint 2.6 report (R7), REVIEW R7 awaiting, Part 2 complete, backlog, STATUS handoff, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **The probe is a replaced global `operator new`, linked only into the test executables.** Alternatives: `_CrtSetAllocHook` (Debug CRT only; the gates run Release); an allocator parameter on every container in every node (touches the whole tree for a test); a hook called from `ValueSource::start`/`next` (a function-pointer test on the per-sample path in production). A definition of the replaceable allocation functions in any translation unit of a program replaces the library's for that program — so the CLI and the UI, which do not link `mforce_alloc_probe`, keep the default allocator untouched (Matt, 2026-10-29).
2. **The gate arms around note-on and render, not around load.** `load_patch_file` pre-renders a scored instrument patch inside the load, and `prepare` may allocate, so a gate that armed around the whole load would fail on legitimate allocations and a gate that armed around `mixer->render` alone would miss the first-sample class the checkpoint exists to remove (the pre-render already paid it). The gate loads the instrument without a score (`load_instrument_patch`), arms, and delivers the embedded score itself through the loader's own walk (Task 1), then renders `Patch.frames` (from an unarmed `load_patch_file` of the same file). A scoreless instrument patch gets one note (C4, half a second) so its `start` and `next` run. Mixer-mode graphs arm around `mixer->render`, whose adapters call `start` and `next`.
3. **The score delivery is extracted, not duplicated.** The loader's walk (hold, onset, articulation, ornament, `finish_open_lines`, `conclude`) is forty lines with rules a second copy would drift from; one function, two callers, null gate `78/78` proves the move. The two loader paths' voice construction stays as it is (Part 4's).
4. **The sizing rule: reserve for the floor at `prepare`, fit within capacity at `start`.** A length that depends on the note's frequency is bounded by a stated floor frequency, `kMinTableHz` = 20 Hz, the DelayLine's 2.2 idiom (`sample_rate() / 20 + 4`) generalised — not the pin's declared minimum, which is 0.01 Hz on every WaveSource and would reserve 19 MB per table per voice; a note below the floor gets the floor's length, a pitch change the A/B lists and REVIEW R7 item (3) weighs against the survey of frequencies in use (Task 2); a count that depends on a setting (partials, stages, layers, bins) is fixed at `prepare` because a graph edited afterwards is stale until the next `prepare` (2.2 ruling 6, Part 7 owns the live edit). `assign(n, v)` with `n == size()` and `resize(len)` with `len <= capacity()` allocate nothing; each such call on a render path carries the fact in a comment, and the gate is the proof.
5. **`WaveEvolution::prepare` takes the owner's maximum length.** The evolutions are parts, not Components (2.2 report section 7; Part 3 moves them), and the length they size for is the owner's to know; one argument added to the existing virtual, every implementation touched once. Alternative, a second `reserve(maxLen)` hook beside `prepare(ctx)`: two hooks for one phase.
6. **No tolerance list** (R3 verdict 2026-10-18, Part 2 section 4). If a family's size is genuinely unknown before the note, execution STOPS and asks (Task 7 step 2); the plan does not pre-decide what is not known.
7. **The measurement is the gate's own `--list`**, kept as a committed tool rather than a counter deleted in the next task (2.4's and 2.5's pattern): the gate IS the deliverable, and its listing mode is how a later reader checks the queue folders. The queue folders (audition, pending) are measured for information and are not the gate's set — the signed sentence is "every baseline patch", and Matt's brainstorm kept the UI and the CLI render free of the probe.
8. **Where the gate runs is decided by its measured time**: under 60 seconds it is a ctest, hence `gates.py --fast`, hence before every commit; above, the slow list. REVIEW R7 item (0) lets Matt move it.
9. **Byte-identity is expected total** (section 7 row 2.6: "none"); the null gate after every task and the whole-sample A/B at the close are the proof, and a moved render is a finding to explain (2.2's DelayLine precedent: a moved allocation changed the heap layout and exposed an out-of-bounds read), never a tolerance.
10. **Order of tasks**: the probe, the extraction, the gate and its measurement, then the families largest first (Partials carries the most sites; the two tables share a rule; the three small nodes together), then whatever the measurement added, then the Review Focus tests, then the close. The gate joins ctest only at the close, when it passes.

(Execution rulings follow here.)

## Self-review notes

- **Spec coverage.** Section 4 "`start(frames)` … may not allocate" — Tasks 3–7 move every allocation the measurement finds; "the allocation test asserts both: no allocation in `start` or in `next`, over every baseline patch" — Task 2's gate arms from note-on (`play_note` → `start`) through every `next`, over `patches/library` and `patches/baselines`, and Task 9 makes it a gate; "the test lands with no tolerance list" — ruling 6 and Task 7 step 2; "R3 verdict: the `start()` and first-`compute()` allocations listed in the checkpoint 2.2 report's section 7 are the work list; each moves into `prepare`" — Tasks 3–6 name those sites by file and line, Task 7 covers what the measurement adds; section 7 row 2.6 "none; the test is the new gate" — ruling 9, Task 9 steps 1 and 3. Matt's brainstorm decision (tool first, gate = the tool over library and baselines, nothing in the UI or CLI) — rulings 1, 7 and the Global Constraints.
- **Step scan.** Every code step names the file, the function and the rule it applies; the probe's API is the exact signature set; the gate's output lines are exact; every test carries its assertions; the measurement and A/B commands carry their expected outputs. Open by design: the registry names and param keys of the family tests (read from a library patch at execution — the plan says which) and which families Task 7 holds (the measurement decides; the plan names the candidates).
- **Type consistency.** `alloc_probe::{arm, disarm, armed, count, bytes, first_stack, reset}`, `perform_embedded_score(PitchedInstrument&, const std::string&)`, `allocations_in_one_note(const char*, float, int)`, `WaveEvolution::prepare(const RenderContext&, int maxLen)`, `kMinTableHz`, `cli_pre26.exe`, `renders/scratch/ab26_tree/<set>/{old,new}` are used with those names throughout.
- **Review Focus.** 1 — Tasks 4 and 5's minimum/maximum-frequency cases; 2 — Task 2's arming point and Task 3's two-pitch case, Task 8(d); 3 — Task 8(a); 4 — Task 2's delivery and Task 8(b); 5 — not pinned, by contract, recorded in ruling 4. The DrumKit note — Task 8(c).
- **Proportion.** Code appears for the probe's API, the two test fixtures and the gate's output format; the rest is signatures, rules and commands with expected outputs. The engine change is a sizing moved in six or seven node files plus one forty-line extraction; the plan is mostly the gate and the proof.
