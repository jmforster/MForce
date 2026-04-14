# Composer Strategy API Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete `StrategyContext`, shrink `realize_*` signatures to two arguments (`Locus`, `Template`), move RNG and registry to singletons, convert composer-brokered dispatch to direct strategy-to-strategy invocation, and replace threaded running-state with queries against realized `Piece` content — all while preserving byte-identical render output for the representative golden set.

**Architecture:** Six sequential stages, each independently revert-able. Stages 0–3 are additive/deletion-only and non-structural. Stages 4–6 change the interface. Every stage ends with a full build + representative render + WAV hash verification against the Stage 0 baseline. Any hash mismatch is a regression and must be investigated before proceeding.

**Tech Stack:** C++20 header-only design under `engine/include/mforce/music/`, CMake build (MSVC on Windows), rendered output as WAV under `renders/`, verification via `certutil -hashfile <wav> SHA256`. No unit-test framework present; verification is end-to-end WAV hash equality.

---

## File Structure

### New files to create

- `engine/include/mforce/music/locus.h` — `Locus` struct and inline helpers (`with_passage`, `with_phrase`, `with_figure`)
- `engine/include/mforce/music/piece_utils.h` — free functions `pitch_before(Locus)` and `passage_at(Locus)` (history queries `realized_before` etc. are Workstream 2, not implemented here)
- `engine/include/mforce/music/rng.h` — `mforce::rng` namespace with `next()`, `next_float()`, and `Scope` RAII guard
- `engine/include/mforce/music/passage_strategy.h` — `PassageStrategy` typed base (split out during Stage 4)
- `engine/include/mforce/music/phrase_strategy.h` — `PhraseStrategy` typed base
- `engine/include/mforce/music/figure_strategy.h` — `FigureStrategy` typed base

### Files to modify

- `engine/include/mforce/music/strategy.h` — becomes a compatibility shim after Stage 4, deleted in Stage 6
- `engine/include/mforce/music/strategy_registry.h` — gains typed `resolve_passage/phrase/figure` methods
- `engine/include/mforce/music/composer.h` — RNG scope install, out-of-line strategy bodies updated, cursor reads become queries
- `engine/include/mforce/music/default_strategies.h` — `DefaultFigureStrategy`, `DefaultPassageStrategy`, `DefaultPhraseStrategy` inherit from typed bases; bodies updated
- `engine/include/mforce/music/phrase_strategies.h` — `PeriodPhraseStrategy`, `SentencePhraseStrategy` inherit from `PhraseStrategy`; ctx reads replaced
- `engine/include/mforce/music/shape_strategies.h` — all 14 shape strategies inherit from `FigureStrategy`; `ctx.rng->rng()` → `mforce::rng::next()`
- `engine/include/mforce/music/alternating_figure_strategy.h` — inherits from `PassageStrategy`; dispatch converted
- `engine/include/mforce/music/classical_composer.h` — any remaining ctx-using code paths
- `tools/mforce_cli/main.cpp` — call-site updates if strategies are invoked directly from the CLI

### Files to delete

- `engine/include/mforce/music/strategy.h` — after Stage 6, nothing references `StrategyContext` or abstract `Strategy`

---

## Representative render set (used for golden verification at every stage)

These are the renders that must produce byte-identical WAVs after each stage. The `--compose` CLI takes an instrument patch FIRST and the composition template via `--template`:

```
mforce_cli.exe --compose <instrument.json> <out_prefix> 1 --template <template.json>
```

| Template | Command (relative to worktree root) | Output WAV |
|----------|-------------------------------------|------------|
| `test_k467_v1.json` | `--compose patches/PluckU.json renders/k467_v1 1 --template patches/test_k467_v1.json` | `renders/k467_v1_1.wav` |
| `test_k467_v2.json` | `--compose patches/PluckU.json renders/k467_v2 1 --template patches/test_k467_v2.json` | `renders/k467_v2_1.wav` |
| `test_k467_v3.json` | `--compose patches/PluckU.json renders/k467_v3 1 --template patches/test_k467_v3.json` | `renders/k467_v3_1.wav` |
| `test_k467_v4.json` | `--compose patches/PluckU.json renders/k467_v4 1 --template patches/test_k467_v4.json` | `renders/k467_v4_1.wav` |
| `test_k467_structural.json` | `--compose patches/PluckU.json renders/k467_structural 1 --template patches/test_k467_structural.json` | `renders/k467_structural_1.wav` |

**Worktree state note.** The worktree was populated during Task 0 with copies of the needed instrument patch (`PluckU.json`), K467 templates (`test_k467_*.json`), and `engine/third_party/{glfw,imgui,imnodes}` from the main checkout — these files are untracked in both checkouts but are now present in the worktree. Subsequent tasks should find them in-place. If a rebuild or re-render fails because a file is missing, re-copy from `C:/@dev/repos/mforce/` (main checkout) into the worktree.

---

## Task 0: Freeze Behavior Baseline

**Goal:** Record SHA-256 hashes of every WAV in the representative set so every subsequent stage can verify byte-exact preservation.

**Files:**
- Create: `docs/superpowers/plans/2026-04-14-baseline-hashes.txt` (baseline record)

- [ ] **Step 1: Build the current engine in Release mode**

Run:
```
cmake -S C:/@dev/repos/mforce -B C:/@dev/repos/mforce/build
cmake --build C:/@dev/repos/mforce/build --config Release
```

Expected: `build/tools/mforce_cli/Release/mforce_cli.exe` exists and builds without error.

- [ ] **Step 2: Render the full representative set**

Run each of these from the repo root (literal paths, no shell variables):

