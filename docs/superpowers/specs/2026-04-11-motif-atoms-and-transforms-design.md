# Motif Atoms and Transforms — Design

> Designed 2026-04-11 after identifying that 4 independently-generated
> figures (K467 structural test) lacked coherence because nothing tied
> them together. The fix: make PulseSequence and StepSequence first-class
> motif types so rhythm and contour can be shared across figures as
> reusable raw material.

## The Problem

The K467 structural renders (Skipping + CadentialApproach) had nice
rhythmic variety but zero motivic coherence. Each figure was generated
independently — no shared rhythm, no shared contour. Real music binds
figures together through shared motivic material: Beethoven 5's entire
first movement is built from one rhythmic cell (`short-short-short-LONG`);
Matt's Allemande example uses a 3-step sequential pattern (`SS{-1, 1, 4}`)
in the climax approach of both sections.

The current motif pool holds only `MelodicFigure` — the combined result.
You can't say "this RHYTHM is the motif" or "this CONTOUR is the motif."
The atoms (`PulseSequence`, `StepSequence`) exist in the codebase as types
and as constructor parameters to `MelodicFigure`, but they aren't
first-class in the motif pool and can't be referenced, transformed, or
shared independently.

## Core Design Decisions

### 1. Motif pool holds three atom types

```cpp
struct Motif {
  std::string name;

  // Content — variant of atom types. Future types (ChordProgression,
  // fragment, etc.) can be added to the variant without restructuring.
  using Content = std::variant<MelodicFigure, PulseSequence, StepSequence>;
  Content content;

  bool userProvided{false};
  uint32_t generationSeed{0};
  std::optional<FigureTemplate> constraints;

  // Convenience type queries
  bool is_figure() const { return std::holds_alternative<MelodicFigure>(content); }
  bool is_rhythm() const { return std::holds_alternative<PulseSequence>(content); }
  bool is_contour() const { return std::holds_alternative<StepSequence>(content); }
};
```

All motifs live at the **Piece level** in `PieceTemplate::motifs`. Even a
motif used in only one passage is a piece-level raw material. A sequential
pattern like `SS{-1, 1, 4}` might appear in both sections of a dance
movement — piece scope captures that reuse naturally.

Using `std::variant` for extensibility — future motif types
(`ChordProgression`, harmonic fragments, etc.) can be added to the variant
without restructuring the Motif struct or adding fields.

### 2. PulseGenerator — the missing rhythm generator

`StepGenerator` exists and produces `StepSequence` via melody-aware rules
(gap-fill, range regression, climax placement, skip/step distribution).
There is no equivalent for rhythm. The `generate_musical_rhythm` function
added in the Skipping/Stepping work is the prototype.

Promote it to a proper `PulseGenerator` class parallel to `StepGenerator`:

```cpp
struct PulseGenerator {
  Randomizer rng;

  explicit PulseGenerator(uint32_t seed = 0xPU15'0000u) : rng(seed) {}

  // Generate a PulseSequence of standard musical durations summing to
  // totalBeats, biased toward defaultPulse but with variety.
  PulseSequence generate(float totalBeats, float defaultPulse = 1.0f);
};
```

Valid durations include both binary subdivisions and triplet groups:

**Binary durations** (individual notes):
`{0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0}` — all multiples of 0.25,
so sums are always exact.

**Triplet groups** (always emitted as 3 notes at once):
- Triplet sixteenths: 3 × 1/6 ≈ 0.1667 = 0.5 beats total (fills an eighth)
- Triplet eighths: 3 × 1/3 ≈ 0.3333 = 1.0 beat total (fills a quarter)
- Triplet quarters: 3 × 2/3 ≈ 0.6667 = 2.0 beats total (fills a half)

Triplet groups are atomic choices in the generator — when selected, all 3
notes are emitted and the group's total duration is subtracted from the
remaining beats. Individual triplet values never appear alone. This
preserves metric coherence (no leftover 1/3-beat remainders that can't
be filled with standard durations).

