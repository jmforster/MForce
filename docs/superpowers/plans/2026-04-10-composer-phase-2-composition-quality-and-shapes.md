# Composer Strategy — Phase 2 Implementation Plan (Composition Quality + Shape Strategies)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land two related changes. First, fix the pre-existing `totalBeats`-as-hint-not-constraint behavior that causes phrases to drift from their declared beat counts (queued in `project_composition_quality.md`). Second, migrate the 14 `FigureBuilder` shape methods (`scalar_run`, `triadic_outline`, `fanfare`, etc.) into individual `FigureStrategy` subclasses and route `DefaultFigureStrategy::generate_shaped_figure`'s switch through the registry instead of hardcoded calls.

**Architecture:** One new header `engine/include/mforce/music/shape_strategies.h` holds all 14 strategy classes. `Composer`'s constructor registers them alongside the three Defaults. `DefaultFigureStrategy::generate_shaped_figure` becomes a one-liner that looks up by name. The totalBeats fix lives in `DefaultFigureStrategy::realize_figure` as a single post-generation proportional-scaling pass applied to Generate-path figures regardless of whether they went through `generate_figure` or `generate_shaped_figure`.

**Tech Stack:** C++17, CMake, header-only. No new dependencies. Mechanical verification at end (render non-silent, non-crash, pin new hash). No audition — user is away; audit on return.

---

## Scope Guardrails

- **Shape migration must be bit-identical.** Every strategy body is a verbatim copy of the corresponding `FigureBuilder` method body. Post-migration golden render hash MUST match pre-migration hash exactly. If it drifts, the migration has a copy bug and must be fixed before proceeding.
- **Composition quality fix is intentionally output-changing.** After the fix, the golden render will NOT match the Phase 1b hash (`7675e3c3...`) because beat enforcement will reshape figures. A new hash is captured and pinned at the end.
- **`FigureBuilder` shape methods stay in place.** Each new strategy COPIES the body rather than calling the method, so `FigureBuilder`'s public API is unchanged. Deletion of the now-unused methods is a follow-on cleanup, not in this plan.
- **No behavioral changes beyond the totalBeats fix.** No new shapes, no renamed shapes, no signature changes, no tweaks to step generation or rhythm variation beyond the proportional-scaling pass.
- **Single branch: `main`.** Commit directly. No PR.
- **Mechanical verification only.** Render after the final commit. Confirm non-silent (rms > 0.01), non-crash. Pin the new hash. Matt audits later.

---

## File Structure

New files:

| File | Responsibility |
|---|---|
| `engine/include/mforce/music/shape_strategies.h` | All 14 `ShapeXxxStrategy` classes, each a `FigureStrategy` subclass whose `realize_figure` emits a specific shape. |

Modified files:

| File | Change |
|---|---|
| `engine/include/mforce/music/composer.h` | Constructor registers all 14 shape strategies; `DefaultFigureStrategy::realize_figure` (out-of-line) adds the totalBeats proportional-scaling pass. |
| `engine/include/mforce/music/default_strategies.h` | `DefaultFigureStrategy::generate_shaped_figure` replaced with a registry lookup; `apply_cadence_rhythm` deleted from the class. |
| `renders/template_golden_phase1a.wav` | Overwritten at the end with the new output from the composition-quality fix. |
| `renders/template_golden_phase1a.sha256` | New hash line. |