```
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe --compose C:/@dev/repos/mforce/patches/test_k467_v1.json C:/@dev/repos/mforce/renders/k467_v1 1
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe --compose C:/@dev/repos/mforce/patches/test_k467_v2.json C:/@dev/repos/mforce/renders/k467_v2 1
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe --compose C:/@dev/repos/mforce/patches/test_k467_v3.json C:/@dev/repos/mforce/renders/k467_v3 1
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe --compose C:/@dev/repos/mforce/patches/test_k467_v4.json C:/@dev/repos/mforce/renders/k467_v4 1
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe --compose C:/@dev/repos/mforce/patches/test_k467_structural.json C:/@dev/repos/mforce/renders/k467_structural 1
```

If one of these patch files does not exist, skip that entry and note it in the baseline file. Do not synthesize new patches — work from what currently renders.

Expected: Each command prints its WAV path and exits with code 0.

- [ ] **Step 3: Hash every output WAV and save to baseline file**

Run:
```
certutil -hashfile C:/@dev/repos/mforce/renders/k467_v1_1.wav SHA256
certutil -hashfile C:/@dev/repos/mforce/renders/k467_v2_1.wav SHA256
certutil -hashfile C:/@dev/repos/mforce/renders/k467_v3_1.wav SHA256
certutil -hashfile C:/@dev/repos/mforce/renders/k467_v4_1.wav SHA256
certutil -hashfile C:/@dev/repos/mforce/renders/k467_structural_1.wav SHA256
```

Write results into `docs/superpowers/plans/2026-04-14-baseline-hashes.txt` in the form:

```
# Composer Strategy API Refactor — Baseline WAV Hashes
# Recorded <YYYY-MM-DD>, commit <git rev-parse HEAD>

k467_v1_1.wav            <hash>
k467_v2_1.wav            <hash>
k467_v3_1.wav            <hash>
k467_v4_1.wav            <hash>
k467_structural_1.wav    <hash>
```

- [ ] **Step 4: Commit the baseline**

Run:
```
git -C C:/@dev/repos/mforce add docs/superpowers/plans/2026-04-14-baseline-hashes.txt
git -C C:/@dev/repos/mforce commit -m "docs: composer API refactor baseline hashes

Records SHA-256 of representative WAV renders prior to the refactor.
Every subsequent stage must reproduce these hashes bit-for-bit.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. No other files touched.

- [ ] **Step 5: Define the "verify" procedure**

Any task in this plan that says **"verify goldens match"** means: re-run all commands from Step 2, re-hash all outputs, compare to `2026-04-14-baseline-hashes.txt`. Every hash must match. If any hash differs, the task has a regression — investigate before committing. Do not overwrite the baseline.

---

## Task 1: Add `Locus` and Query Helpers (Stage 1 — additive only)

**Goal:** Introduce `Locus`, `pitch_before`, `realized_before` as additive code. Prove the query model produces the same cursor answer as the threaded model by replacing one `ctx.cursor` read with a `pitch_before(locus)` call at a single site.

**Files:**
- Create: `engine/include/mforce/music/locus.h`
- Create: `engine/include/mforce/music/piece_utils.h`
- Modify: `engine/include/mforce/music/default_strategies.h` (one ctx.cursor read site)

- [ ] **Step 1: Create `locus.h`**

File: `engine/include/mforce/music/locus.h`

```cpp
#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

namespace mforce {

struct Locus {
  Piece& piece;
  PieceTemplate& pieceTemplate;
  int sectionIdx;
  int partIdx;
  int passageIdx{-1};
  int phraseIdx{-1};
  int figureIdx{-1};

  Locus with_passage(int p) const { Locus l = *this; l.passageIdx = p; l.phraseIdx = -1; l.figureIdx = -1; return l; }
  Locus with_phrase (int p) const { Locus l = *this; l.phraseIdx  = p; l.figureIdx = -1; return l; }
  Locus with_figure (int f) const { Locus l = *this; l.figureIdx  = f; return l; }
};

} // namespace mforce
```

- [ ] **Step 2: Create `piece_utils.h`**

File: `engine/include/mforce/music/piece_utils.h`

```cpp
#pragma once
#include "mforce/music/locus.h"
#include "mforce/music/structure.h"
#include "mforce/music/pitch_reader.h"

namespace mforce::piece_utils {

// Resolve the Passage at this Locus, given the parallel Section/Part indices.
// Returns nullptr if structure isn't populated yet (e.g., mid-composition
// before the passage has been inserted).
inline const Passage* passage_at(const Locus& locus) {
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.piece.parts.size()) return nullptr;
  if (locus.sectionIdx < 0 || locus.sectionIdx >= (int)locus.piece.sections.size()) return nullptr;
  const auto& part = locus.piece.parts[locus.partIdx];
  const auto& sectionName = locus.piece.sections[locus.sectionIdx].name;
  auto it = part.passages.find(sectionName);
  return it == part.passages.end() ? nullptr : &it->second;
}