Complex triplet scenarios (isolated triplets with ties to adjacent notes,
e.g. a triplet eighth tied from a preceding quarter) are deferred — future
work as a "tie" transform applied post-generation.

Weighted toward `defaultPulse` — if the pulse bias is `1.0` (quarter),
quarters and triplet-eighth-groups (which also fill a quarter) are most
likely, but eighths, halves, and other values also appear. Variable note
count per generation.

Returns a `PulseSequence` (not `vector<float>` as the current utility
does). The existing `generate_musical_rhythm` in `rhythm_util.h` can be
refactored into this class or replaced.

### 3. Atom-level transforms

Transforms partition naturally by atom type. Each atom exposes ONLY the
transforms that make musical sense for it:

**StepSequence transforms** (4 operations):

| Transform | What it does | Param | Element count | Duration |
|---|---|---|---|---|
| `inverted()` | Flip all step signs (ascending ↔ descending) | — | same | n/a |
| `retrograded()` | Reverse step order | — | same | n/a |
| `expanded(float factor)` | Multiply all step magnitudes (widen intervals) | factor | same | n/a |
| `contracted(float factor)` | Divide all step magnitudes (narrow intervals) | factor | same | n/a |

No `stretched()` or `compressed()` — those are duration concepts.
No `varied()` or `truncated()` — deferred. Future "vary" work will have
specific named operations (e.g. "perturb toward chord tones," "widen the
peak interval") rather than a vague generic `vary`. Truncation is not a
transform — figures are atoms; author short motifs instead.

**PulseSequence transforms** (3 operations):

| Transform | What it does | Param | Element count | Duration |
|---|---|---|---|---|
| `retrograded()` | Reverse duration order | — | same | same |
| `stretched(float factor)` | Multiply all durations (augmentation) | factor | same | **increases** |
| `compressed(float factor)` | Divide all durations (diminution) | factor | same | **decreases** |

No `inverted()` — inverting a rhythm is not a musical concept.
No `varied()` or `truncated()` — deferred. Future "vary" will include
specific operations like "split a random note" or "dot a note and shorten
its neighbor," each with clear names and semantics. Truncation is not a
transform (see above).

Note: `stretched()` and `compressed()` change the total duration of the
PulseSequence. A rhythm motif that fills 4 beats becomes 8 beats after
`stretched(2.0)`. This is musically correct (augmentation = "play the same
rhythm at half speed") but the caller must account for the changed length.

**All transforms preserve element count.** No transform adds or removes
elements. The atom's number of entries is invariant. (Future "split" and
"merge" operations may change element count but those are deferred and
will have explicit, specific names — not a generic "vary".)

**MelodicFigure transforms** stay on `FigureBuilder` as they are today.
They become thin delegation wrappers:

```cpp
MelodicFigure invert(const MelodicFigure& source) {
  PulseSequence p = source.extract_pulses();
  StepSequence s = source.extract_steps();
  return MelodicFigure(p, s.inverted());
}
```

The existing implementations continue to work; the atom methods are new
additions, not replacements. FigureBuilder can adopt the delegation
pattern when convenient.

### 4. MelodicFigure ↔ atom round-tripping

New methods on `MelodicFigure`:

```cpp
PulseSequence extract_pulses() const;  // durations from each unit
StepSequence extract_steps() const;    // steps from each unit (including unit 0)
```

Combined with the existing `MelodicFigure(PulseSequence, StepSequence)`
constructor, this enables full round-tripping: figure → atoms → transform
→ recombine → figure.

**Constructor update for Phase 1b cursor model**: the existing constructor
expects `steps.count() == pulses.count() - 1` (first note has no step).
Under Phase 1b's "every unit has a step including unit 0," this should
change to `steps.count() == pulses.count()` with `steps[0]` being the
first-unit step (typically 0). Backward-compatible: add an overload or
update the existing constructor with a flag.

### 5. Strategies receive motif atoms as optional inputs

FigureTemplate gains two optional fields:

```cpp
struct FigureTemplate {
  // ... existing fields ...
  std::string rhythmMotifName;    // optional: name of a PulseSequence motif in the pool
  std::string contourMotifName;   // optional: name of a StepSequence motif in the pool
};
```

Strategy internal flow when generating a figure:

1. **Rhythm**: if `rhythmMotifName` is set → look up the named
   `PulseSequence` from the piece's motif pool and use it. Else →
   `PulseGenerator` produces one.
2. **Contour**: if `contourMotifName` is set → look up the named
   `StepSequence` from the pool and use it. Else → `StepGenerator` (or
   direction-based logic, or strategy-specific generation) produces one.
3. **Combine** via `MelodicFigure(pulses, steps)`.
4. **Apply functional coupling** — strategy-specific adjustments that
   override the raw material. CadentialApproach may lengthen the final
   note regardless of what the rhythm motif specified, because that's
   what cadences do. The motif is raw material; the strategy applies
   musical judgment.

When NEITHER motif name is set, the strategy generates both internally
(current behavior). When BOTH are set and the source is `generate`, the
strategy still runs — it may apply transforms, adjust note count to match
the rhythm, or enforce its functional constraints (targetNet, cadence,
etc.). The motif inputs are starting material, not final content.

**Strategies stay at the Figure level.** No `XxxPulseStrategy` or
`YyyStepStrategy`. The rationale: functional strategies like
CadentialApproach couple rhythm and pitch decisions ("arrive on a long
note at the target pitch"). Splitting into atom-level strategies would
decouple things that are musically fused. The atoms are for MOTIFS (data);
strategies are for FIGURES (behavior).

### 6. JSON schema for atom motifs

PieceTemplate's `motifs` array gains a `type` field:

```json
{
  "motifs": [
    {
      "name": "fate_rhythm",
      "type": "rhythm",
      "rhythm": [0.5, 0.5, 0.5, 2.0]
    },
    {
      "name": "arpeggio_contour",
      "type": "contour",
      "contour": [0, -2, -2, 4]
    },
    {
      "name": "main_theme",
      "type": "figure",
      "figure": { "units": [...] }
    }
  ]
}
```

When `type` is omitted, defaults to `"figure"` (backward compatible with
existing templates that have `"figure"` content on motifs). When `type` is
`"rhythm"` or `"contour"`, the corresponding field holds the raw atom.

FigureTemplate's new fields:

```json
{
  "source": "generate",
  "strategy": "cadential_approach",
  "rhythmMotifName": "fate_rhythm",
  "shapeDirection": -1,
  "totalBeats": 4.0
}
```

When `rhythmMotifName` is set, the strategy uses `fate_rhythm`'s
PulseSequence for durations and generates its own steps (descending
cadential approach). The rhythm is shared across any figure that
references `fate_rhythm`.

### 7. Transform references on FigureTemplate

When referencing a motif atom, the template can request a transform be
applied before the strategy uses it. New fields on `FigureTemplate`:

```cpp
struct FigureTemplate {
  // ... existing fields ...
  std::string rhythmMotifName;       // name of a PulseSequence motif
  std::string contourMotifName;      // name of a StepSequence motif
  std::string rhythmTransform;       // transform to apply to the rhythm motif
  float rhythmTransformParam{0};     // parameter for the rhythm transform
  std::string contourTransform;      // transform to apply to the contour motif
  float contourTransformParam{0};    // parameter for the contour transform
};
```

JSON example:

```json
{
  "source": "generate",
  "strategy": "skipping",
  "contourMotifName": "arpeggio_contour",
  "contourTransform": "invert",
  "rhythmMotifName": "fate_rhythm",
  "totalBeats": 4.0
}
```

**Resolution order** when the composer realizes a figure:

1. Look up `rhythmMotifName` → get `PulseSequence` (or null if not set)
2. If `rhythmTransform` is set → apply the named transform:
   - `"retrograde"` → `ps.retrograded()`
   - `"stretch"` → `ps.stretched(rhythmTransformParam)` (param is the factor)
   - `"compress"` → `ps.compressed(rhythmTransformParam)`
   - Any other value → load-time error
3. Look up `contourMotifName` → get `StepSequence` (or null if not set)
4. If `contourTransform` is set → apply the named transform:
   - `"invert"` → `ss.inverted()`
   - `"retrograde"` → `ss.retrograded()`
   - `"expand"` → `ss.expanded(contourTransformParam)`
   - `"contract"` → `ss.contracted(contourTransformParam)`
   - Any other value → load-time error
5. Pass the (optionally-transformed) atoms to the strategy
6. Strategy generates whatever isn't provided, combines, returns MelodicFigure

Invalid cross-type transforms are load-time errors with descriptive
messages: `contourTransform: "stretch"` → error "stretch is a rhythm
transform, not a contour transform"; `rhythmTransform: "invert"` → error
"invert is a contour transform, not a rhythm transform."

`contourTransform` and `rhythmTransform` fields are only meaningful when
the corresponding `MotifName` field is also set. Setting a transform
without a motif reference is a no-op (silently ignored — not an error,
since the strategy may generate the atom internally and the transform
wouldn't apply to generated content).

This enables the K467 coherence pattern:

```json
{
  "motifs": [
    {"name": "arpeggio_rhythm", "type": "rhythm", "rhythm": [1.0, 1.0, 1.0, 1.0]},
    {"name": "cadence_rhythm", "type": "rhythm", "rhythm": [1.5, 0.167, 0.167, 0.167, 1.0, 1.0]},
    {"name": "arpeggio_contour", "type": "contour", "contour": [0, -2, 3, 1]}
  ],
  "figures in phrase 1": [
    {"rhythmMotifName": "arpeggio_rhythm", "contourMotifName": "arpeggio_contour", ...},
    {"rhythmMotifName": "cadence_rhythm", "strategy": "cadential_approach", ...}
  ],
  "figures in phrase 2": [
    {"rhythmMotifName": "arpeggio_rhythm", "contourMotifName": "arpeggio_contour",
     "contourTransform": "invert", ...},
    {"rhythmMotifName": "cadence_rhythm", "strategy": "cadential_approach", ...}
  ]
}
```

Bars 1 and 3 share `arpeggio_rhythm`. Bars 2 and 4 share `cadence_rhythm`.
Bar 3's contour is the inversion of bar 1's. Coherence through shared
rhythm, variation through contour transforms.

## The Allemande Example

Matt's sequential passage: `C-B-C-G / D-C-D-A / E-D-E-B / ...`

```json
{
  "motifs": [
    {"name": "seq_cell", "type": "contour", "contour": [-1, 1, 4]},
    {"name": "seq_rhythm", "type": "rhythm", "rhythm": [0.5, 0.5, 0.5, 0.5]}
  ]
}
```

A figure template referencing `seq_cell` + `seq_rhythm` and using
`replicate(count=6, stepBetween=1)` as a transform produces the full
sequential passage. Each repetition starts 1 degree higher. The connecting
step between repetitions is `stepBetween - net_step(cell) = 1 - 4 = -3`,
which naturally bridges each copy. The pattern appears in both sections'
climax approaches, referenced from the same piece-level motif pool.

## The Beethoven 5 Example

```json
{
  "motifs": [
    {"name": "fate", "type": "rhythm", "rhythm": [0.5, 0.5, 0.5, 2.0]},
    {"name": "tag", "type": "rhythm", "rhythm": [1.0, 4.0]}
  ],
  "figures in exposition": [
    {"rhythmMotifName": "fate", "source": "generate", "strategy": "stepping",
     "direction": "descending", "targetNet": -3},
    {"rhythmMotifName": "fate", "source": "generate", "strategy": "stepping",
     "direction": "descending", "targetNet": -1}
  ],
  "development cadence figures": [
    {"rhythmMotifName": "fate", "strategy": "cadential_approach", ...},
    {"rhythmMotifName": "fate", "strategy": "stepping",
     "direction": "descending", "targetNet": -1},
    {"rhythmMotifName": "fate", "strategy": "stepping",
     "direction": "ascending", "targetNet": 1},
    {"rhythmMotifName": "tag", "strategy": "cadential_approach",
     "direction": "descending", "targetNet": -2}
  ]
}
```

One PulseSequence motif (`fate`) drives the exposition and development.
Contours are per-context (descending exposition, alternating cadence
buildup). The cadence ending (`DAH DUMMMM`) is a SEPARATE motif (`tag`)
— a short 2-note rhythm, not a fragment of the fate motif. Figures are
atoms; if you need a shorter gesture, author a shorter motif.

## Scope

### In scope for implementation

1. Expand `Motif` to hold `{Figure | Rhythm | Contour}` with a type enum
2. `PulseGenerator` class (parallel to `StepGenerator`)
3. Atom-level transform methods on `StepSequence` and `PulseSequence`
4. `MelodicFigure::extract_pulses()` / `extract_steps()`
5. `MelodicFigure(PS, SS)` constructor update for Phase 1b cursor model
6. `FigureTemplate::rhythmMotifName` + `contourMotifName` + transform fields
7. Strategy integration: optional motif inputs, generate when absent
8. JSON schema for all new types and fields
9. `Composer::realize_motifs_` updated to generate atom motifs (via
   `PulseGenerator` and `StepGenerator`) and to route MelodicFigure motifs
   through the full strategy dispatch (fixing the Phase 1a bypass)
10. K467 test template rewritten with atom motifs for coherence

### Out of scope

- Outline strategies (separate spec, deferred)
- FigureConnector resurrection (separate spec, deferred)
- Harmony-aware composition (future layer above strategies)
- Per-passage or per-phrase motif pools (piece level is sufficient)
- Complex triplet scenarios (isolated triplets tied to adjacent notes) —
  deferred to a future "tie transform" pass
- Generic "vary" transforms on atoms — deferred until specific named
  variation operations are designed (e.g. "split a random pulse,"
  "perturb steps toward chord tones," "widen the peak interval")
- Truncate / tail / fragmentation transforms — figures are atoms; author
  short motifs for short gestures rather than programmatically chopping
  longer ones
- TransformOp enum changes on FigureTemplate for the combined-figure path
  (existing transforms continue to work via `apply_transform`; atom
  transforms are accessed via the new `contourTransform`/`rhythmTransform`
  fields)

## Files affected

| File | Change |
|---|---|
| `engine/include/mforce/music/figures.h` | Add `PulseGenerator` class. Add transform methods on `StepSequence` and `PulseSequence`. Add `extract_pulses()` / `extract_steps()` on `MelodicFigure`. Update `MelodicFigure(PS, SS)` constructor. |
| `engine/include/mforce/music/templates.h` | Expand `Motif` struct with type enum + rhythm/contour fields. Add `rhythmMotifName`, `contourMotifName`, `rhythmTransform`, `contourTransform` to `FigureTemplate`. |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for all new types and fields. |
| `engine/include/mforce/music/music_json.h` | JSON round-trip for `PulseSequence` and `StepSequence` as standalone types (currently only serialized as part of MelodicFigure). |
| `engine/include/mforce/music/composer.h` | Update `realize_motifs_` to handle atom motifs. Update strategy dispatch to pass motif atoms into strategies via `StrategyContext` or `FigureTemplate` lookup. |
| `engine/include/mforce/music/shape_strategies.h` | Update Skipping/Stepping/CadentialApproach strategies to accept optional motif inputs. |
| `engine/include/mforce/music/rhythm_util.h` | Refactor into `PulseGenerator` (or absorb into `figures.h`). |
| `patches/test_k467_structural.json` | Rewrite with atom motifs for coherence testing. |
