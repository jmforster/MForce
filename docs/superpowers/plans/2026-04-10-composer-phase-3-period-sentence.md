# Composer Strategy — Phase 3 Implementation Plan (Period + Sentence Phrase Strategies)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two classical phrase-level strategies — `PeriodPhraseStrategy` (antecedent + consequent with half/authentic cadence) and `SentencePhraseStrategy` (basic idea + repetition + continuation). Each strategy takes a typed configuration struct on `PhraseTemplate`. `Composer::realize_phrase` gains registry dispatch driven by the template's `strategy` field. Outline phrase strategy is explicitly DEFERRED.

**Architecture:** One new header `engine/include/mforce/music/phrase_strategies.h` holds the two strategy classes. `PhraseTemplate` gains a `strategy` string field and two `std::optional<XxxConfig>` fields, one per strategy. JSON round-trips the configs as nested blocks. `Composer::realize_phrase` reads the `strategy` field and looks up the strategy in the registry; empty string falls back to `"default_phrase"`.

**Tech Stack:** C++17. Header-only. Mechanical verification via non-silent/non-crash smoke test on a new test template.

---

## Scope Guardrails

- **Only `Period` and `Sentence`.** `OutlinePhraseStrategy` is deferred. Do not implement it. Do not add `OutlinePhraseConfig` or register an `outline_phrase` strategy.
- **Existing golden hash MUST remain unchanged.** `template_golden_phase1a.json` uses the default phrase strategy (via empty `strategy` field). After Phase 3 it continues to use the default, so its rendered hash must match the Phase 5 final hash bit-identically.
- **New test template is a smoke test, not a golden.** Create `patches/test_period_sentence.json` that exercises both Period and Sentence strategies. Render it. Confirm non-silent + non-crash. Do NOT pin it as a regression baseline — it's a sanity check only. Commit the template so Matt can audit it later, but do NOT commit the WAV or a sha256 for it.
- **Registry dispatch only at the phrase level.** `Composer::realize_figure` and `Composer::realize_passage` continue to hardcode `"default_figure"` and `"default_passage"`. Template-driven strategy selection at figure/passage levels is Phase 3+ — Period and Sentence are at the phrase level only, so that's the only level we wire up dispatch for.
- **No changes to `DefaultPhraseStrategy`.** The existing walk-figures-with-cursor-model path continues to work for templates with `strategy == ""`.
- **Single branch: `main`.** Commit directly.

---

## File Structure

New files:

| File | Responsibility |
|---|---|
| `engine/include/mforce/music/phrase_strategies.h` | `PeriodPhraseStrategy` + `SentencePhraseStrategy` class declarations and inline definitions. |
| `patches/test_period_sentence.json` | Smoke test template exercising both strategies. Committed but not pinned. |

Modified files:

| File | Change |
|---|---|
| `engine/include/mforce/music/templates.h` | Add `PeriodPhraseConfig` struct, `SentencePhraseConfig` struct, `PhraseTemplate::strategy` string, `PhraseTemplate::periodConfig`, `PhraseTemplate::sentenceConfig`. |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for `strategy` field and the two configs. |
| `engine/include/mforce/music/composer.h` | Include `phrase_strategies.h`; register `PeriodPhraseStrategy` and `SentencePhraseStrategy` in constructor; update `realize_phrase` to dispatch by `phraseTmpl.strategy` field. |
| `engine/include/mforce/music/default_strategies.h` | Promote `DefaultPhraseStrategy::apply_cadence` from private static to public static (needed by Period and Sentence strategies for their cadence adjustments). |

**Files NOT touched**: `figures.h`, `structure.h`, `strategy.h`, `strategy_registry.h`, `shape_strategies.h`, `classical_composer.h`, `conductor.h`, `dun_parser.h`, `music_json.h`, `basics.h`, `pitch_reader.h`, `patches/template_golden_phase1a.json`, the committed golden WAV / hash, any other template JSON, CLI code.

---

## Task 1: Add data model — `PeriodPhraseConfig`, `SentencePhraseConfig`, `PhraseTemplate::strategy`

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/templates_json.h`

Pure data model change. No composer logic touched. After this task, the new fields exist and JSON parses/emits them, but nothing reads them at runtime yet.

- [ ] **Step 1: Add `PeriodPhraseConfig` and `SentencePhraseConfig` to `templates.h`**

In `engine/include/mforce/music/templates.h`, find the section after `FigureTemplate` and before `PhraseTemplate`. Add the two config structs:

```cpp
// ===========================================================================
// PeriodPhraseConfig — classical period (antecedent + consequent)
// ===========================================================================