// Pitch immediately preceding this Locus position.
// For a fresh figure within a phrase: the cursor after the prior figure's
// last note. For the first figure of a phrase: the phrase's startingPitch.
// For the first figure of the first phrase of a passage: the passage's
// startingPitch (read from the template, since the passage itself may not
// exist yet at the moment of the query).
inline Pitch pitch_before(const Locus& locus) {
  const Passage* pass = passage_at(locus);
  const auto& passTmpl = locus.pieceTemplate
                          .parts[locus.partIdx]
                          .passages[locus.piece.sections[locus.sectionIdx].name];

  // If we are asking about the start of a passage: use passage template's startingPitch.
  if (locus.phraseIdx <= 0 && locus.figureIdx <= 0) {
    if (passTmpl.startingPitch) return *passTmpl.startingPitch;
  }

  // If we have a realized passage, walk it to find the pitch after the last
  // unit preceding (phraseIdx, figureIdx).
  if (pass != nullptr) {
    // Walk all phrases up to locus.phraseIdx (inclusive of previous phrases'
    // complete contents; for the current phrase, up to figureIdx-1).
    Pitch lastSeen = passTmpl.startingPitch ? *passTmpl.startingPitch : pass->phrases.empty() ? Pitch{} : pass->phrases[0].startingPitch;
    PitchReader pr(locus.piece.sections[locus.sectionIdx].scale);
    pr.set_pitch(lastSeen);
    int endPhrase = locus.phraseIdx < 0 ? (int)pass->phrases.size() : locus.phraseIdx;
    for (int pi = 0; pi < endPhrase; ++pi) {
      const Phrase& ph = pass->phrases[pi];
      pr.set_pitch(ph.startingPitch);
      for (const auto& fig : ph.figures) {
        for (const auto& unit : fig->units) {
          pr.advance(unit.step);
        }
      }
    }
    // Partial walk into the current phrase if figureIdx > 0
    if (locus.phraseIdx >= 0 && locus.phraseIdx < (int)pass->phrases.size() && locus.figureIdx > 0) {
      const Phrase& ph = pass->phrases[locus.phraseIdx];
      pr.set_pitch(ph.startingPitch);
      int endFig = std::min(locus.figureIdx, (int)ph.figures.size());
      for (int fi = 0; fi < endFig; ++fi) {
        for (const auto& unit : ph.figures[fi]->units) {
          pr.advance(unit.step);
        }
      }
    }
    return pr.get_pitch();
  }

  // Fallback: passage template's startingPitch if present
  if (passTmpl.startingPitch) return *passTmpl.startingPitch;
  return Pitch{};
}

} // namespace mforce::piece_utils
```

- [ ] **Step 3: Build and confirm new headers compile**

Run:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Expected: Build succeeds. The new headers are header-only and not yet referenced by any existing code, so compilation is additive.

- [ ] **Step 4: Wire `pitch_before` into ONE read site**

Open `engine/include/mforce/music/default_strategies.h`. Find `DefaultPhraseStrategy::apply_cadence`, which reads `phrase.startingPitch` directly (not via ctx). Do not touch that — it's not a ctx read.

Instead, find `phrase.startingPitch = ctx.cursor;` in `phrase_strategies.h:54` (`PeriodPhraseStrategy::realize_phrase`). Change this read site so it ALSO computes the pitch via `piece_utils::pitch_before(locus)` and asserts the two match. This proves query equivalence in the sequential case.

Since `realize_phrase(const PhraseTemplate&, StrategyContext&)` does not yet have a `Locus`, construct one on the spot from `ctx.piece`, `ctx.template_`, and the currently-executing section/part/passage/phrase indices. If those indices aren't directly available via ctx today, add a temporary out-of-band way to pass them (e.g., thread-local state set by the caller) — this is throwaway plumbing that Stage 6 will remove.

Simpler alternative if the index threading is too invasive: skip the assertion and just exercise `pitch_before` in a unit-test-style printout inside an `#ifdef MFORCE_LOCUS_SELFCHECK` block that's off by default. The goal of this step is to compile and exercise the new code path without changing behavior.

Add:
```cpp
// locus_selfcheck.h (internal)
// In whichever strategy picks the assertion point, call:
//   auto check = piece_utils::pitch_before(locus);
//   assert(check == ctx.cursor);  // sequential composition: must match
// guarded by `#ifdef MFORCE_LOCUS_SELFCHECK`
```

- [ ] **Step 5: Build + render + verify goldens match**

Run:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Run the full render set from Task 0 Step 2. Hash all outputs. Compare to the baseline file. All hashes must match — this stage is additive and must not change behavior.

- [ ] **Step 6: Commit Stage 1**

Run:
```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/locus.h engine/include/mforce/music/piece_utils.h engine/include/mforce/music/default_strategies.h engine/include/mforce/music/phrase_strategies.h
git -C C:/@dev/repos/mforce commit -m "feat(composer): add Locus and piece_utils query helpers

Introduces Locus (structural coordinate carrying Piece+PieceTemplate
refs + section/part/passage/phrase/figure indices) and piece_utils
free functions pitch_before() and passage_at() for query-based
cursor resolution.

Purely additive. Existing StrategyContext threading untouched. One
selfcheck assertion added under MFORCE_LOCUS_SELFCHECK verifying
query and threaded cursor agree in sequential composition.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. Goldens unchanged.

---

## Task 2: RNG Singleton (Stage 2)

**Goal:** Replace `ctx.rng->rng()` everywhere with `mforce::rng::next()`. The Composer installs a `rng::Scope(seed)` at the top of its entry points. The draw sequence and per-strategy call order must be preserved exactly.

**Files:**
- Create: `engine/include/mforce/music/rng.h`
- Modify: `engine/include/mforce/music/composer.h` (install Scope at compose entry)
- Modify: `engine/include/mforce/music/default_strategies.h`, `shape_strategies.h`, `phrase_strategies.h`, `alternating_figure_strategy.h`, `classical_composer.h` (replace reads)

- [ ] **Step 1: Create `rng.h`**

File: `engine/include/mforce/music/rng.h`

```cpp
#pragma once
#include "mforce/core/randomizer.h"
#include <cstdint>
#include <memory>

namespace mforce::rng {

namespace detail {
  inline thread_local Randomizer* current_ = nullptr;
}

// Returns the next uint32 from the current thread-local RNG. Undefined
// behavior if called outside a Scope. The intent is that Composer::compose()
// installs a Scope that lives for the duration of one composition.
inline uint32_t next() {
  return detail::current_->rng();
}

inline float next_float() {
  return detail::current_->value();
}

// RAII guard. Installs a Randomizer seeded with `seed` as the current RNG
// for the lifetime of the Scope; restores the previous RNG (usually nullptr)
// on destruction. Nest-safe — inner scope replaces outer, outer is restored.
class Scope {
public:
  explicit Scope(uint32_t seed) : rng_(seed), previous_(detail::current_) {
    detail::current_ = &rng_;
  }
  ~Scope() { detail::current_ = previous_; }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

  Randomizer* raw() { return &rng_; } // escape hatch for code that takes a Randomizer*

private:
  Randomizer rng_;
  Randomizer* previous_;
};

} // namespace mforce::rng
```

