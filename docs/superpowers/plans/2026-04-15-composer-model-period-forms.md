# Composer Composition Model — Period Forms and K467 (Plan B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Prerequisite:** Plan A (`docs/superpowers/plans/2026-04-15-composer-model-infrastructure.md`) must be fully merged to `main` before this plan runs. Plan B's tasks assume Plan A's infrastructure is in place (motif pool on PieceTemplate, `Locus` minus `Composer*`, `FigureConnector.leadStep`, Motif metadata, history queries, `ChordProgressionBuilder` rename).

**Goal:** Land the plan/compose two-phase strategy interface, introduce `PeriodVariant` / `PeriodSpec` / `PassageTemplate.periods[]`, implement `PeriodPassageStrategy`, and produce a K467 bars 1–12 render from a motif pool + period template that is recognizable as Mozart's opening. A new golden hash pins the output.

**Architecture:** Two-phase strategy interface at every level (`plan_*` returns a self-contained template, `compose_*` realizes it). Period structure lives only on templates; runtime stays `Passage → Phrase → Figure`. Variant logic inlined in one `PeriodPassageStrategy` (Option α). Work on branch `composer-model-period-forms` off latest `main` with Plan A merged.

**Tech Stack:** Same as Plan A — C++20 header-only, MSVC/CMake, golden-hash verification via `certutil -hashfile <wav> SHA256`.

---

## Representative render set (verification, same as Plan A)

Baseline hashes to preserve through Tasks 7–11 (pre-K467-golden stages):
```
k467_v1_1.wav            1e9754c9fe129d161ce6e2889333d773de4f39195caef71c5fc1de6d8b7f9354
k467_v2_1.wav            9ae4904c4999cd8797054c8f48f384f046a30831d12ee07ca55a64e1e8e85c1b
k467_v3_1.wav            418107958eae5c6e8bb9ffdc4202b7252c3e3bf97d544abfb55f054fa0649bec
k467_v4_1.wav            59927f4b5756fb5dc07d381ed4f3793e27027b64d0ab021f4e5c977648c9f0ab
k467_structural_1.wav    d7be258e0e6c5261708a434e476423a7bd05275752c495d9673a504d5b9c5ce2
```

Task 12 adds a NEW render (`renders/k467_period_1.wav`) and pins its hash as a new golden.

---

## File structure

### Files created

- `engine/include/mforce/music/period_passage_strategy.h` — the new PassageStrategy (Task 9)
- `patches/test_k467_period.json` — K467 bars 1–12 template (Task 12)
- `patches/k467_motifs.json` — K467 motif pool (Task 12, may be inlined in template JSON instead)

### Files modified

- `engine/include/mforce/music/strategy.h` — three typed bases gain `plan_*` methods with default-impl that returns seed unchanged (Task 7)
- `engine/include/mforce/music/default_strategies.h` — signatures rename `realize_*` → `compose_*` (Task 7)
- `engine/include/mforce/music/phrase_strategies.h` — same rename (Task 7)
- `engine/include/mforce/music/shape_strategies.h` — same rename (Task 7)
- `engine/include/mforce/music/alternating_figure_strategy.h` — same rename (Task 7)
- `engine/include/mforce/music/composer.h` — rename dispatchers; call `plan_*` before `compose_*`; out-of-line strategy bodies updated (Task 7); PeriodPassageStrategy registration (Task 9)
- `engine/include/mforce/music/strategy_registry.h` — optional update to support typed resolves for PassageStrategy (already present post-refactor; confirm)
- `engine/include/mforce/music/templates.h` — `PeriodVariant` enum, `PeriodSpec` struct, `PassageTemplate.periods[]` field (Task 8); `add_motif` / `add_derived_motif` write API on PieceTemplate (Task 7)
- `engine/include/mforce/music/templates_json.h` — PeriodVariant + PeriodSpec JSON (Task 8)
- `docs/superpowers/plans/2026-04-14-baseline-hashes.txt` — add k467_period_1 entry (Task 12)

---

## Task 7: Plan/compose two-phase strategy interface

**Files:**
- Modify: `engine/include/mforce/music/strategy.h` (add `plan_*` with default no-op)
- Modify: `engine/include/mforce/music/templates.h` (add `add_motif` / `add_derived_motif` write API)
- Modify: `engine/include/mforce/music/default_strategies.h`, `phrase_strategies.h`, `shape_strategies.h`, `alternating_figure_strategy.h` (rename `realize_*` → `compose_*` on every strategy)
- Modify: `engine/include/mforce/music/composer.h` (rename dispatcher methods; plan-then-compose pipeline; rename out-of-line strategy bodies)

- [ ] **Step 1: Create working branch**

```
git -C C:/@dev/repos/mforce checkout main
git -C C:/@dev/repos/mforce pull
git -C C:/@dev/repos/mforce checkout -b composer-model-period-forms
```

- [ ] **Step 2: Extend the three typed strategy bases with `plan_*`**

Edit `engine/include/mforce/music/strategy.h`:

```cpp
class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;

  // Default: return seed unchanged. Override for strategies that have
  // planning work (synthesizing motifs, filling unspecified fields).
  virtual FigureTemplate plan_figure(Locus, FigureTemplate seed) { return seed; }

  // Renamed from realize_figure.
  virtual MelodicFigure compose_figure(Locus, const FigureTemplate&) = 0;
};

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;

  virtual PhraseTemplate plan_phrase(Locus, PhraseTemplate seed) { return seed; }
  virtual Phrase compose_phrase(Locus, const PhraseTemplate&) = 0;
};

class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;

  virtual PassageTemplate plan_passage(Locus, PassageTemplate seed) { return seed; }
  virtual Passage compose_passage(Locus, const PassageTemplate&) = 0;
};
```

- [ ] **Step 3: Add `add_motif` / `add_derived_motif` write API to `PieceTemplate`**

