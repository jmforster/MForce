# Period Forms and K467 Bars 1–12 — Design

## Context

With the composer strategy API refactor landed (2026-04-14) and Spec A (`2026-04-15-motif-pool-and-history-queries-design.md`) upgrading the motif pool to first-class role-tagged entries with provenance, the composer is ready to gain its first **phrase-aware** compositional strategy. The next leverage point is a Strategy that can compose the opening 12 bars of Mozart's K467 Piano Concerto Andante — a **double period**, where period 1 is a 4-bar `modified` parallel period (HC → IAC) and period 2 is an 8-bar `parallel` period (HC → PAC).

This is the concrete target that the strategy refactor existed to serve. Getting K467 bars 1–12 to render algorithmically (given user-authored motifs) is the first proof that the phrase-aware composition pipeline works end-to-end and meaningfully improves on the AFS (AlternatingFigureStrategy) impedance gap called out in memory.

This is "Workstream 2 + 3" from the 2026-04-14 brainstorm arc. Workstream 2 adds the period-aware template fields; Workstream 3 adds the strategy that consumes them plus the K467 instance.

## Problem

### AFS impedance

The current AlternatingFigureStrategy (AFS) alternates chord-tone (A) and scalar (B) figures per chord across a chord progression: `A-B-A-B-A-B-A-B`. This aligns with phrase structure only when every chord ends a phrase. For K467's bars 1–12 the phrases are 2+2+4+4 — so only the last chord of each phrase ends a phrase, and AFS would still emit A-B pairs per chord, producing `A-B-A-B | A-B-A-B | …` where the reality wants `body | body | body | cadence` per phrase.

Every phrase-aware rendering today is done via DURN (literal notation). Algorithmic composition covers only single 4-bar phrases (via the v1-v4 structural templates).

### Period vocabulary is not yet representable

K467's opening is, in classical terms, a **double period**: two periods in sequence, period 1 with an IAC closing and period 2 with a PAC (so period 2 is the "stronger" closer). Within each period, an antecedent phrase (ends HC) is followed by a consequent phrase (ends IAC or PAC). The consequent relates to the antecedent by one of three **period variants**:

- **Parallel**: consequent's basic idea = antecedent's basic idea verbatim (parallel period)
- **Modified**: consequent's basic idea is a variant of the antecedent's (common in Mozart)
- **Contrasting**: consequent's basic idea is new material

K467 bars 1–4 = modified period; bars 5–12 = parallel period (expanded to 8 bars).

The current `PassageTemplate` is a flat list of `PhraseTemplate`s with no way to express "these two phrases form a period, period variant is modified". Every consumer has to infer phrase grouping from the flat list.

## Goals

- **Template expresses period structure directly**: `PassageTemplate` can carry an optional `periods[]` list of `PeriodSpec`s. Each period names its variant and its two phrases.
- **Runtime stays genre-neutral**: `Passage` still contains a flat list of `Phrase`s. Period structure is template-side only; it does not appear in the realized piece.
- **A new `PeriodPassageStrategy`** that reads `PassageTemplate.periods[]`, resolves each period's variant into concrete `PhraseTemplate`s (by copy / transform / independent lookup), realizes each phrase via a plain `PhraseStrategy`, and concatenates the results into a `Passage`.
- **Variant logic inlined** in one `PeriodPassageStrategy` class. Not split into `ParallelPeriodStrategy` / `ModifiedPeriodStrategy` / etc. — three variants are small switch cases.
- **K467 bars 1–12 as a concrete template instance** that uses `PeriodPassageStrategy` + user-authored motifs to produce a rendered WAV recognizable as K467's opening melody.
- **Backward compat**: existing `PeriodPhraseStrategy` (which produces a whole period as a single `Phrase` object, 4 figures) stays registered and functional. Current test patches (Period/Sentence smoke, `test_k467_v4`, etc.) keep rendering bit-identically.

## Non-goals

