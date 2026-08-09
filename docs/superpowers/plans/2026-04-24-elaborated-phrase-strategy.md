# ElaboratedPhraseStrategy + FC Foundation Cleanup — Implementation Plan (Phase 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two pieces:
1. Foundation cleanup so figures are never mutated and FCs are dense on the runtime `Phrase` (extend `struct Phrase` with `std::vector<FigureConnector> connectors`; refactor `DefaultPhraseStrategy::compose_phrase` to populate that instead of mutating; teach `realize_phrase_to_events_` to honor it).
2. New `"elaborated_phrase"` PhraseStrategy that builds a skeleton (RFB or supplied), walks anchors, randomly chooses Leave or Generate per anchor, and populates `phrase.figures` + `phrase.connectors` per the leadStep math `FC[0]=0`, `FC[i>0] = skel.step[i] − E_{i-1}.net_step`.

**Architecture:** Foundation-first (Tasks 1-4) so the new strategy can be built on the clean substrate. K467 goldens preserved bit-identically (the math is equivalent: `step_note(cursor, leadStep) → step_note(cursor, 0)` walks the same path as the old `step_note(cursor, leadStep + 0)`). New strategy added as Tasks 5-9.

**Tech Stack:** C++20, header-only engine, nlohmann::json, MSVC on Windows. CMake at `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

**Spec:** `docs/superpowers/specs/2026-04-24-elaborated-phrase-strategy-design.md`.

**Predecessor:** Step 3 (TwoFigurePhraseStrategy) on `figure-builder-redesign` through `e602905`.

---

## Pending decisions (resolved defaults)

Plan executes against these defaults. Override before starting to redirect.

- **D1** — Strategy name: `"elaborated_phrase"`.
- **D2** — Move vocabulary: Leave / Generate, random 50/50 by default.
- **D3** — Skeleton config: literal `MelodicFigure` (optional) + inline RFB build fields.
- **D4** — `Phrase.connectors` type: `std::vector<FigureConnector>` (dense, no optional).
- **D5** — Backward compat: legacy templates with non-zero `phraseTmpl.connectors[0].leadStep` are honored at realize time (no migration in Phase 1).
- **D6** — Test determinism: add `ChoiceMode` enum to config (`Random` / `AllLeave` / `AllGenerate`). Tests force AllLeave / AllGenerate.

If Matt overrides:
- D1 / D2 → small edits in Tasks 5 and 7.
- D4 → `optional<>` wrapper changes serialization shape; revisit Tasks 1, 2.
- D5 → would require auditing patches and migrating; out-of-scope expansion.
- D6 → if no ChoiceMode, tests assert structural invariants only; remove from config.

---

## File Structure

- **Modify:** `engine/include/mforce/music/structure.h` — add `connectors` field to `struct Phrase` (Task 1).
- **Modify:** `engine/include/mforce/music/composer.h` — `DefaultPhraseStrategy::compose_phrase` populates `phrase.connectors` instead of mutating (Task 2); `realize_phrase_to_events_` advances cursor by FC.leadStep before walking figure (Task 3); register new strategy (Task 8).
- **Modify:** `engine/include/mforce/music/templates.h` — add `ElaboratedPhraseConfig` + optional field on `PhraseTemplate` (Task 5).
- **Modify:** `engine/include/mforce/music/templates_json.h` — JSON round-trip (Task 6).
- **Create:** `engine/include/mforce/music/elaborated_phrase_strategy.h` — strategy class (Task 7).
- **Modify:** `tools/test_figures/main.cpp` — integration tests (Task 9).
- **Modify:** `docs/ComposerRefactor3.md` — record done (Task 10).

---

## Task 1: Add `connectors` field to `struct Phrase`

**Files:**
- Modify: `engine/include/mforce/music/structure.h`

- [ ] **Step 1: Locate the `Phrase` struct.**

`engine/include/mforce/music/structure.h:124+`. The struct currently has `startingPitch`, `figures`, copy/move ctors, and `add_figure` / `add_melodic_figure`.

- [ ] **Step 2: Add the `connectors` field and a helper.**

Find the line `std::vector<std::unique_ptr<Figure>> figures;` (around line 126). Immediately AFTER it, add:

```cpp
  // Dense parallel vector — connectors.size() == figures.size() after compose.
  // connectors[0] is a dummy with leadStep=0 by convention; figure 0 is
  // positioned by phrase.startingPitch directly. connectors[i>0].leadStep is
  // the inter-figure cursor advance (applied at realize time, not by mutating
  // the figure).
  std::vector<FigureConnector> connectors;
