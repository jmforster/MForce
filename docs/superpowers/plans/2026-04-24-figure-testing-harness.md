# Figure Testing Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the `test_figures` binary into an exhaustive figure test suite (pure unit tests for every `figure_transforms::*`, integration tests via WrapperPhraseStrategy, pitch-realization checks) and a listening harness for the `fig/invert/retrograde/retrograde-invert` exercise. Retire `--test-rfb` and `--test-replicate` from `mforce_cli` once coverage is in place.

**Architecture:** Single binary `test_figures` with two modes — default (no args) runs all tests via a minimal `RUN_TEST` macro and exits 0 on success / 1 on any failure; `--render <patch> <out_dir>` produces a four-phrase listening passage using WrapperPhraseStrategy. Tests assert directly on `MelodicFigure::units` (unit tests) and on the realized `Phrase::figures` / `Part::elementSequence` (integration tests). No external test framework.

**Tech Stack:** C++20, header-only engine, CMake, MSVC on Windows. Uses the VS-bundled CMake at `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

**Spec:** `docs/superpowers/specs/2026-04-24-figure-testing-harness-design.md`.

**Predecessor:** Step 1 (WrapperPhraseStrategy) on branch `figure-builder-redesign` through commit `cd2491d`.

---

## Pending decisions (resolved defaults)

From the spec — executing against these defaults. Override before starting if any should change.

- **D1** — RFB goldens: delete.
- **D2** — Binary layout: one `test_figures` with `--render` subcommand.
- **D3** — Listening goldens: JSON only, no WAV golden.
- **D4** — Transform coverage: every transform (deterministic + randomized).
- **D5** — Integration coverage: every deterministic transform round-trip.
- **D6** — Listening passage: single canned fig/invert/retrograde/retrograde-invert four-phrase passage. Base figure hand-authored (not RFB-built).
- **D7** — Pitch oracle: hand-coded MIDI note numbers.

If Matt overrides any of these, affected tasks:
- D1 → skip Task 10.
- D2 → Task 8 creates a separate `tools/demo_transforms` binary.
- D3 → skip the golden commit at end of Task 8.
- D4/D5 → drop specific test blocks from Tasks 2–7.
- D6 → Task 8 base-figure factory becomes RFB-built or multi-passage.
- D7 → Task 7 reshapes the oracle computation.

---

## File Structure

- **Modify:** `tools/test_figures/main.cpp` (growing across Tasks 1–8 and 11).
- **Modify:** `tools/mforce_cli/main.cpp` (Task 9) — delete `run_test_rfb`, `run_test_replicate`, supporting namespaces, dispatch lines.
- **Delete:** `renders/rfb_build.json`, `renders/rfb_build_by_count.json`, `renders/rfb_build_by_length.json`, `renders/rfb_build_by_rhythm.json`, `renders/rfb_build_by_steps.json`, `renders/rfb_build_singleton.json` (Task 10).
- **Create:** `renders/golden_fig_inv_retro_ri.json` (Task 8, per D3).
- **Modify:** `docs/ComposerRefactor3.md` (Task 11).

---

## Task 1: Test harness scaffolding

**Goal:** Replace the standalone smoke test with a `RUN_TEST`-driven runner that can hold many tests. Preserve the step-1 smoke test as one of them.

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Rewrite `main.cpp` with the test-runner scaffold.**

Replace the entire file with:

```cpp
#include "mforce/music/basics.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace mforce;

// ============================================================================
// Minimal test harness.
// ============================================================================
namespace {

int g_passed = 0;
int g_failed = 0;

#define EXPECT_EQ(actual, expected, msg) do {                                \
    if (!((actual) == (expected))) {                                          \
        std::cerr << "  FAIL(" << __LINE__ << "): " << (msg)                  \
                  << " — expected " << (expected)                             \
                  << " got " << (actual) << "\n";                             \
        return 1;                                                             \
    }                                                                         \
} while (0)

#define EXPECT_NEAR(actual, expected, tol, msg) do {                         \
    if (std::fabs(double(actual) - double(expected)) > double(tol)) {         \
        std::cerr << "  FAIL(" << __LINE__ << "): " << (msg)                  \
                  << " — expected " << (expected)                             \
                  << " got " << (actual) << "\n";                             \
        return 1;                                                             \
    }                                                                         \
} while (0)

#define RUN_TEST(fn) do {                                                    \
    std::cerr << "[TEST] " #fn " ...";                                        \
    int rc = fn();                                                            \
    if (rc == 0) { std::cerr << " PASS\n"; ++g_passed; }                      \
    else         { std::cerr << " FAIL\n"; ++g_failed; }                      \
} while (0)