- **Sentence form, rondo, sonata form, fugal exposition, etc.** — other classical forms can come later; this spec only adds the Period vocabulary.
- **Automatic period-variant inference from motifs.** The template specifies the variant; the strategy doesn't guess.
- **Motif generation from thin air.** All motifs referenced by a `PeriodSpec` must already exist in the `PieceTemplate::motifs` pool (user-authored or prior-stage-generated). Spec A's metadata expansion enables role-based lookups; this spec does not add generation.
- **Multi-part coordination.** K467's bars 1–12 are melody-only. Adding a bass line is a later workstream (`PartRole::Bass`-aware strategies, voice leading).
- **UI for authoring periods.** This spec defines the data model, JSON, and the strategy. The UI work to compose periods interactively is separate.
- **Renaming `PeriodPhraseStrategy`.** It stays where it is. The new work is additive, not a rename / replace.

## Design

### `PeriodVariant`

```cpp
enum class PeriodVariant {
  Parallel,    // consequent motifs = antecedent motifs (verbatim)
  Modified,    // consequent motifs = derived via transform from antecedent motifs
  Contrasting, // consequent motifs are independent (separate names)
};
```

### `PeriodSpec`

Lives inside `PassageTemplate`. Describes one period's compositional intent.

```cpp
struct PeriodSpec {
  PeriodVariant variant{PeriodVariant::Parallel};
  float bars{4.0f};                                   // period length; informs chord-progression pacing

  PhraseTemplate antecedent;                          // ends on HC
  PhraseTemplate consequent;                          // ends on IAC or PAC (per cadenceType)

  // For Modified: how the consequent's basicIdea is derived from the antecedent's.
  // Ignored for Parallel (identity) and Contrasting (no derivation).
  std::optional<TransformOp> consequentTransform;
  int consequentTransformParam{0};

  // Optional leading connective figure — melodic material that bridges from
  // the PRECEDING period's cadence into this period's antecedent. For the
  // first PeriodSpec in the list, leadingConnective is typically absent
  // (the passage starts cleanly). For K467, period 2 has a leading
  // connective (pickup/ascent) from the period-1 IAC into bar 5.
  std::optional<std::string> leadingConnective;       // motif name in the pool
};
```

`PhraseTemplate` gains no new fields — it already carries `cadenceType`, `cadenceTarget`, `figures[]`. The antecedent/consequent PhraseTemplates inside a PeriodSpec are regular PhraseTemplates. Their `cadenceType` pins HC vs IAC vs PAC per classical convention.

### `PassageTemplate` changes

```cpp
struct PassageTemplate {
  // ... existing fields (strategy, phrases[], startingPitch, etc.) ...

  // NEW — optional. Consumed only by period-aware strategies
  // (e.g. PeriodPassageStrategy). When set, phrases[] is ignored by
  // period-aware strategies (they build the phrase list from periods[]).
  // Other passage strategies (DefaultPassageStrategy, AlternatingFigureStrategy)
  // ignore periods[] entirely.
  std::vector<PeriodSpec> periods;
};
```

No field on `PhraseTemplate` changes. Period metadata lives one level up.

### `PeriodPassageStrategy`

```cpp
class PeriodPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "period_passage"; }
  Passage realize_passage(Locus locus, const PassageTemplate& passTmpl) override;
};
```

Realization pseudocode:

```
Passage passage;
for (int pi = 0; pi < periods.size(); ++pi):
  PeriodSpec& p = periods[pi];

  # Optional leading connective: realize as a small Phrase prepended
  # (ONE figure containing the connective motif's content). Only for
  # periods where leadingConnective is set AND pi > 0.
  if (pi > 0 && p.leadingConnective):
    Phrase connPhrase = build_connective_phrase(locus, *p.leadingConnective)
    passage.add_phrase(connPhrase)

  # Resolve period into two concrete PhraseTemplates.
  (anteTmpl, consqTmpl) = resolve_period_variant(locus, p)

  # Dispatch each through a PhraseStrategy. Per Composer refactor convention,
  # resolve by name from the singleton registry. Default strategy name is
  # "default_phrase" if the per-phrase template doesn't override.
  anteStrategy = StrategyRegistry::instance().resolve_phrase(
      anteTmpl.strategy.empty() ? "default_phrase" : anteTmpl.strategy);
  Phrase ante = anteStrategy->realize_phrase(locus.with_phrase(passage.size()), anteTmpl);
  passage.add_phrase(ante);

  consqStrategy = ... same pattern ...;
  Phrase consq = consqStrategy->realize_phrase(locus.with_phrase(passage.size()), consqTmpl);
  passage.add_phrase(consq);

return passage;
```

### `resolve_period_variant` — the heart of the design

```
(ante, consq) = resolve_period_variant(Locus, PeriodSpec):
  # Antecedent is always used as-authored.
  ante = periodSpec.antecedent  # deep copy

  # Consequent starts from the antecedent as a base (for Parallel/Modified)
  # or from the spec's consequent directly (for Contrasting).
  switch periodSpec.variant:
    case Parallel:
      # Consequent's figure list = antecedent's figure list, verbatim,
      # with the consequent template's cadence type/target overriding.
      consq = periodSpec.antecedent  # deep copy
      consq.cadenceType   = periodSpec.consequent.cadenceType
      consq.cadenceTarget = periodSpec.consequent.cadenceTarget
      # Any user-specified figures in consequent are additive/override —
      # typically only the TAIL figures differ (cadence material).
      if periodSpec.consequent.figures is non-empty:
        consq.figures = periodSpec.consequent.figures

    case Modified:
      # Consequent's figures = antecedent's figures, with each motif-referencing
      # figure transformed by consequentTransform.
      consq = periodSpec.antecedent  # deep copy
      apply_transform_to_motif_refs(consq.figures,
                                     periodSpec.consequentTransform,
                                     periodSpec.consequentTransformParam)
      consq.cadenceType   = periodSpec.consequent.cadenceType
      consq.cadenceTarget = periodSpec.consequent.cadenceTarget
      if periodSpec.consequent.figures is non-empty:
        consq.figures = periodSpec.consequent.figures

    case Contrasting:
      # Consequent uses its own authored figures entirely.
      consq = periodSpec.consequent

  return (ante, consq)
```

The `apply_transform_to_motif_refs` helper walks the consequent's FigureTemplates. For `FigureSource::Reference` figures, it either (a) looks up a derived motif whose `derivedFrom` matches the reference AND whose transform matches the period's `consequentTransform` (preferring an existing derived motif in the pool), or (b) creates a transient `FigureSource::Transform` template pointing at the parent motif with the period's transform. Default: prefer existing derived motif in the pool; fall back to transient transform if none matches.

### K467 bars 1–12 template (reference instance)

Motif pool (authored by user in UI or JSON):

```
motifs:
  Fig1:  { content: <bar 1 triadic descent>, roles: [Thematic] }
  Fig2:  { content: <bar 2 scalar-descent with triplet>, roles: [Thematic, Cadential] }
  Fig1': { content: <Fig1 variant — ascending version for consequent>,
           roles: [Thematic], origin: Derived, derivedFrom: Fig1, transform: Invert }
  Fig2': { content: <Fig2 variant — cadence-tail alteration>,
           roles: [Cadential], origin: Derived, derivedFrom: Fig2, transform: VaryRhythm }
  Fig3:  { content: <bar 5 long-note + scalar>, roles: [Thematic] }
  Fig4:  { content: <bar 6/8 dotted-rhythm continuation>, roles: [Discursive, Cadential] }
  Fig4_short: { content: <Fig4 compressed for decisive PAC>,
                roles: [Cadential], origin: Derived, derivedFrom: Fig4, transform: Compress }
  Lead: { content: <pickup ascent into bar 5>, roles: [Connective] }
```

Passage template for bars 1–12:

```
PassageTemplate:
  name: "K467_i_opening"
  strategy: "period_passage"
  startingPitch: <some pitch>
  periods:
    - PeriodSpec:
        variant: Modified
        bars: 4
        antecedent:
          PhraseTemplate:
            figures: [ Reference(Fig1), Reference(Fig2) ]
            cadenceType: 1   # HC
        consequent:
          PhraseTemplate:
            figures: [ Reference(Fig1'), Reference(Fig2') ]
            cadenceType: 2   # IAC
            cadenceTarget: 2  # 3rd of tonic (imperfect)
        consequentTransform: Invert  # guides motif-ref resolution

    - PeriodSpec:
        variant: Parallel
        bars: 8
        antecedent:
          PhraseTemplate:
            figures: [ Reference(Fig3), Reference(Fig4), Reference(Fig3), Reference(Fig4) ]
            cadenceType: 1   # HC on bar 8
        consequent:
          PhraseTemplate:
            figures: [ Reference(Fig3), Reference(Fig4), Reference(Fig3), Reference(Fig4_short) ]
            cadenceType: 2   # PAC
            cadenceTarget: 0  # tonic (perfect)
        leadingConnective: "Lead"
```

The rendered output is expected to be *recognizable as* K467's opening melody. It does not need to be bit-identical to the DURN-composed version, because:
- the user-authored motifs are simplifications of Mozart's actual figures;
- rhythmic detail (trill in bar 8, specific triplet placements) is not encoded;
- the purpose is proving the period-aware pipeline, not Mozart reconstruction.

### Composer's role in filling unspecified fields

Per brainstorm agreement: when a template field is unspecified, the Composer fills it with a randomized-but-reasonable value **before strategies run**, and the filled-in value is pinned as a plain template value from that point on. The strategy sees a fully-specified template and behaves deterministically.

For `PeriodSpec`, unspecified fields and Composer default policies:

| Field | Default when unspecified |
|-------|-------------------------|
| `variant` | `Parallel` |
| `bars` | Sum of antecedent + consequent `totalBeats` if both non-zero; else `4.0f` |
| `consequentTransform` (when variant = Modified) | `VarySteps` (random neighbor-alteration) |
| `consequentTransformParam` | `0` |
| `leadingConnective` | `nullopt` (no connective) |

These defaults live in the JSON loader or in `PeriodPassageStrategy`'s pre-resolve normalization pass — TBD at plan time. Either way they happen before `resolve_period_variant` runs.

### JSON schema

`PeriodVariant` serializes as enum-name string (`"Parallel"` / `"Modified"` / `"Contrasting"`). `PeriodSpec` in JSON:

```json
{
  "variant": "Modified",
  "bars": 4.0,
  "antecedent": { /* PhraseTemplate JSON */ },
  "consequent": { /* PhraseTemplate JSON */ },
  "consequentTransform": "Invert",
  "consequentTransformParam": 0,
  "leadingConnective": "Lead"
}
```

`PassageTemplate` in JSON gains an optional `"periods"` array. Existing patches without `periods` load unchanged.

### Migration plan