```

Then find the existing `add_melodic_figure` helper (around line 145). Immediately AFTER it, add:

```cpp
  // Convenience: append a (figure, connector) pair atomically. Use this in
  // strategies that produce both, to keep figures.size() == connectors.size().
  void add_melodic_figure_with_connector(MelodicFigure fig, FigureConnector fc) {
    figures.push_back(std::make_unique<MelodicFigure>(std::move(fig)));
    connectors.push_back(fc);
  }
```

- [ ] **Step 3: Update copy ctor and copy-assignment.**

Find the existing copy ctor (around line 132): `Phrase(const Phrase& other) : startingPitch(other.startingPitch) { for (...) figures.push_back(f->clone()); }`. Update to also copy connectors:

```cpp
  Phrase(const Phrase& other)
    : startingPitch(other.startingPitch), connectors(other.connectors) {
    for (const auto& f : other.figures) figures.push_back(f->clone());
  }
```

Similarly find the copy-assignment (around line 135) and update its body to:

```cpp
  Phrase& operator=(const Phrase& other) {
    if (this != &other) {
      startingPitch = other.startingPitch;
      connectors = other.connectors;
      figures.clear();
      for (const auto& f : other.figures) figures.push_back(f->clone());
    }
    return *this;
  }
```

- [ ] **Step 4: Verify `FigureConnector` is reachable.**

`FigureConnector` is defined in `figures.h`. `structure.h` already includes `figures.h` (since `Figure` itself is there). If a build error says `FigureConnector` is undefined, add `#include "mforce/music/figures.h"` at the top of `structure.h` — but it should be transitively reachable already.

- [ ] **Step 5: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build. (No callers of the new field yet, so nothing breaks.)

- [ ] **Step 6: Commit.**

```bash
git add engine/include/mforce/music/structure.h
git commit -m "refactor(structure): add Phrase.connectors parallel vector"
```

---

## Task 2: Update `DefaultPhraseStrategy::compose_phrase` to populate `phrase.connectors`

**Files:**
- Modify: `engine/include/mforce/music/composer.h`

- [ ] **Step 1: Locate the strategy body.**

In `composer.h`, find `inline Phrase DefaultPhraseStrategy::compose_phrase(` (around line 1180).

- [ ] **Step 2: Replace the additive-mutation block.**

Find the block at composer.h:1264-1273:
```cpp
    // Apply FigureConnector.leadStep to the new figure's first unit.
    // Applies for ALL figures including i=0 — connectors[0].leadStep places
    // the first figure relative to the phrase's startingPitch. For
    // placement-neutral motifs (first step = 0), this is the sole placement
    // mechanism. The elide/adjust block above keeps its i>0 guard because
    // those operate on the PREVIOUS figure (which doesn't exist for i=0).
    if (i < int(phraseTmpl.connectors.size())
        && phraseTmpl.connectors[i] && !fig.units.empty()) {
      fig.units[0].step += phraseTmpl.connectors[i]->leadStep;
    }
```

Delete it. Replace with:
```cpp
    // Pull this figure's connector from the template (if any). Default-
    // constructed FCs have leadStep=0/elide=0/adjust=0. Push to phrase.connectors
    // alongside the figure to keep figures.size() == connectors.size().
    // The cursor advance from leadStep happens at realize time (see
    // realize_phrase_to_events_), NOT here. Figures are not mutated.
    FigureConnector fc{};
    if (i < int(phraseTmpl.connectors.size()) && phraseTmpl.connectors[i]) {
      fc = *phraseTmpl.connectors[i];
    }
```

- [ ] **Step 3: Update `runningReader.step` to account for leadStep separately.**

Find the line just below (composer.h:1277):
```cpp
    runningReader.step(fig.net_step());
```

Replace with:
```cpp
    // Advance running cursor by the FC's leadStep (used by Literal-figure
    // path's pitch_before queries) and then by the figure's net step.
    // Under the new model, fig.units[0].step is 0 by convention, so
    // fig.net_step() no longer absorbs leadStep — we add it explicitly.
    runningReader.step(fc.leadStep);
    runningReader.step(fig.net_step());
```

- [ ] **Step 4: Replace `phrase.add_melodic_figure` with the paired helper.**

