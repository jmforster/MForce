# WrapperPhraseStrategy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a minimum-viable `PhraseStrategy` named `"wrapper_phrase"` that realizes a single `FigureTemplate` into a single-figure `Phrase`, plus a smoke-test binary that proves the strategy round-trips a known figure end-to-end through the Composer pipeline.

**Architecture:** New header-only strategy class `WrapperPhraseStrategy : public PhraseStrategy` with an inline `compose_phrase` body. Registered in the `Composer` constructor alongside `DefaultPhraseStrategy` / `PeriodPhraseStrategy` / `SentencePhraseStrategy`. Selected by `PhraseTemplate::strategy = "wrapper_phrase"`. Ignores connectors, cadence, and `MelodicFunction` — dispatches the first `FigureTemplate` through `default_figure->compose_figure` and wraps the result. RFB and `figure_transforms::*` are called at test-build time (not inside the strategy), which keeps step 1 truly trivial and leaves schema questions for step 5.

**Tech Stack:** C++20, header-only engine, CMake, MSVC on Windows. No external test framework — smoke test uses `<cassert>` + stderr + exit code.

**Spec:** `docs/superpowers/specs/2026-04-24-wrapper-phrase-strategy-design.md`.

---

## Pending decisions (from spec — resolved as defaults below)

This plan assumes the spec's default for each pending decision:

- **D1:** Wrapper does **not** honor `connectors[0].leadStep`. Callers bake positioning into `units[0].step` of the locked figure.
- **D2:** RFB / `figure_transforms::*` are called at test-build time. The strategy itself only dispatches through `compose_figure`.
- **D3:** Warn only on `figures.size() > 1`. Stay silent on other ignored fields.

If Matt overrides any of these before execution, the affected tasks are:
- D1 override → extend Task 2's body to copy `DefaultPhraseStrategy`'s `i=0` leadStep handling (lines 1257–1260 in `composer.h`).
- D2 override → out of scope for step 1; would require a new `WrapperPhraseConfig` in `templates.h` + corresponding `templates_json.h` round-trip. Would cascade to schema/K467 work and should move to a separate plan.
- D3 override → extend Task 2's body to check each ignored field and emit a warning if non-default.

---

## File Structure

- **Create:** `engine/include/mforce/music/wrapper_phrase_strategy.h` — class declaration + inline `compose_phrase` body.
- **Modify:** `engine/include/mforce/music/composer.h` — add include; register the strategy.
- **Create:** `tools/test_figures/CMakeLists.txt` — executable target.
- **Create:** `tools/test_figures/main.cpp` — smoke test.
- **Modify:** `CMakeLists.txt` (root) — `add_subdirectory(tools/test_figures)`.

---

## Task 1: Create the strategy header

**Files:**
- Create: `engine/include/mforce/music/wrapper_phrase_strategy.h`

- [ ] **Step 1: Write the header.**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// WrapperPhraseStrategy — minimum-viable phrase strategy.
//
// Realizes `phraseTmpl.figures[0]` into a single-figure Phrase by dispatching
// through the registered `default_figure` strategy. Ignores connectors,
// cadence, and MelodicFunction. Intended as the strategy-driven entry point
// for figure-level testing (see step 2 of ComposerRefactor3).
// ---------------------------------------------------------------------------
class WrapperPhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "wrapper_phrase"; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase WrapperPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  if (phraseTmpl.figures.empty()) {
    std::cerr << "WrapperPhraseStrategy: no figures in template\n";
    return phrase;
  }
  if (phraseTmpl.figures.size() > 1) {
    std::cerr << "WrapperPhraseStrategy: ignoring figures beyond index 0 ("
              << phraseTmpl.figures.size() << " provided)\n";
  }

  FigureStrategy* fs = StrategyRegistry::instance().resolve_figure("default_figure");
  if (!fs) {
    std::cerr << "WrapperPhraseStrategy: default_figure strategy not registered\n";
    return phrase;
  }

  Locus figLocus = locus.with_figure(0);
  MelodicFigure fig = fs->compose_figure(figLocus, phraseTmpl.figures[0]);
  phrase.add_melodic_figure(std::move(fig));

  return phrase;
}

} // namespace mforce
```

- [ ] **Step 2: Verify the header compiles in isolation.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build. No file consumes this header yet, but it's in the include path so the engine translation unit parses it.

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/wrapper_phrase_strategy.h
git commit -m "feat(composer): add WrapperPhraseStrategy header"
```