- [ ] **Step 2: Install `rng::Scope` in the Composer entry points**

Open `engine/include/mforce/music/composer.h`. Find where `rng_` is currently seeded / used as the master RNG for the composition. At the top of the `compose(...)` entry (the one reachable from `--compose` CLI), install an `rng::Scope` seeded with whatever seed the composer currently uses to seed `rng_`.

Also — and this is the important part — set `ctx.rng = scope.raw();` so every strategy that still reads `ctx.rng` gets the same underlying Randomizer. This preserves draw-sequence identity for the mechanical migration in Step 3.

Concretely (illustrative, adapt to actual Composer code):

```cpp
// Inside Composer::compose(...)
::mforce::rng::Scope scope(piece_seed);
ctx.rng = scope.raw();
// ... rest of compose body unchanged
```

Build:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Expected: Build succeeds. No behavior change yet; `ctx.rng` still points at the same Randomizer the Scope wraps.

- [ ] **Step 3: Render + verify goldens match (sanity check before find-replace)**

Run the full render set from Task 0 Step 2. All hashes must match the baseline — the Scope install should be behavior-invariant.

- [ ] **Step 4: Replace `ctx.rng->rng()` with `mforce::rng::next()` in shape strategies**

Open `engine/include/mforce/music/shape_strategies.h`. Find every occurrence of `ctx.rng->rng()` — there are 13, one at the top of each `ShapeXStrategy::realize_figure`.

For each: change
```cpp
uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
```
to
```cpp
uint32_t seed = ft.seed ? ft.seed : mforce::rng::next();
```

Add `#include "mforce/music/rng.h"` at the top of the file.

- [ ] **Step 5: Replace `ctx.rng->rng()` everywhere else**

Search for remaining reads:
```
grep -rn 'ctx\.rng->' C:/@dev/repos/mforce/engine/include/mforce/music/
```

Expected sites: `composer.h` (several, in out-of-line `realize_*` bodies), `default_strategies.h` (in inline bodies if any). Replace each `ctx.rng->rng()` with `mforce::rng::next()`. For any call passing `ctx.rng` as a `Randomizer*` to a function (if such a call exists), replace with `::mforce::rng::detail::current_` or — cleaner — pass the Randomizer by reference using `*ctx.rng` and let the callee change to use the singleton in a later stage. Prefer the minimal change: `ctx.rng->X()` → `::mforce::rng::detail::current_->X()`, or add helper free functions in `rng.h` as needed.

Add `#include "mforce/music/rng.h"` to every file that now references `mforce::rng`.

- [ ] **Step 6: Build + render + verify goldens match bit-for-bit**

This step matters more than others. The RNG draw sequence MUST be preserved. If any hash differs, do not commit — step back and find the reordered draw.

Run:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Then full render set. All hashes must match baseline.

- [ ] **Step 7: Commit Stage 2**

Run:
```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/rng.h engine/include/mforce/music/shape_strategies.h engine/include/mforce/music/default_strategies.h engine/include/mforce/music/phrase_strategies.h engine/include/mforce/music/alternating_figure_strategy.h engine/include/mforce/music/composer.h engine/include/mforce/music/classical_composer.h
git -C C:/@dev/repos/mforce commit -m "refactor(composer): RNG as thread-local singleton

Adds mforce::rng namespace with next()/next_float() and Scope RAII
guard. Composer installs Scope at compose entry, seeded identically
to the prior rng_ member. Every ctx.rng->rng() read site migrated
to mforce::rng::next(). ctx.rng still populated for anything not
yet migrated.

Draw sequence preserved bit-identically; goldens match.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. Goldens unchanged.

---

## Task 3: Delete Dead `StrategyContext` Fields (Stage 3)

**Goal:** Remove the six fields with zero reads: `totalBeats`, `piece`, `template_`, `params`, `keyContexts`, `sectionBeatOffset`. Pure cleanup, zero behavioral risk.

**Files:**
- Modify: `engine/include/mforce/music/strategy.h` (delete fields)
- Modify: `engine/include/mforce/music/composer.h` (delete writes to those fields)

- [ ] **Step 1: Remove the six fields from `StrategyContext`**

Edit `engine/include/mforce/music/strategy.h`. Delete these lines:

```cpp
  float totalBeats{0.0f};
  Piece* piece{nullptr};
  const PieceTemplate* template_{nullptr};
  nlohmann::json params;
  const std::vector<KeyContext>* keyContexts{nullptr};
  float sectionBeatOffset{0.0f};
```

The struct retains: `scale`, `cursor`, `composer`, `rng`, `chordProgression`.

Remove the now-unused `#include <nlohmann/json.hpp>` from the top of the file (the `params` field was the only user).

Remove the `Piece` and `PieceTemplate` forward decls if they're no longer used in the file.

- [ ] **Step 2: Remove writes to deleted fields in Composer**

Search:
```
grep -n 'ctx\.totalBeats\|ctx\.piece\s*=\|ctx\.template_\|ctx\.params\|ctx\.keyContexts\|ctx\.sectionBeatOffset' C:/@dev/repos/mforce/engine/include/mforce/music/composer.h
```