Find the line at composer.h:1282:
```cpp
    phrase.add_melodic_figure(std::move(fig));
```

Replace with:
```cpp
    phrase.add_melodic_figure_with_connector(std::move(fig), fc);
```

- [ ] **Step 5: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 6: Commit.**

```bash
git add engine/include/mforce/music/composer.h
git commit -m "refactor(composer): DefaultPhraseStrategy populates phrase.connectors instead of mutating fig step[0]"
```

---

## Task 3: Update `realize_phrase_to_events_` to honor `phrase.connectors`

**Files:**
- Modify: `engine/include/mforce/music/composer.h`

- [ ] **Step 1: Locate the realize loop.**

`composer.h:373+` defines `realize_phrase_to_events_(...)`. The figure loop starts at composer.h:386: `for (int f = 0; f < phrase.figure_count(); ++f) {`.

- [ ] **Step 2: Add the leadStep advance before the per-unit loop.**

Inside the figure loop, BEFORE the `for (int i = 0; i < fig.note_count(); ++i)` line, add:

```cpp
      // NEW: advance cursor by FC.leadStep before walking figure units.
      // FC[0].leadStep is 0 by convention (figure 0 positioned by
      // phrase.startingPitch); FC[f>0].leadStep is the real inter-figure
      // cursor advance. Defensive size check covers Phrases composed before
      // the connectors field was wired (older test fixtures).
      if (f < int(phrase.connectors.size())) {
        if (isChordFig && section.chordProgression) {
          // Chord-figure path: leadStep advances scale-degree-style.
          // For consistency with the per-unit path below, use step_note
          // against the section scale.
          currentNN = step_note(currentNN, phrase.connectors[f].leadStep, scale);
        } else {
          currentNN = step_note(currentNN, phrase.connectors[f].leadStep, scale);
        }
      }
```

(The two branches are identical here; kept symmetric in case chord-figure handling diverges later.)

- [ ] **Step 3: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/composer.h
git commit -m "refactor(composer): realize_phrase_to_events_ honors phrase.connectors leadStep"
```

---

## Task 4: Verify K467 goldens unchanged

**Files:** read-only verification.

- [ ] **Step 1: Build everything.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_cli
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
```

- [ ] **Step 2: Run baseline tests.**

```
build\Debug\test_figures.exe
```
Expected: all tests pass with the same count as before Task 1.

- [ ] **Step 3: Render K467 and check hash.**

