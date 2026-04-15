# Composer Composition Model — Design (Umbrella)

## Status

This spec supersedes:
- `2026-04-15-motif-pool-and-history-queries-design.md` (Spec A)
- `2026-04-15-period-forms-and-k467-design.md` (Spec B)

Spec A and Spec B are retained as historical record but are not the current design. The open-questions parking-lot doc `2026-04-15-plan-compose-phases-open-questions.md` remains valid and is referenced here.

This document is the consolidated current design for the composer's next implementation phase.

## Context

The composer strategy API refactor (landed 2026-04-14) cleaned up the strategy interface and introduced `Locus`. That refactor was infrastructure; what we're building on top is the first **phrase-aware compositional strategy** capable of composing Mozart K467 bars 1–12 (a double period) from user-authored motifs — and the data-model rework that makes it possible.

Over the course of the 2026-04-15 brainstorm, several architectural assumptions got unwound and reset:

- **Planning and composing are two distinct phases.** A strategy first *plans* (fills in the template, synthesizes motifs, makes compositional decisions) and then *composes* (realizes the resulting template into concrete notes). Strategies implement both.
- **Motifs in the pool are placement-neutral.** The first step of a figure is a *placement* concern, not a *shape* concern. It belongs on the connector between figures, not in the motif itself.
- **`FigureConnector` is mandatory per figure** (model-side), field-level-optional in JSON. It carries `elideCount`, `adjustCount`, and `leadStep`.
- **Cadential tails are multi-figure patterns**, not single motifs. The `Cadential` role applies per-figure; a phrase's cadential approach is several figures in sequence, with the last adjusted to land on the target degree.
- **Figures are the smallest atomic unit** and should be thought of as music-theory *motives*. K467's period 2 has ~11 figures across its two phrases.
- **`Transpose` is not a transform op** — sequence (same motive at different pitch levels) is just the same motif referenced twice with different `leadStep` values.

This spec captures the model that these realizations converge on.

## Problem

Today's composer can produce 4-bar single-phrase output via `AlternatingFigureStrategy` (AFS) or 2-period output via `PeriodPhraseStrategy` (whose name is a misnomer — it emits a whole period into a single `Phrase` object). Both are rigid and don't match the phrase structure of real classical repertoire.

The AFS impedance gap (strict A-B alternation per chord) doesn't match phrase-aware cadence placement. `PeriodPhraseStrategy` hard-codes 4 figures per period (basicIdea + antecedentTail + basicIdea + consequentTail), which breaks down for any period longer than 4 bars.

More deeply, there's no clean separation between "what the user wants" (intent, expressible partially) and "what the strategy produces" (a fully-specified realization). The system has no `plan` phase where a strategy takes partial intent and generates the rest of the template.

And the current motif model conflates *shape* (the thing that makes a motif identifiable) with *placement* (where a specific instance of that motif lands in its phrase). Two instances of the same motive at different pitch levels show up as two separate motifs, which is wrong both conceptually and for pool hygiene.

## Goals

- **Single canonical home for motifs**: `PieceTemplate::motifs`. `Composer`'s member maps go away. `Locus` drops its `Composer*` field.
- **Placement-neutral motifs**: pool entries store only the intra-motif shape. Placement (leadStep) lives on the connector between figures.
- **Rich motif metadata** sufficient for role-based picking: multi-tag role set (`Thematic`, `Cadential`, `PostCadential`, `Discursive`, `Climactic`, `Connective`, `Ornamental`), origin (User/Generated/Derived/Extracted), derivation chain (derivedFrom + transform + transformParam).
- **Mandatory `FigureConnector` with `{elideCount, adjustCount, leadStep}`**, field-level optional in JSON with a bare-int shorthand for the common "only leadStep matters" case.
- **Two-phase strategy interface** at every level: `plan_*` produces a self-contained template (mutating the pool as needed), `compose_*` realizes the template to concrete content (no pool access).
- **Template-side period scaffolding**: `PassageTemplate.periods[]` with `PeriodSpec` entries. Runtime stays genre-neutral (`Passage → Phrase → Figure`).
- **`PeriodPassageStrategy`** that consumes `periods[]`, plans derivation (parallel/modified/contrasting), handles multi-figure cadential tails, supports leading-connective motifs between periods.
- **K467 bars 1–12** composes from user-authored motifs via `PeriodPassageStrategy`, producing a render recognizable as Mozart's opening melody. A new golden hash pins the output.
- **History queries** beyond `pitch_before`: `reader_before`, `range_in_phrase_before`, `range_in_passage_before`, `range_in_piece_before`.
- **No regression** on the five existing K467 golden renders from `2026-04-14-baseline-hashes.txt`.