---

## Task 2: Register the strategy in Composer

**Files:**
- Modify: `engine/include/mforce/music/composer.h`

- [ ] **Step 1: Add the include.**

Find the existing include block near the top of `engine/include/mforce/music/composer.h` (around the other music-header includes such as `phrase_strategies.h`). Add:

```cpp
#include "mforce/music/wrapper_phrase_strategy.h"
```

Place it immediately after `#include "mforce/music/phrase_strategies.h"` so the phrase-strategy headers stay grouped.

- [ ] **Step 2: Register the strategy in the constructor.**

Find the block in the `Composer` constructor where phrase strategies are registered (around `composer.h:130`):

```cpp
    reg.register_phrase(std::make_unique<PeriodPhraseStrategy>());
    reg.register_phrase(std::make_unique<SentencePhraseStrategy>());
```

Add one line immediately after the `SentencePhraseStrategy` registration:

```cpp
    reg.register_phrase(std::make_unique<WrapperPhraseStrategy>());
```

- [ ] **Step 3: Build and verify.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): register WrapperPhraseStrategy"
```

---

## Task 3: Create the smoke-test binary scaffold

**Files:**
- Create: `tools/test_figures/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root)

- [ ] **Step 1: Write `tools/test_figures/CMakeLists.txt`.**

```cmake
add_executable(test_figures main.cpp)
target_link_libraries(test_figures PRIVATE mforce_engine)

if (MSVC)
  target_compile_options(test_figures PRIVATE /W4 /permissive-)
endif()
```

- [ ] **Step 2: Add the subdirectory at the root.**

In the root `CMakeLists.txt`, find the block of `add_subdirectory(tools/...)` lines. Add:

```cmake
add_subdirectory(tools/test_figures)
```

Place it immediately after `add_subdirectory(tools/durn_converter)` (or wherever the existing `tools/*` entries end) to keep the tool subdirectories grouped.

- [ ] **Step 3: Write a placeholder `tools/test_figures/main.cpp`.**

The file must exist before CMake configures the target. Minimal stub:

```cpp
#include <iostream>

int main(int, char**) {
    std::cerr << "test_figures: no tests yet\n";
    return 0;
}
```

- [ ] **Step 4: Reconfigure and build.**

Run:
```bash
cmake -S . -B build
cmake --build build --config Debug --target test_figures
```
Expected: the new target configures and builds cleanly. On Windows/MSVC the binary ends up at `build/tools/test_figures/Debug/test_figures.exe`.

- [ ] **Step 5: Run the stub to confirm it executes.**

Run:
```bash
./build/tools/test_figures/Debug/test_figures.exe
echo "exit=$?"
```
Expected: prints `test_figures: no tests yet`, exits with status 0.

- [ ] **Step 6: Commit.**

```bash
git add tools/test_figures/CMakeLists.txt tools/test_figures/main.cpp CMakeLists.txt
git commit -m "build(tools): scaffold test_figures binary"
```

---

## Task 4: Write the smoke test

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Replace the stub with the smoke test.**