// ----------------------------------------------------------------------------
// Smoke test (preserved from step 1) — round-trips a locked figure through
// the WrapperPhraseStrategy-driven Composer pipeline and verifies the realized
// figure matches the input unit-for-unit.
// ----------------------------------------------------------------------------
int test_smoke_round_trip() {
    MelodicFigure expected;
    expected.units.push_back({1.0f,  0});
    expected.units.push_back({1.0f, +1});
    expected.units.push_back({1.0f, -1});
    expected.units.push_back({1.0f,  0});

    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xC0FFEEu;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 8.0f;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = expected;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    EXPECT_EQ(piece.parts.size(), 1u, "piece.parts.size");
    auto passageIt = piece.parts[0].passages.find("Main");
    if (passageIt == piece.parts[0].passages.end()) {
        std::cerr << "  FAIL: passages[Main] missing\n"; return 1;
    }
    const Passage& p = passageIt->second;
    EXPECT_EQ(p.phrases.size(), 1u, "phrases.size");
    const Phrase& ph = p.phrases[0];
    EXPECT_EQ(ph.figures.size(), 1u, "figures.size");
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    if (!fig) { std::cerr << "  FAIL: not MelodicFigure\n"; return 1; }
    EXPECT_EQ(fig->units.size(), expected.units.size(), "units.size");
    for (size_t i = 0; i < expected.units.size(); ++i) {
        EXPECT_EQ(fig->units[i].step, expected.units[i].step, "step");
        EXPECT_NEAR(fig->units[i].duration, expected.units[i].duration, 1e-5f, "dur");
    }
    return 0;
}

int run_unit_tests() {
    // Populated by subsequent tasks.
    return 0;
}

int run_integration_tests() {
    RUN_TEST(test_smoke_round_trip);
    return 0;
}

int run_render(int argc, char** argv) {
    // Populated by Task 8.
    (void)argc; (void)argv;
    std::cerr << "test_figures --render: not yet implemented\n";
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--render") {
        return run_render(argc, argv);
    }
    run_unit_tests();
    run_integration_tests();
    std::cerr << "\n" << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: stderr prints `[TEST] test_smoke_round_trip ... PASS` then `1 passed, 0 failed`; exit 0.

- [ ] **Step 3: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): scaffold RUN_TEST harness around smoke test"
```

---

## Task 2: Unit tests — elementary transforms

**Goal:** Cover `prune`, `set_last_pulse`, `adjust_last_pulse`, `stretch`, `compress`, `invert`, `retrograde_steps`. Each is a pure function — assert on step sequences and durations.

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Insert unit-test functions above `run_unit_tests()`.**

Add the following block immediately above `int run_unit_tests()`:

```cpp
// ----------------------------------------------------------------------------
// figure_transforms::* — elementary transform unit tests.
// ----------------------------------------------------------------------------

MelodicFigure fig4() {
    MelodicFigure f;
    f.units.push_back({1.0f,  0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -2});
    return f;
}

int test_invert() {
    auto out = figure_transforms::invert(fig4());
    EXPECT_EQ(out.units.size(), 4u, "size");
    EXPECT_EQ(out.units[0].step,  0, "step 0");
    EXPECT_EQ(out.units[1].step, -1, "step 1");
    EXPECT_EQ(out.units[2].step, -1, "step 2");
    EXPECT_EQ(out.units[3].step, +2, "step 3");
    return 0;
}

int test_retrograde_steps() {
    // Doc example: [0, +1, +1, -2] -> [0, +2, -1, -1]
    auto out = figure_transforms::retrograde_steps(fig4());
    EXPECT_EQ(out.units.size(), 4u, "size");
    EXPECT_EQ(out.units[0].step,  0, "step 0");
    EXPECT_EQ(out.units[1].step, +2, "step 1");
    EXPECT_EQ(out.units[2].step, -1, "step 2");
    EXPECT_EQ(out.units[3].step, -1, "step 3");
    return 0;
}

int test_prune_end() {
    auto out = figure_transforms::prune(fig4(), 2);
    EXPECT_EQ(out.units.size(), 2u, "size");
    EXPECT_EQ(out.units[0].step, 0,  "step 0");
    EXPECT_EQ(out.units[1].step, +1, "step 1");
    return 0;
}

int test_prune_start() {
    auto out = figure_transforms::prune(fig4(), 2, /*from_start=*/true);
    EXPECT_EQ(out.units.size(), 2u, "size");
    EXPECT_EQ(out.units[0].step, 0,  "step 0 forced to 0");
    EXPECT_EQ(out.units[1].step, -2, "step 1 preserved");
    return 0;
}

int test_set_last_pulse() {
    MelodicFigure f; f.units.push_back({1.0f, 0}); f.units.push_back({2.0f, +1});
    auto out = figure_transforms::set_last_pulse(f, 0.5f);
    EXPECT_EQ(out.units.size(), 2u, "size");
    EXPECT_NEAR(out.units[0].duration, 1.0f, 1e-5f, "dur 0");
    EXPECT_NEAR(out.units[1].duration, 0.5f, 1e-5f, "dur 1");
    return 0;
}

int test_adjust_last_pulse() {
    MelodicFigure f; f.units.push_back({1.0f, 0}); f.units.push_back({2.0f, +1});
    auto out = figure_transforms::adjust_last_pulse(f, -0.5f);
    EXPECT_NEAR(out.units[1].duration, 1.5f, 1e-5f, "dur 1 adjusted");
    // Negative-clamp check
    auto clamped = figure_transforms::adjust_last_pulse(f, -10.0f);
    EXPECT_NEAR(clamped.units[1].duration, 0.0f, 1e-5f, "clamped at 0");
    return 0;
}

int test_stretch() {
    auto out = figure_transforms::stretch(fig4(), 2.0f);
    EXPECT_EQ(out.units.size(), 4u, "size");
    for (size_t i = 0; i < out.units.size(); ++i)
        EXPECT_NEAR(out.units[i].duration, 2.0f, 1e-5f, "stretched dur");
    return 0;
}