Delete every line that writes to one of these fields. Expected: lines near composer.h:295, 296, 330, 331, 336 — verify by re-reading the file. Do NOT touch writes to fields that survive (`scale`, `cursor`, `composer`, `rng`, `chordProgression`).

- [ ] **Step 3: Build**

Run:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Expected: Build succeeds. If it fails with "no member named X", that means a read survives somewhere the audit missed. Grep for the offending field name across `engine/include/` and remove or migrate that read before continuing.

- [ ] **Step 4: Render + verify goldens match**

Full render set. All hashes must match baseline.

- [ ] **Step 5: Commit Stage 3**

Run:
```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/strategy.h engine/include/mforce/music/composer.h
git -C C:/@dev/repos/mforce commit -m "refactor(composer): delete dead StrategyContext fields

Removes six StrategyContext fields with zero reads in the compose
path: totalBeats, piece, template_, params, keyContexts,
sectionBeatOffset. Removes the writes in Composer. Pure cleanup.

StrategyContext now: scale, cursor, composer, rng, chordProgression.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. Goldens unchanged.

---

## Task 4: Split `Strategy` Base into Three Typed Peers (Stage 4)

**Goal:** Introduce `PassageStrategy`, `PhraseStrategy`, `FigureStrategy` as peer types. Retrofit every existing strategy to inherit from the appropriate one. Delete the old unified abstract `Strategy` at the end. `StrategyRegistry` gains typed `resolve_*` methods.

**Files:**
- Create: `engine/include/mforce/music/passage_strategy.h`
- Create: `engine/include/mforce/music/phrase_strategy.h`
- Create: `engine/include/mforce/music/figure_strategy.h`
- Modify: `engine/include/mforce/music/strategy.h` (delete abstract Strategy base)
- Modify: `engine/include/mforce/music/strategy_registry.h` (typed resolves)
- Modify: All strategy files (inheritance change)

- [ ] **Step 1: Create the three typed base headers**

File: `engine/include/mforce/music/figure_strategy.h`

```cpp
#pragma once
#include "mforce/music/templates.h"
#include "mforce/music/figures.h"
#include <string>

namespace mforce {

struct StrategyContext;  // declared in strategy.h; still used in Stage 4

class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;
  virtual MelodicFigure realize_figure(const FigureTemplate&, StrategyContext&) = 0;
};

} // namespace mforce
```

File: `engine/include/mforce/music/phrase_strategy.h`

```cpp
#pragma once
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include <string>

namespace mforce {

struct StrategyContext;

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;
  virtual Phrase realize_phrase(const PhraseTemplate&, StrategyContext&) = 0;
};

} // namespace mforce
```

File: `engine/include/mforce/music/passage_strategy.h`

```cpp
#pragma once
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include <string>

namespace mforce {

struct StrategyContext;

class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;
  virtual Passage realize_passage(const PassageTemplate&, StrategyContext&) = 0;
};

} // namespace mforce
```

- [ ] **Step 2: Retrofit figure-level strategies**

In `default_strategies.h`:
- Change `class DefaultFigureStrategy : public Strategy` → `class DefaultFigureStrategy : public FigureStrategy`
- Delete `StrategyLevel level() const override { return StrategyLevel::Figure; }` — the typed base makes this redundant
- Keep `realize_figure(const FigureTemplate&, StrategyContext&) override;`
- Add `#include "mforce/music/figure_strategy.h"` at the top

In `shape_strategies.h`:
- For each `ShapeXStrategy`: change base to `public FigureStrategy`. Delete `level()` override.
- Add `#include "mforce/music/figure_strategy.h"` once at the top.

- [ ] **Step 3: Retrofit phrase-level strategies**

In `default_strategies.h`:
- Change `class DefaultPhraseStrategy : public Strategy` → `class DefaultPhraseStrategy : public PhraseStrategy`
- Delete the `level()` override.
- Add `#include "mforce/music/phrase_strategy.h"` at the top.

In `phrase_strategies.h`:
- `PeriodPhraseStrategy` and `SentencePhraseStrategy`: change base to `public PhraseStrategy`. Delete `level()` overrides.
- Add `#include "mforce/music/phrase_strategy.h"` at the top.

- [ ] **Step 4: Retrofit passage-level strategies**

In `default_strategies.h`:
- Change `class DefaultPassageStrategy : public Strategy` → `class DefaultPassageStrategy : public PassageStrategy`
- Delete `level()`.
- Add `#include "mforce/music/passage_strategy.h"` at the top.

In `alternating_figure_strategy.h`:
- Change `class AlternatingFigureStrategy : public Strategy` → `class AlternatingFigureStrategy : public PassageStrategy`
- Delete `level()`.
- Add `#include "mforce/music/passage_strategy.h"`.

- [ ] **Step 5: Update `StrategyRegistry` for typed resolves**

Rewrite `engine/include/mforce/music/strategy_registry.h`:

```cpp
#pragma once
#include "mforce/music/figure_strategy.h"
#include "mforce/music/phrase_strategy.h"
#include "mforce/music/passage_strategy.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

class StrategyRegistry {
public:
  void register_figure (std::unique_ptr<FigureStrategy> s)  { figures_[s->name()]  = std::move(s); }
  void register_phrase (std::unique_ptr<PhraseStrategy> s)  { phrases_[s->name()]  = std::move(s); }
  void register_passage(std::unique_ptr<PassageStrategy> s) { passages_[s->name()] = std::move(s); }

  FigureStrategy*  resolve_figure (const std::string& n) const { auto it = figures_.find(n);  return it == figures_.end()  ? nullptr : it->second.get(); }
  PhraseStrategy*  resolve_phrase (const std::string& n) const { auto it = phrases_.find(n);  return it == phrases_.end()  ? nullptr : it->second.get(); }
  PassageStrategy* resolve_passage(const std::string& n) const { auto it = passages_.find(n); return it == passages_.end() ? nullptr : it->second.get(); }

private:
  std::unordered_map<std::string, std::unique_ptr<FigureStrategy>>  figures_;
  std::unordered_map<std::string, std::unique_ptr<PhraseStrategy>>  phrases_;
  std::unordered_map<std::string, std::unique_ptr<PassageStrategy>> passages_;
};

} // namespace mforce
```

