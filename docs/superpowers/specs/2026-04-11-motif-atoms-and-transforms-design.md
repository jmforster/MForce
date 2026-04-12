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
  enum class Type { Figure, Rhythm, Contour };
  Type type{Type::Figure};

  MelodicFigure figure;       // when type == Figure
  PulseSequence rhythm;        // when type == Rhythm
  StepSequence contour;        // when type == Contour

  bool userProvided{false};
  uint32_t generationSeed{0};
  std::optional<FigureTemplate> constraints;
};
```

All motifs live at the **Piece level** in `PieceTemplate::motifs`. Even a
motif used in only one passage is a piece-level raw material. A sequential
pattern like `SS{-1, 1, 4}` might appear in both sections of a dance
movement — piece scope captures that reuse naturally.

Using separate fields rather than `std::variant` because `PulseSequence`
and `StepSequence` are cheap empty vectors and it avoids visitor-pattern
verbosity. Only the field matching `type` is meaningful; the others are
default-constructed and ignored.

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

Valid durations: `{0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0}` (all
binary subdivisions, multiples of 0.25 so sums are always exact). Weighted
toward `defaultPulse` — if the pulse bias is `1.0` (quarter), quarters are
most likely but eighths and halves also appear. Variable note count per
generation.

Returns a `PulseSequence` (not `vector<float>` as the current utility
does). The existing `generate_musical_rhythm` in `rhythm_util.h` can be
refactored into this class or replaced.

### 3. Atom-level transforms

Transforms partition naturally by atom type. Each atom exposes ONLY the
transforms that make musical sense for it:

**StepSequence transforms:**

| Transform | What it does |
|---|---|
| `inverted()` | Flip all step signs (ascending ↔ descending) |
| `retrograded()` | Reverse step order |
| `expanded(float factor)` | Multiply all step magnitudes (widen intervals) |
| `contracted(float factor)` | Divide all step magnitudes (narrow intervals) |
| `varied(Randomizer&, int count)` | Randomly perturb some step values |
| `truncated(int n)` | Take first N steps (fragmentation) |

No `stretched()` or `compressed()` — those are duration concepts.

**PulseSequence transforms:**

| Transform | What it does |
|---|---|
| `retrograded()` | Reverse duration order |
| `stretched(float factor)` | Multiply all durations (augmentation) |
| `compressed(float factor)` | Divide all durations (diminution) |
| `varied(Randomizer&)` | Split or dot random pulses (same logic as FigureBuilder::vary_rhythm but on the atom) |
| `truncated(int n)` | Take first N durations (fragmentation — Beethoven 5's `DUN` from `dun dun dun DUN`) |

No `inverted()` — inverting a rhythm is not a musical concept.

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
applied before the strategy uses it:

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

`contourTransform` applies `StepSequence::inverted()` to the named contour
motif before the strategy sees it. `rhythmTransform` (if present) applies
to the named rhythm motif. Only atom-appropriate transforms are valid —
`contourTransform: "stretch"` is a load-time error because stretch is a
rhythm transform, not a contour transform.

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
    {"name": "fate", "type": "rhythm", "rhythm": [0.5, 0.5, 0.5, 2.0]}
  ],
  "figures": [
    {"rhythmMotifName": "fate", "source": "generate", "strategy": "stepping",
     "direction": "descending", "targetNet": -3},
    {"rhythmMotifName": "fate", "source": "generate", "strategy": "stepping",
     "direction": "descending", "targetNet": -1}
  ],
  "development cadence figures": [
    {"rhythmMotifName": "fate", "strategy": "cadential_approach", ...},
    {"rhythmMotifName": "fate", "rhythmTransform": "truncate:1",
     "strategy": "stepping", "direction": "descending", "targetNet": -1},
    {"rhythmMotifName": "fate", "rhythmTransform": "truncate:1",
     "strategy": "stepping", "direction": "ascending", "targetNet": 1}
  ]
}
```

One PulseSequence motif (`fate`) drives the entire movement. Contours are
per-context (descending exposition, alternating cadence buildup).
Fragmentation via `truncate:1` produces the `DUN ... DUN ... DUN` cadence
compression. All from one 4-element rhythm motif.

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
- Triplet rhythms in PulseGenerator (binary subdivisions only for v1)
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