struct PeriodPhraseConfig {
  MelodicFigure basicIdea;           // opens both sub-phrases
  MelodicFigure antecedentTail;      // closes antecedent on halfCadenceTarget
  MelodicFigure consequentTail;      // closes consequent on the authentic cadence
  int halfCadenceTarget{4};          // 0-indexed scale degree for half cadence
                                     // default 4 = 5th in a 7-note diatonic scale
};

// ===========================================================================
// SentencePhraseConfig — classical sentence (basic idea + repeat + continuation)
// ===========================================================================

struct SentencePhraseConfig {
  MelodicFigure basicIdea;
  int variationTransposition{0};     // scale-degree offset for the repetition
                                     // (0 = literal repeat, +/-n = sequential)
  MelodicFigure continuation;
};
```

- [ ] **Step 2: Add fields to `PhraseTemplate`**

Still in `templates.h`, find `struct PhraseTemplate`. Add three new fields (after the existing `locked` state field):

```cpp
struct PhraseTemplate {
  std::string name;
  std::optional<Pitch> startingPitch;
  std::vector<FigureTemplate> figures;

  // Phrase-level constraints
  float totalBeats{0.0f};
  int cadenceType{0};
  int cadenceTarget{-1};
  MelodicFunction function{MelodicFunction::Free};

  // State
  uint32_t seed{0};
  bool locked{false};

  // Strategy selection (Phase 3). Empty = use default_phrase strategy.
  // When non-empty, Composer::realize_phrase looks up the strategy in
  // the registry by this name and dispatches to it.
  std::string strategy;

  // Typed configs for specific phrase strategies. Only one is typically
  // populated at a time (matching the selected strategy). None are read
  // when `strategy` is empty.
  std::optional<PeriodPhraseConfig> periodConfig;
  std::optional<SentencePhraseConfig> sentenceConfig;
};
```

**Include check**: ensure `<optional>` is already included at the top of `templates.h` (it should be, since `PhraseTemplate::startingPitch` and `PassageTemplate::startingPitch` both already use `std::optional`). No new includes needed.

- [ ] **Step 3: Add JSON round-trip for the two configs in `templates_json.h`**

Open `engine/include/mforce/music/templates_json.h`. Add dedicated `to_json` / `from_json` functions for `PeriodPhraseConfig` and `SentencePhraseConfig` at a location that precedes the `PhraseTemplate` serialization:

```cpp
// PeriodPhraseConfig
inline void to_json(json& j, const PeriodPhraseConfig& c) {
  j["basicIdea"] = c.basicIdea;
  j["antecedentTail"] = c.antecedentTail;
  j["consequentTail"] = c.consequentTail;
  if (c.halfCadenceTarget != 4) j["halfCadenceTarget"] = c.halfCadenceTarget;
}

inline void from_json(const json& j, PeriodPhraseConfig& c) {
  from_json(j.at("basicIdea"), c.basicIdea);
  from_json(j.at("antecedentTail"), c.antecedentTail);
  from_json(j.at("consequentTail"), c.consequentTail);
  c.halfCadenceTarget = j.value("halfCadenceTarget", 4);
}

// SentencePhraseConfig
inline void to_json(json& j, const SentencePhraseConfig& c) {
  j["basicIdea"] = c.basicIdea;
  j["continuation"] = c.continuation;
  if (c.variationTransposition != 0) j["variationTransposition"] = c.variationTransposition;
}

inline void from_json(const json& j, SentencePhraseConfig& c) {
  from_json(j.at("basicIdea"), c.basicIdea);
  from_json(j.at("continuation"), c.continuation);
  c.variationTransposition = j.value("variationTransposition", 0);
}
```

The `MelodicFigure` type already has a to_json/from_json in `music_json.h`. If `templates_json.h` doesn't include `music_json.h`, add the include at the top of `templates_json.h` (it may already be included — grep to check).

- [ ] **Step 4: Update `PhraseTemplate` JSON round-trip**

In `templates_json.h`, find the `to_json(json&, const PhraseTemplate&)` function. After the existing field emissions, add:

```cpp
if (!pt.strategy.empty()) j["strategy"] = pt.strategy;
if (pt.periodConfig) j["periodConfig"] = *pt.periodConfig;
if (pt.sentenceConfig) j["sentenceConfig"] = *pt.sentenceConfig;
```

Find the `from_json(const json&, PhraseTemplate&)`. After the existing field reads, add:

```cpp
pt.strategy = j.value("strategy", std::string(""));
if (j.contains("periodConfig")) {
  PeriodPhraseConfig c;
  from_json(j.at("periodConfig"), c);
  pt.periodConfig = c;
}
if (j.contains("sentenceConfig")) {
  SentencePhraseConfig c;
  from_json(j.at("sentenceConfig"), c);
  pt.sentenceConfig = c;
}
```

- [ ] **Step 5: Build and verify the golden hash is unchanged**

```
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Release
```

Render the golden:

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase3_task1_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase3_task1_check_1.wav
```