The old `register_strategy(unique_ptr<Strategy>)` and `get(name)` and `list_for_level(lvl)` methods are gone. Search for callers:

```
grep -rn 'register_strategy\|registry_.get\b\|list_for_level' C:/@dev/repos/mforce/engine/ C:/@dev/repos/mforce/tools/
```

Update every site. Every registration becomes typed; every `get(...)` becomes `resolve_figure/phrase/passage(...)` depending on context.

- [ ] **Step 6: Delete the abstract `Strategy` base, keep `StrategyContext`**

Edit `engine/include/mforce/music/strategy.h`. Delete the `class Strategy { ... };` block and the `enum class StrategyLevel`. Keep `StrategyContext`. The file now contains only `StrategyContext` and its forward decls.

If any file in the tree still includes `strategy.h` and references `Strategy` or `StrategyLevel`, either migrate that reference now or temporarily re-add a deprecated `using Strategy = ...` shim — but try to migrate directly.

- [ ] **Step 7: Build**

Run:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Expect several compile errors if the `registry.get(...)` migration missed any sites. Grep for `StrategyLevel` and `class Strategy` (not `class PassageStrategy` etc.); purge stragglers.

Build clean before continuing.

- [ ] **Step 8: Render + verify goldens match**

Full render set. All hashes must match baseline. The behavior is identical — only the type system changed.

- [ ] **Step 9: Commit Stage 4**

Run:
```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/figure_strategy.h engine/include/mforce/music/phrase_strategy.h engine/include/mforce/music/passage_strategy.h engine/include/mforce/music/strategy_registry.h engine/include/mforce/music/strategy.h engine/include/mforce/music/default_strategies.h engine/include/mforce/music/shape_strategies.h engine/include/mforce/music/phrase_strategies.h engine/include/mforce/music/alternating_figure_strategy.h engine/include/mforce/music/composer.h engine/include/mforce/music/classical_composer.h tools/mforce_cli/main.cpp
git -C C:/@dev/repos/mforce commit -m "refactor(composer): split Strategy into three typed peer bases

FigureStrategy, PhraseStrategy, PassageStrategy replace the single
abstract Strategy with throwing virtuals. StrategyRegistry gains
typed register_figure/phrase/passage and resolve_figure/phrase/passage
methods. All existing strategies retrofitted.

StrategyContext still present, unchanged. Dispatch still goes
through ctx.composer->realize_*. Goldens match.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. Goldens unchanged.

---

## Task 5: Direct Strategy-to-Strategy Dispatch (Stage 5)

**Goal:** Convert `DefaultPassageStrategy`, `DefaultPhraseStrategy`, and `AlternatingFigureStrategy` to invoke their children via `StrategyRegistry::instance().resolve_*(name)` directly, not through `ctx.composer->realize_*`. Composer's back-pointer role for dispatch ends.

**Files:**
- Modify: `engine/include/mforce/music/strategy_registry.h` (add `instance()` singleton)
- Modify: `engine/include/mforce/music/composer.h` (out-of-line strategy bodies)
- Modify: `engine/include/mforce/music/classical_composer.h` if it still has dispatch

- [ ] **Step 1: Make `StrategyRegistry` a process-wide singleton**

Add to `strategy_registry.h`:

```cpp
class StrategyRegistry {
public:
  // ... existing register_* and resolve_* ...

  static StrategyRegistry& instance() {
    static StrategyRegistry inst;
    return inst;
  }
};
```

Update the Composer's current registry-population code to populate `StrategyRegistry::instance()` instead of a member `registry_`. If `Composer::registry_` is still used elsewhere, keep it but have its `register_*` calls forward to `instance()` for the transition. Prefer to delete `Composer::registry_` entirely if nothing reads it.

- [ ] **Step 2: Convert `DefaultPassageStrategy` dispatch**

In `composer.h`, find `DefaultPassageStrategy::realize_passage`. The body currently calls:

```cpp
Phrase phrase = ctx.composer->realize_phrase(phraseTmpl, phraseCtx);
```

Replace with:

```cpp
PhraseStrategy* ps = StrategyRegistry::instance().resolve_phrase(
    phraseTmpl.strategy.empty() ? "default_phrase" : phraseTmpl.strategy);
if (!ps) throw std::runtime_error("PhraseStrategy not registered: " + phraseTmpl.strategy);
Phrase phrase = ps->realize_phrase(phraseTmpl, phraseCtx);
```

- [ ] **Step 3: Convert `DefaultPhraseStrategy` dispatch**

In `composer.h`, `DefaultPhraseStrategy::realize_phrase`. Replace:

```cpp
MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);
```

with:

```cpp
FigureStrategy* fs = StrategyRegistry::instance().resolve_figure(
    figTmpl.strategy.empty() ? "default_figure" : figTmpl.strategy);
