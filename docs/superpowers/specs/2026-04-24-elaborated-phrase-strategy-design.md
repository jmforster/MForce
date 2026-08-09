# ElaboratedPhraseStrategy + FC Foundation Cleanup — Design Spec

**Date:** 2026-04-24
**Context:** First concrete strategy emerging from ComposerRefactor3 step-4 brainstorm. Iterated through many design conversations (recursive elaboration / Auskomponierung / "composing-out") and converged on a deliberately minimal Phase 1 plus a small foundation cleanup of FC handling that the new strategy depends on.
**Status:** Draft.
**Predecessor:** Step 3 (TwoFigurePhraseStrategy) on `figure-builder-redesign` through `e602905`.

---

## Active architectural decisions (locked this session)

1. **StepSequence / PulseSequence same length.** SS[0] is always 0 (a placeholder; the figure has no within-figure displacement at unit 0 because the figure's pitch start is positioned externally). Code already enforces same length; code/comments still describe SS[0] as "the bridge from previous figure" — stale.

2. **FC vector is dense.** `phrase.connectors.size() == phrase.figures.size()`. **FC[0] is a dummy with `leadStep = 0`.** FC[i>0] carries the real inter-figure cursor advance. Matches the SS[0]=0 placeholder pattern.

3. **Figure 0 is positioned by `phrase.startingPitch`.** FC[0] does not modify it. (The current `composer.h` line 1264-1265 comment that says "connectors[0].leadStep places the first figure relative to startingPitch" is fabricated drift from prior-session Claude — to be removed.)

4. **Figures are never mutated.** Cursor advance happens at realize/walk time via FC.leadStep. The `fig.units[0].step += leadStep` additive merge in `DefaultPhraseStrategy::compose_phrase` is also drift to be removed; it bakes leadStep into the figure data.

5. **Inter-figure leadStep math** (when a strategy generates both figures and FCs):
   - `FC[0].leadStep = 0` (dummy).
   - `FC[i>0].leadStep = skeleton.step[i] − E_{i−1}.net_step` — copy of the skeleton step, adjusted for any pitch drift the prior elaboration caused.

---

## Goal

Two pieces, ordered.

**A. Foundation cleanup** (must come first; the new strategy depends on it):
- Add `std::vector<FigureConnector> connectors` to `struct Phrase` (dense, parallel to `figures`).
- Update `realize_phrase_to_events_` to advance cursor by `phrase.connectors[f].leadStep` before walking figure f.
- Update `DefaultPhraseStrategy::compose_phrase` to populate `phrase.connectors` instead of mutating `fig.units[0].step += leadStep`.
- K467 goldens preserved bit-identically (the math is equivalent: `step_note(cursor, leadStep) → step_note(cursor, 0)` walks the same path as `step_note(cursor, leadStep + 0)`).

**B. ElaboratedPhraseStrategy Phase 1**:
- New phrase strategy `"elaborated_phrase"`.
- Strategy builds (or accepts) a skeleton MelodicFigure.
- Strategy walks skeleton anchors. For each, randomly chooses `Leave` (single-note figure) or `Generate` (RFB-built figure of matching duration).
- Strategy populates `phrase.figures` + `phrase.connectors` per the leadStep math above.
- JSON round-trip for the new config.
- Integration tests in `test_figures`.

---

## Foundation refactor

### `struct Phrase` extension (structure.h)

```cpp
struct Phrase {
    Pitch startingPitch;
    std::vector<std::unique_ptr<Figure>> figures;
    std::vector<FigureConnector> connectors;   // NEW: dense, size == figures.size after compose
    // ... existing ctors / move / clone ...
};
```

Default-constructed `FigureConnector` has all zeros. New convenience helper or direct push when a strategy adds a figure+FC pair.

### `DefaultPhraseStrategy::compose_phrase` changes (composer.h:1180-1283)

- **Delete** the additive-merge block (currently composer.h:1264-1273):
  ```cpp
  if (i < int(phraseTmpl.connectors.size())
      && phraseTmpl.connectors[i] && !fig.units.empty()) {
    fig.units[0].step += phraseTmpl.connectors[i]->leadStep;
  }
  ```
- **Add** at the same point: copy whatever FC the template provides (or default-construct) into `phrase.connectors`:
  ```cpp
  FigureConnector fc{};  // leadStep=0, elide=0, adjust=0
  if (i < int(phraseTmpl.connectors.size()) && phraseTmpl.connectors[i]) {
    fc = *phraseTmpl.connectors[i];
  }
  phrase.connectors.push_back(fc);
  ```
- **Keep** the elide/adjust block (composer.h:1190-1202) — that legitimately mutates the *prior* figure's shape (truncate trailing units, adjust last unit's duration). Not a "figure pitch" mutation; allowed.
- **Keep** the `runningReader.step(fig.net_step())` advance — this is the in-strategy cursor for the Literal-figure path. Under the new model, `fig.units[0].step` is 0, so `fig.net_step()` no longer includes leadStep; we add it separately:
  ```cpp
  runningReader.step(fc.leadStep);   // NEW: account for the FC's advance
  runningReader.step(fig.net_step());
  ```