```cpp
#include "mforce/music/basics.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace mforce;

namespace {

// Build a deterministic base figure: steps [0, +1, -1, 0], uniform 1-beat pulse.
// Net step = 0, so the figure is contour-neutral — ideal for a round-trip test.
MelodicFigure make_smoke_figure() {
    MelodicFigure fig;
    fig.units.push_back({1.0f,  0});
    fig.units.push_back({1.0f, +1});
    fig.units.push_back({1.0f, -1});
    fig.units.push_back({1.0f,  0});
    return fig;
}

// Assemble a minimal PieceTemplate: one section, one part, one passage,
// one phrase driven by WrapperPhraseStrategy with a single locked figure.
PieceTemplate make_smoke_template(const MelodicFigure& fig) {
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
    part.instrumentPatch = "";  // smoke test does not render audio

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = fig;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    return tmpl;
}

int fail(const char* msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

} // namespace

int main(int, char**) {
    const MelodicFigure expected = make_smoke_figure();
    const PieceTemplate tmpl = make_smoke_template(expected);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    if (piece.parts.size() != 1) return fail("piece.parts.size() != 1");
    const auto& part = piece.parts[0];

    auto passageIt = part.passages.find("Main");
    if (passageIt == part.passages.end()) return fail("passages[\"Main\"] missing");
    const Passage& passage = passageIt->second;

    if (passage.phrases.size() != 1) return fail("passage.phrases.size() != 1");
    const Phrase& phrase = passage.phrases[0];

    if (phrase.figures.size() != 1) return fail("phrase.figures.size() != 1");
    const Figure* figBase = phrase.figures[0].get();
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(figBase);
    if (!fig) return fail("phrase.figures[0] is not a MelodicFigure");

    if (fig->units.size() != expected.units.size())
        return fail("unit count mismatch");

    for (size_t i = 0; i < expected.units.size(); ++i) {
        if (fig->units[i].step != expected.units[i].step) {
            std::cerr << "  step mismatch at i=" << i
                      << " expected=" << expected.units[i].step
                      << " got=" << fig->units[i].step << "\n";
            return fail("step mismatch");
        }
        if (std::fabs(fig->units[i].duration - expected.units[i].duration) > 1e-5f) {
            std::cerr << "  duration mismatch at i=" << i
                      << " expected=" << expected.units[i].duration
                      << " got=" << fig->units[i].duration << "\n";
            return fail("duration mismatch");
        }
    }

    std::cout << "OK: WrapperPhraseStrategy round-trip (" << fig->units.size()
              << " units)\n";
    return 0;
}
```

Note on the `part.passages` type: it is `std::unordered_map<std::string, PassageTemplate>` on the template side (`PartTemplate::passages`) but the realized `Part` on `Piece` uses the same keying — see `structure.h` / `classical_composer.h`. If the realized `Part.passages` is actually a different container, adjust the lookup accordingly; the smoke test is the moment to catch that mismatch.

- [ ] **Step 2: Build and verify.**

Run:
```bash
cmake --build build --config Debug --target test_figures
```
Expected: clean build. If the `part.passages` container type differs from what the test assumes (note above), fix the lookup to match the real container API before proceeding.

- [ ] **Step 3: Run the smoke test and verify success.**

Run:
```bash
./build/tools/test_figures/Debug/test_figures.exe
echo "exit=$?"
```
Expected:
- stdout: `OK: WrapperPhraseStrategy round-trip (4 units)`
- exit status: `0`

- [ ] **Step 4: Negative check — confirm the test catches a real regression.**

Manually perturb `units[1].step` in `make_smoke_figure()` to `+2` and rebuild:
```bash
cmake --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
echo "exit=$?"
```
Expected:
- stderr contains `step mismatch at i=1 expected=2 got=1` (or similar — the expected is now `+2`, the figure sent through the pipeline is still `+1` from the `lockedFigure` data path)
- exit status: non-zero

Wait — this negative test is wrong: both `expected` AND `lockedFigure` come from the same `make_smoke_figure()`, so perturbing the factory perturbs both sides in lockstep and the assertion still passes. Use a better negative check instead:

Temporarily change the Locked figure stuffed into the template to differ from `expected`. Concretely, edit `make_smoke_template` to set `ft.lockedFigure = fig; ft.lockedFigure->units[1].step = +5;` after the assignment. Rebuild, run, expect FAIL with a step mismatch at `i=1`. Then revert the line and rebuild, expect OK.

- [ ] **Step 5: Revert the negative-check perturbation.**

Confirm the file matches Step 1's content byte-for-byte. Re-run the smoke test; expect `OK` and exit 0.

