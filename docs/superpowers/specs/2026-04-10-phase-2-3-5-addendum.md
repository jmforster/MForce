# Composer Strategy — Phase 2/3/5 Addendum

> Supplements `2026-04-10-composer-strategy-architecture-design.md`. Covers
> design decisions for the composition quality fix (Phase 2 Task 1), the
> figure shape strategy migration (Phase 2 body), two classical phrase
> strategies (Phase 3), and the Seed → Motif rename (Phase 5).
>
> Phase 4 (`OutlinePassageStrategy`) and the phrase-level `OutlinePhraseStrategy`
> are explicitly DEFERRED. They need further brainstorming before design —
> see `project_composition_quality.md` memory and the conversation log from
> 2026-04-10.

## Phase 2 Task 1 — `totalBeats` hard constraint

**Current behavior** (`DefaultFigureStrategy::generate_figure`, pre-fix): the
template's `totalBeats` is treated as a hint. The code derives
`noteCount = int(totalBeats / defaultPulse)`, then clamps to
`[minNotes, maxNotes]`, then lets `vary_rhythm` randomly stretch the figure
by 40%. The result is that a figure with `totalBeats: 4.0` routinely emits
at 3, 4, or 5 beats depending on seed — drift compounds across a phrase.

**New behavior**: after all generation + variation steps, if the figure's
actual beat total differs from `totalBeats`, scale every unit's duration
proportionally so the total matches. Proportional scaling preserves the
rhythmic *shape* (ratios between units) while snapping the total.

```cpp
// At the end of generate_figure, before return:
if (figTmpl.totalBeats > 0) {
  float actual = 0;
  for (auto& u : fig.units) actual += u.duration;
  if (actual > 0 && std::abs(actual - figTmpl.totalBeats) > 0.001f) {
    float scale = figTmpl.totalBeats / actual;
    for (auto& u : fig.units) u.duration *= scale;
  }
}
```

Same treatment applies to `generate_shaped_figure` (because shape-based
figures also have a `totalBeats` in their template). Not applied to
`Literal` / `Locked` / `Reference` / `Transform` paths — those either take
durations as given or inherit from a seed/motif, and the user is expected
to set durations correctly there.

## Phase 2 Task 2 — Delete `apply_cadence_rhythm`

The function is dead code — no caller in the current composer. Preserved
verbatim through Phases 1a and 1b because deleting dead code was explicitly
out of scope for a refactor. Phase 2 is the right place to delete it.

No replacement. The cadence work in Phase 2 is limited to deleting the
function and removing the declaration on `DefaultPhraseStrategy`. Any future
"shape the phrase ending's rhythm" work is Phase 2+ and will be designed
from scratch rather than resurrecting this helper.

## Phase 2 body — Figure shape strategy migration

**Current shape methods on `FigureBuilder`** (`engine/include/mforce/music/figures.h`):

| Shape | Method | Lines |
|---|---|---|
| ScalarRun | `scalar_run(dir, count, pulse)` | 682 |
| RepeatedNote | `repeated_note(count, pulse)` | 696 |
| HeldNote | `held_note(duration)` | 707 |
| CadentialApproach | `cadential_approach(fromAbove, approachSteps, ...)` | 718 |
| TriadicOutline | `triadic_outline(dir, includeOctave, pulse)` | 736 |
| NeighborTone | `neighbor_tone(upper, pulse, doublePulseMain)` | 753 |
| LeapAndFill | `leap_and_fill(leapSize, leapUp, fillSteps, pulse)` | 769 |
| ScalarReturn | `scalar_return(dir, extent, returnExtent, pulse)` | 789 |
| Anacrusis | `anacrusis(count, dir, pickupPulse, mainPulse)` | 811 |
| Zigzag | `zigzag(dir, cycles, stepSize, returnStep, pulse)` | 830 |
| Fanfare | `fanfare(intervals, repeatsPerNote, pulse)` | 848 |
| Sigh | `sigh(pulse)` | 867 |
| Suspension | `suspension(holdDuration, resolutionPulse)` | 878 |
| Cambiata | `cambiata(dir, pulse)` | 890 |

**Migration**: each shape becomes its own `FigureStrategy` subclass whose
`realize_figure` reads the shape-specific params from a new
`ShapeFigureConfig` (or the existing `FigureTemplate::shape*` fields —
whichever is cleaner) and emits the same `MelodicFigure` the old method
did.

**Structure**: 14 new strategy classes in a single new file
`engine/include/mforce/music/shape_strategies.h`. Each class registered
under a name like `shape_scalar_run`, `shape_triadic_outline`, etc.