int test_compress() {
    auto out = figure_transforms::compress(fig4(), 4.0f);
    for (size_t i = 0; i < out.units.size(); ++i)
        EXPECT_NEAR(out.units[i].duration, 0.25f, 1e-5f, "compressed dur");
    return 0;
}
```

- [ ] **Step 2: Wire the tests into `run_unit_tests()`.**

Replace:

```cpp
int run_unit_tests() {
    // Populated by subsequent tasks.
    return 0;
}
```

with:

```cpp
int run_unit_tests() {
    RUN_TEST(test_invert);
    RUN_TEST(test_retrograde_steps);
    RUN_TEST(test_prune_end);
    RUN_TEST(test_prune_start);
    RUN_TEST(test_set_last_pulse);
    RUN_TEST(test_adjust_last_pulse);
    RUN_TEST(test_stretch);
    RUN_TEST(test_compress);
    return 0;
}
```

- [ ] **Step 3: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: 8 PASS + smoke test PASS = `9 passed, 0 failed`, exit 0.

- [ ] **Step 4: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): unit tests for elementary transforms"
```

---

## Task 3: Unit tests — combine, replicate, replicate_and_prune

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Insert the test block below the Task 2 block.**

Add after `test_compress`:

```cpp
// --- combine / replicate family ---

int test_combine_basic() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    FigureConnector fc; fc.leadStep = +2;
    auto out = figure_transforms::combine(a, b, fc);
    EXPECT_EQ(out.units.size(), 4u, "size");
    EXPECT_EQ(out.units[0].step,  0, "a[0]");
    EXPECT_EQ(out.units[1].step, +1, "a[1]");
    EXPECT_EQ(out.units[2].step, +2, "b[0] leadStep applied");
    EXPECT_EQ(out.units[3].step, -1, "b[1] preserved");
    return 0;
}

int test_combine_with_elide_and_adjust() {
    MelodicFigure a;
    a.units.push_back({1.0f, 0});
    a.units.push_back({1.0f, +1});
    a.units.push_back({1.0f, +1});  // will be elided
    MelodicFigure b;
    b.units.push_back({1.0f, 0});
    b.units.push_back({1.0f, -1});
    FigureConnector fc;
    fc.elideCount = 1;         // drop last unit of a
    fc.adjustCount = 0.5f;     // extend a's new last unit
    fc.leadStep = +2;
    auto out = figure_transforms::combine(a, b, fc);
    EXPECT_EQ(out.units.size(), 4u, "size after elide");
    EXPECT_NEAR(out.units[1].duration, 1.5f, 1e-5f, "adjusted last of a");
    EXPECT_EQ(out.units[2].step, +2, "leadStep");
    return 0;
}

int test_combine_sugar() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    // elide=true elides 1 trailing unit of a; leadStep=-3 on b[0].
    auto out = figure_transforms::combine(a, b, /*leadStep=*/-3, /*elide=*/true);
    EXPECT_EQ(out.units.size(), 3u, "size: 2 (a minus one elided) + 2 (b)? -- 1 + 2");
    // a loses 1 -> 1 unit; b has 2 units -> total 3
    EXPECT_EQ(out.units[1].step, -3, "b[0] leadStep = -3");
    EXPECT_EQ(out.units[2].step, -1, "b[1] preserved");
    return 0;
}

int test_replicate_repeats() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate(f, /*repeats=*/3, /*leadStep=*/+2, /*elide=*/false);
    EXPECT_EQ(out.units.size(), 9u, "3 × 3 units");
    EXPECT_EQ(out.units[3].step, +2, "copy 2 starts at leadStep");
    EXPECT_EQ(out.units[6].step, +2, "copy 3 starts at leadStep");
    return 0;
}

int test_replicate_connectors() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate(f, std::vector<int>{+2, -2});
    EXPECT_EQ(out.units.size(), 9u, "1 + 2 copies");
    EXPECT_EQ(out.units[3].step, +2, "first connector");
    EXPECT_EQ(out.units[6].step, -2, "second connector");
    return 0;
}

int test_replicate_and_prune() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    // 3 copies total, prune last unit of copy 2.
    auto out = figure_transforms::replicate_and_prune(
        f, std::vector<int>{+2, -2}, /*pruneAt1=*/2);
    // 3 copies × 3 units = 9, minus 1 pruned = 8.
    EXPECT_EQ(out.units.size(), 8u, "pruned 1");
    return 0;
}
```

- [ ] **Step 2: Wire into `run_unit_tests()`.**

Append after `RUN_TEST(test_compress);`:

```cpp
    RUN_TEST(test_combine_basic);
    RUN_TEST(test_combine_with_elide_and_adjust);
    RUN_TEST(test_combine_sugar);
    RUN_TEST(test_replicate_repeats);
    RUN_TEST(test_replicate_connectors);
    RUN_TEST(test_replicate_and_prune);
```

- [ ] **Step 3: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: 15 unit passes + 1 smoke integration pass = `15 passed, 0 failed` on stderr (exit 0).

- [ ] **Step 4: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): unit tests for combine and replicate family"
```

---

## Task 4: Unit tests — split, add_neighbor, add_turn

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Insert the test block below Task 3's block.**

Add after `test_replicate_and_prune`:

```cpp
// --- decorators: split, add_neighbor, add_turn ---