Edit `engine/include/mforce/music/templates.h`. Add to the `PieceTemplate` struct (alongside the existing `find_*` read accessors from Plan A Task 1):

```cpp
// Write API — called from plan_* phase. Idempotent: repeated adds with
// the same name replace the existing entry (overwrite content + metadata).
// Also realizes the motif immediately (populates the appropriate
// realizedFigures/Rhythms/Contours entry) so the pool is consistent.
void add_motif(Motif motif);

// Convenience: synthesize a derived motif by applying transform to the
// parent's content. Stores with origin=Derived, derivedFrom=parent,
// transform=transform, transformParam=param. Returns the chosen name.
//
// Idempotent: if a motif already exists with (derivedFrom == parent,
// transform == t, transformParam == p), the existing name is returned
// and nothing is changed. Otherwise a new name is chosen: if explicitName
// is provided, use it; else auto-generate as parent + "'" (with
// additional primes if that name is taken).
std::string add_derived_motif(const std::string& parentName,
                              TransformOp transform,
                              int transformParam = 0,
                              std::optional<std::string> explicitName = std::nullopt);
```

Implement the methods inline. Example for `add_motif`:
```cpp
inline void PieceTemplate::add_motif(Motif motif) {
  // Replace existing declaration if name matches.
  auto it = std::find_if(motifs.begin(), motifs.end(),
                          [&](const Motif& m){ return m.name == motif.name; });
  if (it != motifs.end()) *it = std::move(motif);
  else motifs.push_back(std::move(motif));
  // Re-realize just this motif into the appropriate map.
  const Motif& m = motifs[it != motifs.end() ? (it - motifs.begin())
                                               : (motifs.size() - 1)];
  if (std::holds_alternative<MelodicFigure>(m.content)) {
    realizedFigures[m.name] = std::get<MelodicFigure>(m.content);
  } else if (std::holds_alternative<PulseSequence>(m.content)) {
    realizedRhythms[m.name] = std::get<PulseSequence>(m.content);
  } else if (std::holds_alternative<StepSequence>(m.content)) {
    realizedContours[m.name] = std::get<StepSequence>(m.content);
  }
}
```

Example for `add_derived_motif`:
```cpp
inline std::string PieceTemplate::add_derived_motif(
    const std::string& parentName, TransformOp transform,
    int transformParam, std::optional<std::string> explicitName) {
  // Idempotency check: existing derived motif with same (parent, transform, param)?
  for (const auto& m : motifs) {
    if (m.origin == MotifOrigin::Derived
        && m.derivedFrom && *m.derivedFrom == parentName
        && m.transform && *m.transform == transform
        && m.transformParam == transformParam) {
      return m.name;
    }
  }
  // Need to create. Resolve name.
  std::string chosenName;
  if (explicitName) chosenName = *explicitName;
  else {
    chosenName = parentName + "'";
    while (std::any_of(motifs.begin(), motifs.end(),
                        [&](const Motif& m){ return m.name == chosenName; })) {
      chosenName += "'";
    }
  }
  // Synthesize content by applying transform to the parent's content.
  // Uses apply_transform helper — declared in default_strategies.h or a new
  // utility. For this task, dispatch on TransformOp and handle the common
  // cases (Invert, Retrograde, Stretch, Compress, RhythmTail) inline here.
  Motif derived;
  derived.name = chosenName;
  derived.origin = MotifOrigin::Derived;
  derived.derivedFrom = parentName;
  derived.transform = transform;
  derived.transformParam = transformParam;
  // ... transform-content synthesis follows: look up parent motif's content,
  // apply transform, assign to derived.content. See Task 10 for the
  // TransformOp::RhythmTail implementation specifically.
  // Initial implementation: handle Invert / Reverse via FigureBuilder
  // (existing transforms); RhythmTail and novel transforms added in Task 10.
  add_motif(std::move(derived));
  return chosenName;
}
```

Note: the full `add_derived_motif` implementation (especially handling all TransformOp values) completes in Task 10. For Task 7, a skeletal version that handles Invert / Reverse is sufficient to let the plan/compose split compile; Task 10 extends it.

- [ ] **Step 4: Rename `realize_*` → `compose_*` across all existing strategies**

For each file listed below, rename the method `realize_X(...)` to `compose_X(...)` where X is figure/phrase/passage matching the strategy's level:

- `default_strategies.h`: `DefaultFigureStrategy::realize_figure` → `compose_figure`; `DefaultPhraseStrategy::realize_phrase` → `compose_phrase`; `DefaultPassageStrategy::realize_passage` → `compose_passage`. `apply_cadence` and `degree_in_scale` stay static, unchanged.
- `shape_strategies.h`: every `ShapeXStrategy::realize_figure` → `compose_figure` (14 classes).
- `phrase_strategies.h`: `PeriodPhraseStrategy::realize_phrase` → `compose_phrase`; `SentencePhraseStrategy::realize_phrase` → `compose_phrase`.
- `alternating_figure_strategy.h`: `AlternatingFigureStrategy::realize_passage` → `compose_passage`.
- `composer.h`: every out-of-line definition matching the above, plus Composer's dispatcher methods `realize_figure` / `realize_phrase` / `realize_passage` → `compose_figure` / `compose_phrase` / `compose_passage`.

Suggested grep to catch every site:
```
grep -rn 'realize_figure\|realize_phrase\|realize_passage' C:/@dev/repos/mforce/engine/ C:/@dev/repos/mforce/tools/
```

- [ ] **Step 5: Update `Composer`'s entry path to run `plan_*` then `compose_*`**