**Files NOT touched**: `figures.h` (FigureBuilder's shape methods stay), `structure.h`, `templates.h`, `templates_json.h`, `music_json.h`, `strategy.h`, `strategy_registry.h`, `classical_composer.h`, `conductor.h`, `dun_parser.h`, `basics.h`, `pitch_reader.h`, any template JSON, any CLI file.

---

## Task 1: Create `shape_strategies.h` with all 14 shape classes

**Files:**
- Create: `engine/include/mforce/music/shape_strategies.h`
- Read: `engine/include/mforce/music/figures.h:680-905` (the 14 `FigureBuilder` shape methods)
- Read: `engine/include/mforce/music/strategy.h` (Strategy base class)
- Read: `engine/include/mforce/music/templates.h` (FigureTemplate and FigureShape)

This is the big paste task. Create a single header with 14 `FigureStrategy` subclasses, each named `Shape<Name>Strategy` (e.g., `ShapeScalarRunStrategy`, `ShapeTriadicOutlineStrategy`). Each class's `realize_figure` method reads shape params from `figTmpl.shape*` fields (the existing `shapeDirection`, `shapeParam`, `shapeParam2`, `minNotes`, `maxNotes`, `totalBeats`, `defaultPulse` fields on `FigureTemplate`) and emits the same `MelodicFigure` the corresponding `FigureBuilder` method would have emitted.

- [ ] **Step 1: Read the 14 FigureBuilder methods**

Read `engine/include/mforce/music/figures.h` from line ~680 to ~905. Confirm the 14 methods and their signatures. Record the exact line range for each method's body so you can cite them in the pasted code.

Also read the current `DefaultFigureStrategy::generate_shaped_figure` in `engine/include/mforce/music/default_strategies.h` (around line 107-167) to see how each shape is currently invoked — specifically, which `FigureTemplate` fields feed which method parameters, and what defaults fall back when a param is zero.

- [ ] **Step 2: Create the scaffold**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// Shape strategies — one FigureStrategy subclass per named shape.
//
// Each class's realize_figure reads the shape-specific parameters from
// FigureTemplate's shape* fields and emits the MelodicFigure that the
// corresponding FigureBuilder method produced pre-Phase-2. Bodies are
// verbatim copies of the FigureBuilder methods at figures.h:~680-905 with
// two mechanical edits:
//   (1) Parameters that were method arguments are read from figTmpl
//       fields instead.
//   (2) The method body's implicit `this` (FigureBuilder) is replaced with
//       a local `FigureBuilder fb(seed);` constructed at entry time.
//
// Seed derivation: each strategy computes its own figSeed the same way
// DefaultFigureStrategy::realize_figure does — figTmpl.seed if non-zero,
// else ctx.rng->rng(). The strategy then constructs local Randomizer
// and FigureBuilder instances from figSeed (matching the pre-migration
// behavior exactly to preserve bit-identicality against the golden).
// ---------------------------------------------------------------------------

// Helper: shared preamble for every shape strategy.
// Computes default pulse, count (where relevant), and the local FigureBuilder.
// Each strategy duplicates this because there's no sensible base class body
// without introducing a virtual-call overhead we don't want in this path.

class ShapeScalarRunStrategy : public Strategy {
public:
  std::string name() const override { return "shape_scalar_run"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

// ... 13 more class declarations, one per shape ...

} // namespace mforce
```

The full list of class names (match the enum exactly):

| Enum | Class name | Registry name |
|---|---|---|
| `FigureShape::ScalarRun` | `ShapeScalarRunStrategy` | `"shape_scalar_run"` |
| `FigureShape::RepeatedNote` | `ShapeRepeatedNoteStrategy` | `"shape_repeated_note"` |
| `FigureShape::HeldNote` | `ShapeHeldNoteStrategy` | `"shape_held_note"` |
| `FigureShape::CadentialApproach` | `ShapeCadentialApproachStrategy` | `"shape_cadential_approach"` |
| `FigureShape::TriadicOutline` | `ShapeTriadicOutlineStrategy` | `"shape_triadic_outline"` |
| `FigureShape::NeighborTone` | `ShapeNeighborToneStrategy` | `"shape_neighbor_tone"` |
| `FigureShape::LeapAndFill` | `ShapeLeapAndFillStrategy` | `"shape_leap_and_fill"` |
| `FigureShape::ScalarReturn` | `ShapeScalarReturnStrategy` | `"shape_scalar_return"` |
| `FigureShape::Anacrusis` | `ShapeAnacrusisStrategy` | `"shape_anacrusis"` |
| `FigureShape::Zigzag` | `ShapeZigzagStrategy` | `"shape_zigzag"` |
| `FigureShape::Fanfare` | `ShapeFanfareStrategy` | `"shape_fanfare"` |
| `FigureShape::Sigh` | `ShapeSighStrategy` | `"shape_sigh"` |
| `FigureShape::Suspension` | `ShapeSuspensionStrategy` | `"shape_suspension"` |
| `FigureShape::Cambiata` | `ShapeCambiataStrategy` | `"shape_cambiata"` |

- [ ] **Step 3: Write all 14 bodies**

For each class, define its `realize_figure` out-of-line as an `inline` function. The body follows the pattern from the pre-Phase-2 `generate_shaped_figure` switch case at `default_strategies.h:107-167`. For each shape, the pattern is:

```cpp
inline MelodicFigure ShapeScalarRunStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int count = (ft.maxNotes > ft.minNotes)
      ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
      : (ft.minNotes > 0 ? ft.minNotes : 4);
  return fb.scalar_run(dir, count > 0 ? count : 4, fb.defaultPulse);
}
```

For each of the other 13 shapes, mirror the EXACT logic that exists today in `generate_shaped_figure`'s switch case for that shape. Specifically, compare with the corresponding case in `default_strategies.h` to make sure the parameters you extract and the arguments you pass to the `FigureBuilder` method are identical.

**Critical copy rules:**
- The `fb.defaultPulse` assignment is the same.
- The `int dir`, `int p1`, `int p2`, `int count` locals are computed the same way.
- The call to `fb.<method>(...)` uses the same arguments in the same order.
- Don't "simplify" anything. Don't extract helpers. Don't DRY the shared preamble. The point is verbatim reproduction.

If a given shape's case doesn't use `dir`, `p1`, `p2`, or `count`, just omit the unused locals. Match the existing case's exact locals.

Specific per-shape patterns (copy these from the existing default_strategies.h):

```cpp
// ShapeRepeatedNoteStrategy
return fb.repeated_note(count > 0 ? count : 3, fb.defaultPulse);

// ShapeHeldNoteStrategy
return fb.held_note(ft.totalBeats > 0 ? ft.totalBeats : fb.defaultPulse * 2);

// ShapeCadentialApproachStrategy
// p1 = ft.shapeParam
return fb.cadential_approach(dir < 0, p1 > 0 ? p1 : 3,
                             fb.defaultPulse * 2, fb.defaultPulse);

// ShapeTriadicOutlineStrategy
// p1 = ft.shapeParam
return fb.triadic_outline(dir, p1 > 0, fb.defaultPulse);

// ShapeNeighborToneStrategy
return fb.neighbor_tone(dir > 0, fb.defaultPulse);

// ShapeLeapAndFillStrategy
// p1 = ft.shapeParam, p2 = ft.shapeParam2
return fb.leap_and_fill(p1 > 0 ? p1 : 4, dir > 0, p2, fb.defaultPulse);

// ShapeScalarReturnStrategy
// p1 = ft.shapeParam, p2 = ft.shapeParam2
return fb.scalar_return(dir, p1 > 0 ? p1 : 3, p2, fb.defaultPulse);

// ShapeAnacrusisStrategy
return fb.anacrusis(count > 0 ? count : 2, dir,
                    fb.defaultPulse * 0.5f, fb.defaultPulse);

// ShapeZigzagStrategy
// p1 = ft.shapeParam
return fb.zigzag(dir, p1 > 0 ? p1 : 3, 2, 1, fb.defaultPulse);

// ShapeFanfareStrategy
// p1 = ft.shapeParam
return fb.fanfare({4, 3}, p1 > 0 ? p1 : 1, fb.defaultPulse);

// ShapeSighStrategy
return fb.sigh(fb.defaultPulse);

// ShapeSuspensionStrategy
return fb.suspension(fb.defaultPulse * 2, fb.defaultPulse);

// ShapeCambiataStrategy
return fb.cambiata(dir, fb.defaultPulse);
```

- [ ] **Step 4: Build**

```
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Release
```

Expected: clean build. `shape_strategies.h` is not yet included anywhere that compiles, so the 14 classes are parse-checked only when first included. If the build fails with an error in shape_strategies.h itself, fix it — likely a missing include or a typo in a `FigureBuilder` call.

- [ ] **Step 5: Commit**

```
git add engine/include/mforce/music/shape_strategies.h
git commit -m "feat(composer): add 14 shape strategy classes (not yet wired)"
```

---

## Task 2: Wire shape strategies into `Composer` and replace `generate_shaped_figure` switch

**Files:**
- Modify: `engine/include/mforce/music/composer.h` — register all 14 in Composer constructor
- Modify: `engine/include/mforce/music/default_strategies.h` — replace `generate_shaped_figure` switch with a registry lookup

After this task, every shape goes through the registry. The pre-existing switch in `generate_shaped_figure` is gone. Bit-identical hash against Phase 1b golden must still match — this is a pure refactor.

- [ ] **Step 1: Include `shape_strategies.h` from `composer.h`**

Add `#include "mforce/music/shape_strategies.h"` to the top of `engine/include/mforce/music/composer.h`. Place it alongside the other `mforce/music/...` includes.

- [ ] **Step 2: Register all 14 in the Composer constructor**

Find `Composer::Composer(...)` in `composer.h`. The current body registers three Default strategies. Add registration for all 14 shape strategies after the three Defaults:

```cpp
explicit Composer(uint32_t seed = 0xC1A5'0001u) : rng_(seed + 200) {
  registry_.register_strategy(std::make_unique<DefaultFigureStrategy>());
  registry_.register_strategy(std::make_unique<DefaultPhraseStrategy>());
  registry_.register_strategy(std::make_unique<DefaultPassageStrategy>());

  // Shape strategies (Phase 2)
  registry_.register_strategy(std::make_unique<ShapeScalarRunStrategy>());
  registry_.register_strategy(std::make_unique<ShapeRepeatedNoteStrategy>());
  registry_.register_strategy(std::make_unique<ShapeHeldNoteStrategy>());
  registry_.register_strategy(std::make_unique<ShapeCadentialApproachStrategy>());
  registry_.register_strategy(std::make_unique<ShapeTriadicOutlineStrategy>());
  registry_.register_strategy(std::make_unique<ShapeNeighborToneStrategy>());
  registry_.register_strategy(std::make_unique<ShapeLeapAndFillStrategy>());
  registry_.register_strategy(std::make_unique<ShapeScalarReturnStrategy>());
  registry_.register_strategy(std::make_unique<ShapeAnacrusisStrategy>());
  registry_.register_strategy(std::make_unique<ShapeZigzagStrategy>());
  registry_.register_strategy(std::make_unique<ShapeFanfareStrategy>());
  registry_.register_strategy(std::make_unique<ShapeSighStrategy>());
  registry_.register_strategy(std::make_unique<ShapeSuspensionStrategy>());
  registry_.register_strategy(std::make_unique<ShapeCambiataStrategy>());
}
```

- [ ] **Step 3: Replace the `generate_shaped_figure` switch with registry lookup**

In `engine/include/mforce/music/default_strategies.h`, find `DefaultFigureStrategy::generate_shaped_figure` (around line 107). Replace the entire body with a registry lookup by shape name:

```cpp
inline MelodicFigure DefaultFigureStrategy::generate_shaped_figure(
    const FigureTemplate& ft, uint32_t seed) {
  // Phase 2: dispatch to a registered shape strategy by name. The strategy
  // name is derived from the FigureShape enum via a name map. The seed
  // here is informational — the strategy reads ft.seed and falls back to
  // ctx.rng->rng() on its own.
  //
  // We need access to the registry, which lives on Composer. But this is
  // a static-like call path inside DefaultFigureStrategy — we don't have
  // a Composer pointer. Two options:
  //   (a) Change the signature to take a StrategyContext& and route via
  //       ctx.composer->registry_.get(name). This requires making
  //       StrategyRegistry::get accessible (public method on Composer).
  //   (b) Keep the switch but replace each case with a direct `ShapeXStrategy::realize_figure(ft, fakeCtx)` call.
  //       Requires constructing a fake StrategyContext.
  //
  // Use (a) — it's the proper registry-driven dispatch. This requires a
  // signature change from (ft, seed) to (ft, ctx), which ripples into
  // DefaultFigureStrategy::realize_figure below.
  //
  // ---- IMPLEMENTATION ----
  // Since this function is no longer called from realize_figure in the
  // old signature, delete it entirely. Inline the registry dispatch into
  // realize_figure directly.
  return MelodicFigure{}; // unreachable — this function is deleted in Step 4
}
```

Actually, **delete `generate_shaped_figure` entirely** — its caller (`DefaultFigureStrategy::realize_figure`) will be updated in Step 4 to call the registry directly. Remove both the declaration and the definition from `default_strategies.h`.

- [ ] **Step 4: Update `DefaultFigureStrategy::realize_figure` in `composer.h`**

Find the out-of-line definition of `DefaultFigureStrategy::realize_figure` in `composer.h` (below the `Composer` class, currently around line 243). In the Generate case, the current code branches:

```cpp
case FigureSource::Generate:
default:
  if (figTmpl.shape != FigureShape::Free)
    return generate_shaped_figure(figTmpl, figSeed);
  return generate_figure(figTmpl, figSeed);
```

Replace the `generate_shaped_figure` call with a registry lookup. Map the `FigureShape` enum value to the strategy name, look up the strategy, and call its `realize_figure`:

```cpp
case FigureSource::Generate:
default:
  if (figTmpl.shape != FigureShape::Free) {
    const char* shapeName = nullptr;
    switch (figTmpl.shape) {
      case FigureShape::ScalarRun:         shapeName = "shape_scalar_run"; break;
      case FigureShape::RepeatedNote:      shapeName = "shape_repeated_note"; break;
      case FigureShape::HeldNote:          shapeName = "shape_held_note"; break;
      case FigureShape::CadentialApproach: shapeName = "shape_cadential_approach"; break;
      case FigureShape::TriadicOutline:    shapeName = "shape_triadic_outline"; break;
      case FigureShape::NeighborTone:      shapeName = "shape_neighbor_tone"; break;
      case FigureShape::LeapAndFill:       shapeName = "shape_leap_and_fill"; break;
      case FigureShape::ScalarReturn:      shapeName = "shape_scalar_return"; break;
      case FigureShape::Anacrusis:         shapeName = "shape_anacrusis"; break;
      case FigureShape::Zigzag:            shapeName = "shape_zigzag"; break;
      case FigureShape::Fanfare:           shapeName = "shape_fanfare"; break;
      case FigureShape::Sigh:              shapeName = "shape_sigh"; break;
      case FigureShape::Suspension:        shapeName = "shape_suspension"; break;
      case FigureShape::Cambiata:          shapeName = "shape_cambiata"; break;
      case FigureShape::Free:
      default:                             shapeName = nullptr; break;
    }
    if (shapeName) {
      // The Composer's registry is the source of truth for shape strategies.
      // StrategyRegistry::get exposed as const public on Composer already.
      Strategy* s = ctx.composer->registry_get_for_phase2(shapeName);
      if (s) return s->realize_figure(figTmpl, ctx);
    }
    // Fall through to generate_figure if lookup failed (defensive)
  }
  return generate_figure(figTmpl, figSeed);
```

The method `Composer::registry_get_for_phase2(name)` is NEW — add it as a public method on `Composer` that forwards to `registry_.get(name)`. This avoids exposing the registry member itself. Alternative: make `registry_` public (simpler, less encapsulation). Use whichever you find cleaner; the plan accepts either.

**Bit-identicality note:** the new registry path calls `ShapeXxxStrategy::realize_figure(figTmpl, ctx)`, which in turn constructs its local `FigureBuilder(figSeed)`. The old path called `generate_shaped_figure(figTmpl, figSeed)`, which also constructed a local `FigureBuilder(seed)`. The seed derivation is the same (`figTmpl.seed ? figTmpl.seed : ctx.rng->rng()`), so the FigureBuilder is seeded identically, so the random draws match, so the output matches. This is the whole argument for bit-identicality in the migration.

- [ ] **Step 5: Build and verify bit-identical output**

```
"/c/Program Files/.../cmake.exe" --build build --target mforce_cli --config Release
```

Then render the golden:

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase2_shape_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase2_shape_check_1.wav
```

**Expected hash: `7675e3c3962fb57b4b3f86da9c43fb85062107f91679a79bf615e7c595136656`** (the Phase 1b golden).

If the hash differs, the shape migration has a copy bug. Most likely causes:
- A parameter swap in one of the 14 shape strategies (e.g., passed `p1` where the old code passed `p2`).
- A missing field read (e.g., forgot to use `dir = ft.shapeDirection` in one class).
- A different seed derivation (e.g., `Randomizer(seed + 99)` became `Randomizer(seed + 98)` in a copy).

Diagnose by comparing the composed JSON output (`renders/phase2_shape_check_1.json`) against a baseline. Do NOT commit if the hash doesn't match — fix the copy bug first.

- [ ] **Step 6: Commit Task 2**

Once the hash matches, commit:

```
git add engine/include/mforce/music/composer.h engine/include/mforce/music/default_strategies.h
git commit -m "refactor(composer): route generate_shaped_figure through shape strategy registry"
```

Clean up:
```
rm renders/phase2_shape_check_1.wav renders/phase2_shape_check_1.json 2>/dev/null
```

---

## Task 3: Composition quality fix — `totalBeats` hard constraint + delete `apply_cadence_rhythm`

**Files:**
- Modify: `engine/include/mforce/music/composer.h` — add totalBeats proportional-scaling pass in `DefaultFigureStrategy::realize_figure`
- Modify: `engine/include/mforce/music/default_strategies.h` — delete `apply_cadence_rhythm` declaration + definition

This task is intentionally output-changing. The golden hash will NOT match `7675e3c3...` after this — that's expected. At the end, the new hash is captured and pinned.

- [ ] **Step 1: Add the totalBeats proportional-scaling pass**

In `composer.h`, find `DefaultFigureStrategy::realize_figure` (out-of-line, below Composer class). The function's Generate case currently branches between `generate_shaped_figure` (registry lookup from Task 2) and `generate_figure`. Wrap the return from each of those branches with a proportional-scaling pass:

```cpp
case FigureSource::Generate:
default: {
  MelodicFigure fig;
  if (figTmpl.shape != FigureShape::Free) {
    // ... existing registry lookup logic from Task 2 ...
    // assign the result to `fig` instead of returning directly
  }
  if (fig.units.empty()) {
    // Either shape lookup failed or shape was Free — use generate_figure.
    fig = generate_figure(figTmpl, figSeed);
  }

  // Phase 2 composition quality: enforce totalBeats as a hard constraint
  // via proportional scaling. Preserves the rhythm shape (ratios between
  // units) while snapping the total to match the template's declared
  // totalBeats. No-op if totalBeats is 0 (unconstrained) or the actual
  // total already matches within a tolerance.
  if (figTmpl.totalBeats > 0 && !fig.units.empty()) {
    float actual = 0;
    for (auto& u : fig.units) actual += u.duration;
    if (actual > 0 && std::abs(actual - figTmpl.totalBeats) > 0.001f) {
      float scale = figTmpl.totalBeats / actual;
      for (auto& u : fig.units) u.duration *= scale;
    }
  }
  return fig;
}
```

**Critical**: the scaling pass applies ONLY in the Generate case. The Locked, Reference, Transform, and Literal cases do NOT scale — those paths either take the user's declared durations verbatim (Literal, Locked) or inherit from a motif/transform (Reference, Transform), and the user is expected to set durations correctly there.

- [ ] **Step 2: Delete `apply_cadence_rhythm`**

In `default_strategies.h`, find `DefaultPhraseStrategy::apply_cadence_rhythm` — both the private static declaration inside the class body and the `inline` definition below. Delete both. It's dead code; nothing calls it.

Also remove any comment in `default_strategies.h` that mentions `apply_cadence_rhythm` by name (there may be a "private helper" comment line).

- [ ] **Step 3: Build**

```
"/c/Program Files/.../cmake.exe" --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 4: Render the golden and verify it's non-silent + non-crashing**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase2_quality_check 1 --template patches/template_golden_phase1a.json
```

Report the CLI's peak/rms output. Expected: peak > 0.05, rms > 0.01 (non-silent), reasonable non-zero values. The hash WILL differ from Phase 1b's `7675e3c3...` because beat-scaling reshapes figures.

Confirm by checking that each phrase's actual beat count in `renders/phase2_quality_check_1.json` now matches the phrase's declared `totalBeats` sum (within ~0.01 beat tolerance). For `template_golden_phase1a.json`:
- Phrase A1: target ~14 beats (reference motif_a + 3×4 generate)
- Phrase B: target ~14 beats (gen 4 + transform motif_a + gen 4 + gen 4)
- Phrase A2: target 16 beats (4×4 generate)

After the fix, A2 should land exactly at 16 beats (was 19 pre-fix). A1 and B may still vary slightly because they include reference/transform figures whose durations are not scaled.

If any phrase's actual duration differs from its target by more than the tolerance of unscaled reference/transform figures, investigate before committing. Use the JSON output to audit.

- [ ] **Step 5: Capture the new golden hash**

Copy the render to the canonical golden filename and hash it:

```
cp renders/phase2_quality_check_1.wav renders/template_golden_phase1a.wav
sha256sum renders/template_golden_phase1a.wav > renders/template_golden_phase1a.sha256
cat renders/template_golden_phase1a.sha256
```

Record the new hash.

- [ ] **Step 6: Commit Phase 2 final**

```
git add engine/include/mforce/music/composer.h engine/include/mforce/music/default_strategies.h
git add -f renders/template_golden_phase1a.wav
git add renders/template_golden_phase1a.sha256
git commit -m "feat(composer): enforce totalBeats constraint, delete dead apply_cadence_rhythm

Phase 2 composition quality fix. Generate-path figures are now
proportionally scaled to match their FigureTemplate.totalBeats after
step generation + rhythm variation + cadence pitch adjustment.
Preserves rhythm shape (ratios between units) while snapping the total
to the template's declared value. Locked/Reference/Transform/Literal
paths are not scaled — user sets durations directly there.

Also deletes DefaultPhraseStrategy::apply_cadence_rhythm, which was
dead code from an unfinished pre-refactor feature. No caller existed.

Golden hash re-pinned against new mechanically-verified output."
```

Clean up:
```
rm renders/phase2_quality_check_1.wav renders/phase2_quality_check_1.json 2>/dev/null
```

---

## Phase 2 exit criteria

1. `cmake --build` succeeds.
2. `shape_strategies.h` exists with all 14 `Shape<Name>Strategy` classes.
3. `DefaultFigureStrategy::generate_shaped_figure` is gone; shape dispatch goes through the registry.
4. `DefaultPhraseStrategy::apply_cadence_rhythm` is gone.
5. `DefaultFigureStrategy::realize_figure` contains the `totalBeats` proportional-scaling pass.
6. Golden WAV re-rendered, non-silent, non-crash, new hash pinned in `.sha256`.
7. All phrases in the golden template's composed output match their declared `totalBeats` totals within ~0.01 beat of target (for Generate-path figures).
8. Commit log shows: shape classes → wire-up → quality fix, in that order.

---

## What is explicitly NOT in this plan

- No deletion of FigureBuilder's shape methods. They stay for now.
- No new shape types.
- No tweaks to step generation, vary_rhythm, or cadence pitch adjustment.
- No FigureSource changes.
- No template JSON changes.
- No CLI changes.
- No `Seed → Motif` rename (Phase 5).
- No Period or Sentence phrase strategies (Phase 3).
- No Outline strategies (deferred).
- No audition — mechanical verification only.