## Non-goals

- Sentence form, rondo, sonata form, fugal exposition, or other classical forms beyond the Period.
- Automatic period-variant inference from motifs (template specifies variant; strategy doesn't guess).
- Multi-Part coordination (K467 bars 1-12 is melody-only).
- UI for motif or period authoring.
- `PeriodPhraseStrategy` renaming or deletion — it stays as backward-compat.
- Cross-Part history queries.
- Automatic regeneration of derived motifs when a parent changes.
- Orphan-motif cleanup after regen (parked in `2026-04-15-plan-compose-phases-open-questions.md`).
- Transactional pool mutation (parked).

## Design

### Motif — placement-neutral shape plus metadata

```cpp
enum class MotifRole {
  Thematic, Cadential, PostCadential, Discursive,
  Climactic, Connective, Ornamental,
};

enum class MotifOrigin { User, Generated, Derived, Extracted };

struct Motif {
  std::string name;
  std::variant<MelodicFigure, PulseSequence, StepSequence> content;

  std::set<MotifRole> roles;                    // multi-tag; empty = untyped
  MotifOrigin origin{MotifOrigin::User};
  std::optional<std::string> derivedFrom;       // parent motif name (walk for root)
  std::optional<TransformOp> transform;         // how derivation was done
  int transformParam{0};

  uint32_t generationSeed{0};                    // EXISTING
};
```

**Placement-neutral convention:** when `content` is a `MelodicFigure`, the first unit's `step` is defined to be `0`. The shape of the motif is the durations of all N units and the intra-motif steps (units 2..N). Placement happens via the `FigureConnector.leadStep` at the reference site.

This convention is enforced at load time: the JSON loader normalizes motif content so that the first unit's step is 0 on entry (and emits a warning if the JSON had a nonzero first step). Strategies that construct motifs via FigureBuilder and then add them to the pool must similarly normalize — a utility helper `Motif::placement_neutral(MelodicFigure)` is provided.

`PulseSequence` and `StepSequence` motifs don't have this concern (they don't encode placement).

### `FigureConnector` — unified between-figures join

```cpp
struct FigureConnector {
  int elideCount{0};      // trim N trailing units from prior figure
  float adjustCount{0};   // adjust prior figure's last-surviving-unit duration (+ extend, - shorten)
  int leadStep{0};        // step to place this figure's first note from post-elide/adjust cursor
};
```

**Model-side mandatory, JSON-side lean.** Every figure in a `PhraseTemplate` has a corresponding `FigureConnector` in `PhraseTemplate.connectors` (size = `figures.size()`). `connectors[0]` carries the connection into the phrase's first figure from the phrase's starting pitch (no longer placeholder/unused as in the pre-rework model).

JSON accepts three forms:

- **Full object**: `{"elideCount": 1, "adjustCount": -0.5, "leadStep": -1}` — when all three fields matter.
- **Partial object**: `{"leadStep": -1}` — unspecified fields default to 0.
- **Bare integer shorthand**: `-1` — interpreted as `{leadStep: -1, elideCount: 0, adjustCount: 0}`.

If a connector is entirely default (`{0, 0, 0}`) it may be omitted from the JSON array by writing `null` (or omitting it, with the loader filling). The loader always produces a dense `connectors` array in memory.

### `Locus` cleanup

```cpp
struct Locus {
  const Piece* piece;
  PieceTemplate* pieceTemplate;     // now non-const — plan_* may mutate the pool
  int sectionIdx, partIdx;
  int passageIdx{-1}, phraseIdx{-1}, figureIdx{-1};
  // with_passage/phrase/figure helpers unchanged.
};
```

`Composer*` is gone. `pieceTemplate` becomes a mutable pointer so `plan_*` methods can invoke pool-write operations directly. During `compose_*`, the contract is that the pool is not modified (enforceable only by discipline; the type system allows mutation).

### `PieceTemplate` — motif pool + API