Locate the K467 golden render command and golden hash. Likely:
```
build\Debug\mforce_cli.exe --compose patches/k467_*.json --render renders/k467_check.wav
```
(The exact patch path depends on what's committed. Check `git ls-files patches/ | grep -i k467` to locate.)

Compute the hash and compare against the committed golden hash. If the render command isn't standardized, run the existing test that renders K467 and asserts on the hash (likely an `integ_*` test in `test_figures` or a CLI flag).

Expected: hash matches.

- [ ] **Step 4: If hash drifts, investigate.**

The arithmetic is equivalent (advance-then-walk == prebake-into-step), so drift would mean a missed branch (e.g. a place that reads `fig.units[0].step` expecting it to contain leadStep). Likely culprits to grep:
- `fig.units[0].step` reads outside the realize/walk path
- `apply_cadence` (composer.h around 1290) which mutates last unit
- ChordFigure walking code paths

Fix the missed read sites to use `phrase.connectors[f].leadStep` instead. Re-run.

- [ ] **Step 5: No commit — Task 4 is verification only.**

If Step 3 passed first try, no fixes needed. If fixes needed in Step 4, commit each fix as a separate `fix(composer): ...` commit before re-running.

---

## Task 5: Add `ElaboratedPhraseConfig` to templates.h

**Files:**
- Modify: `engine/include/mforce/music/templates.h`

- [ ] **Step 1: Add the config struct.**

In `templates.h`, find the `// TwoFigurePhraseConfig` block (around line 230). Immediately AFTER its closing `};`, add:

```cpp
// ===========================================================================
// ElaboratedPhraseConfig — RFB-built (or literal) skeleton + per-anchor
// random Leave/Generate elaboration. First concrete middle-tier strategy.
// ===========================================================================

struct ElaboratedPhraseConfig {
    // Optional literal skeleton. If absent, strategy builds via RFB.
    std::optional<MelodicFigure> skeleton;

    // RFB build spec (used when skeleton is absent).
    enum class Method { ByCount, ByLength, Singleton };
    Method buildMethod{Method::ByCount};
    int   buildCount{4};       // ByCount
    float buildLength{4.0f};   // ByLength
    Constraints buildConstraints;

    // Per-anchor choice mode. Random is the production default; AllLeave /
    // AllGenerate exist for testability and direct authoring.
    enum class ChoiceMode { Random, AllLeave, AllGenerate };
    ChoiceMode choiceMode{ChoiceMode::Random};

    // Constraints for RFB-generated elaboration figures (Generate path).
    Constraints generateConstraints;

    // 0 means "use phraseTmpl.seed, else a deterministic default".
    uint32_t seed{0};
};
```

- [ ] **Step 2: Add the optional field on `PhraseTemplate`.**

Find the existing line `std::optional<TwoFigurePhraseConfig> twoFigureConfig;` (around line 282). Immediately AFTER it, add:

```cpp
    std::optional<ElaboratedPhraseConfig> elaboratedConfig;
```

- [ ] **Step 3: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/templates.h
git commit -m "feat(templates): add ElaboratedPhraseConfig + optional field"
```

---

## Task 6: JSON round-trip for `ElaboratedPhraseConfig`

**Files:**
- Modify: `engine/include/mforce/music/templates_json.h`

- [ ] **Step 1: Add Method/ChoiceMode enum round-trips and config round-trip.**

In `templates_json.h`, locate the existing `to_json(json& j, const TwoFigurePhraseConfig& c)` block. Immediately AFTER its `from_json` counterpart, add:

```cpp
// ===========================================================================
// ElaboratedPhraseConfig
// ===========================================================================

inline void to_json(json& j, ElaboratedPhraseConfig::Method m) {
    switch (m) {
        case ElaboratedPhraseConfig::Method::ByCount:   j = "byCount";   break;
        case ElaboratedPhraseConfig::Method::ByLength:  j = "byLength";  break;
        case ElaboratedPhraseConfig::Method::Singleton: j = "singleton"; break;
    }
}

inline void from_json(const json& j, ElaboratedPhraseConfig::Method& m) {
    const std::string s = j.get<std::string>();
    if      (s == "byCount")   m = ElaboratedPhraseConfig::Method::ByCount;
    else if (s == "byLength")  m = ElaboratedPhraseConfig::Method::ByLength;
    else if (s == "singleton") m = ElaboratedPhraseConfig::Method::Singleton;
    else throw std::runtime_error("Unknown ElaboratedPhraseConfig::Method: " + s);
}

inline void to_json(json& j, ElaboratedPhraseConfig::ChoiceMode m) {
    switch (m) {
        case ElaboratedPhraseConfig::ChoiceMode::Random:       j = "random";       break;
        case ElaboratedPhraseConfig::ChoiceMode::AllLeave:     j = "allLeave";     break;
        case ElaboratedPhraseConfig::ChoiceMode::AllGenerate:  j = "allGenerate";  break;
    }
}

inline void from_json(const json& j, ElaboratedPhraseConfig::ChoiceMode& m) {
    const std::string s = j.get<std::string>();
    if      (s == "random")      m = ElaboratedPhraseConfig::ChoiceMode::Random;
    else if (s == "allLeave")    m = ElaboratedPhraseConfig::ChoiceMode::AllLeave;
    else if (s == "allGenerate") m = ElaboratedPhraseConfig::ChoiceMode::AllGenerate;
    else throw std::runtime_error("Unknown ElaboratedPhraseConfig::ChoiceMode: " + s);
}

inline void to_json(json& j, const ElaboratedPhraseConfig& c) {
    j = json::object();
    if (c.skeleton) j["skeleton"] = *c.skeleton;
    j["buildMethod"]      = c.buildMethod;
    j["buildCount"]       = c.buildCount;
    j["buildLength"]      = c.buildLength;
    j["buildConstraints"] = c.buildConstraints;
    j["choiceMode"]       = c.choiceMode;
    j["generateConstraints"] = c.generateConstraints;
    j["seed"]             = c.seed;
}

inline void from_json(const json& j, ElaboratedPhraseConfig& c) {
    if (j.contains("skeleton")) {
        MelodicFigure mf;
        from_json(j.at("skeleton"), mf);
        c.skeleton = std::move(mf);
    }
    if (j.contains("buildMethod"))         c.buildMethod         = j.at("buildMethod").get<ElaboratedPhraseConfig::Method>();
    if (j.contains("buildCount"))          c.buildCount          = j.at("buildCount").get<int>();
    if (j.contains("buildLength"))         c.buildLength         = j.at("buildLength").get<float>();
    if (j.contains("buildConstraints"))    from_json(j.at("buildConstraints"), c.buildConstraints);
    if (j.contains("choiceMode"))          c.choiceMode          = j.at("choiceMode").get<ElaboratedPhraseConfig::ChoiceMode>();
    if (j.contains("generateConstraints")) from_json(j.at("generateConstraints"), c.generateConstraints);
    if (j.contains("seed"))                c.seed                = j.at("seed").get<uint32_t>();
}
```

- [ ] **Step 2: Extend `PhraseTemplate` round-trip to handle `elaboratedConfig`.**

In `templates_json.h`, find the existing PhraseTemplate `to_json` line `if (pt.twoFigureConfig) j["twoFigureConfig"] = *pt.twoFigureConfig;` (around line 550). Immediately AFTER, add:

```cpp
    if (pt.elaboratedConfig) j["elaboratedConfig"] = *pt.elaboratedConfig;
```

In the PhraseTemplate `from_json`, find the block:
```cpp
    if (j.contains("twoFigureConfig")) {
        TwoFigurePhraseConfig c;
        from_json(j.at("twoFigureConfig"), c);
        pt.twoFigureConfig = c;
    }
```

Immediately AFTER, add:

```cpp
    if (j.contains("elaboratedConfig")) {
        ElaboratedPhraseConfig c;
        from_json(j.at("elaboratedConfig"), c);
        pt.elaboratedConfig = c;
    }
```

- [ ] **Step 3: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/templates_json.h
git commit -m "feat(templates): JSON round-trip for ElaboratedPhraseConfig"
```

---

## Task 7: Create `elaborated_phrase_strategy.h`

**Files:**
- Create: `engine/include/mforce/music/elaborated_phrase_strategy.h`

- [ ] **Step 1: Write the new header.**

Create `engine/include/mforce/music/elaborated_phrase_strategy.h` with:

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/core/randomizer.h"
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// ElaboratedPhraseStrategy — Phase 1 of recursive elaboration.
//
// Builds a skeleton MelodicFigure (via RFB unless config supplies a literal),
// walks each anchor, and per anchor randomly chooses Leave (single-note
// figure) or Generate (RFB-built figure of matching duration). Populates
// phrase.figures and phrase.connectors per the leadStep math:
//   FC[0].leadStep = 0
//   FC[i>0].leadStep = skeleton.units[i].step − E_{i-1}.net_step
//
// Figures are NEVER mutated. The cursor advance happens at realize time.
//
// Spec: docs/superpowers/specs/2026-04-24-elaborated-phrase-strategy-design.md
// ---------------------------------------------------------------------------
class ElaboratedPhraseStrategy : public PhraseStrategy {
public:
    std::string name() const override { return "elaborated_phrase"; }
    Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase ElaboratedPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
    Phrase phrase;

    if (phraseTmpl.startingPitch) {
        phrase.startingPitch = *phraseTmpl.startingPitch;
    } else {
        phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
    }

    if (!phraseTmpl.elaboratedConfig) {
        std::cerr << "ElaboratedPhraseStrategy: phraseTmpl.elaboratedConfig is "
                     "empty; returning empty phrase\n";
        return phrase;
    }
    if (!phraseTmpl.figures.empty()) {
        std::cerr << "ElaboratedPhraseStrategy: ignoring phraseTmpl.figures ("
                  << phraseTmpl.figures.size()
                  << " entries) — strategy uses elaboratedConfig\n";
    }

    const ElaboratedPhraseConfig& cfg = *phraseTmpl.elaboratedConfig;

    uint32_t seed = cfg.seed != 0 ? cfg.seed
                  : phraseTmpl.seed != 0 ? phraseTmpl.seed
                  : 0xE1AB0AAFu;
    RandomFigureBuilder rfb(seed);
    Randomizer rng(seed ^ 0xCAFE);

    // Resolve skeleton.
    MelodicFigure skeleton;
    if (cfg.skeleton) {
        skeleton = *cfg.skeleton;
    } else {
        switch (cfg.buildMethod) {
            case ElaboratedPhraseConfig::Method::ByCount:
                skeleton = rfb.build_by_count(cfg.buildCount, cfg.buildConstraints);
                break;
            case ElaboratedPhraseConfig::Method::ByLength:
                skeleton = rfb.build_by_length(cfg.buildLength, cfg.buildConstraints);
                break;
            case ElaboratedPhraseConfig::Method::Singleton:
                skeleton = rfb.build_singleton(cfg.buildConstraints);
                break;
        }
    }

    if (skeleton.units.empty()) {
        std::cerr << "ElaboratedPhraseStrategy: skeleton is empty; returning "
                     "empty phrase\n";
        return phrase;
    }

    int prev_net_step = 0;  // E_{i-1}.net_step; 0 before any elaboration

    for (size_t i = 0; i < skeleton.units.size(); ++i) {
        const FigureUnit& anchor = skeleton.units[i];

        // Pick choice.
        bool generate;
        switch (cfg.choiceMode) {
            case ElaboratedPhraseConfig::ChoiceMode::AllLeave:
                generate = false;
                break;
            case ElaboratedPhraseConfig::ChoiceMode::AllGenerate:
                generate = true;
                break;
            case ElaboratedPhraseConfig::ChoiceMode::Random:
            default:
                generate = rng.decide(0.5f);
                break;
        }

        // Build elaboration figure.
        MelodicFigure E_i;
        if (generate) {
            E_i = rfb.build_by_length(anchor.duration, cfg.generateConstraints);
            // RFB output convention: units[0].step is 0. Defensive: ensure it.
            if (!E_i.units.empty()) E_i.units[0].step = 0;
        } else {
            // Leave: single-unit figure at anchor pitch (step=0 within figure).
            FigureUnit u{anchor.duration, 0};
            E_i.units.push_back(u);
        }

        // Compute leadStep.
        int leadStep;
        if (i == 0) {
            leadStep = 0;  // FC[0] dummy
        } else {
            leadStep = skeleton.units[i].step - prev_net_step;
        }

        // Append paired figure + FC.
        FigureConnector fc{};
        fc.leadStep = leadStep;

        // Update prev_net_step BEFORE moving E_i (avoid use-after-move).
        int E_i_net_step = 0;
        for (size_t k = 0; k < E_i.units.size(); ++k) {
            E_i_net_step += E_i.units[k].step;
        }
        prev_net_step = E_i_net_step;

        phrase.add_melodic_figure_with_connector(std::move(E_i), fc);
    }

    return phrase;
}

} // namespace mforce
```

- [ ] **Step 2: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build (no consumer yet).

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/elaborated_phrase_strategy.h
git commit -m "feat(composer): add ElaboratedPhraseStrategy header"
```