if (!fs) throw std::runtime_error("FigureStrategy not registered: " + figTmpl.strategy);
MelodicFigure fig = fs->realize_figure(figTmpl, figCtx);
```

Watch out for the shape-strategy dispatch inside the default figure path (the block that looks up a shape strategy via `ctx.composer->registry_get_for_phase2(shapeName)`). That is also a registry lookup; convert it to `StrategyRegistry::instance().resolve_figure(shapeName)`.

- [ ] **Step 4: Convert `AlternatingFigureStrategy` dispatch**

In `composer.h`, `AlternatingFigureStrategy::realize_passage`. Same pattern as Step 3 for its `ctx.composer->realize_figure` call — resolve the figure strategy by name (use `"default_figure"` if the template's `strategy` field is empty) and call directly.

- [ ] **Step 5: Purge `ctx.composer->` reads**

Grep:
```
grep -rn 'ctx\.composer->' C:/@dev/repos/mforce/engine/include/mforce/music/
```

Expected remaining sites: motif lookups (`ctx.composer->find_rhythm_motif(...)`, `ctx.composer->find_contour_motif(...)`, `ctx.composer->realized_motifs()`). Convert these to reach through the template / motif pool directly. If `Composer::realized_motifs()` is a `std::map` member, expose it as a free accessor taking `const PieceTemplate&` or move it onto `PieceTemplate`. Prefer: add accessors to `PieceTemplate` that look up motifs from the template's own motif pool; remove the Composer-side cache if it duplicates the template's data.

If the motif pool relocation is more invasive than expected, you can DEFER this to Stage 6 and leave a single `ctx.composer->realized_motifs()` site for now — just note it in the commit message.

- [ ] **Step 6: Build + render + verify goldens match**

Full render set. All hashes must match baseline.

- [ ] **Step 7: Commit Stage 5**

Run:
```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/strategy_registry.h engine/include/mforce/music/composer.h engine/include/mforce/music/default_strategies.h engine/include/mforce/music/classical_composer.h
git -C C:/@dev/repos/mforce commit -m "refactor(composer): direct strategy-to-strategy dispatch

Passage and phrase strategies now invoke children via
StrategyRegistry::instance().resolve_*() and call directly, no
longer via ctx.composer->realize_*(). Registry is a process-wide
singleton. ctx.composer reads remain only for motif lookups
(deferred to Stage 6).

Goldens match.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. Goldens unchanged.

---

## Task 6: Two-Argument Signatures, Delete `StrategyContext` (Stage 6)

**Goal:** Final stage. Signatures become `realize_*(Locus, const Template&)`. Every ctx field read replaced by a derived-from-Locus query. `StrategyContext` deleted. `ctx.composer` motif accesses replaced with `locus.pieceTemplate.motifs...`.

**Files:**
- Modify: ALL strategy files (signature change)
- Modify: `engine/include/mforce/music/strategy.h` (deleted)
- Modify: `engine/include/mforce/music/composer.h` (Locus construction at top of compose)
- Modify: Motif pool accessors

- [ ] **Step 1: Change the typed base signatures**

Edit each of `figure_strategy.h`, `phrase_strategy.h`, `passage_strategy.h`:

```cpp
// figure_strategy.h
class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;
  virtual MelodicFigure realize_figure(Locus, const FigureTemplate&) = 0;
};
```

Repeat for `PhraseStrategy` (two args: `Locus, const PhraseTemplate&`) and `PassageStrategy` (`Locus, const PassageTemplate&`).

Add `#include "mforce/music/locus.h"` to each. Remove the forward decl of `StrategyContext`.

- [ ] **Step 2: Update every strategy body to the new signature**

For each strategy class (there are ~20: Default{Figure,Phrase,Passage}Strategy, 14 Shape strategies, Period/Sentence, AlternatingFigure, possibly others), change the `override` signature.

For the bodies: every `ctx.X` read must be replaced. Use this mapping:

| Was | Becomes |
|------|---------|
| `ctx.scale` | `locus.piece.sections[locus.sectionIdx].scale` |
| `ctx.cursor` | `piece_utils::pitch_before(locus)` |
| `ctx.chordProgression` | `locus.piece.sections[locus.sectionIdx].chordProgression ? &*locus.piece.sections[locus.sectionIdx].chordProgression : nullptr` (preserve nullable semantics) |
| `ctx.composer->realize_phrase(pt, ctx)` | `StrategyRegistry::instance().resolve_phrase(...)->realize_phrase(locus.with_phrase(i), pt)` |
| `ctx.composer->realize_figure(ft, ctx)` | `StrategyRegistry::instance().resolve_figure(...)->realize_figure(locus.with_figure(i), ft)` |
| `ctx.composer->realized_motifs()` | `locus.pieceTemplate.motifs` (after pool migration — see Step 3) |
| `ctx.composer->find_rhythm_motif(n)` | free function `motifs::find_rhythm(locus.pieceTemplate, n)` or equivalent accessor on PieceTemplate |
| `ctx.rng` (as `Randomizer*`) | `::mforce::rng::detail::current_` or refactor callee to use `mforce::rng::next()` directly |

For the `cursor`-mutation sites in passage/phrase strategies (e.g. `ctx.cursor = reader.get_pitch();` at end of phrase loop) — DELETE these lines. The cursor is no longer threaded state; subsequent queries compute it from realized content.

- [ ] **Step 3: Migrate the motif pool onto `PieceTemplate`**

In `templates.h`, `PieceTemplate` already has some notion of motifs (check `struct PieceTemplate` — it has a `motifs` field per the earlier spec doc). Ensure it holds both declarations and realized forms.