int test_split() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({2.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::split(f, /*splitAt=*/1, /*repeats=*/2);
    EXPECT_EQ(out.units.size(), 4u, "one extra unit");
    EXPECT_NEAR(out.units[1].duration, 1.0f, 1e-5f, "half dur 1a");
    EXPECT_EQ(out.units[1].step, +1, "first sub inherits step");
    EXPECT_NEAR(out.units[2].duration, 1.0f, 1e-5f, "half dur 1b");
    EXPECT_EQ(out.units[2].step, 0, "subsequent sub has step 0");
    return 0;
}

int test_add_neighbor_up() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    auto out = figure_transforms::add_neighbor(f, /*addAt=*/1, /*down=*/false);
    EXPECT_EQ(out.units.size(), 4u, "+2 units at addAt");
    EXPECT_NEAR(out.units[1].duration, 0.5f,  1e-5f, "half dur");
    EXPECT_EQ(out.units[1].step, +1, "main");
    EXPECT_NEAR(out.units[2].duration, 0.25f, 1e-5f, "quarter dur");
    EXPECT_EQ(out.units[2].step, +1, "upper neighbor");
    EXPECT_EQ(out.units[3].step, -1, "return");
    return 0;
}

int test_add_turn_up() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    auto out = figure_transforms::add_turn(f, /*addAt=*/1, /*down=*/false);
    EXPECT_EQ(out.units.size(), 5u, "+3 units at addAt");
    // down=false -> sequence: main, +1, -2, +1
    EXPECT_EQ(out.units[1].step, +1, "main inherits step");
    EXPECT_EQ(out.units[2].step, +1, "upper neighbor");
    EXPECT_EQ(out.units[3].step, -2, "cross to lower");
    EXPECT_EQ(out.units[4].step, +1, "return to main");
    for (int i = 1; i <= 4; ++i)
        EXPECT_NEAR(out.units[i].duration, 0.25f, 1e-5f, "quarter dur");
    return 0;
}
```

- [ ] **Step 2: Wire into `run_unit_tests()`.**

Append:

```cpp
    RUN_TEST(test_split);
    RUN_TEST(test_add_neighbor_up);
    RUN_TEST(test_add_turn_up);
```

- [ ] **Step 3: Build and run; confirm 18 pass, 0 fail.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```

- [ ] **Step 4: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): unit tests for decorators (split/neighbor/turn)"
```

---

## Task 5: Unit tests — randomized transforms

**Goal:** Cover `vary_rhythm`, `vary_steps`, `vary`, `complexify`, `embellish`. Seeded `Randomizer` for determinism; assertions focus on structural invariants (unit count, net step, total duration) rather than exact step values — randomized output changes across Randomizer impl changes, but invariants hold.

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Include `Randomizer`.**

At the top of `main.cpp`, add:

```cpp
#include "mforce/core/randomizer.h"
```

- [ ] **Step 2: Insert the test block.**

Add after `test_add_turn_up`:

```cpp
// --- randomized transforms (seeded) ---

float total_duration(const MelodicFigure& f) {
    float t = 0; for (auto& u : f.units) t += u.duration; return t;
}

int net_step(const MelodicFigure& f) {
    int n = 0; for (auto& u : f.units) n += u.step; return n;
}

int test_vary_rhythm_preserves_length() {
    MelodicFigure f;
    f.units.push_back({2.0f, 0});
    f.units.push_back({2.0f, +1});
    f.units.push_back({2.0f, -1});
    Randomizer rng(0x1234u);
    auto out = figure_transforms::vary_rhythm(f, rng);
    EXPECT_NEAR(total_duration(out), total_duration(f), 1e-4f, "total duration preserved");
    return 0;
}

int test_vary_steps_changes_interior() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0x5678u);
    auto out = figure_transforms::vary_steps(f, rng, /*variations=*/1);
    EXPECT_EQ(out.units.size(), f.units.size(), "size preserved");
    // First and last units may not be touched (vary_steps perturbs interior indices 1..n-2).
    EXPECT_EQ(out.units.front().step, f.units.front().step, "first step untouched");
    EXPECT_EQ(out.units.back().step,  f.units.back().step,  "last step untouched");
    // At least one interior unit should differ — repeat with varied seeds if this is flaky.
    bool anyDiff = false;
    for (int i = 1; i + 1 < (int)f.units.size(); ++i)
        if (out.units[i].step != f.units[i].step) { anyDiff = true; break; }
    if (!anyDiff) { std::cerr << "  FAIL: no interior step changed\n"; return 1; }
    return 0;
}

int test_vary_composite() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xABCDu);
    auto out = figure_transforms::vary(f, rng, /*amount=*/1.0f);
    // amount=1 applies both sub-perturbations with p=1.
    EXPECT_NEAR(total_duration(out), total_duration(f), 1e-4f, "total duration preserved");
    return 0;
}

int test_complexify_grows_unit_count() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xDEADu);
    auto out = figure_transforms::complexify(f, rng, /*amount=*/1.0f);
    // amount=1 targets doubled unit count; tolerate +/- a few due to safety cap.
    if ((int)out.units.size() < (int)f.units.size() + 1) {
        std::cerr << "  FAIL: complexify did not grow units (size=" << out.units.size() << ")\n";
        return 1;
    }
    EXPECT_NEAR(total_duration(out), total_duration(f), 1e-3f, "total duration preserved");
    return 0;
}