---

## Task 8: Register the strategy

**Files:**
- Modify: `engine/include/mforce/music/composer.h`

- [ ] **Step 1: Add the include.**

In `composer.h`, find `#include "mforce/music/two_figure_phrase_strategy.h"`. Immediately AFTER, add:

```cpp
#include "mforce/music/elaborated_phrase_strategy.h"
```

- [ ] **Step 2: Register the strategy.**

Find the line `reg.register_phrase(std::make_unique<TwoFigurePhraseStrategy>());` (around composer.h:135). Immediately AFTER, add:

```cpp
    reg.register_phrase(std::make_unique<ElaboratedPhraseStrategy>());
```

- [ ] **Step 3: Build engine + cli + test_figures.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_cli
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
```
Expected: clean.

- [ ] **Step 4: Run baseline tests.**

```
build\Debug\test_figures.exe
```
Expected: all existing tests pass.

- [ ] **Step 5: Commit.**

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): register ElaboratedPhraseStrategy"
```

---

## Task 9: Integration tests in `test_figures`

**Files:**
- Modify: `tools/test_figures/main.cpp`

The pattern follows existing `integ_two_figure_*` tests near `tools/test_figures/main.cpp:665+`. Each test is `int integ_xxx() { ...; return 0; }`, returns 1 on failure (via `EXPECT_EQ`/`EXPECT_NEAR`), registered via `RUN_TEST(integ_xxx)` in `main()`.

