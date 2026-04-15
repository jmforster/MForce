# Motif Pool Upgrade and History Queries — Design

> **SUPERSEDED** by `2026-04-15-composer-composition-model-design.md` (same day). That umbrella spec incorporates and extends the material in this doc with the plan/compose two-phase interface, placement-neutral motifs, and the FigureConnector rework that followed this spec in the brainstorm. Retained as historical record of the intermediate thinking.

## Context

The composer strategy API refactor (landed 2026-04-14) left one open debt item: the motif pool lives on `Composer` (`realizedMotifs_` + `realizedRhythms_` + `realizedContours_`) and is reached from strategies via a `Composer*` carried on `Locus`. The plan always was to move the pool onto `PieceTemplate` so templates are the canonical source of compositional DNA. This spec settles that relocation and, at the same time, expands what each `Motif` can carry so downstream strategies can pick motifs intelligently rather than by name alone.

Separately but in the same spirit, `piece_utils` today offers `pitch_before(Locus)` — a single query that lets a strategy ask "what pitch precedes my position?". Period-aware strategies and cadential-approach strategies will want more: the pitch range already seen in this phrase/passage, a `PitchReader` seeded to the prior cursor, etc. This spec adds that query surface.

Together this is "Workstream 0 + 1" from the 2026-04-14 brainstorm arc that leads toward algorithmic composition of K467 bars 1–12.

## Problem

### Motif pool home

`Motif` declarations live on `PieceTemplate::motifs` (a `std::vector<Motif>`). Realized forms live on `Composer` member maps and are populated in `Composer::realize_motifs_` during `setup_piece_`. Strategies reach these via `locus.composer->realized_motifs()` / `find_rhythm_motif()` / `find_contour_motif()`. This split is a refactor artifact: before the Locus migration the path was `ctx.composer->*` and during the refactor we preserved the Composer-side cache rather than broaden the template.

Problems:

- Two places to think about when motifs change — author them on the template, look them up on the Composer.
- `Composer*` stays on `Locus` purely to reach motifs — a wart that the refactor spec flagged as cleanup.
- If a future UI regenerates one motif and leaves the rest, it's unclear whether the Composer-side cache gets invalidated.

### Motif metadata

Today `Motif` carries `name`, a `variant<MelodicFigure, PulseSequence, StepSequence>`, and a `generationSeed`. Strategies that want to pick motifs by function ("give me a thematic opening") can't — they must reference by name.

For PeriodPassageStrategy and later phrase-aware strategies, role-tagging motifs is the minimum expressivity the template needs to support "the antecedent's basicIdea is a Thematic motif, the cadential approach is a Cadential motif". Without it the template either hard-codes motif names everywhere (brittle) or the strategy falls back to "whatever motif is in slot i".

### History queries

`pitch_before(locus)` answers "what pitch precedes this position". That's enough for cursor continuity but not for compositional decisions that depend on melodic context:

- A cadential approach that wants to know "where has the melody been — did we already hit the register ceiling?"
- A phrase strategy that wants to seed a `PitchReader` to the prior cursor before walking into its figures (today this is done inline with `pr.set_pitch(pitch_before(locus))` — fine but repetitive).
- A climax-avoidance rule that asks "highest pitch so far in this phrase / passage".

Adding these as `piece_utils` free functions keeps the query surface centralized and mirrors the precedent set by `pitch_before`.

## Goals

- **Single canonical home for motifs**: `PieceTemplate`. Strategies reach motifs through the template, not through `Composer`.
- **`Locus` drops its `Composer*` field**: once the pool moves, no runtime path needs it.
- **Motif metadata sufficient for role-based picking**: a strategy can ask "give me the motif with role Thematic that's referenced here" without branching on strings.
- **Derivation relationships first-class**: a derived motif (e.g. `Theme_a'`) records its parent and the transform that produced it, so the UI and future strategies can walk variant chains.
- **History queries beyond `pitch_before`**: seeded PitchReader, per-scope pitch-range queries.
- **No behavioral regression**: all current K467 golden renders must match bit-for-bit after this work.

## Non-goals

- Any changes to the `Motif` content types themselves (`MelodicFigure`, `PulseSequence`, `StepSequence`) — their shape stays.
- `PeriodPassageStrategy` or any period-aware compositional work — that's Spec B (`2026-04-15-period-forms-and-k467-design.md`).
- Cross-Part history queries (e.g., "highest pitch in Melody and Bass combined"). Per-Part scope is the primary need; cross-Part can be added if/when a multi-Part strategy requires it.
- Auto-regeneration of derived motifs when the parent changes. Derivation records provenance for review/UI; realized forms are static once produced.
- UI for motif authoring. This spec defines the data model and JSON; UI work is separate.

## Design

### Motif type shape

