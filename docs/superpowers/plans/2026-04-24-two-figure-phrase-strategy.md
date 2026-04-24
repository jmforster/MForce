# TwoFigurePhraseStrategy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new `PhraseStrategy` named `"two_figure_phrase"` that builds a base figure from RFB, derives a second figure via `figure_transforms::*`, and assembles both into a two-figure `Phrase` — without touching the existing `DefaultPhraseStrategy`.

**Architecture:** New strategy class in its own header, new `TwoFigurePhraseConfig` struct + optional field on `PhraseTemplate`, refactored `apply_transform` lifted into `figure_transforms::apply` as a free function so both `DefaultFigureStrategy` and the new strategy share it, JSON round-trip added for the new config (and for `Constraints` if missing). Integration tests in `tools/test_figures`.

**Tech Stack:** C++20, header-only engine, nlohmann::json, MSVC on Windows. Uses the VS-bundled CMake at `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

**Spec:** `docs/superpowers/specs/2026-04-24-two-figure-phrase-strategy-design.md`.

**Predecessor:** Step 2 (figure testing harness) on `figure-builder-redesign` through `e4b39d1`.

---

## Pending decisions (resolved defaults)

Plan executes against these defaults. Override before starting to redirect.

- **D1** — Name: `"two_figure_phrase"`.
- **D2** — Config: new `TwoFigurePhraseConfig` struct + optional field on `PhraseTemplate`.
- **D3** — `apply_transform` refactored into free function `figure_transforms::apply(base, op, param, seed)`.
- **D4** — JSON round-trip added for `TwoFigurePhraseConfig`.
- **D5** — Base-build methods: `ByCount`, `ByLength`, `Singleton`.
- **D6** — JSON round-trip added for `Constraints` (not currently present).
- **D7** — Warn on missing config and on non-empty `phraseTmpl.figures`; silent on ignored connectors/cadence/function.

If Matt overrides:
- D1 / D5 → small edits in Tasks 2 and 4.
- D2 → overhauls Task 2 and cascades into schema work.
- D3 → skip Task 3; duplicate the switch inside Task 4 (ugly, rejected as default).
- D4 → skip Task 6.
- D6 → skip the Constraints portion of Task 1.

---

## File Structure

- **Modify:** `engine/include/mforce/music/figure_transforms.h` — add `apply(base, op, param, seed)` free function (Task 3).
- **Modify:** `engine/include/mforce/music/default_strategies.h` — `DefaultFigureStrategy::apply_transform` delegates to the new free function (Task 3).
- **Modify:** `engine/include/mforce/music/templates.h` — add `TwoFigurePhraseConfig` + optional field on `PhraseTemplate` (Task 2).
- **Modify:** `engine/include/mforce/music/templates_json.h` — add JSON round-trip for `Constraints` (Task 1) and `TwoFigurePhraseConfig` (Task 6).
- **Create:** `engine/include/mforce/music/two_figure_phrase_strategy.h` — strategy class (Task 4).
- **Modify:** `engine/include/mforce/music/composer.h` — include + register (Task 5).
- **Modify:** `tools/test_figures/main.cpp` — integration tests (Task 7).
- **Modify:** `docs/ComposerRefactor3.md` — mark step 3 done (Task 8).

---

## Task 1: JSON round-trip for Constraints

**Files:**
- Modify: `engine/include/mforce/music/templates_json.h`

- [ ] **Step 1: Confirm Constraints has no existing round-trip.**

Run:
```
grep -n "Constraints" engine/include/mforce/music/templates_json.h
```
Expected: no lines printed (confirmed during spec work).

- [ ] **Step 2: Add the round-trip above the TransformOp block.**

Locate in `templates_json.h` the existing `inline void to_json(json& j, TransformOp t)` block (around line 33). Immediately ABOVE that block, add:

```cpp
// ===========================================================================
// Constraints (RFB figure constraints) — figure_constraints.h
// ===========================================================================

inline void to_json(json& j, const Constraints& c) {
    j = json::object();
    if (c.count)        j["count"]        = *c.count;
    if (c.length)       j["length"]       = *c.length;
    if (c.net)          j["net"]          = *c.net;
    if (c.ceiling)      j["ceiling"]      = *c.ceiling;
    if (c.floor)        j["floor"]        = *c.floor;
    if (c.defaultPulse) j["defaultPulse"] = *c.defaultPulse;
    if (c.minPulse)     j["minPulse"]     = *c.minPulse;
    if (c.maxPulse)     j["maxPulse"]     = *c.maxPulse;
}