Edit `engine/include/mforce/music/composer.h`. In the method that dispatches at the top level (e.g., `compose_passage_` or whatever `setup_piece_`'s downstream dispatch path calls), change:

```cpp
// Before:
Passage passage = realize_passage(locus, passTmpl);
```

to:

```cpp
// After:
PassageStrategy* s = StrategyRegistry::instance().resolve_passage(
    passTmpl.strategy.empty() ? "default_passage" : passTmpl.strategy);

// Plan phase: strategy returns completed template (may mutate pool).
PassageTemplate planned = s->plan_passage(locus, passTmpl);
// Write the planned template back to PieceTemplate so re-runs and
// lock semantics see the pinned values.
locus.pieceTemplate->parts[locus.partIdx]
    .passages[locus.piece->sections[locus.sectionIdx].name] = planned;
// (Note: the passages field on PartTemplate is already a map keyed by
// section name; this writes through that key.)

// Compose phase: strategy realizes the completed template.
Passage passage = s->compose_passage(locus, planned);
```

Similar for phrase and figure dispatchers if they exist at the Composer level (if the pattern only lives at Passage level today and phrase/figure dispatch happens inside strategies directly, those remain unchanged and use `compose_*` directly).

- [ ] **Step 6: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

Expected errors to find and fix:
- Any remaining `realize_figure` / `realize_phrase` / `realize_passage` (grep-sweep from Step 4 should have caught, Step 6 build confirms)
- If `add_derived_motif` has a reference to a not-yet-declared helper, stub it with a `throw std::logic_error("add_derived_motif not implemented for this TransformOp yet")` — acceptable because no caller exercises it in Task 7.

Every existing strategy has default no-op `plan_*`, so behavior through the whole pipeline is unchanged. Hashes must match the baseline bit-identically.

- [ ] **Step 7: Commit Task 7**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "refactor(composer): plan/compose two-phase strategy interface

Every strategy base (FigureStrategy / PhraseStrategy / PassageStrategy)
gains a plan_* method with default no-op returning seed unchanged.
Existing realize_figure / realize_phrase / realize_passage methods
rename to compose_figure / compose_phrase / compose_passage across
every strategy and the Composer dispatchers.

Composer's top-level entry path now calls plan_* first (allowing the
strategy to fill in template fields and mutate the pool), stores the
resulting template in PieceTemplate (for persistence / lock semantics),
then calls compose_* on the resulting self-contained template.

PieceTemplate gains add_motif (idempotent by name) and a skeletal
add_derived_motif (full TransformOp coverage in Task 10).

All existing strategies have default no-op plan_*, so their behavior
is unchanged. Goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Add `PeriodVariant`, `PeriodSpec`, `PassageTemplate.periods[]`

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (enum, struct, field)
- Modify: `engine/include/mforce/music/templates_json.h` (JSON round-trip)

- [ ] **Step 1: Add `PeriodVariant` enum**

Edit `engine/include/mforce/music/templates.h`. Add near the other enums:

```cpp
enum class PeriodVariant {
  Parallel,     // consequent motifs = antecedent motifs (verbatim)
  Modified,     // consequent motifs derived via transform from antecedent
  Contrasting,  // consequent motifs independent
};
```

- [ ] **Step 2: Add `PeriodSpec` struct**

```cpp
struct PeriodSpec {
  PeriodVariant variant{PeriodVariant::Parallel};
  float bars{4.0f};

  PhraseTemplate antecedent;   // cadenceType typically HC
  PhraseTemplate consequent;   // cadenceType typically IAC or PAC

  // For Modified variant: how the consequent's basicIdea is derived from
  // the antecedent's. Ignored for Parallel and Contrasting.
  std::optional<TransformOp> consequentTransform;
  int consequentTransformParam{0};

  // Optional motif-pool name for a leading connective figure that
  // bridges from the prior period's final cadence into this period's
  // antecedent. The first PeriodSpec in a list typically has no
  // leadingConnective (passage starts cleanly); subsequent periods may.
  std::optional<std::string> leadingConnective;
};
```

- [ ] **Step 3: Add `periods[]` field to `PassageTemplate`**

```cpp
struct PassageTemplate {
  // ... existing fields (strategy, phrases[], startingPitch, etc.) ...

  // NEW — optional. Consumed only by period-aware strategies (primarily
  // PeriodPassageStrategy). Other passage strategies ignore this field.
  std::vector<PeriodSpec> periods;
};
```

- [ ] **Step 4: JSON enum helper for `PeriodVariant`**

In `templates_json.h`:
```cpp
NLOHMANN_JSON_SERIALIZE_ENUM(PeriodVariant, {
  {PeriodVariant::Parallel,    "Parallel"},
  {PeriodVariant::Modified,    "Modified"},
  {PeriodVariant::Contrasting, "Contrasting"},
})
```

- [ ] **Step 5: `PeriodSpec` JSON round-trip**

```cpp
inline void to_json(nlohmann::json& j, const PeriodSpec& ps) {
  j = nlohmann::json::object();
  j["variant"] = ps.variant;
  j["bars"] = ps.bars;
  j["antecedent"] = ps.antecedent;
  j["consequent"] = ps.consequent;
  if (ps.consequentTransform) j["consequentTransform"] = *ps.consequentTransform;
  if (ps.consequentTransformParam != 0) j["consequentTransformParam"] = ps.consequentTransformParam;
  if (ps.leadingConnective) j["leadingConnective"] = *ps.leadingConnective;
}

inline void from_json(const nlohmann::json& j, PeriodSpec& ps) {
  ps = PeriodSpec{};
  ps.variant = j.value("variant", PeriodVariant::Parallel);
  ps.bars = j.value("bars", 4.0f);
  j.at("antecedent").get_to(ps.antecedent);
  j.at("consequent").get_to(ps.consequent);
  if (j.contains("consequentTransform") && !j.at("consequentTransform").is_null()) {
    ps.consequentTransform = j.at("consequentTransform").get<TransformOp>();
  }
  ps.consequentTransformParam = j.value("consequentTransformParam", 0);
  if (j.contains("leadingConnective") && !j.at("leadingConnective").is_null()) {
    ps.leadingConnective = j.at("leadingConnective").get<std::string>();
  }
}
```

