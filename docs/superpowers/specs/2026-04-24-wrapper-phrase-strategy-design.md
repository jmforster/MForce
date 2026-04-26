# WrapperPhraseStrategy — Design Spec

**Date:** 2026-04-24
**Context:** Step 1 of ComposerRefactor3 (see `docs/ComposerRefactor3.md`).
**Status:** Draft — contains pending decisions flagged inline for Matt.

---

## Goal

Add a minimum-viable `PhraseStrategy` that realizes a **single** `FigureTemplate` into a **single-figure** `Phrase`. Nothing else.

Purpose:
1. Provide a clean strategy-driven entry point for step 2's exhaustive figure testing, so tests exercise the full `Composer → Conductor → Performer → Instrument` pipeline instead of bespoke CLI assembly (`--test-rfb` / `--test-replicate`).
2. Serve as the smallest-possible reference strategy — a template other phrase strategies can follow once the "shape" family is retired in steps 4–6.

Non-goal: replace `DefaultPhraseStrategy`. Default stays in place handling multi-figure phrases, connectors, cadence, and `MelodicFunction`-driven shape selection.

---

## Contract

### Registration

Name: `"wrapper_phrase"`.
Registered in `Composer` constructor alongside existing phrase strategies (`default_phrase`, `period_phrase`, `sentence_phrase`).

### Selection

`PhraseTemplate::strategy = "wrapper_phrase"`. Dispatch happens in the existing `Composer::compose_phrase` / `DefaultPassageStrategy::compose_passage` resolvers — no changes there.

### Behavior

1. **Starting pitch.**
   - If `phraseTmpl.startingPitch` is set → use it.
   - Else → `piece_utils::pitch_before(locus)`.
2. **Figure count.**
   - `phraseTmpl.figures.empty()` → log warning, return empty `Phrase` (with `startingPitch` set).
   - `phraseTmpl.figures.size() > 1` → log warning ("WrapperPhraseStrategy ignores figures beyond index 0"), proceed with `figures[0]`.
3. **Figure realization.**
   - Resolve `default_figure` from `StrategyRegistry`.
   - Call `compose_figure(locus.with_figure(0), phraseTmpl.figures[0])`.
   - This transparently picks up `FigureSource::{Locked, Generate, Reference, Transform, Literal}`.
4. **Append** the realized figure to `phrase.figures` via `phrase.add_melodic_figure`.
5. **Return** the phrase.

### Explicitly ignored fields (by design)

- `phraseTmpl.connectors` — no leadStep / elide / adjust logic.
- `phraseTmpl.cadenceType`, `cadenceTarget` — no cadence adjustment.
- `phraseTmpl.function` (`MelodicFunction`) — no shape auto-selection.
- `phraseTmpl.periodConfig`, `sentenceConfig` — typed configs for other strategies.

Rationale: this strategy is a test harness primitive. Cadence, connectors, and function-driven shaping belong to `DefaultPhraseStrategy` and the higher-level strategies that will replace it.

### Literal figures