- [ ] **Step 1: Add the include.**

Find `#include "mforce/music/two_figure_phrase_strategy.h"` in test_figures/main.cpp (around line 10). Immediately AFTER, add:

```cpp
#include "mforce/music/elaborated_phrase_strategy.h"
```

- [ ] **Step 2: Add the helper.**

After all the `integ_two_figure_*` tests in test_figures/main.cpp (find the last one and continue after it), add:

```cpp
// ----------------------------------------------------------------------------
// ElaboratedPhraseStrategy integration tests
// ----------------------------------------------------------------------------

struct ElaboratedResult {
    bool ok;
    std::vector<MelodicFigure> figures;
    std::vector<FigureConnector> connectors;
};

ElaboratedResult compose_elaborated(const ElaboratedPhraseConfig& cfg) {
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
    phrase.name = "ep";
    phrase.strategy = "elaborated_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);
    phrase.elaboratedConfig = cfg;

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    if (piece.parts.size() != 1) return {false, {}, {}};
    auto it = piece.parts[0].passages.find("Main");
    if (it == piece.parts[0].passages.end()) return {false, {}, {}};
    if (it->second.phrases.size() != 1) return {false, {}, {}};
    const Phrase& ph = it->second.phrases[0];

    std::vector<MelodicFigure> figs;
    for (const auto& fp : ph.figures) {
        const MelodicFigure* mf = dynamic_cast<const MelodicFigure*>(fp.get());
        if (!mf) return {false, {}, {}};
        figs.push_back(*mf);
    }
    return {true, std::move(figs), ph.connectors};
}
```