- [ ] **Step 6: Extend `PassageTemplate` JSON to emit/read `periods[]`**

In existing `to_json(PassageTemplate)` / `from_json(PassageTemplate)`:

```cpp
// to_json additions:
if (!pt.periods.empty()) j["periods"] = pt.periods;

// from_json additions:
if (j.contains("periods") && j.at("periods").is_array()) {
  pt.periods = j.at("periods").get<std::vector<PeriodSpec>>();
}
```

- [ ] **Step 7: Build and verify goldens**

No consumer yet. Goldens trivially unchanged.

- [ ] **Step 8: Commit Task 8**

```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git -C C:/@dev/repos/mforce commit -m "feat(composer): PeriodVariant + PeriodSpec + PassageTemplate.periods[]

Adds PeriodVariant enum (Parallel / Modified / Contrasting) and
PeriodSpec struct describing one period:
  - variant
  - bars
  - antecedent + consequent (PhraseTemplates)
  - optional consequentTransform + consequentTransformParam (for Modified)
  - optional leadingConnective (motif-pool name)

PassageTemplate gains optional periods[] field. Other passage strategies
ignore it; period-aware strategy consumes it in Task 9.

JSON round-trip for both new types; existing patches without periods
load unchanged. Goldens trivially unchanged.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: `PeriodPassageStrategy` skeleton (Parallel variant only)

**Files:**
- Create: `engine/include/mforce/music/period_passage_strategy.h`
- Modify: `engine/include/mforce/music/composer.h` (register the new strategy)

- [ ] **Step 1: Create `period_passage_strategy.h`**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/strategy_registry.h"

namespace mforce {

// PeriodPassageStrategy — Option α (variant logic inlined).
//
// plan_passage: reads seed.periods[], resolves each period's variant into
// concrete PhraseTemplates for antecedent + consequent, flattens into
// seed.phrases[] in order. May mutate pieceTemplate's motif pool via
// add_derived_motif for Modified-variant consequents.
//
// compose_passage: dispatches each phrase via the registered PhraseStrategy
// named on phraseTmpl.strategy (falling back to default_phrase), then
// concatenates the phrases into a Passage.
class PeriodPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "period_passage"; }

  PassageTemplate plan_passage(Locus locus, PassageTemplate seed) override;
  Passage compose_passage(Locus locus, const PassageTemplate& pt) override;
};

// --- Out-of-line definitions ---

inline PassageTemplate PeriodPassageStrategy::plan_passage(
    Locus locus, PassageTemplate seed) {

  // For each PeriodSpec, resolve (antecedent, consequent) PhraseTemplates
  // and append to seed.phrases. Also handle leading-connective motif if
  // present (pi > 0 and p.leadingConnective set): prepend a one-figure
  // Phrase referencing the connective motif.
  for (int pi = 0; pi < (int)seed.periods.size(); ++pi) {
    PeriodSpec& p = seed.periods[pi];

    // Leading connective (only between periods, not before period 0).
    if (pi > 0 && p.leadingConnective) {
      PhraseTemplate connPhrase;
      connPhrase.name = "connective_" + std::to_string(pi);
      FigureTemplate ft;
      ft.source = FigureSource::Reference;
      ft.motifName = *p.leadingConnective;
      connPhrase.figures.push_back(ft);
      // Default connector (zeros).
      FigureConnector fc;
      connPhrase.connectors.push_back(fc);
      seed.phrases.push_back(connPhrase);
    }

    // TASK 9 implements Parallel only. Modified + Contrasting land in Task 10.
    if (p.variant == PeriodVariant::Parallel) {
      // Consequent starts as a copy of antecedent (same motifs, same connectors),
      // with cadence fields replaced by the consequent's authored values.
      PhraseTemplate ante = p.antecedent;
      PhraseTemplate consq = p.antecedent;   // start from antecedent
      consq.cadenceType   = p.consequent.cadenceType;
      consq.cadenceTarget = p.consequent.cadenceTarget;
      // If the authored consequent has non-empty figures, use those as
      // overrides (e.g., when the user wants cadence-specific tail figures).
      if (!p.consequent.figures.empty()) {
        consq.figures = p.consequent.figures;
        consq.connectors = p.consequent.connectors;
      }
      seed.phrases.push_back(std::move(ante));
      seed.phrases.push_back(std::move(consq));
    } else {
      // Modified and Contrasting not yet implemented (Task 10).
      // Fall back to passing antecedent + consequent through verbatim.
      seed.phrases.push_back(p.antecedent);
      seed.phrases.push_back(p.consequent);
    }
  }

  return seed;
}

inline Passage PeriodPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {
  Passage passage;
  for (int i = 0; i < (int)pt.phrases.size(); ++i) {
    const PhraseTemplate& phraseTmpl = pt.phrases[i];
    if (phraseTmpl.locked) continue;

    std::string pn = phraseTmpl.strategy.empty()
                     ? std::string("default_phrase")
                     : phraseTmpl.strategy;
    PhraseStrategy* ps = StrategyRegistry::instance().resolve_phrase(pn);
    if (!ps) {
      std::cerr << "Unknown phrase strategy '" << pn
                << "', falling back to default_phrase\n";
      ps = StrategyRegistry::instance().resolve_phrase("default_phrase");
    }
    Phrase phrase = ps->compose_phrase(locus.with_phrase(i), phraseTmpl);
    passage.add_phrase(std::move(phrase));
  }
  return passage;
}

} // namespace mforce
```

- [ ] **Step 2: Register in Composer**

Edit `engine/include/mforce/music/composer.h`. Add the include near the other strategy headers:
```cpp
#include "mforce/music/period_passage_strategy.h"
```