If `Composer` has a `realized_motifs_` map, move it (and any population logic) to `PieceTemplate::realizedMotifs` (or reuse `PieceTemplate::motifs` if that's where declarations already live). Update every prior `ctx.composer->find_rhythm_motif(n)` etc. to read from `pieceTemplate.motifs.find_rhythm(n)` or equivalent.

If touching this is too invasive for one commit, split into two sub-steps: (a) mechanical signature change with a temporary `locus.pieceTemplate_motifs_hack` free function that calls the old `Composer::find_rhythm_motif` via a global Composer pointer; (b) proper migration of the pool. The final result must be that no strategy reads anything via a `Composer*`.

- [ ] **Step 4: Construct `Locus` at Composer's entry and propagate**

In `composer.h`, `Composer::compose(...)`:

Build a root `Locus` for each (sectionIdx, partIdx) pair, with `passageIdx/phraseIdx/figureIdx` at -1. Pass that Locus to the resolved passage strategy's `realize_passage(locus, passTmpl)`.

```cpp
for (int si = 0; si < (int)piece.sections.size(); ++si) {
  for (int pi = 0; pi < (int)piece.parts.size(); ++pi) {
    Locus root{piece, tmpl, si, pi};
    // ... resolve passage strategy by name, get its PassageTemplate ...
    PassageStrategy* ps = StrategyRegistry::instance().resolve_passage(strategyName);
    Passage p = ps->realize_passage(root, passTmpl);
    piece.parts[pi].passages[piece.sections[si].name] = std::move(p);
  }
}
```

Each strategy internally builds child Locus values via `locus.with_passage(i)`, `with_phrase(i)`, `with_figure(i)` and passes them down.

- [ ] **Step 5: Delete `StrategyContext`**

Delete `engine/include/mforce/music/strategy.h` entirely. Remove it from any `#include` line that survived — expect several includes of `strategy.h` in files that now reference `figure_strategy.h` / `phrase_strategy.h` / `passage_strategy.h` instead.

- [ ] **Step 6: Build**

Run:
```
cmake --build C:/@dev/repos/mforce/build --config Release
```

Expect many compile errors on the first try. Each one will be either:
- An `#include "mforce/music/strategy.h"` that needs replacement with the appropriate typed base header.
- A `ctx.X` read that was missed in Step 2 — apply the mapping table.
- A call site expecting the old signature.

Fix until clean.

- [ ] **Step 7: Render + verify goldens match**

Full render set. All hashes must match baseline.

**If hashes differ:** the most likely culprit is a changed RNG draw order or a cursor computation that differs from the threaded value at a specific call site. Bisect:
1. Temporarily re-add a `#define MFORCE_LOCUS_SELFCHECK` at the top of one strategy file.
2. Re-enable the assertion from Task 1 Step 4 that compared `pitch_before(locus)` against `ctx.cursor`.
3. Since ctx.cursor no longer exists, you'll need to re-thread a shadow cursor locally to the strategy for the comparison. This is throwaway debugging code.
4. Find the first site where query and shadow differ. That's your bug.

- [ ] **Step 8: Commit Stage 6**

Run:
```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/figure_strategy.h engine/include/mforce/music/phrase_strategy.h engine/include/mforce/music/passage_strategy.h engine/include/mforce/music/default_strategies.h engine/include/mforce/music/shape_strategies.h engine/include/mforce/music/phrase_strategies.h engine/include/mforce/music/alternating_figure_strategy.h engine/include/mforce/music/composer.h engine/include/mforce/music/classical_composer.h engine/include/mforce/music/templates.h engine/include/mforce/music/piece_utils.h tools/mforce_cli/main.cpp
git -C C:/@dev/repos/mforce rm engine/include/mforce/music/strategy.h
git -C C:/@dev/repos/mforce commit -m "refactor(composer): two-arg realize signatures; delete StrategyContext

realize_figure/phrase/passage signatures shrink to (Locus, Template&).
StrategyContext deleted. All ctx.X reads replaced: scale/cursor/
chordProgression now derived from Locus+Piece queries; ctx.composer
recursion replaced in Stage 5; motif pool moved to PieceTemplate;
cursor mutations removed (query-based now).

Goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

Expected: Commit created. Goldens unchanged.

---

## Post-Refactor Cleanup (optional, can land separately)

- [ ] **Step 1: Remove MFORCE_LOCUS_SELFCHECK scaffolding**

Any selfcheck blocks from Task 1 Step 4 or Task 6 Step 7 that are no longer needed — delete them. Don't leave dead `#ifdef` noise in the tree.

- [ ] **Step 2: Audit `Composer` for post-refactor leftovers**

`Composer` no longer dispatches. Check that:
- No `Composer*` is passed as an argument anywhere except `compose()` entry
- No `registry_` member on Composer (deleted in Stage 5 or rewired to forward to singleton)
- No `realized_motifs_` member (moved to PieceTemplate in Stage 6)

Anything that survived these stages but doesn't need to be on Composer — delete.

- [ ] **Step 3: Commit cleanup**

```
git -C C:/@dev/repos/mforce commit -m "chore(composer): remove post-refactor scaffolding and leftovers

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| RNG draw reorder in Stage 2 | Medium | Stage 2 Step 6 hash check is explicit about bit-identical. Revert if off. |
| Motif pool migration breaks Stage 6 | Medium | Stage 6 Step 3 allows temporary hack with global pointer; split commit if needed. |
| `pitch_before(locus)` disagrees with threaded cursor at some exotic site (negative octave wrap, scale modulation across phrase) | Low | Task 1 Step 4 adds assertion under MFORCE_LOCUS_SELFCHECK to catch this early. Stage 6 Step 7 has bisection procedure. |
| Hidden `Strategy`/`StrategyLevel` references in headers outside this audit (e.g., `conductor.h`) | Low | Stage 4 Step 7 build will surface them. Purge before commit. |
| Goldens don't exist for all listed patches | Medium | Task 0 Step 2 notes to skip missing patches. Adjust plan scope at that point. |

## Success criteria

1. `engine/include/mforce/music/strategy.h` no longer exists.
2. No file in `engine/` or `tools/` references `StrategyContext`.
3. All `realize_*` signatures are `(Locus, const XTemplate&)`.
4. Task 0 baseline hashes match final renders bit-for-bit.
5. Code compiles clean under `/W4 /permissive-` (MSVC).
6. `Composer` no longer exposes a registry or a motif map as members; it is a thin entry point.