**Expected hash: the Phase 5 final hash** (whatever is in `renders/template_golden_phase1a.sha256` at the start of Phase 3). The data model changes are additive — no runtime behavior changes — so the render must be bit-identical.

If the hash differs, one of the new JSON fields has a parsing bug that silently mutates an existing field. Diagnose.

- [ ] **Step 6: Commit Task 1**

```
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git commit -m "feat(composer): add PeriodPhraseConfig, SentencePhraseConfig, strategy field to PhraseTemplate"
```

Clean up:
```
rm renders/phase3_task1_check_1.wav renders/phase3_task1_check_1.json 2>/dev/null
```

---

## Task 2: Create strategy classes and wire registry dispatch

**Files:**
- Create: `engine/include/mforce/music/phrase_strategies.h`
- Modify: `engine/include/mforce/music/composer.h`
- Modify: `engine/include/mforce/music/default_strategies.h` (promote `apply_cadence` to public)

- [ ] **Step 1: Promote `DefaultPhraseStrategy::apply_cadence` to public**

In `engine/include/mforce/music/default_strategies.h`, find the `DefaultPhraseStrategy` class declaration. The `apply_cadence` method is currently private. Promote it to public so Period and Sentence strategies can call it directly:

Change:
```cpp
private:
  static int degree_in_scale(const Pitch& pitch, const Scale& scale);   // already public from Phase 1b Task 1
  static void apply_cadence(Phrase& phrase, const PhraseTemplate& tmpl,
                            const Scale& scale);
```

to (move `apply_cadence` above `private:` so it's under `public:`):

```cpp
public:
  static int degree_in_scale(const Pitch& pitch, const Scale& scale);
  static void apply_cadence(Phrase& phrase, const PhraseTemplate& tmpl,
                            const Scale& scale);
private:
```

The `apply_cadence` definition (`inline void DefaultPhraseStrategy::apply_cadence(...)`) stays where it is — only the access-level changes in the class declaration.

**Note**: `apply_cadence` assumes the whole phrase's net movement is calculated from `startingPitch` forward through all figures. When Period/Sentence call it, they're passing the WHOLE phrase (all 3-4 figures) so the math is correct. When used for the half-cadence inside PeriodPhraseStrategy (which only wants to adjust the antecedent's last unit based on the antecedent's partial net movement), we cannot use `apply_cadence` directly — we need a helper that operates on a SUBSET of the phrase. Task 2 Step 3 addresses this.

- [ ] **Step 2: Create `phrase_strategies.h` scaffold**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/default_strategies.h"
#include <cmath>
#include <iostream>

namespace mforce {

struct Composer;  // forward

// ---------------------------------------------------------------------------
// PeriodPhraseStrategy
//
// Classical period form: antecedent (basicIdea + antecedentTail ending on
// halfCadenceTarget) + consequent (basicIdea + consequentTail ending on
// the authentic cadence target from PhraseTemplate.cadenceTarget).
//
// Reads phraseTmpl.periodConfig. Dispatched via the strategy registry when
// phraseTmpl.strategy == "period_phrase".
// ---------------------------------------------------------------------------
class PeriodPhraseStrategy : public Strategy {
public:
  std::string name() const override { return "period_phrase"; }
  StrategyLevel level() const override { return StrategyLevel::Phrase; }
  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) override;
};

// ---------------------------------------------------------------------------
// SentencePhraseStrategy
//
// Classical sentence form: basicIdea + (basicIdea transposed by
// variationTransposition scale degrees) + continuation ending on
// PhraseTemplate.cadenceTarget.
//
// Reads phraseTmpl.sentenceConfig. Dispatched when phraseTmpl.strategy ==
// "sentence_phrase".
// ---------------------------------------------------------------------------
class SentencePhraseStrategy : public Strategy {
public:
  std::string name() const override { return "sentence_phrase"; }
  StrategyLevel level() const override { return StrategyLevel::Phrase; }
  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) override;
};

