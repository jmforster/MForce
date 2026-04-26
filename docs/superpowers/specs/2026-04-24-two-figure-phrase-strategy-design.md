# TwoFigurePhraseStrategy — Design Spec

**Date:** 2026-04-24
**Context:** Step 3 of ComposerRefactor3 (see `docs/ComposerRefactor3.md`).
**Status:** Draft — pending decisions flagged inline for Matt.
**Predecessor:** Step 2 (figure testing harness) on branch `figure-builder-redesign` through commit `e4b39d1`.

**Key interpretation decision (Matt: "B"):** Additive sibling strategy — the existing `DefaultPhraseStrategy` is not touched in step 3. A new strategy named `two_figure_phrase` lands alongside it. The rename-to-default move is deferred to step 6 (middle-tiers refactor), where it fits alongside the FigureSource / shape retirement.

---

## Goal

Add a new `PhraseStrategy` named `"two_figure_phrase"` that:
1. Uses `RandomFigureBuilder` directly to generate a base figure from Constraints.
2. Uses `figure_transforms::*` to derive a second figure from the base.
3. Assembles both into a two-figure `Phrase`.
4. Has no dependence on `FigureSource` or the shape system (those are being retired in step 4–6).

Purpose:
- Prove out a phrase-level strategy that works with the new RFB + transforms primitives directly, independent of the legacy `FigureTemplate::source` / `shape` / `MelodicFunction` machinery.
- Provide a test-bed for auditioning transform variants at the phrase level (extending the step-2 `--render` exercise from single phrases to actual two-figure phrases).

Non-goal: handle phrases with more than 2 figures. Non-goal: connector / cadence / shape logic. Non-goal: become the new default right now (step 6 does the renaming).

---

## Contract

### Registration

Name: `"two_figure_phrase"`.
Registered in `Composer` constructor alongside `wrapper_phrase`.

### Selection

`PhraseTemplate::strategy = "two_figure_phrase"`. Routed through the existing dispatcher.

### Input

A new optional field on `PhraseTemplate`: `std::optional<TwoFigurePhraseConfig> twoFigureConfig;`. When the strategy resolves and this field is missing, the strategy emits a warning and returns an empty Phrase.

`TwoFigurePhraseConfig` shape (minimum viable):

```cpp
struct TwoFigurePhraseConfig {
    // Build method for the base figure (figure 1).
    enum class Method { ByCount, ByLength, Singleton };
    Method method{Method::ByCount};
    int   count{4};           // for ByCount
    float length{4.0f};       // for ByLength

    // Generator constraints passed to RFB::build_*.
    Constraints constraints;

    // Seed for RFB. 0 means "use phraseTmpl.seed, else a deterministic value".
    uint32_t seed{0};

    // How figure 2 is derived from figure 1.
    TransformOp transform{TransformOp::Invert};
    int transformParam{0};
};
```

The config deliberately does NOT wrap a second set of method/constraints for figure 2. Figure 2 is always a `figure_transforms::*` derivation of figure 1. The "base + variant" pattern is what the step-2 `--render` exercise demonstrates, and is what musicians actually want.

### Behavior

1. **Starting pitch:** `phraseTmpl.startingPitch` if set, else `piece_utils::pitch_before(locus)`.
2. **If `phraseTmpl.twoFigureConfig` absent:** warn, return empty Phrase.
3. **If `phraseTmpl.figures` non-empty:** warn ("TwoFigurePhraseStrategy ignores the figures list; use twoFigureConfig instead"), proceed.
4. **Build figure 1** via RFB according to `method` + `count`/`length` + `constraints`. Uses a `RandomFigureBuilder(seed)` where `seed` resolves from config → `phraseTmpl.seed` → `0xF1F1F1F1u`.
5. **Build figure 2** as `apply_transform(figure1, transform, transformParam, seed+1)` where `apply_transform` is the same switch-case used by `DefaultFigureStrategy::apply_transform` in `default_strategies.h`. Reusing that body keeps transform semantics consistent.

   To avoid exposing `DefaultFigureStrategy`'s member function to a new caller, factor `apply_transform` into a free function in `figure_transforms.h` or a new helper header. **See D3 below.**
6. **Assemble:** `phrase.add_melodic_figure(figure1); phrase.add_melodic_figure(figure2);`
7. **No connector logic.** Figure 2's `units[0].step` is kept as the transform emitted. Whatever leading step it carries will be honored by the downstream Composer pitch walk.
8. **No cadence adjustment.**
9. **Return** the phrase.

### Out-of-scope fields (ignored, not warned)

- `phraseTmpl.connectors` (no inter-figure connectors)
- `phraseTmpl.cadenceType` / `cadenceTarget`
- `phraseTmpl.function` (MelodicFunction)
- `phraseTmpl.periodConfig`, `sentenceConfig`

---

## Pending decisions (Matt)

Defaults locked in so plan can execute. Override any before starting.