### `realize_phrase_to_events_` changes (composer.h:373+)

In the figure loop (composer.h:386):
```cpp
for (int f = 0; f < phrase.figure_count(); ++f) {
    const auto& fig = *phrase.figures[f];

    // NEW: advance cursor by FC.leadStep before walking the figure.
    // FC[0].leadStep = 0 by convention, so this is a no-op for figure 0.
    if (f < int(phrase.connectors.size())) {
        currentNN = step_note(currentNN, phrase.connectors[f].leadStep, scale);
    }

    // ... existing walk of fig.units ...
}
```

### Backward compatibility

- Existing K467 templates: per the original 28b707e commit message, they author `leadStep=0`. So removing the additive mutation is an arithmetic no-op for K467.
- Commit a67458b extended the application to i=0 to support "placement-neutral motifs". If any K467 (or other) template authors `connectors[0].leadStep != 0`, that value is now applied at realize time instead of via mutation — same end-effect, K467 goldens unchanged.
- The strict convention "FC[0]=0 dummy / figure 0 positioned by phrase.startingPitch" applies to **new strategies** (ElaboratedPhraseStrategy). Legacy templates that author non-zero `connectors[0].leadStep` are tolerated for backward compat; an audit + migration to `phrase.startingPitch` is a follow-on cleanup, not Phase 1.

### What stays the same

- `PhraseTemplate.connectors` keeps its current type `std::vector<std::optional<FigureConnector>>` (authoring side allows sparse).
- `FigureConnector` itself unchanged (has `elideCount`, `adjustCount`, `leadStep`).
- ChordFigure path in realize: same advance-by-leadStep treatment if Phrase.connectors covers chord-figure phrases too. (Phase 1 only tests via melodic phrases; chord-phrase coverage is verified by K467 goldens not drifting.)

---

## ElaboratedPhraseStrategy Phase 1

### Registration

Name: `"elaborated_phrase"`. Registered in `Composer` constructor next to `wrapper_phrase` / `two_figure_phrase`.

### Config

```cpp
struct ElaboratedPhraseConfig {
    // The skeleton — structural backbone. If absent, strategy builds via RFB.
    std::optional<MelodicFigure> skeleton;

    // RFB build spec for the skeleton (used when skeleton is absent).
    enum class Method { ByCount, ByLength, Singleton };
    Method buildMethod{Method::ByCount};
    int   buildCount{4};       // ByCount
    float buildLength{4.0f};   // ByLength
    Constraints buildConstraints;

    // 0 means "use phraseTmpl.seed, else a deterministic default"
    uint32_t seed{0};
};
```

Optional field on PhraseTemplate:
```cpp
std::optional<ElaboratedPhraseConfig> elaboratedConfig;
```

No per-anchor override vector in Phase 1. The strategy decides every anchor.

### Behavior

1. **Starting pitch**: `phraseTmpl.startingPitch` if set, else `pitch_before(locus)`.
2. **If `elaboratedConfig` absent**: warn, return empty Phrase.
3. **If `phraseTmpl.figures` non-empty**: warn (strategy uses elaboratedConfig), proceed.
4. **Resolve seed**: `cfg.seed` > `phraseTmpl.seed` > `0xE1AB0AAFu`.
5. **Build skeleton**:
   - If `cfg.skeleton` present, use it.
   - Else: `RandomFigureBuilder(seed)` → `build_by_count` / `build_by_length` / `build_singleton` per `cfg.buildMethod`.
6. **Walk skeleton anchors**. For each `i in [0, skeleton.units.size())`:
   - **Pick choice ∈ {Leave, Generate}** by uniform random (50/50). Reuse the seeded RNG.
   - **Build elaboration figure E_i**:
     - **Leave** → single-unit `MelodicFigure` with `units = [{duration: skeleton.units[i].duration, step: 0}]`.
     - **Generate** → `rfb.build_by_length(skeleton.units[i].duration, generateConstraints)`. The RFB output's `units[0].step` is 0 by convention.
   - **Compute leadStep**:
     - For `i == 0`: `leadStep = 0` (FC[0] dummy).
     - For `i > 0`: `leadStep = skeleton.units[i].step − E_{i−1}.net_step`.
   - **Append** to `phrase.connectors` a default `FigureConnector` with `leadStep` set.
   - **Append** `E_i` to `phrase.figures` via `phrase.add_melodic_figure(...)`.
