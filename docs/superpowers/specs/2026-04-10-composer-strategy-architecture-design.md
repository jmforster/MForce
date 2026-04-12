# Composer Strategy Architecture — Design

## Vision

Algorithmic composition in mforce should produce musically coherent and plausible
results across genres. No single formula works for all music — Beethoven's
motivic saturation is different from Mozart's periodic phrase structure which
is different from Billy Joel's nested ur-line writing. The system therefore
needs a **library of composition strategies**, each implementing a specific
compositional approach, with the **Composer** choosing which strategy to apply
at each level of the piece.

The user interacts with the system through **templates** that can be
proscriptive at any level from 0% (just "compose a song") to 100% ("here are
the exact notes"). A template that doesn't specify a strategy lets the
Composer pick; one that does, forces the choice. The same applies to figures,
phrases, passages, and pieces.

## Goals

- **Separation of concerns**: data (templates) vs. logic (composer + strategies) vs. output (piece)
- **Pluggability**: adding a new strategy = new class + registration. No core changes.
- **Reproducibility**: same template + same RNG seed = same output
- **Reuse**: motifs shared across phrases via named references; phrases reused via named references (e.g. recapitulation)
- **Incremental specification**: template can specify everything, nothing, or anything in between at any level
- **Hierarchical dispatch**: each level of the hierarchy delegates to the level below through the Composer

## Core Concepts

### Motif
A **motif** is a recurring musical idea — specifically, a named `MelodicFigure`
that appears in multiple places within a piece. Motifs give a piece coherence.
A motif can be as small as a single note or as large as a full 16-bar theme —
any figure that recurs. Motifs are NOT one-off figures that only appear in one place —
those are anonymous figures inlined into a phrase template.

Motifs live in a **motif pool** on the `Piece` (`map<string, MelodicFigure>`).
The pool is populated during the first phase of composition. Any template
element (figure template, phrase strategy, etc.) can reference a motif by
name.

### Template Hierarchy
The template is a parallel hierarchy to the piece:

```
PieceTemplate          ← what to compose (top level)
├── motifs             ← named motif definitions or constraints
├── sections           ← time spans
└── parts              ← voices/instruments
    └── passages       ← PassageTemplate per (part, section)
        └── phrases    ← PhraseTemplate list
            └── figures ← FigureTemplate list (legacy path, used inside DefaultPhraseStrategy)
```

Each template level can have:
- `name` (user-specified or auto-assigned)
- `strategy` (name of strategy to use, or empty for Default)
- `params` (strategy-specific parameters, JSON)
- Level-specific fields:
  - `PassageTemplate` has `startingPitch` (**required** — the initial PitchReader cursor position for the passage).
  - `PhraseTemplate` has optional `startingPitch` (**override** — if present, the cursor is reset to this pitch at the start of the phrase; otherwise it carries over from the previous phrase), plus `cadenceType`, `cadenceTarget`.
  - `FigureTemplate` has **no** `startingPitch`. Figures carry only steps; the first step of a figure's step sequence moves the cursor from wherever the previous figure left it to that figure's effective starting pitch. This replaces the old connector mechanism — there are no connectors, just a running cursor and figures whose first step bridges the gap.

Templates are **pure data**. They do not contain logic. They do not mutate
during composition.

### Strategy
A **Strategy** is a pluggable piece of logic that produces one level of the
hierarchy from a context. Strategies are abstract classes registered by name:

```cpp
enum class StrategyLevel { Figure, Phrase, Passage, Piece };

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::string name() const = 0;
    virtual StrategyLevel level() const = 0;
    // Subclasses override one of these based on their level
    virtual MelodicFigure realize_figure(const StrategyContext&);
    virtual Phrase realize_phrase(const StrategyContext&);
    virtual Passage realize_passage(const StrategyContext&);
};
```

**Strategy types by level:**

- **`FigureStrategy`** — produces a `MelodicFigure`. Examples: `ScalarRunFigureStrategy`, `FanfareFigureStrategy`, `TriadicOutlineFigureStrategy`, `NeighborToneFigureStrategy`, `RepeatedNoteFigureStrategy`, `RandomWalkFigureStrategy` (the current random_sequence), etc.
- **`PhraseStrategy`** — produces a `Phrase`. Examples: `DefaultPhraseStrategy` (legacy walk-figures path), `OutlinePhraseStrategy`, `PeriodPhraseStrategy`, `SentencePhraseStrategy`.
- **`PassageStrategy`** — produces a `Passage`. Examples: `DefaultPassageStrategy` (walks phrase templates in order), `OutlinePassageStrategy`, `SonataExpositionStrategy` (future), etc.
- **`PieceStrategy`** — produces a `Piece` structure. Future work; initially the Composer handles piece-level orchestration directly.

### StrategyContext
Bundle of data passed to every strategy:

```cpp
struct StrategyContext {
    Scale scale;
    Pitch cursor;                  // current PitchReader position; figures advance it
    float totalBeats;
    Piece* piece;                  // in-progress piece, for motif/phrase lookup
    const PieceTemplate* template_;// source template
    Composer* composer;            // for dispatching sub-levels
    nlohmann::json params;         // strategy-specific parameters from template
    Randomizer* rng;               // shared RNG
};
```

The `cursor` is set from `PassageTemplate.startingPitch` when a passage begins,
optionally reset by a `PhraseTemplate.startingPitch` override at phrase
boundaries, and advanced by each figure's step sequence as it's realized. A
FigureStrategy never reads a "figure starting pitch" — it reads the cursor and
emits steps from there.

Strategies are stateless — they're singletons in the registry. All state
lives in the context. This makes them reproducible given the same context.

### StrategyRegistry
Name → Strategy lookup with listing:

```cpp
class StrategyRegistry {
public:
    void register_strategy(std::unique_ptr<Strategy>);
    Strategy* get(const std::string& name);
    std::vector<Strategy*> list_for_level(StrategyLevel);
};
```

At startup (or composer construction), all strategies are registered. A
single global registry serves the entire composer.

### Composer
Orchestrates composition. Walks the template, dispatches to strategies,
populates the Piece. Exposes `realize_*` methods that strategies call to
delegate to sub-levels.

```cpp
class Composer {
public:
    void compose(Piece& piece, const PieceTemplate& template_);

    // Called by strategies to delegate to sub-levels
    MelodicFigure realize_figure(const FigureTemplate&, StrategyContext&);
    Phrase realize_phrase(const PhraseTemplate&, StrategyContext&);
    Passage realize_passage(const PassageTemplate&, StrategyContext&);

private:
    StrategyRegistry registry_;
    void realize_motifs(Piece&, const PieceTemplate&);
};
```

The dispatch logic in each `realize_*` method:
1. Look at the template's `strategy` field.
2. If empty → use `DefaultXxxStrategy`.
3. Look up the strategy in the registry.
4. Build a `StrategyContext` from the template and piece state.
5. Call the strategy's `realize_*` method and return the result.

Per-level handling before dispatch:
- **`realize_figure`**:
  - `source == reference` → look up motif by name in `piece.motifs`, return copy.
  - `source == locked` → return the embedded `MelodicFigure` directly (round-trip of a previously composed figure; step-sequence form, used by lock/re-compose).
  - `source == literal` → convert the embedded note list (pitch + duration per note) into a `MelodicFigure` and return it. This is the "write these exact notes" path for human-authored input.
  - otherwise → dispatch to a FigureStrategy.
- **`realize_phrase`**: if `source == reference`, find named phrase in the already-composed piece and copy; otherwise dispatch.
- **`realize_passage`**: no reference mechanism currently; always dispatch.

### Piece (Output)
Grows during composition:

```cpp
struct Piece {
    Key key;
    Meter meter;
    float bpm;
    std::unordered_map<std::string, MelodicFigure> motifs;  // motif pool
    std::vector<Section> sections;
    std::vector<Part> parts;  // each has passages per section, each with phrases
};
```

Phrases inside passages have names (user-provided or auto-assigned). Phrase
references are resolved by walking the score to find the name.

## Composition Flow

```
Composer.compose(piece, template)
  │
  ├─ Set up piece.key, piece.meter, piece.bpm, piece.sections, piece.parts
  │
  ├─ realize_motifs(piece, template)
  │   └─ For each motif definition in template.motifs:
  │       ├─ If figure provided (user-specified) → copy to piece.motifs[name]
  │       └─ Else → build a FigureTemplate from the motif's constraints,
  │                 call realize_figure() which dispatches to a FigureStrategy,
  │                 store result in piece.motifs[name]
  │
  └─ For each part × section:
      └─ realize_passage(passageTemplate, ctx)
          └─ Dispatch to PassageStrategy (DefaultPassageStrategy if unspecified)
              └─ PassageStrategy may call composer.realize_phrase(...) for each phrase
                  └─ If phrase has source: reference
                  │     → walk piece to find named phrase, copy
                  │   Else
                  │     → Dispatch to PhraseStrategy (DefaultPhraseStrategy if unspecified)
                  │       └─ PhraseStrategy may call composer.realize_figure(...) per figure
                  │           └─ If figure source == reference
                  │           │     → look up motif in piece.motifs, return copy
                  │           │   Else
                  │           │     → Dispatch to FigureStrategy, returns MelodicFigure
                  │           │       └─ FigureStrategy uses FigureBuilder/StepGenerator primitives
```

**Delegation rules:**
- Every arrow crosses a well-defined boundary.
- A PassageStrategy never touches figures directly — only phrases (via the composer).
- A PhraseStrategy never touches step sequences directly — only figures (via the composer).
- A FigureStrategy never knows about phrases or passages.

## FigureBuilder and Transforms

**Before**: `FigureBuilder` had methods like `scalar_run()`, `fanfare()`, `neighbor_tone()` that generated specific shapes.

**After**:
- Those methods are **removed** from FigureBuilder.
- Each shape becomes a **FigureStrategy** subclass that uses the generic builder primitives internally.
- `FigureBuilder` keeps only generic assembly: `build(PulseSequence, StepSequence)`, `vary_rhythm()`, `replicate()`.
- **`FigureTransform`** is a separate concept: free functions (or a dedicated namespace) that take a figure and produce a modified figure (`invert`, `reverse`, `stretch`, `compress`). Separate from FigureBuilder to keep it thin.

## Naming and References

### Motif names
Motifs are always named (user-specified). References use the name.
No auto-naming for motifs — they're explicit artifacts.

### Phrase names
User-specified if the user cares about referencing them later. Otherwise
auto-assigned by the Composer (e.g. `part_section_phrase_0`). Globally unique
across the piece.

### Backward references only
A phrase reference must point to a phrase that's already been composed at the
time the reference is resolved. No forward references in v1. (Two-pass
composition is a future extension.)

## Reproducibility

The Composer holds a root RNG. Each strategy call receives a derived RNG
(seeded from the root + a path identifier like `passage_0/phrase_2/figure_1`).
This means:
- Same template + same root seed = same output.
- Re-rolling one phrase (with a different seed for that phrase only) changes
  only that phrase.
- Locking a phrase = saving its seed alongside the template.

## Backward Compatibility

The existing `ClassicalComposer` has a working code path that handles
templates with `FigureTemplate`, `PhraseTemplate`, and cadences. Connectors
have been removed — the running-cursor model replaces them. This path becomes
`DefaultPhraseStrategy` — the figure-walking behavior is preserved, wrapped in
the strategy interface, with connector handling deleted.

Existing templates continue to work unchanged. Adding new strategies is
purely additive.

## JSON Template Format

New fields on `PhraseTemplate`:
```json
{
  "name": "A1",
  "strategy": "outline_phrase",
  "params": { "outline": "fig2", "surface": "fig3", "tail": "fig4" },
  "startingPitch": { "octave": 4, "pitch": "E" }
}
```

New fields on `PassageTemplate`:
```json
{
  "name": "Main",
  "strategy": "outline_passage",
  "params": { "outline": [2,1,2,1,0], "phrases": ["P1","P2","P1","P3"] },
  "phrases": [ ... ]
}
```

New fields on `FigureTemplate`. The `source` field selects which of the five
paths in `realize_figure` is taken:

```json
// generate — composer picks notes via a FigureStrategy
{ "source": "generate", "strategy": "fanfare", "params": { "intervals": [4, 3], "repeats": 2 } }

// reference — reuse a motif from the piece's motif pool
{ "source": "reference", "referenceName": "main_motif" }

// locked — embed a previously composed MelodicFigure verbatim
// (used by the lock/re-compose flow to pin a figure while regenerating
//  everything else; stored in the scale-degree-step form that MelodicFigure
//  already serializes to)
{ "source": "locked", "figure": { /* full MelodicFigure JSON */ } }

// literal — user types exact notes by pitch + duration
// (converted to a MelodicFigure at load time; this is the "write these exact
//  notes" path for human-authored input, at any level of the hierarchy)
{ "source": "literal", "notes": [
    { "pitch": "C4", "duration": "1/4" },
    { "pitch": "E4", "duration": "1/4" },
    { "pitch": "G4", "duration": "1/2" }
] }
```

Any figure — whether generated, literal, or locked — may be a single note. No
minimum length is enforced.

`locked` vs `literal`: both specify exact pitches, but they serve different
needs. `locked` is the round-trip form the Composer itself emits when a user
pins a figure during lock/re-compose; it stores the figure in the same
scale-degree form the rest of the system uses. `literal` is the human-authored
form — easier to type, translated to MelodicFigure at load time. They are kept
separate so that neither path has to compromise: `locked` stays bit-identical
across round-trips, and `literal` stays ergonomic for hand-written templates.

The `strategy` field is optional everywhere. If missing, `DefaultXxxStrategy`
is used (which for phrases = walk figures in order, advancing the cursor; for
passages = walk phrases in order).

Phrase references:
```json
{ "name": "A_reprise", "source": "reference", "referenceName": "A1" }
```

## Strategy Context Lifecycle

1. Composer creates a root StrategyContext when composition begins.
2. Each `realize_*` call receives a context **cloned** from the parent, with
   level-specific fields updated (e.g. cursor, params).
3. Shared state lives on the Piece and accessed via the context's pointer.
4. The RNG is derived (not shared) so strategies don't interfere with each
   other's randomness.

## Implementation Plan (summary; details in implementation plan)

**Phase 1: Framework**
- Strategy base class, StrategyLevel enum, StrategyContext struct
- StrategyRegistry with registration and lookup
- Composer refactor: `realize_figure`, `realize_phrase`, `realize_passage` dispatchers
- DefaultFigureStrategy wrapping current `generate_figure` / `random_sequence` path
- DefaultPhraseStrategy wrapping current `realize_phrase` figure-walking path
- DefaultPassageStrategy wrapping current `realize_passage` behavior

**Phase 2: Figure-level shape strategies**
- Migrate the shape methods I added yesterday (scalar_run, fanfare, etc.) to
  individual FigureStrategy classes. Delete them from FigureBuilder.

**Phase 3: Phrase strategies**
- OutlinePhraseStrategy (first real strategy, testable with My Life)
- PeriodPhraseStrategy (for K.467 theme)
- SentencePhraseStrategy

**Phase 4: Passage strategies**
- OutlinePassageStrategy

**Phase 5: Terminology cleanup**
- Rename "seed" → "motif" throughout
- Update docs, JSON schema, tests

**Phase 6: Test cases**
- Round-trip My Life template → expected output
- Round-trip K.467 theme template → expected output

Each phase produces a working build. Later phases depend on earlier ones.

## Testing Strategy

- Unit test each strategy in isolation with a mock context.
- Round-trip tests: a fully-specified template produces a bit-identical
  rendering to a known-good WAV (using hash comparison).
- Regression tests: existing templates (template_mary.json, template_ode_to_joy.json)
  continue to render identically after the refactor.

## Open Questions (to resolve during implementation)

1. How exactly does a derived RNG work? (`seed + hash("path/identifier")`?)
2. Do passage strategies need to override the starting pitch per phrase, or
   does the phrase template already specify it?
3. How are strategy parameters validated at load time vs. runtime?
4. Should FigureTransforms (invert, reverse, stretch) also become strategies
   of a different kind (`FigureTransformStrategy`)? Or stay as plain functions?