### D1 — Strategy name

- **Default: `"two_figure_phrase"`** — descriptive, symmetric with `period_phrase` / `sentence_phrase` / `wrapper_phrase`.
- Alternatives: `"base_variant_phrase"`, `"paired_phrase"`, `"default_phrase_v2"`.

### D2 — Config location

- **Default: new `TwoFigurePhraseConfig` struct in `templates.h`, optional field on `PhraseTemplate`.** Mirrors `PeriodPhraseConfig` / `SentencePhraseConfig`.
- Alternative: stash into `PhraseTemplate::figures` using FigureTemplate's legacy fields. Couples the new strategy to schema we're retiring — bad idea.

### D3 — Where `apply_transform` lives

Currently `DefaultFigureStrategy::apply_transform` (in `default_strategies.h`) wraps the TransformOp switch. The new strategy needs the same switch.

- **Default: factor it into a free function `figure_transforms::apply(base, op, param, seed)`** and have both `DefaultFigureStrategy` and `TwoFigurePhraseStrategy` delegate to it. Pure refactor of existing behavior — DefaultFigureStrategy's output is preserved bit-for-bit if done correctly (verify via K467 golden check).
- Alternative: duplicate the switch inside `TwoFigurePhraseStrategy`. Quick, slightly gross, risks divergence. Rejected.
- Alternative: make `DefaultFigureStrategy::apply_transform` a free `static` usable externally. Works but awkward — `TwoFigurePhraseStrategy` would depend on `DefaultFigureStrategy`, which is a directional inversion.

### D4 — JSON round-trip

- **Default: add `to_json` / `from_json` for `TwoFigurePhraseConfig` in `templates_json.h`**, and extend `PhraseTemplate` round-trip to serialize/deserialize `twoFigureConfig` the same way it handles `periodConfig` / `sentenceConfig`. Small cost, leaves the code complete.
- Alternative: skip JSON for step 3; in-code construction only (test_figures creates templates in code). Half-finished; won't be usable from external template JSON. Rejected.

### D5 — Base-build method coverage

- **Default: ByCount, ByLength, Singleton.** Covers the three most commonly useful RFB entry points.
- Alternative: also ByStepSequence / ByPulseSequence. Deferred — these want fixed `StepSequence`/`PulseSequence` inputs which are richer to serialize; add when a test case actually needs them.

### D6 — `Constraints` schema for JSON

RFB's `Constraints` is defined in `engine/include/mforce/music/figure_constraints.h` with `std::optional<int>` / `std::optional<float>` members. I will check whether JSON round-trip already exists for it; if not, Task 1 adds it (small).

- **Default: add JSON for `Constraints` if not already present.** Necessary to round-trip `TwoFigurePhraseConfig` end-to-end.

### D7 — Warning behavior on misuse

- **Default: warn on** config absent, and on `figures` non-empty (ignored).
- **Default: silent on** ignored connectors / cadence / function (same pattern as WrapperPhraseStrategy).

---

## File layout

- **Modify:** `engine/include/mforce/music/templates.h` — add `TwoFigurePhraseConfig` struct + optional field on `PhraseTemplate`.
- **Modify:** `engine/include/mforce/music/figure_transforms.h` — add free function `apply(base, op, param, seed)` per D3.
- **Modify:** `engine/include/mforce/music/default_strategies.h` — `DefaultFigureStrategy::apply_transform` delegates to the free function (behavior preserved).
- **Create:** `engine/include/mforce/music/two_figure_phrase_strategy.h` — class declaration + inline body.
- **Modify:** `engine/include/mforce/music/composer.h` — include + register.
- **Modify:** `engine/include/mforce/music/templates_json.h` — JSON round-trip for `TwoFigurePhraseConfig` and, if missing, for `Constraints`.
- **Modify:** `tools/test_figures/main.cpp` — integration tests for the new strategy.

---

## Success criteria

- `test_figures` passes — existing tests remain green, new integration tests for two_figure_phrase pass.
- `two_figure_phrase` selectable via `phraseTmpl.strategy = "two_figure_phrase"`.
- RFB directly drives figure 1; `figure_transforms::apply` drives figure 2.
- JSON round-trip for `TwoFigurePhraseConfig` confirmed by an integration test that serializes a `PieceTemplate`, reads it back, composes from the loaded copy, and matches the in-code compose output.
- `DefaultFigureStrategy::apply_transform` behavior preserved (existing K467 render / `--compose` goldens unchanged — explicit K467 golden diff confirms).
- Step 3 entry in `docs/ComposerRefactor3.md` marked done with pointers to spec/plan.

---

## Out of scope for step 3

- Connector logic (elide, adjust, leadStep) between the two figures.
- Cadence adjustment.
- Multi-figure (>2) phrases.
- Retirement of old `DefaultPhraseStrategy` (step 6).
- Migration of K467 templates or other `patches/*.json` templates to use the new strategy.