7. **Return** the phrase.

Invariant after compose: `phrase.figures.size() == phrase.connectors.size()` and both equal `skeleton.units.size()`.

### What's deliberately NOT here

- No auto-decide policy with rep-then-vary bias. Random per anchor.
- No move vocabulary beyond Leave / Generate.
- No constraints on RFB-generated figures' `net_step` (so the bridging math's `− E_{i−1}.net_step` term is real and exercised).
- No MotifPool integration.
- No ornaments on output figures.
- No cadence-aware skeleton arrival.
- No per-anchor config override.
- No literal-skeleton vs build-spec discrimination beyond "if skeleton present, use it; else build".

These are all explicit later phases.

### JSON round-trip

```json
{
  "name": "test",
  "strategy": "elaborated_phrase",
  "elaboratedConfig": {
    "skeleton": null,
    "buildMethod": "byCount",
    "buildCount": 4,
    "buildLength": 4.0,
    "buildConstraints": { "count": 4 },
    "seed": 0
  }
}
```

Method enum serializes lowercase camelCase: `"byCount"`, `"byLength"`, `"singleton"` (matching `TwoFigurePhraseConfig::Method` precedent if it does the same; otherwise just consistent with itself).

---

## File layout (Phase 1)

| File | Change |
|---|---|
| `engine/include/mforce/music/structure.h` | Add `connectors` field to `struct Phrase` |
| `engine/include/mforce/music/composer.h` (DefaultPhraseStrategy::compose_phrase) | Drop additive mutation; populate phrase.connectors; add runningReader.step(fc.leadStep) |
| `engine/include/mforce/music/composer.h` (realize_phrase_to_events_) | Advance cursor by FC.leadStep before walking each figure |
| `engine/include/mforce/music/templates.h` | Add `ElaboratedPhraseConfig` + optional field on `PhraseTemplate` |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for `ElaboratedPhraseConfig` |
| `engine/include/mforce/music/elaborated_phrase_strategy.h` | NEW — strategy class |
| `engine/include/mforce/music/composer.h` (registration) | Include + register |
| `tools/test_figures/main.cpp` | Integration tests (3-4 cases) |
| `docs/ComposerRefactor3.md` | Mark Phase 1 done with pointers |

---

## Pending decisions (defaults locked in)

- **D1** — Strategy name: `"elaborated_phrase"`.
- **D2** — Move vocabulary in Phase 1: `Leave` / `Generate`. Random 50/50.
- **D3** — Skeleton config shape: literal `MelodicFigure` (optional) + RFB build spec inline. No separate "build spec" wrapper struct.
- **D4** — Phrase.connectors type: `std::vector<FigureConnector>` (dense, no optional). Default-constructed entries have all zeros.
- **D5** — Backward compat for K467 templates that author non-zero `connectors[0].leadStep`: honored at realize time (cursor advances by it). New strategies follow the FC[0]=0 convention.
- **D6** — Integration tests: at minimum (a) Leave-forced-via-seed, (b) Generate-forced-via-seed, (c) mixed-via-seed, (d) JSON round-trip. Forcing requires either a deterministic RNG (controlling the random pick by seed) or a temporary Phase-1-only config knob; pick whichever lands cleaner during the plan.

---

## Success criteria

- K467 goldens unchanged after foundation refactor (the math is equivalent; behavior preservation is mechanical).
- All existing `test_figures` tests pass after foundation refactor.
- `elaborated_phrase` registered, dispatchable via `phraseTmpl.strategy`.
- New integration tests pass.
- JSON round-trip test confirms config serializes and re-composes equivalently.
- `phrase.figures.size() == phrase.connectors.size() == skeleton.units.size()` invariant holds.
- `phrase.connectors[0].leadStep == 0` for outputs of the new strategy.

---

## Out of scope for Phase 1

- Auto-decide policy (rep-then-vary or otherwise).
- Move vocabulary expansion (Turn / Neighbor / Split / Replace-with-cell / parametric).
- MotifPool integration with role tagging.
- Ornaments on output figures (Trill, Marcato, etc.).
- Cadence-aware skeleton arrival.
- Constraints on RFB-generated elaboration `net_step`.
- Per-anchor recipe override in config.
- `phraseTmpl.startingPitch` migration of legacy templates that author non-zero `connectors[0].leadStep`.
- Helper-class extraction across phrase strategies (rule of three; revisit when next strategy lands).
- Renaming "skeleton" to a better term (Matt flagged the term as not-good; deferred).