**Stage 0 — Baseline.** Re-verify the 2026-04-14 K467 representative set still hashes correctly (inherits from Spec A's completion).

**Stage 1 — `PeriodVariant` + `PeriodSpec` types.** Add the enum and struct to `templates.h`. Add JSON round-trip. No consumer yet. Build + verify hashes.

**Stage 2 — `PassageTemplate.periods[]` field + JSON.** Add the field, extend `to_json` / `from_json`. Existing JSONs without `periods` load correctly. Build + verify hashes.

**Stage 3 — `PeriodPassageStrategy` skeleton.** Add the class header and registration. `realize_passage` body initially just iterates `periods[]` handling only the `Parallel` case (simplest). Register under name `"period_passage"`. Build + verify hashes (no existing template uses `period_passage`, so behavior is unchanged).

**Stage 4 — `Modified` and `Contrasting` cases.** Implement the variant switch logic, including `apply_transform_to_motif_refs`. Build + verify hashes.

**Stage 5 — Leading connective realization.** Implement `build_connective_phrase`. Build + verify hashes.

**Stage 6 — K467 template instance.** Author the K467 motif pool + PassageTemplate as `patches/test_k467_period.json` + companion motif JSON. Render via `mforce_cli --compose patches/PluckU.json renders/k467_period 1 --template patches/test_k467_period.json`. Confirm the WAV plays a recognizable K467-like opening. The hash of this render becomes a NEW golden (not a pre-existing baseline), pinned in a small addendum to `2026-04-14-baseline-hashes.txt`.

**Stage 7 — Composer default-fill pass.** Decide at plan time whether to implement now or defer. If now: add a `Composer::normalize_template_(PieceTemplate&)` method that walks PassageTemplate.periods and fills unspecified fields per the default table. Build + verify hashes (including the new K467 period golden).

Stages are independently revert-able. Each ends with a hash check.

## Resolved during brainstorm

- **Option C**: classical-form strategies; K467 is one instance.
- **Runtime stays `Passage → Phrase → Figure`** — no `Period` type at runtime.
- **Template-side Option A**: `PassageTemplate.periods[]`; PhraseTemplate stays clean.
- **Option α**: variant logic inlined in one `PeriodPassageStrategy`, not split into `ParallelPeriodStrategy` / etc.
- **Cadences**: period 1 = HC→IAC, period 2 = HC→PAC (classical double-period arc).
- **Coin-flip at Composer**, not inside strategy. Random fills become pinned template values.
- **Motifs on `PieceTemplate` pool**, user-authored in UI. Connectives are first-class pool entries when they have melodic identity (K467's `Lead` = yes).
- **Keep `PeriodPhraseStrategy`** as legacy — existing patches still use it.

## Open questions to settle during planning

- **Connective figure realization detail.** A leading connective is a motif that should render into a short Phrase — but how many figures, and how does it connect to the following antecedent's starting pitch? Options: (a) one-figure Phrase containing the connective motif verbatim, let the antecedent's startingPitch be computed from the connective's end; (b) merge the connective figure into the antecedent Phrase as a pickup. Plan picks one.
- **Modified variant and derived motifs in the pool.** When `consequentTransform` is set, `apply_transform_to_motif_refs` can either look up an existing derived motif (e.g., `Fig1'` whose `derivedFrom = Fig1`) or synthesize a transient transform. Which takes precedence, and how does the user express "use this specific derived motif, don't synthesize"? Lean: explicit `Reference(Fig1')` in the consequent's figures overrides everything else; if the consequent's figures are empty AND `consequentTransform` is set, synthesize.
- **Passage-level `startingPitch` for a period-only passage.** If `PassageTemplate.periods[]` is set and `phrases[]` is empty, where does the passage's starting pitch come from? Option: the first `PeriodSpec.antecedent.startingPitch`, or PassageTemplate.startingPitch directly (existing field). Plan picks.
- **Stage 7 scope.** Whether the Composer's default-fill normalization lands with this spec or as its own later workstream. If later, every K467-demo template must be fully specified (no reliance on defaults).

## What this enables

With this work landed:

- `PeriodPassageStrategy` is the first phrase-aware passage strategy in the codebase. The AFS impedance gap is closed for double-period forms.
- K467 bars 1–12 can be composed from motif pool + period template, producing a recognizable opening.
- The Period vocabulary (`Parallel`, `Modified`, `Contrasting`) is authored and operable in code, ready to be reused by future strategies.
- The pattern (template carries form-specific scaffolding, runtime stays generic) is established for future form strategies — Sentence, Small Ternary, Rondo can follow the same template-scaffolding-plus-passage-strategy recipe.
- Downstream work on multi-part coordination (bass, harmony, voicing) has a clear integration point: the same PassageTemplate can host a period structure for the melody Part while other Parts carry their own PassageTemplates against the same Section's chord progression.