In Composer's constructor, alongside `reg.register_passage(std::make_unique<DefaultPassageStrategy>())` and `reg.register_passage(std::make_unique<AlternatingFigureStrategy>())`, add:
```cpp
reg.register_passage(std::make_unique<PeriodPassageStrategy>());
```

- [ ] **Step 3: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

No existing template uses `strategy: "period_passage"`, so existing renders don't hit this code path. Goldens match bit-identically.

- [ ] **Step 4: Commit Task 9**

```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/period_passage_strategy.h engine/include/mforce/music/composer.h
git -C C:/@dev/repos/mforce commit -m "feat(composer): PeriodPassageStrategy skeleton (Parallel variant)

Adds PeriodPassageStrategy implementing the plan/compose two-phase
pattern. plan_passage reads seed.periods[], expands each into antecedent
+ consequent PhraseTemplates, and flattens into seed.phrases[]. For
this task only the Parallel variant is implemented; Modified and
Contrasting land in Task 10. Leading-connective motifs (between periods)
are realized as a single-figure Phrase prepended between periods.

compose_passage dispatches each flattened phrase via its registered
PhraseStrategy (default_phrase fallback) and collects the results
into a Passage.

Registered under name 'period_passage'. No existing template uses
this strategy; goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Modified and Contrasting variants + full `add_derived_motif`

**Files:**
- Modify: `engine/include/mforce/music/period_passage_strategy.h` (implement Modified, Contrasting cases)
- Modify: `engine/include/mforce/music/templates.h` (complete `add_derived_motif` implementation for TransformOp coverage)
- Modify: `engine/include/mforce/music/default_strategies.h` (extend `apply_transform` if needed for RhythmTail and any new TransformOp values)

- [ ] **Step 1: Complete `add_derived_motif` TransformOp coverage**

Edit `engine/include/mforce/music/templates.h`. In the `add_derived_motif` implementation, replace the stub content-synthesis section with a dispatch on `transform`:

```cpp
// Look up parent motif's content.
const Motif* parent = nullptr;
for (const auto& m : motifs) {
  if (m.name == parentName) { parent = &m; break; }
}
if (!parent) throw std::runtime_error(
    "add_derived_motif: parent not in motif pool: " + parentName);

