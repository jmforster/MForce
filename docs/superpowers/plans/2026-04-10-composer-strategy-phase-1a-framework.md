# Composer Strategy — Phase 1a Framework Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce the Strategy/StrategyRegistry/Composer framework from the composer-strategy spec and reroute the existing `ClassicalComposer` logic through it via three `DefaultXxxStrategy` classes — with **bit-identical output** against a pinned golden template/WAV so the refactor is mechanically verifiable.

**Architecture:** Add a small set of new headers (`strategy.h`, `strategy_registry.h`, `composer.h`, `default_strategies.h`). Move the existing generation/realization logic out of `ClassicalComposer`'s private methods into the three default strategies. `ClassicalComposer` becomes a thin `IComposer` wrapper around a `Composer` instance. No semantic changes: connectors stay, cursor/literal/cadence changes are deferred to Phase 1b.

**Tech Stack:** C++17, nlohmann::json, CMake. Header-only music module (following existing convention). No test framework — regression is a golden WAV SHA-256 hash compared byte-for-byte after the refactor.

---

## Scope Guardrails

- **No behavior changes.** Every generated figure, every step, every byte of rendered audio must be identical before and after this plan. If any step causes the golden hash to drift, it is a bug in the refactor, not an intentional change.
- **Do not delete connectors.** `FigureConnector`, `Phrase::connectors`, the default `step(-1)` insertion between figures, and the connector-step contribution to cadence math all stay in place.
- **Do not rename `startingPitch` to `cursor`.** That rename is Phase 1b.
- **Do not add `literal` figure source.** That is Phase 1b.
- **Do not rename `Seed` to `Motif`.** That is Phase 5.
- **Do not touch the CLI (`tools/mforce_cli/main.cpp`)** unless a task in this plan explicitly changes it. The construction `ClassicalComposer composer(tmpl.masterSeed)` at `main.cpp:549` must continue to work and produce the same output.
- **Do not touch `FigureBuilder`, `StepGenerator`, `PitchReader`, or any file under `tools/durn_converter/`.** Shape-level migration is Phase 2.
- **Header-only implementations** for new files, matching the existing convention in `engine/include/mforce/music/`.

---

## File Structure

New files (all under `engine/include/mforce/music/`):

| File | Responsibility |
|---|---|
| `strategy.h` | `StrategyLevel` enum, `StrategyContext` struct, `Strategy` abstract base with default no-op virtuals. |
| `strategy_registry.h` | `StrategyRegistry` — owns `unique_ptr<Strategy>` instances, lookup by name, listing by level. |
| `default_strategies.h` | `DefaultFigureStrategy`, `DefaultPhraseStrategy`, `DefaultPassageStrategy` — each holds the extracted body of one of `ClassicalComposer`'s existing private methods. |
| `composer.h` | `Composer` class — owns a `StrategyRegistry`, populates it with the three defaults in its constructor, exposes `compose()` plus the `realize_figure / realize_phrase / realize_passage` dispatchers that strategies call to delegate to sub-levels. |

Modified files:

| File | Change |
|---|---|
| `engine/include/mforce/music/classical_composer.h` | All private method bodies for `realize_figure`, `generate_figure`, `generate_shaped_figure`, `apply_transform`, `choose_shape`, `realize_phrase`, `apply_cadence`, `apply_cadence_rhythm`, `realize_passage`, `setup_piece`, `realize_seeds`, `generate_default_passage` are removed. The class becomes a thin `IComposer` holding an internal `Composer` and delegating the 3 `compose()` overloads. `realizedSeeds_` moves onto `Composer` (as the motif pool; for Phase 1a it's still called `realizedSeeds_` and keyed off `Seed` entries). |
| `patches/template_golden_phase1a.json` | New file. A deterministic template with `masterSeed` fixed, authored in the style of `patches/template_shaped_test.json`. This is the regression input. |
| `renders/template_golden_phase1a.wav` | Committed binary. The pre-refactor render output. |
| `renders/template_golden_phase1a.sha256` | Committed hash of the WAV. The one-line regression check. |

No files in `tools/` are modified.

---

## Background: What Each Extracted Method Does

To make moves explicit, here is a map of every piece of existing logic and where it lands:

| Source (all in `classical_composer.h`) | Lines | Destination |
|---|---|---|
| `setup_piece` | 87–112 | `Composer::setup_piece_` (private, on `composer.h`) |
| `realize_seeds` | 114–137 | `Composer::realize_seeds_` (private) |
| `degree_in_scale` (static) | 167–175 | Free helper in `default_strategies.h` anonymous namespace |
| `realize_passage` | 143–159 | `DefaultPassageStrategy::realize_passage` |
| `realize_phrase` | 181–224 | `DefaultPhraseStrategy::realize_phrase` |
| `apply_cadence` | 230–259 | Private helper on `DefaultPhraseStrategy` |
| `apply_cadence_rhythm` | 265–304 | Private helper on `DefaultPhraseStrategy` (currently unused — keep as-is so we don't delete dead code in a refactor) |
| `realize_figure` | 310–341 | `DefaultFigureStrategy::realize_figure` |
| `generate_figure` | 347–393 | Public helper on `DefaultFigureStrategy` (public so Composer::realize_seeds_ can call it directly — see Task 4 & Task 7) |
| `generate_shaped_figure` | 399–458 | Public helper on `DefaultFigureStrategy` |
| `choose_shape` | 464–511 | Public static on `DefaultFigureStrategy` (called directly from `DefaultPhraseStrategy::realize_phrase`) |
| `apply_transform` | 517–580 | Public helper on `DefaultFigureStrategy` |
| `generate_default_passage` | 586–633 | `Composer::generate_default_passage_` (private) |
| `realizedSeeds_` field | 81 | Moves to `Composer` as a private member |
| Instance `stepGen`, `builder`, `rng` | 20–22 | `stepGen` and `builder` are dead (the existing `generate_figure` etc. construct local instances from seeds); remove them. `rng` moves to `Composer`. |

Nothing else in `classical_composer.h` remains after the extraction except the three `IComposer::compose(...)` overloads, which become thin forwards to the owned `Composer`.

---

## Task 1: Author the golden template and pin the pre-refactor render

**Files:**
- Create: `patches/template_golden_phase1a.json`
- Create: `renders/template_golden_phase1a.wav`
- Create: `renders/template_golden_phase1a.sha256`

This is the regression safety net. Everything else in this plan is verified against the hash generated here. Do this FIRST, before touching any source code.

- [ ] **Step 1: Create the golden template JSON**

Create `patches/template_golden_phase1a.json` with a fixed non-zero `masterSeed` so the composer's RNG chain is deterministic. Use the structure of `patches/template_shaped_test.json` as a starting point, add an explicit seed, and include at least one of each of the source paths we care about (generate, shaped, one seed+reference). That way the golden exercises `realize_figure`'s full switch.

```json
{
  "keyName": "G",
  "scaleName": "Major",
  "bpm": 100.0,
  "masterSeed": 314159,
  "seeds": [
    {
      "name": "motif_a",
      "constraints": {
        "source": "generate",
        "totalBeats": 2.0,
        "minNotes": 3,
        "maxNotes": 4,
        "preferStepwise": true
      }
    }
  ],
  "sections": [
    {"name": "Main", "beats": 48}
  ],
  "parts": [
    {
      "name": "melody",
      "role": "melody",
      "passages": {
        "Main": {
          "phrases": [
            {
              "name": "A1",
              "startingPitch": {"octave": 4, "pitch": "G"},
              "function": "statement",
              "figures": [
                {"source": "reference", "seedName": "motif_a"},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 5},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 5, "shape": "scalar_run", "shapeDirection": 1},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 2, "maxNotes": 3}
              ],
              "cadenceType": 2,
              "cadenceTarget": 4
            },
            {
              "name": "B",
              "startingPitch": {"octave": 4, "pitch": "D"},
              "function": "development",
              "figures": [
                {"source": "generate", "totalBeats": 4.0, "minNotes": 4, "maxNotes": 6},
                {"source": "transform", "seedName": "motif_a", "transform": "invert"},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 4, "maxNotes": 6, "shape": "triadic_outline"},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 4}
              ],
              "cadenceType": 1,
              "cadenceTarget": 4
            },
            {
              "name": "A2",
              "startingPitch": {"octave": 4, "pitch": "G"},
              "function": "cadential",
              "figures": [
                {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 5},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 3, "maxNotes": 4},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 2, "maxNotes": 3},
                {"source": "generate", "totalBeats": 4.0, "minNotes": 1, "maxNotes": 2}
              ],
              "cadenceType": 2,
              "cadenceTarget": 0
            }
          ]
        }
      }
    }
  ]
}
```

Before saving, cross-check every JSON field against the `from_json` path in `engine/include/mforce/music/templates_json.h` to confirm each spelling is accepted. If `templates_json.h` uses a different spelling for `shape` values or `transform` ops (e.g. `"Invert"` vs `"invert"`), fix the template to match what the loader accepts. This is a one-time verification — the template must load without warnings before it's used as a golden.

- [ ] **Step 2: Build the current `mforce_cli` exactly as-is**

Run from repo root:

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build of `build/tools/mforce_cli/Release/mforce_cli.exe` (or equivalent). No code changes yet. If the build fails, stop — the baseline is broken and the plan cannot proceed until it's fixed independently.

- [ ] **Step 3: Render the golden template with the current composer**

Run from repo root:

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/template_golden_phase1a 1 --template patches/template_golden_phase1a.json
```

(Patch choice: use a small, existing patch — `kick_drum.json` is committed and the patch is just the instrument voice; any existing patch works. If `kick_drum.json` is not the right kind of instrument to render a melody through, substitute any other committed `patches/*.json` that works as a melody voice. The exact patch choice does not matter — what matters is that the same command produces the same output on both sides of the refactor.)

Expected output: `renders/template_golden_phase1a.wav` exists and plays a recognizable deterministic melody.

- [ ] **Step 4: Manually audition the golden render**

Play `renders/template_golden_phase1a.wav` once. It does not need to be *good music*. It needs to be **not broken**: no silence, no clipping to the point of being unlistenable, no crashes during rendering. If it sounds broken, the template is wrong — fix it and re-render before proceeding. The golden is what the whole refactor is locked against; if we pin garbage, the refactor passes while the composer is silently broken.

- [ ] **Step 5: Compute and commit the SHA-256 hash**

Compute the hash of the committed WAV and write it to a sibling file. On Windows bash:

```
sha256sum renders/template_golden_phase1a.wav > renders/template_golden_phase1a.sha256
```

Expected: `renders/template_golden_phase1a.sha256` contains one line: `<64-hex-chars>  renders/template_golden_phase1a.wav`.

- [ ] **Step 6: Commit the golden baseline**

```
git add patches/template_golden_phase1a.json renders/template_golden_phase1a.wav renders/template_golden_phase1a.sha256
git commit -m "test: pin golden template+WAV for composer phase-1a refactor"
```

---

## Task 2: Add the Strategy interface

**Files:**
- Create: `engine/include/mforce/music/strategy.h`

This file introduces the three types every later task depends on: `StrategyLevel` (enum), `StrategyContext` (shared data bundle), and `Strategy` (abstract base with default no-op virtuals for each level). No other file is touched in this task.

- [ ] **Step 1: Write `strategy.h`**

```cpp
#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>

namespace mforce {

struct PieceTemplate;     // fwd
struct Composer;          // fwd

enum class StrategyLevel { Figure, Phrase, Passage, Piece };

// Shared data bundle passed to every strategy call. Strategies are stateless
// singletons living in the registry; all per-call state lives here.
struct StrategyContext {
  Scale scale;
  Pitch startingPitch;                // initial pitch for the current unit of work
  float totalBeats{0.0f};             // target length of current unit (0 = unconstrained)
  Piece* piece{nullptr};              // in-progress piece (for seed/phrase lookup)
  const PieceTemplate* template_{nullptr};
  Composer* composer{nullptr};        // for dispatching to sub-levels
  nlohmann::json params;              // strategy-specific params from the template
  Randomizer* rng{nullptr};           // shared RNG for this composition
};

// Abstract base. Subclasses override exactly one of the realize_* methods,
// matching their level(). Calling a level that isn't overridden throws —
// that's a programming error (you asked a Figure strategy to realize a Phrase).
class Strategy {
public:
  virtual ~Strategy() = default;
  virtual std::string name() const = 0;
  virtual StrategyLevel level() const = 0;

  virtual MelodicFigure realize_figure(const FigureTemplate&, StrategyContext&) {
    throw std::logic_error("realize_figure not implemented for strategy: " + name());
  }
  virtual Phrase realize_phrase(const PhraseTemplate&, StrategyContext&) {
    throw std::logic_error("realize_phrase not implemented for strategy: " + name());
  }
  virtual Passage realize_passage(const PassageTemplate&, StrategyContext&) {
    throw std::logic_error("realize_passage not implemented for strategy: " + name());
  }
};

} // namespace mforce
```

- [ ] **Step 2: Build to verify the header compiles**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. Nothing uses `strategy.h` yet; this is just header-parse verification. If it fails, the includes are wrong or a type name drifted from the current codebase — fix before moving on.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/strategy.h
git commit -m "refactor(composer): add Strategy interface + StrategyContext"
```

---

## Task 3: Add the StrategyRegistry

**Files:**
- Create: `engine/include/mforce/music/strategy_registry.h`

A name-keyed container of `unique_ptr<Strategy>` with the two lookup operations the dispatcher needs: `get(name)` and `list_for_level(level)`. That's all — no static state, no globals.

- [ ] **Step 1: Write `strategy_registry.h`**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

class StrategyRegistry {
public:
  void register_strategy(std::unique_ptr<Strategy> s) {
    const std::string n = s->name();
    strategies_[n] = std::move(s);
  }

  Strategy* get(const std::string& name) const {
    auto it = strategies_.find(name);
    return it == strategies_.end() ? nullptr : it->second.get();
  }

  std::vector<Strategy*> list_for_level(StrategyLevel lvl) const {
    std::vector<Strategy*> out;
    out.reserve(strategies_.size());
    for (auto& kv : strategies_) {
      if (kv.second->level() == lvl) out.push_back(kv.second.get());
    }
    return out;
  }

private:
  std::unordered_map<std::string, std::unique_ptr<Strategy>> strategies_;
};

} // namespace mforce
```

- [ ] **Step 2: Build to verify the header compiles**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/strategy_registry.h
git commit -m "refactor(composer): add StrategyRegistry"
```

---

## Task 4: Create the DefaultFigureStrategy shell and move `realize_figure`

**Files:**
- Create: `engine/include/mforce/music/default_strategies.h`
- Read: `engine/include/mforce/music/classical_composer.h:310-341` (realize_figure body)
- Read: `engine/include/mforce/music/classical_composer.h:347-393` (generate_figure body)
- Read: `engine/include/mforce/music/classical_composer.h:399-458` (generate_shaped_figure body)
- Read: `engine/include/mforce/music/classical_composer.h:464-511` (choose_shape body)
- Read: `engine/include/mforce/music/classical_composer.h:517-580` (apply_transform body)

This task lifts the figure-level generation logic out of `ClassicalComposer` into a new `DefaultFigureStrategy` class. The logic is copied verbatim, with two textual changes:
1. References to `realizedSeeds_` become `ctx.piece->get_realized_seeds_` — see the helper method discussion in Task 7. Until Task 7 lands, use `ctx.composer->realized_seeds()` (a method we'll add on Composer in Task 7). For now the compiler will complain; that's expected and resolved when Task 7 lands.
2. References to the instance `rng` member become `*ctx.rng`.

The existing code in `classical_composer.h` STAYS in place during this task. `DefaultFigureStrategy` is additive — nothing calls it yet.

- [ ] **Step 1: Create `default_strategies.h` with the figure strategy scaffold**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>

namespace mforce {

// Forward — Composer is defined in composer.h and referenced via ctx.composer
struct Composer;

// ---------------------------------------------------------------------------
// DefaultFigureStrategy
//
// Wraps the pre-refactor ClassicalComposer::realize_figure / generate_figure /
// generate_shaped_figure / choose_shape / apply_transform code paths, so that
// a template FigureTemplate routed through Composer::realize_figure produces
// byte-identical output compared to pre-refactor ClassicalComposer.
// ---------------------------------------------------------------------------
class DefaultFigureStrategy : public Strategy {
public:
  std::string name() const override { return "default_figure"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }

  MelodicFigure realize_figure(const FigureTemplate& figTmpl,
                               StrategyContext& ctx) override;

  // PUBLIC so Composer::realize_seeds_ can call it directly, bypassing the
  // realize_figure switch. The pre-refactor ClassicalComposer::realize_seeds
  // called the private generate_figure directly (classical_composer.h:134),
  // not the public realize_figure, so preserving that call shape is required
  // for bit-identical output against the golden. Do not change this to
  // private without also changing Composer::realize_seeds_ to route through
  // realize_figure — and if you do that, you must re-pin the golden, because
  // the RNG call order will shift.
  MelodicFigure generate_figure(const FigureTemplate& figTmpl, uint32_t seed);
  MelodicFigure generate_shaped_figure(const FigureTemplate& ft, uint32_t seed);
  MelodicFigure apply_transform(const MelodicFigure& base, TransformOp op,
                                int param, uint32_t seed);

  // Exposed so DefaultPhraseStrategy (Task 6) can call it when auto-selecting
  // a shape for a Free figure under a MelodicFunction.
  static FigureShape choose_shape(MelodicFunction func, int posInPhrase,
                                  int totalFigures, uint32_t seed);
};

// -- realize_figure -----------------------------------------------------------
inline MelodicFigure DefaultFigureStrategy::realize_figure(
    const FigureTemplate& figTmpl, StrategyContext& ctx) {
  // Body is lifted verbatim from classical_composer.h:310-341 with two edits:
  //   (1) rng.rng()         -> ctx.rng->rng()
  //   (2) realizedSeeds_    -> ctx.composer->realized_seeds()
  //
  // Paste the full body here, applying only those two textual changes. DO NOT
  // alter the switch semantics, the ordering of checks, the fallback branches,
  // or the seed derivation. Bit-identicality depends on preserving every
  // arithmetic operation in the same order.
  //
  // [IMPLEMENTER: copy the exact body from classical_composer.h lines 310-341]
}

// -- generate_figure, generate_shaped_figure, apply_transform, choose_shape --
// Each of these is lifted verbatim from the corresponding lines in
// classical_composer.h. No edits required — they take their seeds as
// parameters and construct local StepGenerator / FigureBuilder instances, so
// they don't touch any class state that moved.
//
// [IMPLEMENTER: copy bodies from classical_composer.h:347-393 (generate_figure),
//               classical_composer.h:399-458 (generate_shaped_figure),
//               classical_composer.h:517-580 (apply_transform),
//               classical_composer.h:464-511 (choose_shape)]

} // namespace mforce
```

When you paste the bodies, every function lives inside `namespace mforce` as `inline MelodicFigure DefaultFigureStrategy::generate_figure(...) { ... }` etc. `choose_shape` is `inline FigureShape DefaultFigureStrategy::choose_shape(...)`.

Note that the code inside `realize_figure` that looks up a seed (`realizedSeeds_.find(figTmpl.seedName)`) will not compile in this task because `Composer::realized_seeds()` does not exist yet. This is expected — the unresolved reference is a deliberate pin that Task 7 resolves. Mark this task's build as "expected to fail on `ctx.composer->realized_seeds()` only" and move on. Do not work around it. Do not comment it out. Do not stub it. The next three tasks continue to use the old `ClassicalComposer` code path for the regression run, so the compiler error must only occur in files that include `default_strategies.h` — which for now is only `default_strategies.h` itself.

**If nothing in the repo includes `default_strategies.h` yet, nothing breaks the build** — that's the intended state. The header defines a class that won't be instantiated until Task 7. Confirm with a build.

- [ ] **Step 2: Build — nothing should include `default_strategies.h` yet, so the build should still succeed**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. `default_strategies.h` is a dangling header at this point; it is syntactically checked only by the one file that includes it (itself, via the `#pragma once` header-only scheme). If you get errors, you included `default_strategies.h` from somewhere you shouldn't have — remove the include.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/default_strategies.h
git commit -m "refactor(composer): add DefaultFigureStrategy (not yet wired)"
```

---

## Task 5: Add DefaultPassageStrategy to `default_strategies.h`

**Files:**
- Modify: `engine/include/mforce/music/default_strategies.h`
- Read: `engine/include/mforce/music/classical_composer.h:143-159` (realize_passage body)

`DefaultPassageStrategy` walks the phrase list of a `PassageTemplate` exactly as `ClassicalComposer::realize_passage` did. The walk dispatches each phrase via `ctx.composer->realize_phrase(...)`, which Task 7 will wire through the registry to `DefaultPhraseStrategy`.

- [ ] **Step 1: Append the DefaultPassageStrategy class to `default_strategies.h`**

Add this block inside `namespace mforce`, after `DefaultFigureStrategy`:

```cpp
class DefaultPassageStrategy : public Strategy {
public:
  std::string name() const override { return "default_passage"; }
  StrategyLevel level() const override { return StrategyLevel::Passage; }

  Passage realize_passage(const PassageTemplate& passTmpl,
                          StrategyContext& ctx) override;
};

inline Passage DefaultPassageStrategy::realize_passage(
    const PassageTemplate& passTmpl, StrategyContext& ctx) {
  // Body lifted from classical_composer.h:143-159 with edits:
  //   (1) realize_phrase(phraseTmpl, pieceTmpl, scale, reader)
  //       becomes
  //       ctx.composer->realize_phrase(phraseTmpl, ctx)
  //       (The Composer::realize_phrase dispatcher, Task 7, is what hits
  //        DefaultPhraseStrategy via the registry. The PitchReader instance
  //        that was local to the old realize_passage STAYS here — create it
  //        the same way, seed it the same way, and pass the same pitch
  //        through ctx.startingPitch on the per-phrase context clone.)
  //   (2) pieceTmpl and scale are read from ctx.template_ and ctx.scale.
  //
  // Specifically, the shape of the new body is:
  //
  //   Passage passage;
  //   PitchReader reader(ctx.scale);
  //   reader.set_pitch(5, 0);
  //
  //   for (auto& phraseTmpl : passTmpl.phrases) {
  //     if (phraseTmpl.locked) continue;
  //
  //     StrategyContext phraseCtx = ctx;           // clone
  //     phraseCtx.startingPitch = phraseTmpl.startingPitch
  //         ? *phraseTmpl.startingPitch
  //         : reader.get_pitch();
  //
  //     Phrase phrase = ctx.composer->realize_phrase(phraseTmpl, phraseCtx);
  //     passage.add_phrase(std::move(phrase));
  //   }
  //   return passage;
  //
  // IMPORTANT: in the pre-refactor code, the PitchReader was created inside
  // realize_passage and mutated as `reader.set_pitch(5, 0)` once before the
  // loop; per-phrase starting pitch was resolved INSIDE realize_phrase, not
  // out here. So the only loop body that affected RNG consumption was the
  // body of realize_phrase itself. Keeping the per-phrase fallback to
  // `reader.get_pitch()` here is fine because the pre-refactor fallback did
  // exactly this: `reader.set_pitch(5, 0); phrase.startingPitch =
  // reader.get_pitch();` (classical_composer.h:188-190). The RNG is not
  // touched by PitchReader, so this refactor does not perturb the seed chain.
  //
  // [IMPLEMENTER: write the body as shown above.]
}
```

- [ ] **Step 2: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. Nothing includes `default_strategies.h` yet.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/default_strategies.h
git commit -m "refactor(composer): add DefaultPassageStrategy (not yet wired)"
```

---

## Task 6: Add DefaultPhraseStrategy to `default_strategies.h`

**Files:**
- Modify: `engine/include/mforce/music/default_strategies.h`
- Read: `engine/include/mforce/music/classical_composer.h:181-224` (realize_phrase body)
- Read: `engine/include/mforce/music/classical_composer.h:230-259` (apply_cadence body)
- Read: `engine/include/mforce/music/classical_composer.h:265-304` (apply_cadence_rhythm body)
- Read: `engine/include/mforce/music/classical_composer.h:167-175` (degree_in_scale helper)

`DefaultPhraseStrategy` owns the per-phrase walk: each figure is realized (via dispatch), joined to the previous figure with a connector (keeping the connector mechanism in place for Phase 1a), and a cadence adjustment is applied at the end. `choose_shape` is called as a static on `DefaultFigureStrategy` for the `MelodicFunction`-driven shape selection.

- [ ] **Step 1: Append the DefaultPhraseStrategy class to `default_strategies.h`**

Add this block after `DefaultPassageStrategy`:

```cpp
class DefaultPhraseStrategy : public Strategy {
public:
  std::string name() const override { return "default_phrase"; }
  StrategyLevel level() const override { return StrategyLevel::Phrase; }

  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) override;

private:
  static int degree_in_scale(const Pitch& pitch, const Scale& scale);
  static void apply_cadence(Phrase& phrase, const PhraseTemplate& tmpl,
                            const Scale& scale);
  static void apply_cadence_rhythm(MelodicFigure& fig, int cadenceType);
};

// -- degree_in_scale: verbatim from classical_composer.h:167-175 -------------
inline int DefaultPhraseStrategy::degree_in_scale(const Pitch& pitch,
                                                  const Scale& scale) {
  // [IMPLEMENTER: paste body from classical_composer.h:167-175 verbatim]
}

// -- apply_cadence: verbatim from classical_composer.h:230-259 ---------------
//
// Keep the connector-step summation EXACTLY as-is. This is the line that
// reads `phrase.connectors[f - 1]` and, for Step connectors, adds
// `conn.stepValue` into netSteps. That logic stays in Phase 1a and goes
// away in Phase 1b.
inline void DefaultPhraseStrategy::apply_cadence(Phrase& phrase,
                                                 const PhraseTemplate& tmpl,
                                                 const Scale& scale) {
  // [IMPLEMENTER: paste body from classical_composer.h:230-259 verbatim,
  //  with one change: replace the static call to
  //  `ClassicalComposer::degree_in_scale(...)` (if there is one; in the
  //  current source it's a private static accessible without qualifier)
  //  with `DefaultPhraseStrategy::degree_in_scale(...)`.]
}

// -- apply_cadence_rhythm: verbatim from classical_composer.h:265-304 --------
inline void DefaultPhraseStrategy::apply_cadence_rhythm(MelodicFigure& fig,
                                                        int cadenceType) {
  // [IMPLEMENTER: paste body from classical_composer.h:265-304 verbatim.]
}

// -- realize_phrase: body rewritten to dispatch via composer ------------------
inline Phrase DefaultPhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  // Per-phrase starting pitch: if the template has one, use it; otherwise
  // fall back to ctx.startingPitch (which DefaultPassageStrategy set from
  // reader.get_pitch()). This matches the pre-refactor behavior at
  // classical_composer.h:185-190.
  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.startingPitch;
  }

  const int numFigs = int(phraseTmpl.figures.size());
  for (int i = 0; i < numFigs; ++i) {
    // MelodicFunction-driven shape selection: identical to
    // classical_composer.h:197-202.
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = DefaultFigureStrategy::choose_shape(
          phraseTmpl.function, i, numFigs, ctx.rng->rng());
    }

    // Dispatch to the figure level via the Composer. The figure context
    // clones the current (phrase) context; per-figure scale/startingPitch
    // don't differ in this pre-refactor path.
    StrategyContext figCtx = ctx;
    MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);

    if (i == 0) {
      phrase.add_figure(std::move(fig));
    } else {
      // Connector path — UNCHANGED from classical_composer.h:207-213.
      // Default `step(-1)` when the template doesn't specify one.
      FigureConnector conn = FigureConnector::step(-1);
      if (i - 1 < (int)phraseTmpl.connectors.size()) {
        conn = phraseTmpl.connectors[i - 1];
      }
      phrase.add_figure(std::move(fig), conn);
    }
  }

  // Cadence adjustment — unchanged from classical_composer.h:217-222.
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}
```

The one subtle point: the pre-refactor code used `rng.rng()` (the composer's instance member) for the shape-selection seed. After the refactor this is `ctx.rng->rng()`. **These must resolve to the same underlying RNG object**, so that the sequence of `rng()` calls during composition is identical and the deterministic output matches the golden hash. Task 7 wires this up: `Composer` owns a single `Randomizer rng_` seeded from `masterSeed`, and every context it hands out points at that single instance via `ctx.rng = &rng_`.

- [ ] **Step 2: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. `default_strategies.h` is still not included from anywhere that gets instantiated, so unresolved references (`ctx.composer->realize_figure`, `ctx.composer->realize_phrase`) remain as pending symbols. Those are resolved in Task 7.

Note: the `inline` member function definitions will be syntax-checked only when their header is included. Since Task 7 is the first task that includes `default_strategies.h` from `composer.h`, any paste errors surface there. That's fine — but review the pasted bodies carefully before committing this task, because the bisect granularity is per-commit.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/default_strategies.h
git commit -m "refactor(composer): add DefaultPhraseStrategy (not yet wired)"
```

---

## Task 7: Add the Composer class and wire up dispatch

**Files:**
- Create: `engine/include/mforce/music/composer.h`
- Read: `engine/include/mforce/music/classical_composer.h:87-137` (setup_piece, realize_seeds)
- Read: `engine/include/mforce/music/classical_composer.h:586-633` (generate_default_passage)

This is the task where the whole framework becomes live code. `Composer` owns the `StrategyRegistry`, the `Randomizer`, and the `realizedSeeds_` map. Its constructor registers the three Defaults. Its `compose(piece, tmpl)` walks the piece structure and calls `realize_passage(...)` through the dispatcher. The dispatcher in turn looks up the registered strategy by name (hardcoded to `"default_passage"` etc. for Phase 1a — template `strategy` field support ships with Phase 3).

- [ ] **Step 1: Create `composer.h`**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// Composer
//
// Owns the StrategyRegistry, the master RNG, and the realized-seed pool
// (the precursor to the "motif pool" from the spec; Phase 5 renames it).
// Walks the PieceTemplate and dispatches to registered strategies at each
// level of the hierarchy.
//
// For Phase 1a, the ONLY strategies registered are the three Defaults, and
// dispatch always selects them regardless of any `strategy` field on the
// template. Template-driven strategy selection arrives in Phase 3.
// ---------------------------------------------------------------------------
struct Composer {
  explicit Composer(uint32_t seed = 0xC1A5'0001u) : rng_(seed) {
    registry_.register_strategy(std::make_unique<DefaultFigureStrategy>());
    registry_.register_strategy(std::make_unique<DefaultPhraseStrategy>());
    registry_.register_strategy(std::make_unique<DefaultPassageStrategy>());
  }

  // --- Top-level composition ---
  void compose(Piece& piece, const PieceTemplate& tmpl) {
    setup_piece_(piece, tmpl);

    for (const auto& partTmpl : tmpl.parts) {
      compose_part_(piece, tmpl, partTmpl);
    }
  }

  // --- Dispatchers called from strategies ---
  MelodicFigure realize_figure(const FigureTemplate& figTmpl,
                               StrategyContext& ctx) {
    Strategy* s = registry_.get("default_figure");
    return s->realize_figure(figTmpl, ctx);
  }

  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) {
    Strategy* s = registry_.get("default_phrase");
    return s->realize_phrase(phraseTmpl, ctx);
  }

  Passage realize_passage(const PassageTemplate& passTmpl,
                          StrategyContext& ctx) {
    Strategy* s = registry_.get("default_passage");
    return s->realize_passage(passTmpl, ctx);
  }

  // --- Exposed so DefaultFigureStrategy can look up motifs ---
  const std::unordered_map<std::string, MelodicFigure>& realized_seeds() const {
    return realizedSeeds_;
  }

private:
  Randomizer rng_;
  StrategyRegistry registry_;
  std::unordered_map<std::string, MelodicFigure> realizedSeeds_;

  // Moved from classical_composer.h:87-112
  void setup_piece_(Piece& piece, const PieceTemplate& tmpl) {
    // [IMPLEMENTER: paste body from classical_composer.h:87-112 verbatim.]
    //
    // At the end of the function the original code calls `realize_seeds(tmpl)`
    // — that becomes `realize_seeds_(tmpl)` (this class's private method).
  }

  // Moved from classical_composer.h:114-137
  void realize_seeds_(const PieceTemplate& tmpl) {
    // [IMPLEMENTER: paste body from classical_composer.h:114-137 verbatim,
    //  with these edits:
    //    (1) `rng.rng()`      -> `rng_.rng()`
    //    (2) `generate_figure(ft, s)` -> dispatch through the figure strategy:
    //
    //        StrategyContext ctx;
    //        ctx.scale = piece_.key.scale;   // <- but we don't have `piece_`
    //        ctx.composer = this;
    //        ctx.rng = &rng_;
    //        Strategy* s_ = registry_.get("default_figure");
    //        realizedSeeds_[seed.name] = s_->realize_figure(ft, ctx);
    //
    //  Wait — we don't have `piece` in scope here in the new signature. Two
    //  options:
    //    (a) pass piece by reference into realize_seeds_(piece, tmpl), which
    //        matches how setup_piece_ already handles it;
    //    (b) store a Piece* member during compose() and read it here.
    //
    //  Use (a): change the signature to `realize_seeds_(const Piece& piece,
    //  const PieceTemplate& tmpl)` and update the call from setup_piece_
    //  accordingly.
    //
    //  IMPORTANT: the pre-refactor `realize_seeds` called the instance
    //  `generate_figure` directly, bypassing `realize_figure`'s switch. For
    //  bit-identical output, the NEW code must also bypass the switch. Do
    //  NOT call `realize_figure` here — call DefaultFigureStrategy's
    //  public `generate_figure` directly (it was made public in Task 4
    //  specifically for this call site). Construct a local
    //  DefaultFigureStrategy instance (it's stateless and cheap) or fetch
    //  the registered instance from registry_ and downcast — the simpler
    //  choice is to just construct a local one:
    //
    //    DefaultFigureStrategy figStrat;
    //    realizedSeeds_[seed.name] = figStrat.generate_figure(ft, s);
    //
    //  This matches the pre-refactor call shape bit-for-bit.]
  }

  // Moved from classical_composer.h:586-633
  Passage generate_default_passage_(const Scale& scale) {
    // [IMPLEMENTER: paste body from classical_composer.h:586-633 verbatim,
    //  replacing the final `return realize_passage(passTmpl, dummyTmpl,
    //  scale);` with a dispatch through `this->realize_passage(passTmpl,
    //  ctx)` after constructing a StrategyContext with scale/rng/composer
    //  set. The `pieceTmpl` parameter the old code threaded through is
    //  replaced by `ctx.template_`; set `ctx.template_` to a pointer to a
    //  local empty PieceTemplate (the `dummyTmpl` in the old code) to
    //  preserve semantics.]
  }

  // Per-part composition. Moved from classical_composer.h compose(piece, tmpl,
  // partName, sectionName) at lines 49-77, with the dispatch redirected.
  void compose_part_(Piece& piece, const PieceTemplate& tmpl,
                     const PartTemplate& partTmpl) {
    for (auto& sec : piece.sections) {
      compose_passage_(piece, tmpl, partTmpl, sec.name);
    }
  }

  void compose_passage_(Piece& piece, const PieceTemplate& tmpl,
                        const PartTemplate& partTmpl,
                        const std::string& sectionName) {
    Part* part = nullptr;
    for (auto& p : piece.parts) if (p.name == partTmpl.name) { part = &p; break; }
    if (!part) return;

    auto passIt = partTmpl.passages.find(sectionName);
    Scale scale = piece.key.scale;

    Passage passage;
    if (passIt != partTmpl.passages.end()) {
      StrategyContext ctx;
      ctx.scale = scale;
      ctx.piece = &piece;
      ctx.template_ = &tmpl;
      ctx.composer = this;
      ctx.rng = &rng_;
      passage = realize_passage(passIt->second, ctx);
    } else {
      passage = generate_default_passage_(scale);
    }

    part->passages[sectionName] = std::move(passage);
  }
};

} // namespace mforce
```

Remember the note in Task 4 about the pending reference from `DefaultFigureStrategy::realize_figure` to `ctx.composer->realized_seeds()`. It resolves now that `composer.h` defines the accessor. Before committing, verify the include chain: `default_strategies.h` must include `composer.h` **in the cpp body of `realize_figure` only, not at the top of the header**, because `composer.h` includes `default_strategies.h`. This is a circular header dependency.

The clean break: `composer.h` includes `default_strategies.h` at the top (because its constructor instantiates the classes), and the bodies of `DefaultFigureStrategy::realize_figure` access `ctx.composer->realized_seeds()` through the forward declaration of `Composer` in `default_strategies.h` PLUS an out-of-line definition of `DefaultFigureStrategy::realize_figure` that lives AFTER `composer.h`'s full definition. In a header-only world, that means:

1. `default_strategies.h` only forward-declares `struct Composer;` at the top.
2. The `inline` definition of `DefaultFigureStrategy::realize_figure` in `default_strategies.h` accesses `ctx.composer->realized_seeds()` — but compilation of that body requires the full definition of `Composer`, so it must happen AFTER `composer.h` is included.
3. Achieve this by moving `DefaultFigureStrategy::realize_figure`'s definition OUT of `default_strategies.h` and INTO `composer.h`, below the `Composer` class.

So `composer.h` ends with:

```cpp
// Out-of-line definition of DefaultFigureStrategy::realize_figure. Lives here
// because the body needs the full definition of Composer to call
// ctx.composer->realized_seeds(). All other Default* member bodies can stay
// inline in default_strategies.h because they only need Composer's forward
// declaration + Composer::realize_figure / realize_phrase access, which are
// member-function calls through a pointer (resolved at instantiation time).
inline MelodicFigure DefaultFigureStrategy::realize_figure(
    const FigureTemplate& figTmpl, StrategyContext& ctx) {
  // [IMPLEMENTER: paste body from classical_composer.h:310-341 verbatim,
  //  with edits:
  //    (1) `rng.rng()`                -> `ctx.rng->rng()`
  //    (2) `realizedSeeds_.find(...)` -> `ctx.composer->realized_seeds().find(...)`
  //    (3) `realizedSeeds_.end()`     -> `ctx.composer->realized_seeds().end()`
  //    (4) `generate_figure(figTmpl, figSeed)` -> bound as the member call
  //        (since we're still inside DefaultFigureStrategy:: scope).
  //    (5) `generate_shaped_figure(...)`, `apply_transform(...)`, `builder.single_note(...)`
  //        same rule — member calls via `this->`.
  //  ]
}
```

Actually, `DefaultPhraseStrategy::realize_phrase` also calls `ctx.composer->realize_figure(...)` through a pointer — that's fine via forward declaration because member access through a pointer to a forward-declared type only requires the full definition at the call site where the method is *instantiated*, which for an inline function is when it's used. Since nothing outside `composer.h` uses these strategies, and `composer.h` includes the full definition after `default_strategies.h`, inline functions that only call `Composer` members through pointers resolve correctly. Only the call to `realized_seeds()` (which dereferences the pointer and calls a method that returns a reference) is the one that can stay fragile — but the same rule applies: it's fine as long as the instantiation point sees the full definition.

To cut through the noise: **move only `DefaultFigureStrategy::realize_figure`'s body into `composer.h` below the `Composer` definition, leave everything else `inline` inside `default_strategies.h`, and include `composer.h` from exactly one place (the old `classical_composer.h`).**

- [ ] **Step 2: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. `composer.h` is not included from anywhere yet, so the new code is parse-checked only by `composer.h` itself. If it fails, fix include order / forward-declarations.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/composer.h engine/include/mforce/music/default_strategies.h
git commit -m "refactor(composer): add Composer class and resolve strategy dispatch"
```

---

## Task 8: Switch ClassicalComposer over to Composer and delete the old private methods

**Files:**
- Modify: `engine/include/mforce/music/classical_composer.h`

This is the cutover. `ClassicalComposer` stops doing any of its own work — it owns a `Composer` and forwards the three `IComposer::compose(...)` overloads. Every private method is deleted.

- [ ] **Step 1: Rewrite `classical_composer.h`**

Complete replacement — the file shrinks from ~636 lines to roughly 60:

```cpp
#pragma once
#include "mforce/music/compose.h"
#include "mforce/music/composer.h"
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// ClassicalComposer — IComposer façade around the new Composer framework.
//
// Phase 1a: preserves the three-overload IComposer API the CLI uses at
// tools/mforce_cli/main.cpp:549 so no caller needs to change. All the
// composition logic lives in Composer + the three Default strategies.
// Phase 3+ will add more strategies; Phase 5 will rename Seed to Motif.
// ---------------------------------------------------------------------------
struct ClassicalComposer : IComposer {
  Composer inner;

  explicit ClassicalComposer(uint32_t seed = 0xC1A5'0001u) : inner(seed) {}

  std::string name() const override { return "Classical"; }

  void compose(Piece& piece, const PieceTemplate& tmpl) override {
    inner.compose(piece, tmpl);
  }

  void compose(Piece& piece, const PieceTemplate& tmpl,
               const std::string& partName) override {
    // The old code iterated `piece.sections` and called the 4-arg overload
    // per section. For Phase 1a we preserve that exact loop shape so the
    // CLI's behavior for the (piece, tmpl, partName) overload is unchanged.
    // The CLI only calls the 2-arg overload at tools/mforce_cli/main.cpp:549,
    // but IComposer requires all three. Implement the narrower overloads by
    // building a minimal per-part / per-passage call path through inner.
    //
    // [IMPLEMENTER: keep the same loop structure as the pre-refactor
    //  ClassicalComposer::compose(piece, tmpl, partName) at
    //  classical_composer.h:41-46. Because the new Composer doesn't expose
    //  a per-part compose(), call inner.compose(piece, tmpl) if partName is
    //  empty, else loop sections and invoke the 4-arg overload below.]
    for (auto& sec : piece.sections) {
      compose(piece, tmpl, partName, sec.name);
    }
  }

  void compose(Piece& piece, const PieceTemplate& tmpl,
               const std::string& partName,
               const std::string& sectionName) override {
    // Look up the part template and delegate to inner via a narrow helper.
    // Phase 1a preserves the old behavior: if the passage template exists,
    // realize it; otherwise fall through to Composer's default-passage
    // generator. The pre-refactor code did this inside
    // ClassicalComposer::compose(piece, tmpl, partName, sectionName) at
    // classical_composer.h:49-77.
    //
    // Implementation options:
    //   (a) Add a public Composer::compose_one_passage(piece, tmpl,
    //       partName, sectionName) method that wraps compose_passage_ and
    //       expose the CLI path through it.
    //   (b) Just call inner.compose(piece, tmpl) here and accept that the
    //       narrower overloads recompose the whole piece. This changes
    //       behavior (because the whole piece is recomposed every call),
    //       which is NOT acceptable in Phase 1a.
    //
    //  Use (a). Rename Composer::compose_passage_ to public
    //  Composer::compose_one_passage() and call it here.
    //
    // [IMPLEMENTER: add the public wrapper on Composer in a small follow-on
    //  edit to composer.h, then call it here with the four parameters.]
  }
};

} // namespace mforce
```

After writing this file, open `composer.h` and add the public wrapper referenced above:

```cpp
// Public helper for IComposer-compatible per-passage composition. Used by
// the ClassicalComposer façade; internal composition goes through the
// private compose_passage_ during compose().
void compose_one_passage(Piece& piece, const PieceTemplate& tmpl,
                         const std::string& partName,
                         const std::string& sectionName) {
  const PartTemplate* partTmpl = nullptr;
  for (auto& pt : tmpl.parts) if (pt.name == partName) { partTmpl = &pt; break; }
  if (!partTmpl) return;
  compose_passage_(piece, tmpl, *partTmpl, sectionName);
}
```

- [ ] **Step 2: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. This is the first task where `default_strategies.h` and `composer.h` are actually included in a compilation unit — all of the pasted bodies are now compiled. If there are syntax errors in pasted bodies, they show up here. Fix them by re-checking the source lines (`classical_composer.h:<start>-<end>` from Task 4 / 5 / 6) — never "fix" by changing what the body does, because any behavioral change breaks bit-identicality.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/music/classical_composer.h engine/include/mforce/music/composer.h
git commit -m "refactor(composer): ClassicalComposer becomes Composer façade"
```

---

## Task 9: Regression verification against the golden

**Files:**
- Read: `renders/template_golden_phase1a.sha256`

This is the single test that proves the refactor is correct. If the hash matches, the refactor is done. If it doesn't, something in the extraction drifted from the original control flow and must be found before this plan can be considered complete.

- [ ] **Step 1: Render the golden template with the refactored composer**

Run the exact same command as Task 1 Step 3. Use the same patch, same template, same output prefix. The output file is overwritten in place:

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/template_golden_phase1a 1 --template patches/template_golden_phase1a.json
```

Expected: `renders/template_golden_phase1a.wav` is (re)written. No crashes.

- [ ] **Step 2: Compute the new hash and compare to the committed golden**

```
sha256sum -c renders/template_golden_phase1a.sha256
```

Expected output: `renders/template_golden_phase1a.wav: OK`

- [ ] **Step 3: If the hash mismatches, diagnose**

If `sha256sum -c` reports mismatch, do NOT update the committed hash. The refactor has a bug. Likely causes, in order of likelihood:

1. **RNG call order changed.** Somewhere, an `rng.rng()` call in pre-refactor code resolved to a call that in post-refactor code either happens in a different order or against a different RNG instance. Walk through `DefaultPhraseStrategy::realize_phrase`, `DefaultFigureStrategy::realize_figure`, and `Composer::realize_seeds_` and verify every `rng` call happens against `&Composer::rng_` in the same order the old code hit `ClassicalComposer::rng`.
2. **Extra or missing `Randomizer` construction.** The old code constructed local `Randomizer` / `StepGenerator` / `FigureBuilder` instances from seeds at specific call sites. If one of those moved or was replaced with a shared instance, the seed chain diverges.
3. **Context cloning lost a field.** The `StrategyContext` clones in `DefaultPassageStrategy::realize_passage` and `DefaultPhraseStrategy::realize_phrase` must carry forward every field the downstream call reads. If any field gets zeroed on clone that was non-zero before, behavior drifts.
4. **Seed-map lookup order.** `realizedSeeds_` is a `std::unordered_map`. Iteration order is not deterministic, but the pre-refactor code never iterated it — it only did keyed `find()`. If you added an iteration (e.g. for logging), remove it.

Do NOT attempt to "close the gap" by touching the golden. The golden is the contract.

- [ ] **Step 4: When the hash matches, commit the verification run**

If the render produces a byte-identical WAV but you want to double-check by re-hashing:

```
sha256sum renders/template_golden_phase1a.wav > /tmp/new.sha256
diff renders/template_golden_phase1a.sha256 /tmp/new.sha256
```

Expected: empty diff.

No commit in this step — the render output is byte-identical to the already-committed one, so `git status` shows a clean working tree. If `git status` shows `renders/template_golden_phase1a.wav` as modified despite the hash matching, something is writing additional metadata (timestamps?) that isn't covered by the hash — investigate before continuing.

---

## Task 10: Final cleanup and sanity sweep

**Files:**
- Modify (if present): leftover references to removed `ClassicalComposer` private methods anywhere in the codebase.

- [ ] **Step 1: Search for orphan references**

Run these searches from repo root:

```
grep -rn "generate_default_passage\|realize_seeds\|generate_shaped_figure" engine tools
```

Expected: only hits are inside `composer.h`, `default_strategies.h`, and `docs/`. If anything else references these names, it was a dangling call site that Task 8 missed.

- [ ] **Step 2: Search for dead members of old ClassicalComposer**

```
grep -rn "ClassicalComposer::stepGen\|ClassicalComposer::builder\|ClassicalComposer::rng" engine tools
```

Expected: no matches. The old instance members are gone.

- [ ] **Step 3: Re-run the golden hash check one more time**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/template_golden_phase1a 1 --template patches/template_golden_phase1a.json
sha256sum -c renders/template_golden_phase1a.sha256
```

Expected: `renders/template_golden_phase1a.wav: OK`

- [ ] **Step 4: Commit the cleanup (only if anything was changed in Steps 1–2)**

If Steps 1–2 surfaced nothing to clean, there is no commit. If they did, stage the fixes and:

```
git commit -m "refactor(composer): remove dangling references to pre-refactor ClassicalComposer internals"
```

---

## Phase 1a exit criteria

All of the following must be true before declaring Phase 1a done:

1. `cmake --build build --target mforce_cli --config Release` succeeds from a clean tree.
2. `sha256sum -c renders/template_golden_phase1a.sha256` passes.
3. `engine/include/mforce/music/classical_composer.h` is approximately 60 lines and contains only the `ClassicalComposer` façade class.
4. `engine/include/mforce/music/composer.h`, `strategy.h`, `strategy_registry.h`, and `default_strategies.h` exist and compile.
5. The CLI invocation at `tools/mforce_cli/main.cpp:549` (`ClassicalComposer composer(tmpl.masterSeed)`) is unchanged and still works.
6. `grep -rn "realize_seeds_\|compose_passage_\|compose_one_passage" engine tools` shows hits only inside the new files (not inside `classical_composer.h`).
7. The commit log between the pre-Task-1 HEAD and the post-Task-10 HEAD tells a clean story: golden baseline → Strategy → Registry → DefaultFigure → DefaultPassage → DefaultPhrase → Composer → ClassicalComposer façade → verification.

Anything less than all seven and the plan is not done; stop and report status.

---

## What is explicitly NOT in this plan

Reminder for anyone tempted to "while I'm in here":

- No connector removal (Phase 1b).
- No cursor rename (Phase 1b).
- No `literal` figure source (Phase 1b).
- No template `strategy` field support (Phase 3+).
- No new phrase strategies (Phase 3).
- No new passage strategies (Phase 4).
- No Seed → Motif rename (Phase 5).
- No unit test framework (out of scope; we use the golden hash).
- No cleanup of the unused `apply_cadence_rhythm` helper (even though it's dead code; deleting it would require proving no path calls it, which is orthogonal to this refactor).
- No changes to `patches/template_shaped_test.json`, `patches/template_mary.json`, `patches/template_binary.json`, or any other committed template.
- No changes to `tools/mforce_cli/main.cpp`.
- No changes to `FigureBuilder`, `StepGenerator`, `PitchReader`, `Scale`, `Pitch`, or any other supporting type.

If any task in this plan turns out to require one of these, STOP and surface the conflict. It means the scope split between Phase 1a and Phase 1b was wrong and needs revision before continuing.