```cpp
struct PieceTemplate {
  // ... existing fields ...

  std::vector<Motif> motifs;
  std::unordered_map<std::string, MelodicFigure>  realizedFigures;
  std::unordered_map<std::string, PulseSequence>  realizedRhythms;
  std::unordered_map<std::string, StepSequence>   realizedContours;

  // --- Read API (available always, const-friendly) ---
  const Motif*          find(const std::string& name) const;
  const MelodicFigure*  find_motif(const std::string& name) const;
  const PulseSequence*  find_rhythm_motif(const std::string& name) const;
  const StepSequence*   find_contour_motif(const std::string& name) const;
  std::vector<const Motif*> motifs_with_role(MotifRole role) const;
  std::vector<const Motif*> derivatives_of(const std::string& parentName) const;

  // --- Write API (called from plan_* phase) ---
  // Idempotent on name: if a motif with that name exists, replace content and
  // metadata (honoring any `locked` flag we eventually add).
  void add_motif(Motif motif);

  // Convenience: synthesize a derived motif via TransformOp applied to parent,
  // store with origin=Derived, return the chosen name. Idempotent: if a motif
  // already exists with this (parent, transform, transformParam), that name
  // is returned without creating a duplicate.
  std::string add_derived_motif(const std::string& parentName,
                                TransformOp transform,
                                int transformParam = 0,
                                std::optional<std::string> explicitName = std::nullopt);
};
```

Realization of pool contents happens via a free function `mforce::realize_motifs(PieceTemplate&)`, called by `Composer::setup_piece_` after template load and before any strategy runs. When strategies add motifs during `plan_*`, they call `pieceTemplate->add_motif(motif)`, which both registers the declaration and realizes it (populating the appropriate realized map).

Ordering constraint: derived motifs require their parents to be realized first. The loader and `realize_motifs` perform a topological pass by walking `derivedFrom` chains.

### Two-phase strategy interface

```cpp
class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;

  // Default: return the seed unchanged. Strategies override if they have
  // planning work to do (synthesizing motifs, filling fields).
  virtual FigureTemplate plan_figure(Locus, FigureTemplate seed) { return seed; }
  virtual MelodicFigure  compose_figure(Locus, const FigureTemplate&) = 0;
};

// PhraseStrategy and PassageStrategy mirror the shape:
//   virtual XxxTemplate plan_xxx(Locus, XxxTemplate seed) { return seed; }
//   virtual Xxx compose_xxx(Locus, const XxxTemplate&) = 0;
```

**Pipeline:**

```
1. User / Composer pins top-level template choices (by hand or by coin-flip-and-pin).
2. Composer dispatches plan_* at top level. plan_* may recurse (calling plan_* on
   children via ctx.composer->plan_passage etc.) or build child templates inline.
3. plan_* returns a fully-specified template. Composer stores it in PieceTemplate
   (overwriting any prior instance). The motif pool has accreted any derived/
   generated motifs the strategy synthesized.
4. (Optional) User reviews the filled-in template and pool; locks / edits / rerolls.
5. Composer dispatches compose_* at top level, which walks the self-contained
   template and returns realized content. No pool access.
6. Realized content is stored in Piece.
```

**Coin-flip-and-pin in plan:** when `plan_*` encounters an unspecified template field (e.g., `PeriodVariant` is unset on a `PeriodSpec`), it rolls from RNG and writes the rolled value back into the template before returning. Subsequent rerun with the same seed produces the same pin. A future `locked: bool` per-field mechanism (parked) lets users distinguish "I specified this" from "plan rolled this".

**Compose-phase contract:** `compose_*` receives a `const XxxTemplate&` and must not mutate the pool. Every motif reference inside the template must already have its realized content available — either via a name reference into the pool (which is safe because the pool is populated) or via inline `lockedFigure` content on the FigureTemplate.

### Period scaffolding — template side

```cpp
enum class PeriodVariant { Parallel, Modified, Contrasting };

struct PeriodSpec {
  PeriodVariant variant{PeriodVariant::Parallel};
  float bars{4.0f};

  PhraseTemplate antecedent;    // ends on HC
  PhraseTemplate consequent;    // ends on IAC or PAC (per cadenceType)

  std::optional<TransformOp> consequentTransform;
  int consequentTransformParam{0};

  std::optional<std::string> leadingConnective;  // motif-pool name
};

struct PassageTemplate {
  // ... existing ...
  std::vector<PeriodSpec> periods;   // NEW — optional, consumed by period-aware strategies
};
```