// Synthesize derived content per transform.
switch (transform) {
  case TransformOp::Invert: {
    if (std::holds_alternative<MelodicFigure>(parent->content)) {
      FigureBuilder fb(parent->generationSeed + 1);
      derived.content = fb.invert(std::get<MelodicFigure>(parent->content));
    } else {
      throw std::runtime_error("Invert requires MelodicFigure parent");
    }
    break;
  }
  case TransformOp::Reverse: {
    if (std::holds_alternative<MelodicFigure>(parent->content)) {
      FigureBuilder fb(parent->generationSeed + 1);
      derived.content = fb.reverse(std::get<MelodicFigure>(parent->content));
    } else {
      throw std::runtime_error("Reverse requires MelodicFigure parent");
    }
    break;
  }
  case TransformOp::RhythmTail: {
    // Produce a PulseSequence from the parent's rhythm, skipping the
    // first `transformParam` pulses.
    const MelodicFigure* mf = std::get_if<MelodicFigure>(&parent->content);
    if (!mf) throw std::runtime_error("RhythmTail requires MelodicFigure parent");
    PulseSequence ps;
    int skip = transformParam;
    for (int i = skip; i < (int)mf->units.size(); ++i) {
      ps.pulses.push_back(mf->units[i].duration);
    }
    derived.content = std::move(ps);
    break;
  }
  case TransformOp::VarySteps: {
    const MelodicFigure* mf = std::get_if<MelodicFigure>(&parent->content);
    if (!mf) throw std::runtime_error("VarySteps requires MelodicFigure parent");
    FigureBuilder fb(parent->generationSeed + 2);
    MelodicFigure copy = *mf;
    derived.content = fb.vary_steps(copy, std::max(1, transformParam));
    break;
  }
  case TransformOp::VaryRhythm: {
    const MelodicFigure* mf = std::get_if<MelodicFigure>(&parent->content);
    if (!mf) throw std::runtime_error("VaryRhythm requires MelodicFigure parent");
    FigureBuilder fb(parent->generationSeed + 3);
    derived.content = fb.vary_rhythm(*mf);
    break;
  }
  // Extend for Stretch, Compress, etc. as needed.
  default:
    throw std::runtime_error("add_derived_motif: TransformOp not supported");
}
```

- [ ] **Step 2: Implement `Modified` variant in `PeriodPassageStrategy::plan_passage`**

Edit `period_passage_strategy.h`. Replace the "Modified and Contrasting not yet implemented" fallback with real logic:

```cpp
if (p.variant == PeriodVariant::Modified) {
  PhraseTemplate ante = p.antecedent;
  PhraseTemplate consq = p.antecedent;  // start from antecedent

  // For each FigureTemplate in the consequent-being-built that references
  // a motif, if consequentTransform is set, replace the motif reference
  // with a derived-motif reference.
  if (p.consequentTransform) {
    for (auto& ft : consq.figures) {
      if (ft.source == FigureSource::Reference && !ft.motifName.empty()) {
        std::string derivedName = locus.pieceTemplate->add_derived_motif(
            ft.motifName,
            *p.consequentTransform,
            p.consequentTransformParam);
        ft.motifName = derivedName;
      }
    }
  }

  // Apply cadence override from authored consequent.
  consq.cadenceType   = p.consequent.cadenceType;
  consq.cadenceTarget = p.consequent.cadenceTarget;
  // If authored consequent has explicit figure overrides, use them
  // (same as Parallel).
  if (!p.consequent.figures.empty()) {
    consq.figures = p.consequent.figures;
    consq.connectors = p.consequent.connectors;
  }

  seed.phrases.push_back(std::move(ante));
  seed.phrases.push_back(std::move(consq));
}
else if (p.variant == PeriodVariant::Contrasting) {
  // Consequent uses its own authored figures entirely. Antecedent and
  // consequent are independent.
  seed.phrases.push_back(p.antecedent);
  seed.phrases.push_back(p.consequent);
}
```

- [ ] **Step 3: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

No existing template uses PeriodPassageStrategy; goldens match bit-identically.

- [ ] **Step 4: Commit Task 10**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "feat(composer): PeriodPassageStrategy Modified + Contrasting variants

PeriodPassageStrategy.plan_passage now handles all three variants:
- Parallel (Task 9): consequent motifs = antecedent verbatim
- Modified (this task): each motif reference in the consequent is
  swapped for an auto-derived variant via
  pieceTemplate->add_derived_motif(motif, consequentTransform, param).
  New derived motifs accrete in the pool.
- Contrasting (this task): antecedent and consequent are independent;
  both pass through verbatim.

add_derived_motif's content-synthesis covers Invert, Reverse,
RhythmTail, VarySteps, VaryRhythm. Other TransformOps throw with a
clear error message (extend on demand).

Goldens match bit-identically (no existing template uses this strategy).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Multi-figure cadential tail + harmony resolution integration

**Files:**
- Modify: `engine/include/mforce/music/period_passage_strategy.h` (plan_passage harmony resolution; cadential-tail adjustment hook)
- Modify: `engine/include/mforce/music/composer.h` (pre-dispatch harmony resolution for Section)

- [ ] **Step 1: Harmony resolution in Composer's dispatch**

Edit `engine/include/mforce/music/composer.h`. Before any PassageStrategy runs for a Section, ensure `section.chordProgression` is populated. Add this logic in the setup path (around `setup_piece_` / `compose_passage_`):

```cpp
// Resolve Section.chordProgression before dispatching any PassageStrategy
// for this section.
Section& section = piece.sections[sectionIdx];
if (!section.chordProgression) {
  if (!sectionTmpl.progressionName.empty()) {
    // Complex mode: named progression via ChordProgressionBuilder.
    section.chordProgression = ChordProgressionBuilder::build(
        sectionTmpl.progressionName, sectionTmpl.beats);
  }
  // else: simple mode — leave empty. A PassageStrategy.plan_* may fill
  // it during its own plan pass. If multiple Parts are present, the
  // first strategy to run wins (WARN at runtime if section has > 1 Part
  // and no progression is set).
  else if (section.parts_using_section_count > 1) {
    // or equivalent multi-part detection using piece.parts
    std::cerr << "WARN: Section '" << section.name
              << "' has multiple Parts but no chord progression; "
              << "first PassageStrategy to run will determine harmony.\n";
  }
}
```

(The exact detection for multi-Part in a Section depends on current code — use whatever the existing path does to count Parts with a Passage in that Section.)

- [ ] **Step 2: Derive chord progression in `PeriodPassageStrategy::plan_passage` when unset**

In `period_passage_strategy.h`, at the top of `plan_passage` (before processing periods), add:

```cpp
// Simple-mode harmony resolution: if Section has no chordProgression,
// derive one from the periods' cadence pattern.
Section& section = const_cast<Section&>(locus.piece->sections[locus.sectionIdx]);
if (!section.chordProgression) {
  ChordProgression cp;
  // Minimal derivation: one chord per bar, using HC → V and PAC/IAC → I
  // at phrase boundaries, I otherwise. For K467 bars 1-12 this yields
  // I-V | I-I-V | ... — trivially derivable.
  //
  // For a more nuanced derivation, extend with:
  //  - V7 for bars leading into cadences
  //  - Section key contexts for modulation
  //
  // Implementation lifts the loop from the existing HarmonyComposer
  // "I-V7-V7-I" lambda, generalized per-period.
  for (const auto& p : seed.periods) {
    int barsThisPeriod = int(p.bars);
    // ... fill cp.add(...) per-bar chords based on cadence type ...
    (void)barsThisPeriod;  // plan details in implementation
  }
  section.chordProgression = std::move(cp);
}
```

For the minimum viable K467 bars 1–12 implementation, hard-code the progression "I-V | I-V7-I | (four-bar HC approach) | (four-bar PAC arrival)" as a literal ChordProgression that the strategy constructs. Not context-aware — that's explicitly Plan B's follow-on spec. Verify by inspection that the rendered output sounds harmonically plausible.

- [ ] **Step 3: Multi-figure cadential tail handling**

The existing `DefaultPhraseStrategy::apply_cadence` adjusts ONLY the last figure of the phrase to land on `cadenceTarget`. Under the new multi-figure cadential model, this is still correct — the last figure IS the arrival figure, and only it gets target-adjusted. The preceding Cadential-tagged figures shape the approach but don't each need individual target-landing.

No code change needed for multi-figure tails specifically — existing `apply_cadence` covers the case. Document this in a comment at the apply_cadence call site to make the intent explicit:

Edit `engine/include/mforce/music/default_strategies.h`, above the `apply_cadence` declaration:

```cpp
// Adjusts only the last figure of the phrase to land on cadenceTarget.
// When a phrase's cadential tail spans multiple figures (e.g., K467 bars
// 7-8 where bar 7's two figures prepare and bar 8 arrives), the earlier
// figures shape the approach trajectory but are NOT target-adjusted —
// only the final arrival figure is. This matches the classical cadence
// structure and keeps apply_cadence focused on the arrival constraint.
static void apply_cadence(Phrase& phrase, const PhraseTemplate& tmpl,
                          const Scale& scale);