```cpp
enum class MotifRole {
  Thematic,      // main memorable melodic idea
  Cadential,     // approach to a cadence
  PostCadential, // post-cadence tag / codetta-like extension
  Discursive,    // continuation / development material
  Climactic,     // arrival / high-point material
  Connective,    // bridge / pickup / link between larger units
  Ornamental,    // decorative filigree (spelled-out trill, turn-figure)
};

enum class MotifOrigin {
  User,       // authored by hand in UI / JSON
  Generated,  // produced by a procedural generator
  Derived,    // created via transform of another motif
  Extracted,  // pulled from a model / corpus (future-proofing)
};

struct Motif {
  std::string name;
  std::variant<MelodicFigure, PulseSequence, StepSequence> content;

  // --- NEW metadata ---
  std::set<MotifRole> roles;                    // multi-tag; empty = untyped
  MotifOrigin origin{MotifOrigin::User};
  std::optional<std::string> derivedFrom;       // immediate-parent motif name
  std::optional<TransformOp> transform;         // how derivation was done
  int transformParam{0};                        // transform-specific

  uint32_t generationSeed{0};                    // EXISTING, retained
};
```

Multi-tag vs single-tag: multi-tag. A motif can be both `Thematic` and `Cadential` (the opening antecedent that *starts* a piece and *also* approaches a HC). Set semantics — order doesn't matter, no duplicates.

Empty-roles default: a motif with no declared role matches only queries that don't require a specific role. No wildcard / `Free` role — empty is empty.

Derivation model: store immediate parent only. Root ancestor is derivable by walking the `derivedFrom` chain. `derivedFrom` may point into an earlier-numbered motif in `PieceTemplate::motifs` (load-time validation rejects forward references). Cycles are a schema error.

Name convention for derived motifs (`Theme_a`, `Theme_a'`, `Theme_a''`): **UI-level suggestion, not load-bearing in code**. When the user clicks "derive" the UI pre-fills `parent.name + "'"`. Users may rename freely. Code uses `derivedFrom` to walk chains, not string parsing.

### Motif pool home: `PieceTemplate`

Add to `PieceTemplate`:

```cpp
struct PieceTemplate {
  // ... existing fields ...
  std::vector<Motif> motifs;                                   // declarations (EXISTS)

  // Realized forms, populated during setup_piece_. Keyed by motif name.
  // Indexed by content kind so existing find_* helpers have a direct home.
  std::unordered_map<std::string, MelodicFigure> realizedFigures;
  std::unordered_map<std::string, PulseSequence> realizedRhythms;
  std::unordered_map<std::string, StepSequence> realizedContours;

  // Accessor helpers (matching Composer's current interface).
  const MelodicFigure* find_motif(const std::string& name) const;
  const PulseSequence* find_rhythm_motif(const std::string& name) const;
  const StepSequence*  find_contour_motif(const std::string& name) const;

  // Role-based lookup — returns every motif whose role set contains `role`.
  std::vector<const Motif*> motifs_with_role(MotifRole role) const;
};
```

The realization pass in `Composer::realize_motifs_` moves to a free function `mforce::realize_motifs(PieceTemplate&)` (non-const `PieceTemplate&` — realization mutates the realized maps) called from `Composer::setup_piece_`. Composer's member maps go away.

### `Locus` cleanup

```cpp
struct Locus {
  const Piece* piece;
  const PieceTemplate* pieceTemplate;
  // REMOVED: Composer* composer;
  int sectionIdx;
  int partIdx;
  int passageIdx{-1};
  int phraseIdx{-1};
  int figureIdx{-1};

  // with_passage/phrase/figure helpers unchanged.
};
```

All strategy code that currently reads `locus.composer->find_rhythm_motif(name)` etc. changes to `locus.pieceTemplate->find_rhythm_motif(name)`. Single-line edits across `composer.h` and `shape_strategies.h`.

The forward-decl `struct Composer;` in `locus.h` goes away.

### History-query helpers

Added to `mforce::piece_utils`:

```cpp
namespace mforce::piece_utils {

// EXISTING: passage_at, pitch_before.

// Convenience: a PitchReader seeded to pitch_before(locus), with scale
// drawn from locus.piece->sections[sectionIdx].scale. Strategies that need
// to walk forward from the prior cursor use this instead of calling
// PitchReader(scale) + set_pitch(pitch_before(locus)) by hand.
PitchReader reader_before(const Locus& locus);

// Pitch-range query. "Before" semantics: considers realized content up
// to (but not including) this Locus position, within the named scope.
//
// If no realized content precedes this Locus within the scope, `empty`
// is true and lowest/highest are default-constructed — callers must
// check `empty` before using the pitches.
struct PitchRange {
  Pitch lowest;
  Pitch highest;
  bool empty{true};
};

PitchRange range_in_phrase_before(const Locus& locus);
PitchRange range_in_passage_before(const Locus& locus);
PitchRange range_in_piece_before(const Locus& locus);

} // namespace mforce::piece_utils
```

Scope semantics:

- **Phrase**: within the current phrase (`locus.phraseIdx`), from figure 0 up to but not including `locus.figureIdx`. If `figureIdx <= 0`, returns empty.
- **Passage**: within the current passage (`locus.sectionIdx` + `locus.partIdx`), all phrases 0..`phraseIdx-1` complete, plus phrase `phraseIdx` up to figure `figureIdx`. If no prior figures, empty.
- **Piece**: within this `partIdx` across the whole piece, all passages of this part up to and including the current passage up to this Locus.