int test_embellish_marks_articulation() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xBEEFu);
    auto out = figure_transforms::embellish(f, rng, /*count=*/2);
    EXPECT_EQ(out.units.size(), f.units.size(), "size preserved");
    int marcatoCount = 0;
    for (auto& u : out.units) {
        if (std::holds_alternative<articulations::Marcato>(u.articulation)) ++marcatoCount;
    }
    EXPECT_EQ(marcatoCount, 2, "expected 2 marcato marks");
    return 0;
}
```

- [ ] **Step 3: Wire into `run_unit_tests()`.**

Append:

```cpp
    RUN_TEST(test_vary_rhythm_preserves_length);
    RUN_TEST(test_vary_steps_changes_interior);
    RUN_TEST(test_vary_composite);
    RUN_TEST(test_complexify_grows_unit_count);
    RUN_TEST(test_embellish_marks_articulation);
```

- [ ] **Step 4: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: 23 pass, 0 fail.

Note: if `test_vary_steps_changes_interior` is flaky (a specific seed happens to leave all interior steps unchanged), change the seed to one that empirically changes at least one. Do not relax the assertion — the test is checking that `vary_steps` actually varies.

- [ ] **Step 5: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): unit tests for randomized transforms"
```

---

## Task 6: Integration tests — deterministic transform round-trips

**Goal:** For each deterministic transform, build the transformed figure in test code, push it through the Composer via WrapperPhraseStrategy, and assert the realized figure matches the transform's direct output. A helper factorises the Composer-run and extraction boilerplate.

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Add the integration helper above `test_smoke_round_trip`.**

```cpp
// ----------------------------------------------------------------------------
// Integration helper: run `figure` through the Composer via WrapperPhraseStrategy
// and return the realized MelodicFigure (or nullptr + an error message on stderr).
// ----------------------------------------------------------------------------
struct ComposeResult {
    bool ok;
    MelodicFigure fig;
};

ComposeResult compose_locked(const MelodicFigure& figure) {
    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xABCDu;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 32.0f;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = figure;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    if (piece.parts.size() != 1) return {false, {}};
    auto it = piece.parts[0].passages.find("Main");
    if (it == piece.parts[0].passages.end()) return {false, {}};
    if (it->second.phrases.size() != 1) return {false, {}};
    const Phrase& ph = it->second.phrases[0];
    if (ph.figures.size() != 1) return {false, {}};
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    if (!fig) return {false, {}};
    return {true, *fig};
}

int expect_figures_equal(const MelodicFigure& a, const MelodicFigure& b, const char* tag) {
    if (a.units.size() != b.units.size()) {
        std::cerr << "  FAIL: " << tag << " unit count " << a.units.size()
                  << " vs " << b.units.size() << "\n";
        return 1;
    }
    for (size_t i = 0; i < a.units.size(); ++i) {
        if (a.units[i].step != b.units[i].step) {
            std::cerr << "  FAIL: " << tag << " step[" << i << "] " << a.units[i].step
                      << " vs " << b.units[i].step << "\n";
            return 1;
        }
        if (std::fabs(a.units[i].duration - b.units[i].duration) > 1e-5f) {
            std::cerr << "  FAIL: " << tag << " dur[" << i << "] " << a.units[i].duration
                      << " vs " << b.units[i].duration << "\n";
            return 1;
        }
    }
    return 0;
}
```

- [ ] **Step 2: Add integration tests covering each deterministic transform.**

Add after the helper, above `run_integration_tests`:

```cpp
int integ_invert() {
    MelodicFigure base = fig4();
    MelodicFigure expected = figure_transforms::invert(base);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "invert");
}

int integ_retrograde() {
    MelodicFigure expected = figure_transforms::retrograde_steps(fig4());
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "retrograde");
}

int integ_combine() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    FigureConnector fc; fc.leadStep = +2;
    MelodicFigure expected = figure_transforms::combine(a, b, fc);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "combine");
}

int integ_replicate() {
    MelodicFigure expected = figure_transforms::replicate(
        fig4(), std::vector<int>{+2, -2});
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "replicate");
}

int integ_split() {
    MelodicFigure expected = figure_transforms::split(fig4(), /*splitAt=*/1, /*repeats=*/2);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "split");
}

int integ_add_neighbor() {
    MelodicFigure expected = figure_transforms::add_neighbor(fig4(), /*addAt=*/1);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "add_neighbor");
}

int integ_add_turn() {
    MelodicFigure expected = figure_transforms::add_turn(fig4(), /*addAt=*/1);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "add_turn");
}

int integ_stretch() {
    MelodicFigure expected = figure_transforms::stretch(fig4(), 2.0f);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "stretch");
}
```

- [ ] **Step 3: Wire into `run_integration_tests()`.**

Replace the existing body:

```cpp
int run_integration_tests() {
    RUN_TEST(test_smoke_round_trip);
    RUN_TEST(integ_invert);
    RUN_TEST(integ_retrograde);
    RUN_TEST(integ_combine);
    RUN_TEST(integ_replicate);
    RUN_TEST(integ_split);
    RUN_TEST(integ_add_neighbor);
    RUN_TEST(integ_add_turn);
    RUN_TEST(integ_stretch);
    return 0;
}
```

- [ ] **Step 4: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: 23 unit + 9 integration = `32 passed, 0 failed`.