// ============================================================================
// Implementations
// ============================================================================

inline Phrase PeriodPhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  if (!phraseTmpl.periodConfig) {
    std::cerr << "PeriodPhraseStrategy: phraseTmpl.periodConfig is empty; returning empty phrase\n";
    return phrase;
  }
  const PeriodPhraseConfig& cfg = *phraseTmpl.periodConfig;

  // Starting pitch: phrase override or inherited cursor.
  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  // Figure 0: basicIdea at cursor.
  phrase.add_figure(cfg.basicIdea);

  // Figure 1: antecedentTail at cursor-after-figure-0.
  phrase.add_figure(cfg.antecedentTail);

  // Half-cadence adjustment: compute the antecedent's net movement from
  // phrase.startingPitch and adjust antecedentTail's last unit so the
  // antecedent lands on halfCadenceTarget (in scale degrees).
  //
  // This is a LOCAL version of apply_cadence that walks only the first 2
  // figures (the antecedent) instead of the whole phrase.
  {
    int startDeg = DefaultPhraseStrategy::degree_in_scale(phrase.startingPitch, ctx.scale);
    int len = ctx.scale.length();
    int netSteps = 0;
    // Walk figures [0, 2): basicIdea + antecedentTail
    for (int f = 0; f < 2; ++f) {
      netSteps += phrase.figures[f].net_step();
    }
    int landingDeg = ((startDeg + netSteps) % len + len) % len;
    int target = cfg.halfCadenceTarget % len;
    if (landingDeg != target) {
      int diff = target - landingDeg;
      if (diff > len / 2) diff -= len;
      if (diff < -len / 2) diff += len;
      auto& lastFig = phrase.figures.back();  // antecedentTail
      if (!lastFig.units.empty()) {
        lastFig.units.back().step += diff;
      }
    }
  }

  // Figure 2: basicIdea again (parallel opening of consequent).
  phrase.add_figure(cfg.basicIdea);

  // Figure 3: consequentTail.
  phrase.add_figure(cfg.consequentTail);

  // Authentic cadence adjustment: use the standard apply_cadence against
  // the whole phrase (all 4 figures).
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    DefaultPhraseStrategy::apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}

inline Phrase SentencePhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  if (!phraseTmpl.sentenceConfig) {
    std::cerr << "SentencePhraseStrategy: phraseTmpl.sentenceConfig is empty; returning empty phrase\n";
    return phrase;
  }
  const SentencePhraseConfig& cfg = *phraseTmpl.sentenceConfig;

  // Starting pitch.
  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  // Figure 0: basicIdea at cursor.
  phrase.add_figure(cfg.basicIdea);

  // Figure 1: basicIdea transposed. We don't transpose the figure's
  // internal steps — instead, we add variationTransposition to the FIRST
  // unit's step, which under the cursor model effectively shifts the
  // entire repetition by that many scale degrees relative to where it
  // would naturally land.
  MelodicFigure repetition = cfg.basicIdea;
  if (!repetition.units.empty()) {
    repetition.units[0].step += cfg.variationTransposition;
  }
  phrase.add_figure(std::move(repetition));

  // Figure 2: continuation.
  phrase.add_figure(cfg.continuation);

  // Cadence adjustment on the whole phrase.
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    DefaultPhraseStrategy::apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}

} // namespace mforce
```

- [ ] **Step 3: Register the two strategies in Composer**

In `engine/include/mforce/music/composer.h`, add an include at the top:
```cpp
#include "mforce/music/phrase_strategies.h"
```

In the `Composer` constructor, after the Default and Shape strategies are registered, add:

```cpp
// Phrase strategies (Phase 3)
registry_.register_strategy(std::make_unique<PeriodPhraseStrategy>());
registry_.register_strategy(std::make_unique<SentencePhraseStrategy>());
```

- [ ] **Step 4: Update `Composer::realize_phrase` to dispatch by strategy field**

In `composer.h`, find the `realize_phrase` dispatcher on `Composer`. Replace its body:

Before:
```cpp
Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                      StrategyContext& ctx) {
  Strategy* s = registry_.get("default_phrase");
  return s->realize_phrase(phraseTmpl, ctx);
}
```

After:
```cpp
Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                      StrategyContext& ctx) {
  const std::string& n = phraseTmpl.strategy.empty() ? std::string("default_phrase") : phraseTmpl.strategy;
  Strategy* s = registry_.get(n);
  if (!s) {
    std::cerr << "Unknown phrase strategy '" << n << "', falling back to default_phrase\n";
    s = registry_.get("default_phrase");
  }
  return s->realize_phrase(phraseTmpl, ctx);
}
```

(The `std::string(...)` conversion in the conditional is to avoid dangling-reference issues with the ternary on different types.)

- [ ] **Step 5: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 6: Verify the golden hash is STILL unchanged**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase3_task2_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase3_task2_check_1.wav
```