Implementation walks realized content from scope start to Locus, accumulating min/max of each unit's pitch (unit.step applied to a `PitchReader` seeded at the scope's starting pitch). Rest units contribute nothing. Cost: O(n) prior units. Composition isn't hot path.

### JSON schema changes

`Motif` JSON:
```json
{
  "name": "Fig1",
  "content": { /* MelodicFigure | PulseSequence | StepSequence */ },
  "roles": ["Thematic"],
  "origin": "User",
  "derivedFrom": "Fig1_raw",
  "transform": "Invert",
  "transformParam": 0,
  "generationSeed": 12345
}
```

Fields `roles`, `origin`, `derivedFrom`, `transform`, `transformParam` are all optional in JSON. Loader defaults:
- `roles` → empty set
- `origin` → `User`
- `derivedFrom` → absent
- `transform` → absent
- `transformParam` → 0

`roles` serializes as a JSON array of enum-name strings; the loader validates each is a known `MotifRole`.

`origin`, `transform` serialize as enum-name strings.

No schema change to any other type.

### Migration plan

Staged like the prior refactor:

**Stage 0 — Baseline.** Re-hash the representative K467 set (reuse `docs/superpowers/plans/2026-04-14-baseline-hashes.txt` as the target — these hashes must survive unchanged).

**Stage 1 — Pool relocation.** Move realization from `Composer::realize_motifs_` to a free function `mforce::realize_motifs(PieceTemplate&)`. Add the three realized maps + lookup helpers to `PieceTemplate`. Composer's constructor / `setup_piece_` call the free function. `Composer::realized_motifs_` / `realizedRhythms_` / `realizedContours_` become thin forwarders to the PieceTemplate-owned maps (so existing call sites keep working during transition). Build + render + verify baseline hashes match.

**Stage 2 — Strategy call-site migration.** Replace every `locus.composer->find_rhythm_motif(n)` / `find_contour_motif(n)` / `realized_motifs()` with `locus.pieceTemplate->find_rhythm_motif(n)` etc. Verify hashes match.

**Stage 3 — Drop `Composer*` from `Locus`.** Remove the field + the forward decl in `locus.h`. Update `locus.h`'s fwd-decl + every `Locus{...}` initializer list (Composer constructs Locus at a few call sites — narrow update). Delete `Composer::realize_motifs_`, `Composer::realizedMotifs_`, etc. Verify hashes match.

**Stage 4 — Add motif metadata.** Extend `Motif` struct with `roles`, `origin`, `derivedFrom`, `transform`, `transformParam`. Update JSON round-trip (to_json + from_json). Defaults keep existing patches loading unchanged. Verify hashes match.

**Stage 5 — Add history queries.** Implement `reader_before`, `range_in_phrase_before`, `range_in_passage_before`, `range_in_piece_before` in `piece_utils.h`. Purely additive — no consumer yet. Verify hashes match (should be trivially true since no code path reads them).

Stages are independently revert-able. Each ends with a golden hash check.

## Resolved during brainstorm

- **Chord suitability not a motif property.** Figures are scale-degree-relative; chord constraint is a Conductor/Performer concern, not a motif-authoring concern.
- **Multi-tag roles.** A motif can wear multiple role hats.
- **Empty roles default** (not a `Free` wildcard). Empty means empty.
- **Derivation stores immediate parent only.** Root ancestor is a walk.
- **Name convention (`Theme_a'`) is UI-level**, not load-bearing in code.
- **No wrapper type for derived motifs.** Derivation is metadata on `Motif`; content remains a plain variant.

## Open questions to settle during planning

- **Cross-Part piece queries.** `range_in_piece_before` currently limits to this `partIdx`. Should a `range_in_piece_all_parts_before` exist for use cases like "has any Part already hit this register"? Not needed for K467 (melody-only opening); defer to Spec B if it comes up.
- **Seed hygiene for derived motifs.** Deterministic transforms (Invert, Retrograde) don't need a seed. Stochastic ones (VaryRhythm, VarySteps) do. During planning, decide whether `transformParam` doubles as the transform seed or whether the derived motif carries its own `transformSeed` field.
- **Motif realization order when derivation is present.** If `Motif B` is `derivedFrom: A`, realization must happen in a topologically-sorted order. The loader should either enforce declaration order (parent before child) OR the realize pass does a two-pass topo sort. Plan picks one.

## What this enables

With this work landed:

- `PieceTemplate` is the single canonical source for both motif declarations and their realized forms.
- `Locus` is clean — a pure structural coordinate with no Composer escape hatch.
- Strategies can query motifs by role ("give me a Thematic motif") rather than by exact name, enabling generic template authoring.
- Derivation relationships let the UI show "motif family trees" and let future strategies walk variant chains.
- `piece_utils` has enough query surface for phrase-level and passage-level compositional decisions (climax avoidance, register tracking, seeded cursor reads).

Spec B (Period Forms and K467) builds directly on this foundation: `PassageTemplate.periods[]` will reference motifs by name and role, and `PeriodPassageStrategy` will use `range_in_passage_before` for climax-shape decisions.