```

- [ ] **Step 4: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

Existing templates don't exercise PeriodPassageStrategy, so goldens still match bit-identically.

- [ ] **Step 5: Commit Task 11**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "feat(composer): harmony resolution + multi-figure cadential tail

Composer now resolves Section.chordProgression before dispatching any
PassageStrategy: complex mode uses ChordProgressionBuilder::build when
progressionName is set; simple mode leaves it empty for PassageStrategy.
plan_* to fill.

PeriodPassageStrategy.plan_passage in simple mode derives a trivial
chord progression from the period cadence pattern when Section has
no progression. For K467 bars 1-12 this is hard-coded; richer
context-aware progression planning is deferred to the next spec.

apply_cadence semantics clarified via comment: only the final arrival
figure of the phrase is target-adjusted; preceding Cadential figures
shape the approach. No code change to apply_cadence itself — existing
implementation already supports multi-figure tails correctly.

Goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: K467 bars 1–12 reference template, render, new golden

**Files:**
- Create: `patches/test_k467_period.json` (motifs + passage template + section + part)
- Modify: `docs/superpowers/plans/2026-04-14-baseline-hashes.txt` (append new golden entry)

- [ ] **Step 1: Author `patches/test_k467_period.json`**

Create a PieceTemplate JSON file with:
- `keyName: "C"`, `scaleName: "Major"`, `bpm: 100`, `meter: "4/4"`
- `motifs`: array of the K467 motifs as enumerated in the spec (Fig1, Fig2, Fig1_mod, Fig2_mod, Fig5, Fig6, A_core, A_rhythm_tail, C_arrival, F_tonic, Lead). Each with content (MelodicFigure or PulseSequence), roles, origin, and for derived motifs: derivedFrom + transform.
- `sections[0]`: `name: "K467_opening"`, `beats: 48` (12 bars × 4 beats), empty `progressionName` (let PPS derive).
- `parts[0]`: `name: "Melody"`, one passage template.
- Passage template: `strategy: "period_passage"`, `startingPitch: { degree: 0, octave: 5 }`, `periods: [ ... ]` with two entries matching the spec's K467 reference instance.

Use the spec's worked example (umbrella spec §"K467 bars 1–12 reference instance") as the source of truth for the template contents. Translate the YAML-ish shape in the spec into proper JSON.

The motif contents (MelodicFigure for Fig1 etc.) are user-authored shapes — this task approximates them. Mozart's exact notes are not required; the test is that the rendered output is recognizably a K467-like opening.

Representative JSON skeleton (actual figure contents to be authored — use DURN reference from `patches/k467_bars_1_to_27.dun` as inspiration):

```json
{
  "keyName": "C", "scaleName": "Major", "bpm": 100, "meter": "4/4",
  "motifs": [
    {
      "name": "Fig1",
      "content": { /* MelodicFigure — the bar 1 triadic descent */ },
      "roles": ["Thematic"],
      "origin": "User"
    },
    {
      "name": "Fig2",
      "content": { /* MelodicFigure — bar 2 scalar descent */ },
      "roles": ["Cadential"], "origin": "User"
    },
    {
      "name": "Fig5",
      "content": { /* MelodicFigure — bar 5 thematic */ },
      "roles": ["Thematic"], "origin": "User"
    },
    {
      "name": "Fig6",
      "content": { /* MelodicFigure — bar 6 parallel to Fig5 */ },
      "roles": ["Thematic"], "origin": "User"
    },
    {
      "name": "A_core",
      "content": { /* MelodicFigure — 3-repeated-note motive */ },
      "roles": ["Cadential"], "origin": "User"
    },
    {
      "name": "A_rhythm_tail",
      "content": { /* PulseSequence — A_core's rhythm minus first pulse */ },
      "roles": ["Cadential"],
      "origin": "Derived",
      "derivedFrom": "A_core",
      "transform": "RhythmTail",
      "transformParam": 1
    },
    {
      "name": "C_arrival",
      "content": { /* MelodicFigure — bar 8 trilled arrival */ },
      "roles": ["Cadential"], "origin": "User"
    },
    {
      "name": "F_tonic",
      "content": { /* PulseSequence — whole-note tonic */ },
      "roles": ["Cadential"], "origin": "User"
    },
    {
      "name": "Lead",
      "content": { /* MelodicFigure — pickup ascent */ },
      "roles": ["Connective"], "origin": "User"
    }
  ],
  "sections": [
    { "name": "K467_opening", "beats": 48 }
  ],
  "parts": [{
    "name": "Melody",
    "instrumentType": "Melody",
    "passages": {
      "K467_opening": {
        "strategy": "period_passage",
        "startingPitch": { "degree": 0, "octave": 5 },
        "periods": [
          {
            "variant": "Modified",
            "bars": 4,
            "antecedent": {
              "figures": [
                { "source": "Reference", "motifName": "Fig1" },
                { "source": "Reference", "motifName": "Fig2" }
              ],
              "connectors": [ {"leadStep": 0}, {"leadStep": -3} ],
              "cadenceType": 1
            },
            "consequent": {
              "figures": [],
              "connectors": [],
              "cadenceType": 2, "cadenceTarget": 2
            },
            "consequentTransform": "Invert"
          },
          {
            "variant": "Parallel",
            "bars": 8,
            "antecedent": {
              "figures": [
                { "source": "Reference", "motifName": "Fig5" },
                { "source": "Reference", "motifName": "Fig6" },
                { "source": "Reference", "motifName": "A_core" },
                { "source": "Reference", "motifName": "A_core" },
                { "source": "Reference", "motifName": "C_arrival" }
              ],
              "connectors": [
                {"leadStep": 2},
                {"leadStep": -1},
                {"leadStep": -1},
                {"leadStep": 1},
                {"leadStep": 0}
              ],
              "cadenceType": 1
            },
            "consequent": {
              "figures": [],
              "connectors": [],
              "cadenceType": 2, "cadenceTarget": 0
            },
            "leadingConnective": "Lead"
          }
        ]
      }
    }
  }]
}
```

Fill in each motif's `content` with appropriate MelodicFigure / PulseSequence JSON. Use durations in beats (e.g., quarter = 1.0, eighth = 0.5), and the step sequence (first step = 0 per placement-neutral convention; subsequent steps shape the motif). The exact pitch contour of each figure is a compositional choice — aim for the spec's narrative (triadic descent for Fig1, scalar descent for Fig2, etc.).

- [ ] **Step 2: Render the template**

```
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/PluckU.json renders/k467_period 1 --template patches/test_k467_period.json
```

Expected: `renders/k467_period_1.wav` produced, containing recognizable K467-opening-style melody.

- [ ] **Step 3: Listen and validate**

Play the WAV. Confirm:
- Two distinct periods, each with an HC landing and a stronger closing cadence
- Period 2 sounds like an 8-bar expansion of the same musical idea with parallel figures
- The cadence approaches (especially period 2's PAC landing) feel appropriately cadential

If the output doesn't match these broad criteria, iterate on the motif content and connector leadSteps in `test_k467_period.json` until it does. This is an authoring task, not a mechanical one.

- [ ] **Step 4: Hash the final render**

```
certutil -hashfile C:/@dev/repos/mforce/renders/k467_period_1.wav SHA256
```

Record the hash.

- [ ] **Step 5: Pin the hash in the baseline file**

Edit `docs/superpowers/plans/2026-04-14-baseline-hashes.txt`. Append:

```
# 2026-04-15 — Plan B (period forms) K467 golden
k467_period_1.wav        <the hash from Step 4>
```

- [ ] **Step 6: Verify all six goldens now match**

Re-run all 5 pre-existing renders + the new K467 period render. Hash all 6. Confirm each matches its pinned baseline.

- [ ] **Step 7: Commit Task 12**

```
git -C C:/@dev/repos/mforce add patches/test_k467_period.json docs/superpowers/plans/2026-04-14-baseline-hashes.txt
git -C C:/@dev/repos/mforce commit -m "test(composer): K467 bars 1-12 via PeriodPassageStrategy, new golden