- [ ] **Step 3: Add Test 1 — AllLeave.**

```cpp
int integ_elab_all_leave() {
    ElaboratedPhraseConfig cfg;
    // Provide a literal skeleton so behavior is fully deterministic.
    cfg.skeleton = MelodicFigure{};
    cfg.skeleton->units = {
        {2.0f, 0}, {2.0f, +1}, {2.0f, +1}, {2.0f, -1}
    };
    cfg.choiceMode = ElaboratedPhraseConfig::ChoiceMode::AllLeave;

    auto r = compose_elaborated(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    EXPECT_EQ(r.figures.size(), 4u, "4 figures");
    EXPECT_EQ(r.connectors.size(), 4u, "4 connectors");

    // All Leave → all single-unit figures with step=0.
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(r.figures[i].units.size(), 1u, "leave = 1 unit");
        EXPECT_EQ(r.figures[i].units[0].step, 0, "leave step is 0");
        EXPECT_NEAR(r.figures[i].units[0].duration, 2.0f, 1e-5f, "leave duration matches anchor");
    }

    // FC[0] dummy = 0; FC[i>0].leadStep = skel.step[i] − prev_net_step.
    // Leave figures have net_step = 0, so FC[i>0].leadStep = skel.step[i].
    EXPECT_EQ(r.connectors[0].leadStep, 0, "FC[0] dummy");
    EXPECT_EQ(r.connectors[1].leadStep, +1, "FC[1] = skel step (leave net=0)");
    EXPECT_EQ(r.connectors[2].leadStep, +1, "FC[2] = skel step");
    EXPECT_EQ(r.connectors[3].leadStep, -1, "FC[3] = skel step");
    return 0;
}
```

- [ ] **Step 4: Add Test 2 — AllGenerate.**

```cpp
int integ_elab_all_generate() {
    ElaboratedPhraseConfig cfg;
    cfg.skeleton = MelodicFigure{};
    cfg.skeleton->units = {
        {2.0f, 0}, {2.0f, +1}, {2.0f, -1}
    };
    cfg.choiceMode = ElaboratedPhraseConfig::ChoiceMode::AllGenerate;
    cfg.seed = 0xE1AB0001u;

    auto r = compose_elaborated(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    EXPECT_EQ(r.figures.size(), 3u, "3 figures");
    EXPECT_EQ(r.connectors.size(), 3u, "3 connectors");

    // Each generated figure should have units.size() >= 1 and total_duration == anchor duration.
    for (size_t i = 0; i < 3; ++i) {
        const auto& fig = r.figures[i];
        if (fig.units.empty()) { std::cerr << "  FAIL: generated fig " << i << " empty\n"; return 1; }
        EXPECT_EQ(fig.units[0].step, 0, "generated fig units[0].step is 0");
        float total = 0.0f;
        for (const auto& u : fig.units) total += u.duration;
        EXPECT_NEAR(total, 2.0f, 1e-3f, "generated fig total duration matches anchor");
    }

    // FC[0] dummy.
    EXPECT_EQ(r.connectors[0].leadStep, 0, "FC[0] dummy");

    // FC[i>0].leadStep = skel.step[i] − prev_net_step. Verify the math holds
    // by recomputing from observed figure net_steps.
    int prev_net = 0;
    for (size_t k = 0; k < r.figures[0].units.size(); ++k) prev_net += r.figures[0].units[k].step;
    EXPECT_EQ(r.connectors[1].leadStep, 1 - prev_net, "FC[1] math");

    int prev_net_2 = 0;
    for (size_t k = 0; k < r.figures[1].units.size(); ++k) prev_net_2 += r.figures[1].units[k].step;
    EXPECT_EQ(r.connectors[2].leadStep, -1 - prev_net_2, "FC[2] math");
    return 0;
}
```