`FigureSource::Literal` routes through `default_figure->compose_figure`, which resolves the cursor via `piece_utils::pitch_before(locus)`. For a single-figure phrase, `pitch_before` walks the passage so far and returns the correct starting cursor (for the first phrase of a passage it falls back to the passage template's startingPitch). Acceptable for the test-harness usage this strategy exists to serve.

---

## Pending decisions (Matt)

### D1 — Should the wrapper honor `connectors[0].leadStep`?

A `FigureConnector` with only `leadStep` set could shift the first unit's step, offering a positioning knob without requiring the caller to rebuild the figure. `DefaultPhraseStrategy` supports this for all figures (including `i=0`).

- **Default (assumed in this spec): no.** Keep the wrapper truly trivial; tests that want a shifted figure set `units[0].step` directly when building the locked figure.
- **Alternative: yes.** Symmetric with `DefaultPhraseStrategy`; small code cost; makes the strategy slightly less trivial.

### D2 — Primary usage pattern

The strategy dispatches through `compose_figure`, so it supports all `FigureSource` values via `DefaultFigureStrategy`. But in the expected step-2 test harness, the figure is pre-built by RFB + transforms in test code and handed in via `FigureSource::Locked`. Two paths are thus exercised:

- **Locked (primary).** Test builds `MelodicFigure` with `RandomFigureBuilder` and `figure_transforms`, drops it into `FigureTemplate::lockedFigure`. Wrapper round-trips it.
- **Generate / Reference / Transform / Literal (incidental).** Supported transparently because wrapper dispatches through `compose_figure`.

Flagging only to confirm: RFB/transforms are **called at test-build time, not inside the strategy**. The strategy is genuinely just a phrase-level wrapper.

- **Default: confirm.** Proceed on this reading.
- **Alternative: strategy owns RFB/transform invocation** via a new template field (e.g. `PhraseTemplate::wrapperConfig` carrying `Constraints` + transform list). Would require schema additions; probably premature given step 5 may rework FigureSource anyway.

### D3 — Unused fields: silent or warn?

Current spec: log a warning if `figures.size() > 1`. No warnings for ignored `connectors` / `cadenceType` / `function`.

- **Default (assumed): warn only on figure-count mismatch, stay silent on the other ignored fields.** They're defaulted/empty in any sensible wrapper-phrase template, so warnings would be noise.
- **Alternative: warn on any non-default ignored field.** More pedantic; flags template-authoring mistakes earlier.

---

## File layout

Strategy class is small enough for a single header with inline body, matching `phrase_strategies.h`.

- **Create:** `engine/include/mforce/music/wrapper_phrase_strategy.h`
- **Modify:** `engine/include/mforce/music/composer.h`
  - Add `#include "mforce/music/wrapper_phrase_strategy.h"`.
  - Register in constructor next to `PeriodPhraseStrategy` / `SentencePhraseStrategy`.

Smoke test (part of this step, minimal):

- **Create:** `tools/test_figures/CMakeLists.txt`
- **Create:** `tools/test_figures/main.cpp`
- **Modify:** root `CMakeLists.txt` — `add_subdirectory(tools/test_figures)`

The full step-2 test harness (pure unit tests on `figure_transforms` + comprehensive integration suite, + removal of `--test-rfb` / `--test-replicate`) is a **separate spec and plan**. Step 1 only adds a single smoke test confirming the strategy wires through end-to-end.

---

## Smoke-test scope (step 1 only)

One binary, one test case:

1. Build a `MelodicFigure` manually (e.g. steps `[0, 1, -1, 0]`, uniform 1-beat pulse).
2. Build a `PieceTemplate` with:
   - One section, one part, one passage.
   - One phrase, `strategy = "wrapper_phrase"`.
   - One `FigureTemplate` with `source = Locked`, `lockedFigure = <the figure>`.
3. Run the `ClassicalComposer`.
4. Assert:
   - `piece.parts[0].passages["Main"].phrases.size() == 1`.
   - `phrase.figures.size() == 1`.
   - Each unit's `step` and `duration` in the realized figure matches the input.
5. Print `OK` or `FAIL` on stderr. Exit code 0 on success, non-zero on failure.

No WAV rendering, no JSON dump. This is the smallest thing that proves the wiring. Step 2 grows this binary into the full suite.

---

## Out of scope for step 1

- **Multi-figure phrases** — step 3 (`DefaultPhraseStrategy` rewrite).
- **Direct RFB invocation from a phrase-level spec** — deferred; may be obviated by step 5 redesign.
- **Removal of `--test-rfb` / `--test-replicate`** — step 2.
- **Exhaustive `figure_transforms::*` unit tests** — step 2.
- **The `fig / inv / retro / retro-inv` listening exercise** — step 2, consumes this strategy across 4 phrases in a single passage.