`PhraseTemplate` is unchanged from today (apart from `connectors[]`'s new semantics for `[0]`). Period metadata lives one level up.

### `PeriodPassageStrategy` — Option α (variants inlined)

```cpp
class PeriodPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "period_passage"; }
  PassageTemplate plan_passage(Locus, PassageTemplate seed) override;
  Passage         compose_passage(Locus, const PassageTemplate&) override;
};
```

**`plan_passage` pseudocode:**

```
for each PeriodSpec p in seed.periods:
  # Fill any unspecified fields in p via coin-flip (variant, transform, bars, etc.),
  # writing pins back into the PeriodSpec.
  normalize_period_spec(p)

  # Synthesize consequent motifs as the variant demands.
  switch p.variant:
    case Parallel:
      # consequent references antecedent's motifs verbatim; only cadence differs.
      resolve_consequent_as_parallel(p)
    case Modified:
      # synthesize derived motifs via pieceTemplate->add_derived_motif, store names
      # as FigureTemplate references in p.consequent.figures.
      resolve_consequent_as_modified(p, pieceTemplate)
    case Contrasting:
      # consequent uses its own authored figures; plan pulls appropriate motifs
      # from the pool by role (role=Thematic for basicIdea, role=Cadential for tail).
      resolve_consequent_as_contrasting(p, pieceTemplate)

  # Plan the cadential tail. Pick N figures with role=Cadential from the pool
  # (or synthesize) as the last N figures of the consequent.figures. Adjust the
  # last figure's last unit step to land on cadenceTarget.
  resolve_cadential_tail(p.antecedent, pieceTemplate)
  resolve_cadential_tail(p.consequent, pieceTemplate)

# Flatten periods[] into the passage's phrases[] list: [ante0, consq0, ante1, consq1, ...].
# Optionally insert a connective-motif phrase between periods when leadingConnective is set.
for each period p, index pi:
  if pi > 0 and p.leadingConnective is set:
    seed.phrases.append(build_connective_phrase(p.leadingConnective))
  seed.phrases.append(p.antecedent)
  seed.phrases.append(p.consequent)

# Return the seed with periods[] preserved (for UI display / re-plan) AND with
# phrases[] populated as the authoritative input to compose_passage.
return seed
```

**`compose_passage`** walks `seed.phrases` (which is now populated) and dispatches each phrase to `StrategyRegistry::instance().resolve_phrase(phraseTmpl.strategy)->compose_phrase(locus.with_phrase(i), phraseTmpl)`. Same pattern as `DefaultPassageStrategy` today, just renamed.

### Multi-figure cadential tails

A phrase's *cadential tail* is the final run of figures in the phrase with `role=Cadential` (or explicitly marked as tail in the template). Length varies: simple cadence = 1 figure; K467-style = 2–3 figures.

Only the last figure is step-adjusted to land on `cadenceTarget`. Prior figures in the tail contribute the approach trajectory but aren't target-adjusted individually.

`apply_cadence` (currently in `DefaultPhraseStrategy::apply_cadence`) stays the correct mechanism — it already operates on the last figure of the phrase, which under the new model is the arrival figure of the cadential tail.

### History queries in `piece_utils`

```cpp
namespace mforce::piece_utils {

// EXISTING: passage_at, pitch_before.

PitchReader reader_before(const Locus& locus);  // seeded to pitch_before(locus)

struct PitchRange { Pitch lowest; Pitch highest; bool empty{true}; };
PitchRange range_in_phrase_before(const Locus&);
PitchRange range_in_passage_before(const Locus&);
PitchRange range_in_piece_before(const Locus&);  // limited to locus.partIdx

}
```

Semantics: "up to but not including this Locus position, within the named scope". Used by compose phase only (and potentially plan phase for register-aware decisions).

### K467 bars 1–12 reference instance

**Motif pool** (user-authored in UI or JSON):

```
Fig1:              { MelodicFigure, roles: [Thematic] }                        # bar 1 triadic descent
Fig2:              { MelodicFigure, roles: [Cadential] }                       # bar 2 scalar descent w/ triplet
Fig1_mod:          { MelodicFigure, roles: [Thematic], origin: Derived,
                     derivedFrom: Fig1, transform: Invert }                    # bar 3 variant
Fig2_mod:          { MelodicFigure, roles: [Cadential], origin: Derived,
                     derivedFrom: Fig2, transform: VaryRhythm }                # bar 4 variant
Fig5:              { MelodicFigure, roles: [Thematic] }                        # bars 5, 9 long-note+scalar
Fig6:              { MelodicFigure, roles: [Thematic] }                        # bars 6, 10 parallel to Fig5
A_core:            { MelodicFigure, roles: [Cadential] }                       # the 3-repeated-note motive
A_rhythm_tail:     { PulseSequence, roles: [Cadential], origin: Derived,
                     derivedFrom: A_core, transform: RhythmTail }
C_arrival:         { MelodicFigure, roles: [Cadential] }                       # bar 8 trilled arrival
F_tonic:           { PulseSequence, roles: [Cadential] }                       # whole-note arrival (rhythm only)
Lead:              { MelodicFigure, roles: [Connective] }                      # pickup ascent into bar 5
```

**Passage template** (authored, or output of `plan_passage` from a thinner seed):

```json
{
  "name": "K467_i_opening",
  "strategy": "period_passage",
  "periods": [
    {
      "variant": "Modified",
      "bars": 4,
      "antecedent": {
        "figures": [ {"motif": "Fig1"}, {"motif": "Fig2"} ],
        "connectors": [ {"leadStep": 0}, {"leadStep": -3} ],
        "cadenceType": 1
      },
      "consequent": {
        "figures": [ {"motif": "Fig1_mod"}, {"motif": "Fig2_mod"} ],
        "connectors": [ {"leadStep": 0}, {"leadStep": -3} ],
        "cadenceType": 2, "cadenceTarget": 2
      },
      "consequentTransform": "Invert"
    },
    {
      "variant": "Parallel",
      "bars": 8,
      "antecedent": {
        "figures": [
          {"motif": "Fig5"}, {"motif": "Fig6"},
          {"motif": "A_core"}, {"motif": "A_core"}, {"motif": "C_arrival"}
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
        "figures": [
          {"motif": "Fig5"}, {"motif": "Fig6"},
          {"motif": "A_core"},
          {"motif_rhythm": "A_rhythm_tail", "steps": [1, -1]},
          {"motif_rhythm": "A_rhythm_tail", "steps": [1, 1]},
          {"motif": "F_tonic"}
        ],
        "connectors": [
          {"leadStep": 2},
          {"leadStep": -1},
          {"leadStep": -1},
          {"leadStep": 1},
          {"leadStep": 1},
          {"leadStep": -2}
        ],
        "cadenceType": 2, "cadenceTarget": 0
      },
      "leadingConnective": "Lead"
    }
  ]
}
```

The `{motif_rhythm: "A_rhythm_tail", steps: [1, -1]}` shape is a FigureTemplate variant: rhythm from a PulseSequence motif, steps authored inline. This expresses D and E (the rhythm-inherited-new-steps figures from the second cadence) without needing separate pool entries for each.

Render target: produces a WAV recognizable as K467's opening. Not required to be bit-identical to the DURN version — motifs are simplifications of Mozart's actual figures. A new golden hash pins this render in a follow-up addendum to `2026-04-14-baseline-hashes.txt`.

## Migration plan

Staged, each golden-hash verified against `2026-04-14-baseline-hashes.txt`.

**Stage 0 — Baseline reverify.** Inherit existing K467 goldens. Confirm current `main` still produces identical hashes.

**Stage 1 — Motif pool relocation.** Move pool from `Composer` to `PieceTemplate`. Composer's maps become thin forwarders during transition. Free function `mforce::realize_motifs(PieceTemplate&)` introduced. Verify.

**Stage 2 — Strategy call-site migration.** `locus.composer->*motif*` → `locus.pieceTemplate->*motif*`. Verify.

**Stage 3 — Drop `Composer*` from `Locus`.** Delete the field + fwd decl. Delete Composer's pool maps. `Locus::pieceTemplate` becomes non-const pointer. Verify.

**Stage 4 — `FigureConnector.leadStep` field.** Add the field (default 0). Existing templates without explicit leadStep load as 0. Existing code that sets the first unit's step directly still works (back-compat: loader normalization turns first-step-nonzero into leadStep + first-step-zero). Verify goldens.

**Stage 5 — `Motif` metadata expansion.** Add `roles`, `origin`, `derivedFrom`, `transform`, `transformParam`. JSON round-trip, defaults. Verify.

**Stage 6 — History query helpers.** `reader_before`, `range_in_*_before` in `piece_utils`. Additive, no consumer. Verify.

**Stage 7 — Plan/compose strategy split.** Add default `plan_*` methods (no-op returning seed) to `FigureStrategy` / `PhraseStrategy` / `PassageStrategy`. Rename `realize_*` → `compose_*` across all existing strategies and Composer's dispatchers. Composer's entry path now calls `plan_*` then `compose_*`. Verify (all existing strategies have no-op plan, so behavior unchanged).

**Stage 8 — `PeriodVariant` + `PeriodSpec` + `PassageTemplate.periods[]`.** Types + JSON, no consumer yet. Verify.

**Stage 9 — `PeriodPassageStrategy` skeleton.** Plan handles Parallel only; compose dispatches to `DefaultPhraseStrategy` for each phrase. Register under `"period_passage"`. Verify.

**Stage 10 — Modified + Contrasting variants.** Implement the variant branches, pool add/lookup for derived motifs. Verify.

**Stage 11 — Multi-figure cadential tail + leading connective.** Verify.

**Stage 12 — K467 template and motif pool.** Author `patches/test_k467_period.json` + motif definitions. Render. Pin hash as new golden.

Stages 7 and 9–12 are the substantive content; 0–6 are infrastructure prerequisites. Each stage independently revert-able.

## Resolved during brainstorm

- **Option C** (classical-form strategies with K467 as instance).
- **Runtime stays `Passage → Phrase → Figure`** — no `Period` type at runtime.
- **Template-side `periods[]`** on PassageTemplate (Option A).
- **Option α** — variant logic inlined in `PeriodPassageStrategy`.
- **Cadences**: period 1 = HC→IAC, period 2 = HC→PAC.
- **Coin-flip and synthesis happen inside strategies' `plan_*` phase**, not in a separate Composer pre-pass.
- **Motifs on `PieceTemplate`**; strategies write via `add_motif` / `add_derived_motif` during plan, read-only during compose.
- **Motif pool is placement-neutral.** First step of a figure is a placement concern on `FigureConnector.leadStep`.
- **`Transpose` not needed as TransformOp.** Sequence = reuse of same motif at different leadSteps.
- **Cadential tails are multi-figure sequences**; `Cadential` role is per-figure.
- **Cadential motifs can be PulseSequence-only for rhythm-stereotyped arrivals**, but approach figures typically need full MelodicFigure shape.
- **FigureConnector is mandatory per figure** in the model, field-level optional in JSON, bare-int shorthand for leadStep-only cases.
- **Role set**: `{Thematic, Cadential, PostCadential, Discursive, Climactic, Connective, Ornamental}`, multi-tag, empty default.
- **Derivation model**: `derivedFrom` stores immediate parent; root ancestor is a walk.
- **`PeriodPhraseStrategy` stays** as backward-compat; new work targets `PeriodPassageStrategy`.

## Open questions parked

See `2026-04-15-plan-compose-phases-open-questions.md`:

- Orphan motif cleanup after regen
- Cadential motif shape heuristic (when PulseSequence suffices)
- Plan phase at Figure/Phrase levels (addressed: default no-op, strategies opt in)
- Motif pool API atomic vs transactional
- Where resolved content lives in templates (addressed via `FigureTemplate::lockedFigure` reuse and `motif_rhythm` + inline steps for composites)

Additional open questions from the placement-neutral reshaping:

- **Loader normalization of legacy motifs.** When a JSON motif has a nonzero first step, the loader extracts it to the connector's leadStep and zeros the first step. What if that motif is referenced by multiple FigureTemplates — is the extracted leadStep preserved per-reference? Plan: normalization happens at load time, the loader emits a warning, and all references get the extracted value as their connector default. User can override per-reference.
- **RhythmTail TransformOp.** Referenced in K467 (`A_rhythm_tail = derivedFrom A_core + RhythmTail`). Not in the current `TransformOp` enum. Plan: add it alongside the existing transforms (Stage 5).

## What this enables

- **`PeriodPassageStrategy`** is the first phrase-aware passage strategy in the codebase. The AFS impedance gap closes for double-period forms.
- **K467 bars 1–12** composes from motif pool + period template.
- **Period vocabulary** (Parallel / Modified / Contrasting) in code and JSON, ready for reuse by future strategies.
- **Template/runtime separation** clarified: templates carry form-specific scaffolding, runtime stays genre-neutral.
- **Plan/compose split** establishes the pattern for future "generative" strategies that need to synthesize content before realizing it.
- **Placement-neutral motif pool** eliminates duplicate pool entries for sequenced figures; pool becomes a true identity-keyed store of distinct shapes.
- **Mandatory-but-terse `FigureConnector`** uniforms the model and gives `connectors[0]` a first-class role.
- **History queries** ready for use by cadential approach strategies (register tracking, climax avoidance).
- **Motif metadata** supports role-based picking in template authoring and plan-phase decisions.