**Dispatch in `DefaultFigureStrategy::generate_shaped_figure`**: replace
the giant switch with a registry lookup by shape name. The switch goes
away; the new path calls
`ctx.composer->registry_.get("shape_" + name)->realize_figure(...)`.

**`FigureBuilder` shape methods**: stay in place through Phase 2. Each new
strategy's body is a literal copy of the method body (not a call to the
method), so the method and the strategy drift separately but we don't
break `FigureBuilder`'s public API. A follow-on cleanup pass can remove
the now-unused methods after confirming no other callers exist (dun_parser,
conductor, main.cpp, etc.).

**Hash impact**: the migration MUST be bit-identical. Each strategy body is
a verbatim copy of the corresponding `FigureBuilder` method body. Post-
migration render of the golden template must match the pre-migration hash.
Unlike Phase 1b, this is a pure refactor inside the generate-shaped path.

**Note on registration**: each shape strategy is registered in
`Composer::Composer(...)` next to the three Default strategies. The
constructor grows by 14 `register_strategy` calls. Optional: extract that
into a helper function `register_all_shape_strategies(registry_)` for
readability.

## Phase 3 — `PeriodPhraseStrategy`

**Classical form**: antecedent (opens with basic idea, ends on a half
cadence — typically scale degree 5, the dominant) + consequent (opens with
the same basic idea parallel, ends on the authentic cadence target from
`PhraseTemplate.cadenceTarget`, typically scale degree 1).

**Config (`PeriodPhraseConfig`) — new struct on `PhraseTemplate`:**

```cpp
struct PeriodPhraseConfig {
  MelodicFigure basicIdea;          // opens both sub-phrases
  MelodicFigure antecedentTail;     // figure(s) closing antecedent on halfCadenceTarget
  MelodicFigure consequentTail;     // figure(s) closing consequent on authentic cadence
  int halfCadenceTarget{4};         // 0-indexed scale degree (4 = 5th in a diatonic scale)
};
```

The `PhraseTemplate` gains:
```cpp
std::optional<PeriodPhraseConfig> periodConfig;
```

**Algorithm** (`PeriodPhraseStrategy::realize_phrase`):

1. Initialize `phrase.startingPitch` from `phraseTmpl.startingPitch` or
   `ctx.cursor` (same pattern as `DefaultPhraseStrategy`).
2. Read `phraseTmpl.periodConfig.value()`.
3. Place `basicIdea` at cursor. Append to `phrase.figures`. Update cursor
   via `net_step()`.
4. Place `antecedentTail` at cursor. Append.
5. Apply half-cadence adjustment: walk phrase figures so far (2 figures),
   compute running scale-degree total from phrase start, calculate delta
   to `halfCadenceTarget`, add delta to `antecedentTail.units.back().step`.
   (Same math as the existing `apply_cadence` helper, but targeting the
   antecedent's final unit instead of the whole phrase's final unit.)
6. Place `basicIdea` again at cursor (parallel opening of consequent).
   Append. Update cursor.
7. Place `consequentTail` at cursor. Append. Update cursor.
8. If `phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0`, call
   the existing `DefaultPhraseStrategy::apply_cadence` against the whole
   phrase (the authentic cadence). This adjusts `consequentTail`'s last
   unit.

**Result**: 4 figures, AA'BB' form, half-cadence at midpoint and authentic
cadence at end. Uses `DefaultPhraseStrategy::apply_cadence` as a public
helper — same pattern as `degree_in_scale` exposed in Phase 1b Task 1.
`apply_cadence` is already accessible via `DefaultPhraseStrategy::` if
promoted to public.

## Phase 3 — `SentencePhraseStrategy`

**Classical form**: basic idea (2 measures) → basic idea repeated or
sequentially transposed (2 measures) → continuation/cadence (4 measures).

**Config (`SentencePhraseConfig`):**

```cpp
struct SentencePhraseConfig {
  MelodicFigure basicIdea;
  int variationTransposition{0};  // scale-degree offset for the repetition
                                  // (0 = literal repeat; +1 = up one degree;
                                  //  -1 = down one degree; etc.)
  MelodicFigure continuation;
};
```

The `PhraseTemplate` gains:
```cpp
std::optional<SentencePhraseConfig> sentenceConfig;
```

**Algorithm** (`SentencePhraseStrategy::realize_phrase`):

1. Initialize `phrase.startingPitch` as usual.
2. Read `phraseTmpl.sentenceConfig.value()`.
3. Place `basicIdea` at cursor. Append.
4. Copy `basicIdea` again. Adjust first-unit step by `variationTransposition`
   — this effectively starts the repetition `variationTransposition` scale
   degrees away from where it would otherwise land. All internal unit steps
   stay identical, so the pattern IS transposed. Append.