inline void from_json(const json& j, Constraints& c) {
    if (j.contains("count"))        c.count        = j.at("count").get<int>();
    if (j.contains("length"))       c.length       = j.at("length").get<float>();
    if (j.contains("net"))          c.net          = j.at("net").get<int>();
    if (j.contains("ceiling"))      c.ceiling      = j.at("ceiling").get<int>();
    if (j.contains("floor"))        c.floor        = j.at("floor").get<int>();
    if (j.contains("defaultPulse")) c.defaultPulse = j.at("defaultPulse").get<float>();
    if (j.contains("minPulse"))     c.minPulse     = j.at("minPulse").get<float>();
    if (j.contains("maxPulse"))     c.maxPulse     = j.at("maxPulse").get<float>();
}
```

- [ ] **Step 3: Verify the include for Constraints is present.**

`templates_json.h` includes `templates.h` which includes `figure_transforms.h`. `Constraints` lives in `figure_constraints.h`. Check that `figure_constraints.h` is reachable:

```
grep -n "figure_constraints" engine/include/mforce/music/templates.h engine/include/mforce/music/figure_transforms.h engine/include/mforce/music/random_figure_builder.h
```
Expected: at least one file includes it (likely `random_figure_builder.h`). If not transitively reachable from `templates_json.h`, add `#include "mforce/music/figure_constraints.h"` at the top of `templates_json.h`.

- [ ] **Step 4: Build engine.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 5: Commit.**

```bash
git add engine/include/mforce/music/templates_json.h
git commit -m "feat(templates): JSON round-trip for Constraints"
```

---

## Task 2: TwoFigurePhraseConfig + optional field on PhraseTemplate

**Files:**
- Modify: `engine/include/mforce/music/templates.h`

- [ ] **Step 1: Add the struct above the `PhraseTemplate` definition.**

Locate in `templates.h` the `// PhraseTemplate — sequence of FigureTemplates` section header (around line 245). Immediately ABOVE the `struct PhraseTemplate {` line, add:

```cpp
// ===========================================================================
// TwoFigurePhraseConfig — base + transform phrase (step 3, ComposerRefactor3)
// ===========================================================================

struct TwoFigurePhraseConfig {
    enum class Method { ByCount, ByLength, Singleton };
    Method method{Method::ByCount};
    int   count{4};        // used when method == ByCount
    float length{4.0f};    // used when method == ByLength

    Constraints constraints;

    // 0 means "use phraseTmpl.seed, else a deterministic default"
    uint32_t seed{0};

    // How figure 2 is derived from figure 1.
    TransformOp transform{TransformOp::Invert};
    int transformParam{0};
};
```

- [ ] **Step 2: Add the optional field on `PhraseTemplate`.**

Inside `struct PhraseTemplate { ... }`, locate the lines:

```cpp
    std::optional<PeriodPhraseConfig> periodConfig;
    std::optional<SentencePhraseConfig> sentenceConfig;
```

Add immediately after them:

```cpp
    std::optional<TwoFigurePhraseConfig> twoFigureConfig;
```

- [ ] **Step 3: Build.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean. `templates_json.h` will not yet touch `twoFigureConfig` — that's Task 6.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/templates.h
git commit -m "feat(templates): add TwoFigurePhraseConfig + optional field"
```

---

## Task 3: Factor apply_transform into figure_transforms::apply

**Goal:** Lift `DefaultFigureStrategy::apply_transform` (body in `default_strategies.h`) into a free function `figure_transforms::apply(base, op, param, seed)` so `DefaultFigureStrategy` and the new `TwoFigurePhraseStrategy` share one implementation. Behavior preserved bit-for-bit.

**Files:**
- Modify: `engine/include/mforce/music/figure_transforms.h`
- Modify: `engine/include/mforce/music/default_strategies.h`

- [ ] **Step 1: Add the free function to `figure_transforms.h`.**

At the end of `figure_transforms.h`, immediately BEFORE the closing `} // namespace mforce::figure_transforms`, add:

```cpp
// ---------------------------------------------------------------------------
// apply(base, op, param, seed) — TransformOp dispatch.
// Shared by DefaultFigureStrategy::apply_transform and TwoFigurePhraseStrategy.
// Behavior must stay bit-identical to the pre-refactor body — any change here
// changes Composer output for every FigureTemplate with source=Transform.
// ---------------------------------------------------------------------------
inline MelodicFigure apply(const MelodicFigure& base, TransformOp op,
                           int param, uint32_t seed) {
  Randomizer rng(seed);

  switch (op) {
    case TransformOp::Invert:
      return invert(base);

    case TransformOp::Reverse:
      return retrograde_steps(base);

    case TransformOp::Stretch:
      return stretch(base, param > 0 ? float(param) : 2.0f);

    case TransformOp::Compress:
      return compress(base, param > 0 ? float(param) : 2.0f);

    case TransformOp::VaryRhythm:
      return vary_rhythm(base, rng);

    case TransformOp::VarySteps:
      return vary_steps(base, rng, std::max(1, param));

    case TransformOp::NewSteps: {
      StepGenerator sg(seed);
      StepSequence raw = sg.random_sequence(base.note_count() - 1);
      StepSequence newSS; newSS.add(0);
      for (int i = 0; i < raw.count(); ++i) newSS.add(raw.get(i));
      float pulse = base.units.empty() ? 1.0f : base.units[0].duration;
      return MelodicFigure::from_steps(newSS, pulse);
    }

    case TransformOp::NewRhythm: {
      MelodicFigure fig = base;
      for (auto& u : fig.units) {
        u.duration *= (rng.decide(0.5f) ? 0.5f : 1.0f) * (rng.decide(0.3f) ? 1.5f : 1.0f);
      }
      return fig;
    }

    case TransformOp::Replicate: {
      int count = (param > 0) ? param : 2;
      int step = rng.select_int({-2, -1, 1, 2});
      return replicate(base, count, step, false);
    }

    case TransformOp::TransformGeneral: {
      float choice = rng.value();
      if (choice < 0.25f) return invert(base);
      if (choice < 0.50f) return vary_rhythm(base, rng);
      if (choice < 0.75f) return retrograde_steps(base);
      return stretch(base, 2.0f);
    }

    case TransformOp::RhythmTail:
      // Not a MelodicFigure-returning op; falls through to base.
      return base;

    case TransformOp::None:
    default:
      return base;
  }
}
```

Note: `Randomizer`, `StepGenerator`, `StepSequence`, and `MelodicFigure::from_steps` are all reachable through `figure_transforms.h`'s existing includes (`figures.h` defines `MelodicFigure` and `StepGenerator`; `core/randomizer.h` is already included).

- [ ] **Step 2: Replace `DefaultFigureStrategy::apply_transform` body to delegate.**

In `default_strategies.h`, locate the `inline MelodicFigure DefaultFigureStrategy::apply_transform(...)` definition (around line 111) and replace its entire body with:

```cpp
inline MelodicFigure DefaultFigureStrategy::apply_transform(
    const MelodicFigure& base, TransformOp op, int param, uint32_t seed) {
    return figure_transforms::apply(base, op, param, seed);
}
```

- [ ] **Step 3: Build engine + cli + test_figures.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_cli
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
```
Expected: clean builds (pre-existing warnings only).

- [ ] **Step 4: Run test_figures — confirm no regression.**

```
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: `32 passed, 0 failed`.

- [ ] **Step 5: Regenerate the step-2 listening render and diff against the committed golden.**

```
mkdir -p /tmp/tfp_sanity && ./build/tools/test_figures/Debug/test_figures.exe --render patches/PluckU.json /tmp/tfp_sanity
diff -q renders/golden_fig_inv_retro_ri.json /tmp/tfp_sanity/fig.json
```
Expected: no output from `diff` (files are byte-identical). If `diff` prints a line, STOP — the refactor broke something. Inspect `figure_transforms::apply` against the original body in `default_strategies.h` line-by-line.

- [ ] **Step 6: Commit.**

```bash
git add engine/include/mforce/music/figure_transforms.h engine/include/mforce/music/default_strategies.h
git commit -m "refactor(figures): lift apply_transform into figure_transforms::apply"
```