Adds patches/test_k467_period.json — a PieceTemplate with user-authored
motif pool + period-structured passage template that renders K467
bars 1-12 via the new PeriodPassageStrategy:

- 11 motifs in the pool (Fig1, Fig2, Fig5, Fig6, A_core, A_rhythm_tail
  derived, C_arrival, F_tonic, Lead + two implicit-derived variants
  for the Modified period 1)
- Two PeriodSpecs: Modified period (bars 1-4, HC then IAC) and Parallel
  period (bars 5-12 with leading connective, HC then PAC)
- Passage strategy: 'period_passage' routing through
  PeriodPassageStrategy

Render: renders/k467_period_1.wav. Hash pinned in
2026-04-14-baseline-hashes.txt as the new K467 period golden.

This is the first algorithmic composition of K467's opening from
motif pool + period structure — the payoff of the entire 2026-04-14
brainstorm arc.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Wrap-up: squash, push, merge

- [ ] **Step 1: Review commit history**

```
git -C C:/@dev/repos/mforce log --oneline main..HEAD
```

Expected 6 commits (Tasks 7, 8, 9, 10, 11, 12).

- [ ] **Step 2: Push branch**

```
git -C C:/@dev/repos/mforce push -u origin composer-model-period-forms
```

- [ ] **Step 3: Merge to main when approved**

```
git -C C:/@dev/repos/mforce checkout main
git -C C:/@dev/repos/mforce merge --ff-only composer-model-period-forms
git -C C:/@dev/repos/mforce push origin main
git -C C:/@dev/repos/mforce branch -d composer-model-period-forms
```

---

## Success criteria

1. All 6 Task commits on branch `composer-model-period-forms` with preceding goldens (v1..v4, structural) matching after each task.
2. `engine/include/mforce/music/period_passage_strategy.h` exists with both `plan_passage` and `compose_passage`.
3. `PassageTemplate.periods[]` field present; `PeriodVariant` and `PeriodSpec` types present with JSON round-trip.
4. Every strategy has `plan_*` (default no-op) + `compose_*` (renamed from `realize_*`).
5. `PieceTemplate` has `add_motif` and `add_derived_motif` write methods.
6. `renders/k467_period_1.wav` renders cleanly, sounds recognizably K467-like, and has a pinned hash in `2026-04-14-baseline-hashes.txt`.
7. Code compiles clean under `/W4 /permissive-` (MSVC).
8. All six goldens produce hashes bit-identical to their pinned baselines.

## Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Task 7 (rename realize → compose) misses a site, build fails | Medium | Step 4's grep sweep; Step 6 build catches. |
| Task 10 `add_derived_motif` TransformOp dispatch throws on a needed transform | Low-Medium | Start with Invert/Reverse/RhythmTail/VarySteps/VaryRhythm; extend on demand. |
| Task 11 simple-mode chord progression derivation produces wrong harmony for K467 | Medium | First pass hard-codes I-V7-I-V | I-V7-I-V progression; audible check in Task 12 Step 3 detects misalignment. |
| Task 12 K467 motifs don't sound convincingly Mozartean | Medium-High | Accept — spec explicitly says "recognizable as K467-like, not bit-identical to Mozart's actual figures". Iterate on motif authoring until the character is right. |
| Mass signature rename breaks out-of-tree consumers (if any) | Low | Tools under `tools/` only call `Composer::compose_*` at top level; rename there. |

---

## Execution

Tasks 7 and 12 are the substantive content; 8–11 add types + logic incrementally. Each task is a clean build + render + hash gate (except Task 12 which introduces a new golden rather than verifying an existing one).

Per Matt's feedback (`feedback_inline_vs_subagent.md`), prefer inline execution for this plan. The tasks are sequential, mechanical, and share context that would be expensive to rebuild in fresh subagents; the existing "build + render + hash" loop is a well-understood per-task gate.