- [ ] **Step 5: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): integration round-trips for deterministic transforms"
```

---

## Task 7: Pitch-realization integration test

**Goal:** Assert that after composing + the Conductor walk, `Part::elementSequence` carries Notes with the expected MIDI `noteNumber`s derived from `startingPitch` + the figure's step sequence on the C-Major scale.

**Oracle (D7 default):** Hand-coded MIDI numbers for base figure `[0, +1, -1, 0]` starting at C4 in C major:
- Step 0 (C4) → MIDI 60
- Step +1 (D4) → MIDI 62
- Step -1 (C4) → MIDI 60
- Step 0 (C4) → MIDI 60

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Add `Conductor` include + test function.**

Add near the other includes at the top of `main.cpp`:

```cpp
#include "mforce/music/conductor.h"
```

Add the test function after `integ_stretch`:

```cpp
int integ_pitch_realization() {
    MelodicFigure base;
    base.units.push_back({1.0f,  0});
    base.units.push_back({1.0f, +1});
    base.units.push_back({1.0f, -1});
    base.units.push_back({1.0f,  0});

    // Build the same template compose_locked() uses — we need the Piece for
    // the Conductor walk, not just the realized MelodicFigure.
    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xABCDu;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 8.0f;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = base;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    // After compose(), Composer owns the elementSequence (per the 2026-04-22
    // Composer-owns-events refactor). Walk it and assert on Note MIDI numbers.
    const auto& es = piece.parts[0].elementSequence;
    int noteCount = 0;
    const int expected[] = {60, 62, 60, 60};
    for (size_t i = 0; i < es.elements.size(); ++i) {
        const Element& e = es.elements[i];
        if (std::holds_alternative<Note>(e.content)) {
            const Note& n = std::get<Note>(e.content);
            if (noteCount >= 4) {
                std::cerr << "  FAIL: more than 4 notes\n"; return 1;
            }
            int midi = int(std::round(n.noteNumber));
            if (midi != expected[noteCount]) {
                std::cerr << "  FAIL: note " << noteCount << " MIDI " << midi
                          << " expected " << expected[noteCount] << "\n";
                return 1;
            }
            ++noteCount;
        }
    }
    EXPECT_EQ(noteCount, 4, "4 notes realized");
    return 0;
}
```

- [ ] **Step 2: Wire into `run_integration_tests()`.**

Append:

```cpp
    RUN_TEST(integ_pitch_realization);
```

- [ ] **Step 3: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: `33 passed, 0 failed`.

**If the elementSequence shape doesn't match** (e.g. field name is different, or Notes carry a pitch object instead of a float noteNumber), inspect `engine/include/mforce/music/structure.h` — the `Element`, `Note`, and `ElementSequence` definitions are authoritative. Adjust the walk to match. Do not relax the assertion.

- [ ] **Step 4: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): pitch-realization integration test"
```

---

## Task 8: Listening harness — `--render` subcommand

**Goal:** `test_figures --render <patch.json> <out_dir> [seed]` builds a four-phrase passage (base / invert / retrograde / retrograde-invert), composes, performs via Conductor, and writes `<out_dir>/fig.wav` plus `<out_dir>/fig.json`.

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Add render-path includes at the top.**

Append to the include block:

```cpp
#include "mforce/render/patch_loader.h"
#include "mforce/render/wav_writer.h"
#include "mforce/music/music_json.h"

#include <filesystem>
#include <fstream>
```

- [ ] **Step 2: Replace the stub `run_render`.**

Replace the existing `run_render(...)` stub with:

```cpp
int run_render(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: test_figures --render <patch.json> <out_dir> [seed]\n"
                  << "  Produces fig.wav + fig.json demonstrating invert /\n"
                  << "  retrograde / retrograde-invert of a hand-authored base.\n";
        return 1;
    }
    std::string patchPath = argv[2];
    std::string outDir    = argv[3];
    uint32_t    seed      = (argc > 4) ? uint32_t(std::stoul(argv[4])) : 0xF16EF16Eu;
    (void)seed;  // hand-authored base doesn't depend on seed; kept for future use

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch not found: " << patchPath << "\n"; return 1;
    }
    std::filesystem::create_directories(outDir);

    // Base figure: steps [0, +2, -1, +1], 1-beat pulses. Hand-authored so the
    // listening comparison isn't influenced by RFB choices.
    MelodicFigure base;
    base.units.push_back({1.0f,  0});
    base.units.push_back({1.0f, +2});
    base.units.push_back({1.0f, -1});
    base.units.push_back({1.0f, +1});

    MelodicFigure inv = figure_transforms::invert(base);
    MelodicFigure ret = figure_transforms::retrograde_steps(base);
    MelodicFigure ri  = figure_transforms::retrograde_steps(inv);

    auto make_phrase = [](const char* name, const MelodicFigure& fig) {
        PhraseTemplate ph;
        ph.name = name;
        ph.strategy = "wrapper_phrase";
        ph.startingPitch = Pitch::from_name("C", 4);
        FigureTemplate ft;
        ft.source = FigureSource::Locked;
        ft.lockedFigure = fig;
        ph.figures.push_back(ft);
        return ph;
    };

    auto make_rest_phrase = [](float beats, int idx) {
        PhraseTemplate ph;
        ph.name = "rest_" + std::to_string(idx);
        FigureTemplate ft;
        ft.source = FigureSource::Literal;
        FigureTemplate::LiteralNote ln; ln.rest = true; ln.duration = beats;
        ft.literalNotes.push_back(ln);
        ph.figures.push_back(ft);
        return ph;
    };

    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = seed;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    // Four phrases of 4 beats each + three 1-beat rests between + 1 tail beat.
    sec.beats = 4 * 4 + 3 * 1 + 1;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;
    part.instrumentPatch = patchPath;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);
    passage.phrases.push_back(make_phrase("base",   base));
    passage.phrases.push_back(make_rest_phrase(1.0f, 0));
    passage.phrases.push_back(make_phrase("invert", inv));
    passage.phrases.push_back(make_rest_phrase(1.0f, 1));
    passage.phrases.push_back(make_phrase("retrograde", ret));
    passage.phrases.push_back(make_rest_phrase(1.0f, 2));
    passage.phrases.push_back(make_phrase("retro_invert", ri));

    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    auto ip = load_instrument_patch(patchPath);
    ip.instrument->volume = 0.5f;
    ip.instrument->hiBoost = 0.3f;

    Conductor conductor;
    for (const auto& p : piece.parts) {
        conductor.instruments[p.instrumentType] = ip.instrument.get();
    }
    conductor.perform(piece);

    float totalBeats = 0.0f;
    for (auto& s : piece.sections) totalBeats += s.beats;
    float bpm = piece.sections[0].tempo;
    float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    { RenderContext ctx{ip.sampleRate}; ip.instrument->render(ctx, mono.data(), frames); }
    std::vector<float> stereo(frames * 2);
    for (int j = 0; j < frames; ++j) { stereo[j*2] = mono[j]; stereo[j*2+1] = mono[j]; }

    std::string wavPath  = outDir + "/fig.wav";
    std::string jsonPath = outDir + "/fig.json";
    if (!write_wav_16le_stereo(wavPath, ip.sampleRate, stereo)) {
        std::cerr << "Failed to write " << wavPath << "\n"; return 1;
    }
    {
        nlohmann::json pj = piece;
        std::ofstream jf(jsonPath);
        jf << pj.dump(2);
    }

    float peak = 0.0f; double rms = 0.0;
    for (auto s : stereo) { float a = std::fabs(s); if (a > peak) peak = a; rms += double(s)*s; }
    rms = std::sqrt(rms / stereo.size());
    std::cout << "render: base=" << base.units.size() << "u  inv=" << inv.units.size()
              << "u  retro=" << ret.units.size() << "u  ri=" << ri.units.size()
              << "u  peak=" << peak << "  rms=" << rms << "\n"
              << "  " << wavPath << "\n  " << jsonPath << "\n";
    return 0;
}
```

- [ ] **Step 3: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
mkdir -p renders/fig_demo
./build/tools/test_figures/Debug/test_figures.exe --render patches/PluckU.json renders/fig_demo
```
Expected:
- stdout: `render: base=4u inv=4u retro=4u ri=4u peak=<nonzero> rms=<nonzero>` + the two paths.
- Files `renders/fig_demo/fig.wav` and `renders/fig_demo/fig.json` exist.

- [ ] **Step 4: Sanity-listen once, confirm the four variants are audibly present.**

Play `renders/fig_demo/fig.wav`. Goal of this step is a quick ear check, not full acceptance — we're verifying the harness, not evaluating musicality.

- [ ] **Step 5: Commit the source change.**

```bash
git add tools/test_figures/main.cpp
git commit -m "feat(test_figures): --render subcommand for fig/inv/retro/ri listening"
```

- [ ] **Step 6: Commit the JSON golden.**

Per D3: `fig.json` is committed as a regression golden (audio WAV is not).

```bash
cp renders/fig_demo/fig.json renders/golden_fig_inv_retro_ri.json
git add renders/golden_fig_inv_retro_ri.json
git commit -m "chore(renders): add golden for --render fig/inv/retro/ri"
```

---

## Task 9: Retire `--test-rfb` and `--test-replicate` from mforce_cli

**Goal:** Delete the two subcommands and their supporting code now that `test_figures` covers the same ground.

**Files:**
- Modify: `tools/mforce_cli/main.cpp`

- [ ] **Step 1: Remove the two dispatch lines in `main()`.**

Find and delete:

```cpp
        if (argc >= 2 && std::string(argv[1]) == "--test-rfb")
            return run_test_rfb(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "--test-replicate")
            return run_test_replicate(argc, argv);
```

- [ ] **Step 2: Delete the supporting code blocks.**

Remove:
- `static int run_test_rfb(int argc, char** argv) { ... }` and its entire body.
- `static int run_test_replicate(int argc, char** argv) { ... }` and its entire body.
- The anonymous `namespace { ... }` block containing `make_figure_phrase`, `make_rest_phrase`, `make_rfb_test_piece`, the `figs_*` factory functions, the `ReplicateCase` struct, `make_replicate_base`, and `replicate_cases`. These were introduced in commit `2c9a11e feat(cli): add --test-rfb subcommand` and exist solely for the two subcommands.

Verify by grep-checking afterwards that no remaining `mforce_cli/main.cpp` reference survives for these symbols — if anything still uses `make_figure_phrase` etc. outside the deleted block, stop and reconsider the delete scope.

- [ ] **Step 3: Build `mforce_cli`.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_cli
```
Expected: clean build (pre-existing warnings only).

- [ ] **Step 4: Verify the remaining subcommands still list/dispatch.**

```
./build/tools/mforce_cli/Debug/mforce_cli.exe 2>&1 | head -5
```
Expected: some usage output, no segfault. Then spot-check an existing subcommand still works:

```
./build/tools/mforce_cli/Debug/mforce_cli.exe --chords patches/PluckU.json /tmp/chord_sanity.wav 120 4 C:M F:M G:7 C:M
```
Expected: WAV written, exit 0.

- [ ] **Step 5: Commit.**

```bash
git add tools/mforce_cli/main.cpp
git commit -m "refactor(cli): drop --test-rfb and --test-replicate"
```

---

## Task 10: Delete the RFB goldens

**Goal:** Per D1 — the six `renders/rfb_*.json` goldens committed for `--test-rfb` have no driver anymore; delete them. History preserves the files if ever needed.

**Files:**
- Delete: `renders/rfb_build.json`, `renders/rfb_build_by_count.json`, `renders/rfb_build_by_length.json`, `renders/rfb_build_by_rhythm.json`, `renders/rfb_build_by_steps.json`, `renders/rfb_build_singleton.json`

- [ ] **Step 1: Remove the files via git.**

```bash
git rm renders/rfb_build.json \
       renders/rfb_build_by_count.json \
       renders/rfb_build_by_length.json \
       renders/rfb_build_by_rhythm.json \
       renders/rfb_build_by_steps.json \
       renders/rfb_build_singleton.json
```

- [ ] **Step 2: Commit.**

```bash
git commit -m "chore(renders): drop RFB goldens (tied to removed --test-rfb)"
```

---

## Task 11: Close the loop

**Goal:** Mark step 2 complete in `docs/ComposerRefactor3.md`, final build + test verification, report out.

**Files:**
- Modify: `docs/ComposerRefactor3.md`

- [ ] **Step 1: Update the step 2 entry.**

Find in `docs/ComposerRefactor3.md`:

```
2. Exhaustively test figures
   - remove the CLI hacks put in yesterday
   - build Pieces with a single Passage containing a single Phrase containing a single Figure
  [table]
  For transforms uses assert() in a tools/test_figures/ binary and upgrade later if needed.
```

Append a completion note below the "For transforms..." line:

```
  DONE 2026-04-24: test_figures binary now covers figure_transforms (unit +
  integration) and the fig/invert/retrograde/retro-invert listening exercise
  via `test_figures --render`. --test-rfb / --test-replicate retired.
  (spec: docs/superpowers/specs/2026-04-24-figure-testing-harness-design.md)
  (plan: docs/superpowers/plans/2026-04-24-figure-testing-harness.md)
```

- [ ] **Step 2: Final full build + test run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: all tests pass, exit 0.

Spot-check that `mforce_cli` still builds:

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_cli
```
Expected: clean build.

- [ ] **Step 3: Commit the doc update.**

```bash
git add docs/ComposerRefactor3.md docs/superpowers/specs/2026-04-24-figure-testing-harness-design.md docs/superpowers/plans/2026-04-24-figure-testing-harness.md
git commit -m "docs: ComposerRefactor3 step 2 done — figure testing harness"
```

---

## Self-review

**Spec coverage:**
- Stage 1 (unit tests): Tasks 2–5 cover every transform listed in spec D4.
- Stage 2 (integration tests): Task 6 (round-trips) + Task 7 (pitch realization) cover the spec's two integration layers.
- Stage 3 (listening harness): Task 8 implements `--render` and commits the JSON golden per D3.
- Stage 4 (retire CLI hacks): Tasks 9 + 10.
- Success criterion "K467 render unchanged": no explicit task, but nothing touched modifies Composer dispatch, and Task 11's build check catches breakage. If a deeper check is warranted, rerun the K467 compose command used in step 1's Task 5.

**Placeholder scan:** None — every step has literal code and literal commands.

**Type consistency:**
- `PhraseTemplate::strategy`, `FigureSource::Locked`, `FigureTemplate::lockedFigure`, `PartTemplate::passages`, `PassageTemplate::phrases`, `Piece::parts`, `Part::passages`, `Part::elementSequence`, `Phrase::figures`, `Figure`, `MelodicFigure`, `FigureConnector::{leadStep, elideCount, adjustCount}`, `Note::noteNumber`, `Element::content` (std::variant) — all verified against `templates.h` / `structure.h` / `figures.h` during spec work.
- `Randomizer(uint32_t)` constructor matches existing usage in `composer.h` and `figure_transforms.h`.
- `load_instrument_patch`, `RenderContext`, `write_wav_16le_stereo`, `Conductor::perform`, `Conductor::instruments` — all match usage in `tools/mforce_cli/main.cpp`.

**Known fragility points:**
- Task 7 depends on the 2026-04-22 "Composer owns events" refactor having landed (`piece.parts[0].elementSequence` populated by `ClassicalComposer::compose`). If `compose` alone doesn't populate events and a `Conductor::perform` pass is needed to realize the element sequence, add that call before asserting — the task's step 3 diagnostic note covers this case.
- Task 8's section `beats` computation is approximate (4 phrases × 4 beats + 3 rests × 1 beat + 1 tail). If the passage overflows the section budget, composer may truncate. Bump `sec.beats` to 32 to give slack if needed.
- Task 5's `test_vary_steps_changes_interior` depends on the seed actually perturbing. If the specific Randomizer+seed combination happens not to mutate, swap the seed; the invariant (at least one interior step differs) must not be relaxed.

**Scope-check reminder:**
- Step 3 of ComposerRefactor3 (DefaultPhraseStrategy rewrite) is explicitly out of scope here. If multi-figure phrase testing becomes necessary mid-plan, carve a follow-up plan rather than expanding this one.