---

## Task 4: Create TwoFigurePhraseStrategy header

**Files:**
- Create: `engine/include/mforce/music/two_figure_phrase_strategy.h`

- [ ] **Step 1: Write the header.**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/figure_transforms.h"
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// TwoFigurePhraseStrategy — builds figure 1 via RFB, derives figure 2 via
// figure_transforms::apply, returns a two-figure Phrase. Ignores connectors,
// cadence, and MelodicFunction. Expects a TwoFigurePhraseConfig on the
// PhraseTemplate.
// ---------------------------------------------------------------------------
class TwoFigurePhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "two_figure_phrase"; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase TwoFigurePhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  if (!phraseTmpl.twoFigureConfig) {
    std::cerr << "TwoFigurePhraseStrategy: phraseTmpl.twoFigureConfig is empty; returning empty phrase\n";
    return phrase;
  }
  if (!phraseTmpl.figures.empty()) {
    std::cerr << "TwoFigurePhraseStrategy: ignoring phraseTmpl.figures ("
              << phraseTmpl.figures.size() << " entries) — strategy uses twoFigureConfig\n";
  }

  const TwoFigurePhraseConfig& cfg = *phraseTmpl.twoFigureConfig;

  uint32_t seed = cfg.seed != 0 ? cfg.seed
                : phraseTmpl.seed != 0 ? phraseTmpl.seed
                : 0xF1F1F1F1u;
  RandomFigureBuilder rfb(seed);

  MelodicFigure fig1;
  switch (cfg.method) {
    case TwoFigurePhraseConfig::Method::ByCount:
      fig1 = rfb.build_by_count(cfg.count, cfg.constraints);
      break;
    case TwoFigurePhraseConfig::Method::ByLength:
      fig1 = rfb.build_by_length(cfg.length, cfg.constraints);
      break;
    case TwoFigurePhraseConfig::Method::Singleton:
      fig1 = rfb.build_singleton(cfg.constraints);
      break;
  }

  MelodicFigure fig2 = figure_transforms::apply(
      fig1, cfg.transform, cfg.transformParam, seed + 1);

  phrase.add_melodic_figure(std::move(fig1));
  phrase.add_melodic_figure(std::move(fig2));
  return phrase;
}

} // namespace mforce
```

- [ ] **Step 2: Build engine (no consumer yet).**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean build; nothing consumes this header yet but it's on the include path.

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/two_figure_phrase_strategy.h
git commit -m "feat(composer): add TwoFigurePhraseStrategy header"
```

---

## Task 5: Register the strategy in Composer

**Files:**
- Modify: `engine/include/mforce/music/composer.h`

- [ ] **Step 1: Add the include.**

Near the top of `composer.h`, immediately after `#include "mforce/music/wrapper_phrase_strategy.h"`, add:

```cpp
#include "mforce/music/two_figure_phrase_strategy.h"
```

- [ ] **Step 2: Register in the Composer constructor.**

In the constructor, locate:

```cpp
    reg.register_phrase(std::make_unique<WrapperPhraseStrategy>());
```

Add immediately after:

```cpp
    reg.register_phrase(std::make_unique<TwoFigurePhraseStrategy>());
```

- [ ] **Step 3: Build everything.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_cli
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
```
Expected: clean across the board.

- [ ] **Step 4: Run test_figures to confirm no regression.**

```
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: `32 passed, 0 failed`.

- [ ] **Step 5: Commit.**

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): register TwoFigurePhraseStrategy"
```

---

## Task 6: JSON round-trip for TwoFigurePhraseConfig

**Files:**
- Modify: `engine/include/mforce/music/templates_json.h`

- [ ] **Step 1: Add the config round-trip above the `PhraseTemplate` block.**

Locate in `templates_json.h` the block `// SentencePhraseConfig` (around line 457–471). Immediately AFTER the `SentencePhraseConfig`'s `from_json` closing brace, add:

```cpp
// ===========================================================================
// TwoFigurePhraseConfig
// ===========================================================================

inline void to_json(json& j, const TwoFigurePhraseConfig& c) {
    switch (c.method) {
        case TwoFigurePhraseConfig::Method::ByCount:   j["method"] = "by_count";   break;
        case TwoFigurePhraseConfig::Method::ByLength:  j["method"] = "by_length";  break;
        case TwoFigurePhraseConfig::Method::Singleton: j["method"] = "singleton";  break;
    }
    if (c.count != 4)    j["count"]  = c.count;
    if (c.length != 4.0f) j["length"] = c.length;
    j["constraints"] = c.constraints;
    if (c.seed != 0) j["seed"] = c.seed;
    j["transform"] = c.transform;
    if (c.transformParam != 0) j["transformParam"] = c.transformParam;
}

inline void from_json(const json& j, TwoFigurePhraseConfig& c) {
    auto methodStr = j.value("method", std::string("by_count"));
    if      (methodStr == "by_count")   c.method = TwoFigurePhraseConfig::Method::ByCount;
    else if (methodStr == "by_length")  c.method = TwoFigurePhraseConfig::Method::ByLength;
    else if (methodStr == "singleton")  c.method = TwoFigurePhraseConfig::Method::Singleton;
    else                                c.method = TwoFigurePhraseConfig::Method::ByCount;
    c.count  = j.value("count", 4);
    c.length = j.value("length", 4.0f);
    if (j.contains("constraints")) from_json(j.at("constraints"), c.constraints);
    c.seed = j.value("seed", 0u);
    if (j.contains("transform")) c.transform = j.at("transform").get<TransformOp>();
    c.transformParam = j.value("transformParam", 0);
}
```

- [ ] **Step 2: Wire the config into the `PhraseTemplate` round-trip.**

Still in `templates_json.h`, locate the PhraseTemplate `to_json` block (around line 476). Find:

```cpp
    if (pt.periodConfig) j["periodConfig"] = *pt.periodConfig;
    if (pt.sentenceConfig) j["sentenceConfig"] = *pt.sentenceConfig;
```

Add immediately after:

```cpp
    if (pt.twoFigureConfig) j["twoFigureConfig"] = *pt.twoFigureConfig;
```

Then find the `from_json` counterpart for `PhraseTemplate` (around line 530):

```cpp
    if (j.contains("sentenceConfig")) {
        SentencePhraseConfig c;
        from_json(j.at("sentenceConfig"), c);
        pt.sentenceConfig = c;
    }
```

Add immediately after:

```cpp
    if (j.contains("twoFigureConfig")) {
        TwoFigurePhraseConfig c;
        from_json(j.at("twoFigureConfig"), c);
        pt.twoFigureConfig = c;
    }
```

- [ ] **Step 3: Build.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target mforce_engine
```
Expected: clean.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/templates_json.h
git commit -m "feat(templates): JSON round-trip for TwoFigurePhraseConfig"
```

---

## Task 7: Integration tests in test_figures

**Goal:** Exercise `two_figure_phrase` through the Composer pipeline. Verify (a) the strategy produces exactly 2 figures, (b) figure 2 equals the expected transform applied to figure 1, (c) JSON round-trip preserves behavior.

**Files:**
- Modify: `tools/test_figures/main.cpp`

- [ ] **Step 1: Add the integration-test helper + three tests above `run_integration_tests`.**

Insert the following block immediately above `int run_integration_tests() {`:

```cpp
// ----------------------------------------------------------------------------
// TwoFigurePhraseStrategy integration tests
// ----------------------------------------------------------------------------

#include "mforce/music/random_figure_builder.h"

struct TwoFigureResult {
    bool ok;
    MelodicFigure fig1;
    MelodicFigure fig2;
};

TwoFigureResult compose_two_figure(const TwoFigurePhraseConfig& cfg) {
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
    phrase.name = "tf";
    phrase.strategy = "two_figure_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);
    phrase.twoFigureConfig = cfg;

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
    if (ph.figures.size() != 2) return {false, {}, {}};
    const MelodicFigure* f1 = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    const MelodicFigure* f2 = dynamic_cast<const MelodicFigure*>(ph.figures[1].get());
    if (!f1 || !f2) return {false, {}, {}};
    return {true, *f1, *f2};
}

int integ_two_figure_count_invert() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::ByCount;
    cfg.count = 4;
    cfg.seed = 0x2F1Cu;
    cfg.transform = TransformOp::Invert;
    auto r = compose_two_figure(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    EXPECT_EQ(r.fig1.units.size(), 4u, "fig1 unit count");
    // fig2 = invert(fig1): same count, negated steps.
    EXPECT_EQ(r.fig2.units.size(), r.fig1.units.size(), "fig2 unit count");
    for (size_t i = 0; i < r.fig1.units.size(); ++i) {
        EXPECT_EQ(r.fig2.units[i].step, -r.fig1.units[i].step, "fig2 step = -fig1 step");
    }
    return 0;
}

int integ_two_figure_length_retrograde() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::ByLength;
    cfg.length = 4.0f;
    cfg.seed = 0x2F1Du;
    cfg.transform = TransformOp::Reverse;
    auto r = compose_two_figure(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    // Expected fig2 is figure_transforms::apply with op=Reverse which calls
    // retrograde_steps on fig1.
    MelodicFigure expected = figure_transforms::apply(r.fig1, TransformOp::Reverse, 0, cfg.seed + 1);
    return expect_figures_equal(r.fig2, expected, "two_figure length+retrograde");
}

int integ_two_figure_singleton_stretch() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::Singleton;
    cfg.seed = 0x2F1Eu;
    cfg.transform = TransformOp::Stretch;
    cfg.transformParam = 2;
    auto r = compose_two_figure(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    EXPECT_EQ(r.fig1.units.size(), 1u, "singleton has 1 unit");
    EXPECT_EQ(r.fig2.units.size(), 1u, "stretched singleton has 1 unit");
    EXPECT_NEAR(r.fig2.units[0].duration, r.fig1.units[0].duration * 2.0f, 1e-5f,
                "stretch factor 2");
    return 0;
}

int integ_two_figure_json_round_trip() {
    // Build a PieceTemplate in code, serialize to JSON, parse back, compose
    // from both, assert equal figures.
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::ByCount;
    cfg.count = 3;
    cfg.seed = 0x2F1Fu;
    cfg.transform = TransformOp::Invert;
    cfg.constraints.net = 0;

    auto r1 = compose_two_figure(cfg);
    if (!r1.ok) { std::cerr << "  FAIL: in-code compose\n"; return 1; }

    // Serialize + deserialize the config via JSON.
    nlohmann::json j = cfg;
    TwoFigurePhraseConfig cfg2;
    from_json(j, cfg2);

    auto r2 = compose_two_figure(cfg2);
    if (!r2.ok) { std::cerr << "  FAIL: json-round-trip compose\n"; return 1; }

    int rc = expect_figures_equal(r1.fig1, r2.fig1, "fig1 after round-trip");
    if (rc) return rc;
    return expect_figures_equal(r1.fig2, r2.fig2, "fig2 after round-trip");
}
```

- [ ] **Step 2: Add the includes at the top of `main.cpp` if missing.**

Near the other `#include` lines, ensure these are present:

```cpp
#include "mforce/music/two_figure_phrase_strategy.h"
#include "mforce/music/templates_json.h"
```

(The top-of-file block from Task 8 of step-2 already has `music_json.h` but not necessarily `templates_json.h` or the new strategy header — add if absent.)

- [ ] **Step 3: Wire the new tests into `run_integration_tests`.**

Append immediately before the final `return 0;` in `run_integration_tests`:

```cpp
    RUN_TEST(integ_two_figure_count_invert);
    RUN_TEST(integ_two_figure_length_retrograde);
    RUN_TEST(integ_two_figure_singleton_stretch);
    RUN_TEST(integ_two_figure_json_round_trip);
```

- [ ] **Step 4: Build and run.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target test_figures
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: `36 passed, 0 failed`. If any of the four new tests fails, diagnose before continuing:
- `integ_two_figure_count_invert` tests the invert invariant `fig2.step[i] == -fig1.step[i]`. Failure means either the strategy wired RFB wrong or `apply(Invert)` doesn't call `invert`.
- `integ_two_figure_length_retrograde` compares directly against `figure_transforms::apply(fig1, Reverse, 0, seed+1)`. Failure means the seed passed into `apply` differs from what the strategy does.
- `integ_two_figure_singleton_stretch` checks unit count and duration scaling.
- `integ_two_figure_json_round_trip` detects JSON schema drift — missing field in `to_json` or `from_json`.