- [ ] **Step 6: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): smoke-test WrapperPhraseStrategy round-trip"
```

---

## Task 5: Verify nothing else regressed

- [ ] **Step 1: Rebuild everything.**

Run:
```bash
cmake --build build --config Debug
```
Expected: all targets build clean.

- [ ] **Step 2: Run the K467 render to confirm existing behavior is unchanged.**

The K467 render is the existing golden smoke test for the Composer/Conductor pipeline. `WrapperPhraseStrategy` is additive — it shouldn't affect any existing path, but confirming the K467 render still matches its golden protects against accidentally perturbing default dispatch.

Run the existing `mforce_cli` compose command used to produce the K467 baseline (check `docs/` and recent commits for the exact invocation — see commit `a0ae45a chore(renders): re-baseline K467 goldens after FigureBuilder redesign` for the current expected output location). Compare against the golden.

**If no K467 command is readily recoverable:** at minimum run `./build/tools/mforce_cli/Debug/mforce_cli.exe --test-rfb <any patch> renders/rfb_sanity 42` and confirm it still produces the same six `rfb_*.wav` files it did before. This covers the dispatch path even though it doesn't use `WrapperPhraseStrategy`.

- [ ] **Step 3: No commit unless something had to be fixed.**

If this task surfaces a regression, stop and diagnose before proceeding. If it passes clean, move on.

---

## Task 6: Close the loop

- [ ] **Step 1: Update `docs/ComposerRefactor3.md` to check off step 1.**

Find the "Next steps" section. Add a short completion note under step 1:

```
1. Write a trival WrapperPhraseStrategy
   - wire in the new RandomFigureBuilder and FigureTransforms
   - simply makes a one-figure Phrase
   DONE 2026-04-24: wrapper_phrase strategy + smoke test
   (spec: docs/superpowers/specs/2026-04-24-wrapper-phrase-strategy-design.md)
   (plan: docs/superpowers/plans/2026-04-24-wrapper-phrase-strategy.md)
```

- [ ] **Step 2: Commit the docs update.**

```bash
git add docs/ComposerRefactor3.md
git commit -m "docs: mark WrapperPhraseStrategy step 1 complete"
```

---

## Self-review

**Spec coverage:**
- Strategy declaration + registration: Tasks 1 & 2.
- `"wrapper_phrase"` name selection: Task 1, verified via Task 4's template.
- `startingPitch` fallback behavior: exercised implicitly — smoke test sets both the passage and phrase startingPitch. A follow-up test that omits both and relies on `pitch_before` would be a step-2 addition.
- Warning on `figures.size() > 1`: not exercised in the smoke test. Could be added as a second test case in Task 4 but step 1 is minimum-viable; defer to step 2.
- Warnings suppressed on other ignored fields: satisfied by the strategy body (no emission paths for connectors/cadence/function).
- Smoke test proving the figure round-trips end-to-end: Task 4.

**Placeholder scan:** None — all code and commands are literal.

**Type consistency:**
- `PhraseStrategy::compose_phrase` signature matches `strategy.h`.
- `Phrase::add_melodic_figure`, `phrase.figures`, `phrase.startingPitch` match `structure.h`.
- `FigureTemplate::{source, lockedFigure}`, `PhraseTemplate::{name, strategy, startingPitch, figures}`, `PassageTemplate::{name, startingPitch, phrases}`, `PartTemplate::{name, role, instrumentPatch, passages}`, `PieceTemplate::{keyName, scaleName, bpm, masterSeed, sections, parts}` match `templates.h`.
- `Pitch::from_name(std::string, int)` is the expected factory — verify at build time; if the signature differs, adjust the smoke test call.

**Known fragility in Task 4:** the smoke test does a `dynamic_cast<const MelodicFigure*>` because `Phrase::figures` is `std::vector<std::unique_ptr<Figure>>`. If `Figure` doesn't have a virtual destructor or RTTI is disabled, this cast will fail at runtime. Engine headers appear to rely on RTTI elsewhere; if this assumption is wrong, switch to a type tag on `Figure` or expose a `MelodicFigure*` downcast helper. Document the fix inline if encountered.