- [ ] **Step 5: Add Test 3 — JSON round-trip.**

```cpp
int integ_elab_json_round_trip() {
    ElaboratedPhraseConfig cfg;
    cfg.skeleton = MelodicFigure{};
    cfg.skeleton->units = {
        {2.0f, 0}, {2.0f, +1}, {2.0f, -1}
    };
    cfg.choiceMode = ElaboratedPhraseConfig::ChoiceMode::AllLeave;
    cfg.seed = 0xE1AB1234u;

    auto r1 = compose_elaborated(cfg);
    if (!r1.ok) { std::cerr << "  FAIL: in-code compose\n"; return 1; }

    nlohmann::json j = cfg;
    ElaboratedPhraseConfig cfg2;
    from_json(j, cfg2);

    auto r2 = compose_elaborated(cfg2);
    if (!r2.ok) { std::cerr << "  FAIL: post-roundtrip compose\n"; return 1; }

    EXPECT_EQ(r1.figures.size(), r2.figures.size(), "fig count match");
    EXPECT_EQ(r1.connectors.size(), r2.connectors.size(), "conn count match");
    for (size_t i = 0; i < r1.figures.size(); ++i) {
        EXPECT_EQ(r1.connectors[i].leadStep, r2.connectors[i].leadStep, "FC leadStep match");
        if (expect_figures_equal(r1.figures[i], r2.figures[i], "json roundtrip figure") != 0) return 1;
    }
    return 0;
}
```

- [ ] **Step 6: Register the tests in `main()`.**

Find the existing `RUN_TEST(integ_two_figure_*)` registrations near the end of `main()`. Immediately AFTER the last of those, add:

```cpp
    RUN_TEST(integ_elab_all_leave);
    RUN_TEST(integ_elab_all_generate);
    RUN_TEST(integ_elab_json_round_trip);
```

- [ ] **Step 7: Build test_figures.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
```
Expected: clean build.

- [ ] **Step 8: Run.**

```
build\Debug\test_figures.exe
```
Expected: all baseline tests pass + 3 new `integ_elab_*` tests pass.

- [ ] **Step 9: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): integration tests for ElaboratedPhraseStrategy"
```

---

## Task 10: Mark done in ComposerRefactor3.md

**Files:**
- Modify: `docs/ComposerRefactor3.md`

- [ ] **Step 1: Append a Phase 1 done marker.**

Open `docs/ComposerRefactor3.md`. Find the most appropriate position (under step 4 / step 7, Matt's choice). Append:

```
   DONE 2026-04-24: ElaboratedPhraseStrategy Phase 1 — RFB-built skeleton
   (literal optional override), per-anchor random Leave/Generate,
   FC.leadStep math FC[0]=0 / FC[i>0]=skel.step[i] − E_{i-1}.net_step,
   N figures + N connectors output. Foundation cleanup: Phrase gained
   parallel connectors vector; figures no longer mutated; realize advances
   cursor by FC.leadStep. K467 goldens preserved.
   (spec: docs/superpowers/specs/2026-04-24-elaborated-phrase-strategy-design.md)
   (plan: docs/superpowers/plans/2026-04-24-elaborated-phrase-strategy.md)
```

- [ ] **Step 2: Commit.**

```bash
git add docs/ComposerRefactor3.md
git commit -m "docs: ComposerRefactor3 ElaboratedPhraseStrategy Phase 1 done"
```

---

## Verification

After Task 10:

```
build\Debug\test_figures.exe
```

Confirm:
- All baseline tests pass.
- `integ_elab_all_leave`, `integ_elab_all_generate`, `integ_elab_json_round_trip` all PASS.

K467 golden hash unchanged (verified in Task 4).