- [ ] **Step 5: Commit.**

```bash
git add tools/test_figures/main.cpp
git commit -m "test(figures): integration tests for TwoFigurePhraseStrategy"
```

---

## Task 8: Close the loop

**Files:**
- Modify: `docs/ComposerRefactor3.md`

- [ ] **Step 1: Append a completion note to step 3.**

Find in `docs/ComposerRefactor3.md`:

```
3. Rewrite DefaultPhraseStrategy
   - wire in the new RandomFigureBuilder and FigureTransforms
   - produces a 2-figure Phrase
   - test a few Phrases
```

Append immediately below:

```
   DONE 2026-04-24: landed as additive sibling two_figure_phrase strategy
   (interpretation B — leaves existing default_phrase untouched). Uses RFB +
   figure_transforms::apply directly; TwoFigurePhraseConfig carries the
   base-build method + Constraints + transform op. JSON round-trip + four
   integration tests in test_figures. Rename-to-default deferred to step 6.
   (spec: docs/superpowers/specs/2026-04-24-two-figure-phrase-strategy-design.md)
   (plan: docs/superpowers/plans/2026-04-24-two-figure-phrase-strategy.md)
```

- [ ] **Step 2: Final full build + test.**

```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug
./build/tools/test_figures/Debug/test_figures.exe
```
Expected: all targets build (pre-existing `mforce_keys` failure unrelated). test_figures reports `36 passed, 0 failed`.

- [ ] **Step 3: Commit doc + spec + plan.**

```bash
git add docs/ComposerRefactor3.md docs/superpowers/specs/2026-04-24-two-figure-phrase-strategy-design.md docs/superpowers/plans/2026-04-24-two-figure-phrase-strategy.md
git commit -m "docs: ComposerRefactor3 step 3 done — two_figure_phrase"
```

---

## Self-review

**Spec coverage:**
- Strategy declaration + registration: Tasks 4 + 5.
- `"two_figure_phrase"` name selection: Task 4, used via `PhraseTemplate::strategy` in the Task 7 helper.
- `TwoFigurePhraseConfig` struct + optional field: Task 2.
- `figure_transforms::apply` free-function factoring: Task 3, with golden-JSON regression check.
- RFB direct use for figure 1: Task 4 body.
- JSON round-trip for `Constraints` and `TwoFigurePhraseConfig`: Tasks 1 and 6, validated by Task 7's `integ_two_figure_json_round_trip`.
- Integration tests covering a few variants: Task 7 (count+invert, length+retrograde, singleton+stretch).
- Success criterion "existing goldens unchanged": Task 3 Step 5 diffs against `renders/golden_fig_inv_retro_ri.json` after the `apply_transform` refactor.

**Placeholder scan:** None — literal code in every step.

**Type consistency:**
- `TwoFigurePhraseConfig::Method`, `method`, `count`, `length`, `constraints`, `seed`, `transform`, `transformParam` — consistent across Tasks 2, 4, 6, 7.
- `PhraseTemplate::twoFigureConfig` (`std::optional<TwoFigurePhraseConfig>`) — declared Task 2, read Task 4, JSON Task 6, populated Task 7.
- `figure_transforms::apply(base, op, param, seed)` signature — declared Task 3, called Tasks 4 and 7.
- `RandomFigureBuilder::{build_by_count, build_by_length, build_singleton}` — matches `random_figure_builder.h` from step-1 context.

**Known fragility points:**
- Task 3's golden diff depends on `renders/golden_fig_inv_retro_ri.json` existing (it was committed in step 2 Task 8). If it was removed, generate a fresh one with `test_figures --render patches/PluckU.json <dir>` before the refactor and use it as the reference.
- Task 7's tests assume `RandomFigureBuilder` produces a figure with at least 1 unit for `build_by_count(4, ...)` and `build_by_length(4.0f, ...)`. If Constraints include a `count`/`length` that zeroes out the result, the unit-count EXPECT_EQ fires with a clear message — not a plan bug.
- `figure_transforms::apply` vs `DefaultFigureStrategy::apply_transform` — if the two diverge later (e.g. someone edits the figure-transforms version without mirroring the strategy delegate), the strategy body's one-line delegation makes it a trivial re-audit.