**Expected: bit-identical to the Phase 5 final hash.** The golden template has `strategy == ""` on every phrase, so the dispatch falls back to `default_phrase` — identical to pre-Phase-3 behavior.

If the hash differs, the dispatcher has a bug. Most likely cause: typo in the string lookup, or the fallback path doesn't return the right type.

- [ ] **Step 7: Commit Task 2**

```
git add engine/include/mforce/music/phrase_strategies.h
git add engine/include/mforce/music/composer.h
git add engine/include/mforce/music/default_strategies.h
git commit -m "feat(composer): add PeriodPhraseStrategy, SentencePhraseStrategy, registry dispatch for phrases"
```

Clean up:
```
rm renders/phase3_task2_check_1.wav renders/phase3_task2_check_1.json 2>/dev/null
```

---

## Task 3: Smoke-test template and verification

**Files:**
- Create: `patches/test_period_sentence.json`

Write a small deterministic template that exercises Period on one phrase and Sentence on another. Render it. Confirm non-silent and non-crash. Commit the template but NOT the render (it's a smoke test, not a golden).

- [ ] **Step 1: Author the smoke test template**

Create `patches/test_period_sentence.json`:

```json
{
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 100.0,
  "masterSeed": 271828,
  "sections": [
    {"name": "Main", "beats": 32}
  ],
  "parts": [
    {
      "name": "melody",
      "role": "melody",
      "passages": {
        "Main": {
          "startingPitch": {"octave": 4, "pitch": "C"},
          "phrases": [
            {
              "name": "Period_Phrase",
              "startingPitch": {"octave": 4, "pitch": "C"},
              "strategy": "period_phrase",
              "cadenceType": 2,
              "cadenceTarget": 0,
              "periodConfig": {
                "basicIdea": {
                  "units": [
                    {"step": 0, "duration": 1.0},
                    {"step": 2, "duration": 1.0},
                    {"step": -1, "duration": 1.0},
                    {"step": 0, "duration": 1.0}
                  ]
                },
                "antecedentTail": {
                  "units": [
                    {"step": 0, "duration": 1.0},
                    {"step": 1, "duration": 1.0},
                    {"step": 0, "duration": 1.0},
                    {"step": 0, "duration": 1.0}
                  ]
                },
                "consequentTail": {
                  "units": [
                    {"step": 0, "duration": 1.0},
                    {"step": -1, "duration": 1.0},
                    {"step": -1, "duration": 1.0},
                    {"step": 0, "duration": 2.0}
                  ]
                },
                "halfCadenceTarget": 4
              }
            },
            {
              "name": "Sentence_Phrase",
              "startingPitch": {"octave": 4, "pitch": "G"},
              "strategy": "sentence_phrase",
              "cadenceType": 2,
              "cadenceTarget": 0,
              "sentenceConfig": {
                "basicIdea": {
                  "units": [
                    {"step": 0, "duration": 1.0},
                    {"step": 1, "duration": 1.0},
                    {"step": 1, "duration": 1.0},
                    {"step": 0, "duration": 1.0}
                  ]
                },
                "variationTransposition": -2,
                "continuation": {
                  "units": [
                    {"step": 0, "duration": 1.0},
                    {"step": -1, "duration": 1.0},
                    {"step": -1, "duration": 1.0},
                    {"step": -1, "duration": 1.0},
                    {"step": 0, "duration": 2.0},
                    {"step": 0, "duration": 2.0}
                  ]
                }
              }
            }
          ]
        }
      }
    }
  ]
}
```

**Verify the JSON fields match the loader exactly.** Specifically: `"strategy"` is a string, `"periodConfig"` and `"sentenceConfig"` are nested objects. `"units"` on each figure is a list with `{step, duration}` pairs — match how `MelodicFigure` is serialized in `music_json.h`. Grep `music_json.h` for `"step"` and `"duration"` to confirm the spelling.

**The pitch and tempo choices are arbitrary.** C major, 100 bpm, 32 beats, masterSeed 271828 (because it's a fun non-314159 number). Two phrases: Period form on scale degree 0 (C), Sentence form starting on scale degree 4 (G).

- [ ] **Step 2: Render**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase3_smoke 1 --template patches/test_period_sentence.json
```

Expected:
- `Loaded template: patches/test_period_sentence.json` in stdout.
- `Composed #1: renders/phase3_smoke_1.wav (32 beats @ 100 bpm)`.
- A reasonable peak/rms: `peak` between 0.05 and 1.0, `rms` > 0.01.
- No parse errors, no crashes.
- The `renders/phase3_smoke_1.json` output exists and shows 2 phrases, 4 figures in the first phrase, 3 figures in the second.

**Verify phrase structure in the JSON output**:

```
python -c "
import json
with open('renders/phase3_smoke_1.json') as f: p = json.load(f)
for part in p['parts']:
    for pname, passage in part['passages'].items():
        print(f'Passage {pname}:')
        for pi, phr in enumerate(passage['phrases']):
            print(f'  phrase {pi}: {len(phr[\"figures\"])} figures')
            for fi, fig in enumerate(phr['figures']):
                print(f'    fig {fi}: {len(fig[\"units\"])} units')
"
```

Expected output:
- Phrase 0 (Period): 4 figures (basicIdea, antecedentTail, basicIdea, consequentTail)
- Phrase 1 (Sentence): 3 figures (basicIdea, repetition, continuation)

If either phrase has the wrong figure count, the strategy didn't dispatch or didn't execute correctly. Diagnose.

- [ ] **Step 3: Verify golden hash is still unchanged one more time**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase3_final_golden_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase3_final_golden_check_1.wav
```

**Expected: the Phase 5 final hash.** This confirms no phase strategy work accidentally touched the default code path.

- [ ] **Step 4: Commit the smoke test template**

```
git add patches/test_period_sentence.json
git commit -m "test(composer): add smoke test template exercising Period and Sentence strategies

Committed as a non-golden verification input. Not pinned against a hash
because the output hasn't been auditioned — smoke test only confirms the
strategies dispatch, parse their configs, produce phrases with the
expected figure counts, and render non-silent audio. Matt can audit the
WAV on return; adjust or pin as desired."
```

Clean up:
```
rm renders/phase3_smoke_1.wav renders/phase3_smoke_1.json 2>/dev/null
rm renders/phase3_final_golden_check_1.wav renders/phase3_final_golden_check_1.json 2>/dev/null
```

---

## Phase 3 exit criteria

1. `cmake --build` succeeds.
2. `phrase_strategies.h` exists with `PeriodPhraseStrategy` and `SentencePhraseStrategy`.
3. `PhraseTemplate` has `strategy`, `periodConfig`, `sentenceConfig` fields with JSON round-trip.
4. `Composer::realize_phrase` dispatches by the template's `strategy` field.
5. `PeriodPhraseStrategy` and `SentencePhraseStrategy` are registered in the Composer constructor.
6. `DefaultPhraseStrategy::apply_cadence` is public (for reuse by Period/Sentence).
7. Golden WAV hash is unchanged — `renders/template_golden_phase1a.wav` still matches `renders/template_golden_phase1a.sha256` and the Phase 5 final hash.
8. `patches/test_period_sentence.json` exists and renders with the expected phrase structure.
9. Three commits in order: data model → strategies + dispatch → smoke test.

## What is explicitly NOT in this plan

- **`OutlinePhraseStrategy`** — deferred. Will be brainstormed with Matt on return.
- Figure-level registry dispatch — `Composer::realize_figure` still hardcodes `"default_figure"`. Phase 3+ work.
- Passage-level registry dispatch — `Composer::realize_passage` still hardcodes `"default_passage"`. Phase 4 work (also deferred).
- Variation beyond single scale-degree transposition for `SentencePhraseConfig`.
- Multi-sub-phrase periods or extended periods (16-bar periods, 6-bar antecedents, etc.).
- Any audition step — mechanical verification only.
- Updates to other committed templates.
- Any change to the golden template.