5. Place `continuation` at cursor. Append.
6. If `phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0`, apply
   cadence to the whole phrase (adjusts `continuation`'s last unit).

**Result**: 3 figures in sentence form. Simple, testable with fixed seed.

## Phase 3 dispatch wiring

`Composer::realize_phrase` currently hardcodes `registry_.get("default_phrase")`.
For Phase 3 to actually USE Period and Sentence, dispatch needs to consult
the PhraseTemplate's `strategy` field. The Phase 1a plan deferred template-
driven strategy selection to "Phase 3+"; this is where it lands.

**Dispatch logic** (updated `Composer::realize_phrase`):

```cpp
Phrase realize_phrase(const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  std::string name = phraseTmpl.strategy.empty() ? "default_phrase" : phraseTmpl.strategy;
  Strategy* s = registry_.get(name);
  if (!s) {
    // Unknown strategy — fall back to default, emit warning.
    std::cerr << "Unknown phrase strategy '" << name << "', falling back to default_phrase\n";
    s = registry_.get("default_phrase");
  }
  return s->realize_phrase(phraseTmpl, ctx);
}
```

`PhraseTemplate::strategy` is added as a new field:
```cpp
std::string strategy;   // empty = default
```

With JSON round-trip. Same treatment for `FigureTemplate::strategy` and
`PassageTemplate::strategy` — all three template levels gain a `strategy`
field as part of Phase 3's dispatch wiring. The Phase 3 plan handles this
as a preparatory task before adding Period/Sentence.

## Phase 5 — Seed → Motif rename

Mechanical rename. Every `Seed`-related symbol becomes `Motif`:

| Before | After |
|---|---|
| `struct Seed` | `struct Motif` |
| `PieceTemplate::seeds` | `PieceTemplate::motifs` |
| `FigureTemplate::seedName` | `FigureTemplate::motifName` |
| `Composer::realizedSeeds_` | `Composer::realizedMotifs_` |
| `Composer::realized_seeds()` | `Composer::realized_motifs()` |
| `Composer::realize_seeds_()` | `Composer::realize_motifs_()` |
| `PieceTemplate::find_seed()` | `PieceTemplate::find_motif()` |
| `Seed::userProvided` | `Motif::userProvided` (no change) |
| `Seed::generationSeed` | `Motif::generationSeed` (no change — "generation seed" is RNG, not motif identity) |
| `Seed::constraints` | `Motif::constraints` (no change) |
| `FigureSource::Reference` | (no change — still Reference, just references a motif) |

**JSON field renames** (templates_json.h):
- `"seeds"` → `"motifs"` on `PieceTemplate`
- `"seedName"` → `"motifName"` on `FigureTemplate`

**Golden template update**: `patches/template_golden_phase1a.json` references
`"seedName": "motif_a"` in two places. Rename to `"motifName": "motif_a"`
(the motif name itself was already `motif_a`, appropriately).

**Hash expectation**: rename is purely cosmetic at the code level. The
masterSeed, RNG call order, and generation algorithm are unchanged. Golden
render must match post-Phase-3 hash bit-identically.

**RNG variable `Randomizer` named `rng_` stays as `rng_`** — the
`Randomizer` is the RNG, not a "Seed" in the musical sense. No rename.

## Out of scope for this batch

- `OutlinePhraseStrategy` and `OutlinePassageStrategy` — both deferred to
  post-batch brainstorming. The conversation on 2026-04-10 reached a
  design stub but needed more iteration that couldn't happen before Matt
  stepped away.
- Phase 4 in its entirety — the only content was `OutlinePassageStrategy`.
- Beat-constraint handling on literal/locked/reference/transform figures
  — only the Generate path is fixed in Phase 2 Task 1.
- Variation beyond single-transposition for Sentence's repetition —
  future enhancement.
- Any reshape of `apply_cadence_rhythm`-style rhythmic phrase-ending
  shaping — future enhancement.
- Test/corpus template updates: only the committed golden
  (`template_golden_phase1a.json`) is maintained. Other templates
  (`template_shaped_test.json`, `template_mary.json`, `template_binary.json`,
  `template_ode_to_joy*.json`) are NOT updated for Phase 5's motif rename
  and will need drive-by fixes the next time someone uses them.

## User review gate skipped

Matt approved the Period/Sentence designs inline on 2026-04-10 and
explicitly instructed "proceed with all phases... mechanical verification
all on main, implement only the 2 strategies you said you could infer from
training data." User review of this written spec is therefore SKIPPED at
Matt's direction — execution proceeds directly to writing-plans and
subagent dispatch.
