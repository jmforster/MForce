# Constrained Atom Supply — Tagged Markov Pool + Pool-Backed FigureGenerator

**Date:** 2026-06-22
**Status:** Approved (design); pre-implementation
**Depends on:** Phase 1 Markov generator (`corpus/mtd_seg/markov_*.py`)
**Builds toward:** C++ Phrase/Passage strategy work — this is the *atom supply* the strategies pull from.

## Goal

Give the C++ strategy layer a clean way to request a melodic atom by **constraint** —
"a 3-note ascending figure spanning 2 beats" — and get back a real, idiomatic Markov-
generated figure. The generator (Python Markov) stays offline and untouched; the C++ side
sees only "good atoms tagged by length/beats/contour" and **selects** one.

## Constraint vocabulary (all optional, independently combinable)

| Constraint | Maps to | Meaning |
|---|---|---|
| `noteCount` | existing `Constraints.count` | number of notes |
| `totalBeats` | existing `Constraints.length` | summed duration → **density** falls out (notes ÷ beats) |
| `contour` | **new** `Constraints.contour` | one of `Up, Down, Arch, Valley, Level` |

- **All three are optional.** Absent = wildcard. `build({})` ("give me a figure") is valid and
  returns a weighted draw from the whole pool. Any subset is valid.
- **`net` is deliberately NOT here.** Net movement is a *seam/skeleton* concern (handled by
  connector `leadStep` between cells), not an atom-generation constraint — forcing net on a short
  cell over-constrains it and duplicates the placement axis we already built.

## Mechanism: pre-generated tagged pool + weighted selection

Contour is a **whole-figure** property (you only know a figure is "ascending" once it's complete),
so the natural mechanism is **generate → measure → tag → select**, not biasing a left-to-right
sampler. Two deliverables, joined by the pool JSON contract.

### A. Python — pool generator (`corpus/mtd_seg/markov_pool.py`)

1. **Sample** a large batch of figures from the Markov model (reusing `gen_model`), across the
   note-count range (2–9). Sampling is the existing weighted Markov draw — so common shapes recur,
   rare shapes are rare.
2. **Leap cap (pool-inclusion filter):** discard any atom containing an interval with
   `abs(step) > 7` scale degrees (> one octave). Trims outliers; the model already makes big leaps
   rare.
3. **Measure + tag** each surviving atom: `noteCount`, `totalBeats`, `contour` (classifier below).
4. **Accumulate weight by identity:** dedupe atoms by their `(steps, pulses)` content and keep a
   **`count`** = how many times the model produced it. This `count` IS the Markov weight that
   selection must honor (so C-D-E-F outweighs C-D-E-G within the "Up" bucket).
5. **Write** the pool JSON (schema below) to an engine-readable path (`lib/figures/markov_pool.json`).

**Coverage is deliberately corpus-shaped, not force-filled.** Common (noteCount × contour ×
beats) buckets get many options; rare buckets (e.g. 8-note Level) may be thin. We do **not**
top-up rare buckets with targeted sampling, because that would distort the weights. Thin/empty
buckets are handled by the selector's no-match path, not by faking coverage.

**Contour classifier** (Python, at tag time). From the cumulative scale-degree path
`d[0..n-1]` (`d[0]=0`), with `e=d[-1]`, `hi=max(d)`, `lo=min(d)`, define:
- `upExc   = hi - max(0, e)`   (how far the path peaks above both endpoints)
- `downExc = min(0, e) - lo`   (how far it troughs below both endpoints)
- `R = 2`  — minimum return (in scale degrees) for an excursion to count as Arch/Valley.
  Default 2; **tunable**. Keeps Arch/Valley meaning a *real* there-and-back, so a single
  terminal pullback stays directional.

Classify in order:
1. `Level`  — `hi - lo <= 1` and `abs(e) <= 1`  (repeated notes / neighbor hover).
2. `Arch`   — `upExc >= R` and `upExc >= downExc`  (dominant interior peak).
3. `Valley` — `downExc >= R` and `downExc > upExc`  (dominant interior trough).
4. `Up`     — `e > 0`.
5. `Down`   — `e < 0`.
6. `Level`  — fallback (`e == 0`, no excursion).

Worked: `C-D-E-F-E` (path `0 1 2 3 2`) → `upExc=1 < R` → **Up** (one-step pullback isn't an
arch). `C-D-E-D-C` (`0 1 2 1 0`) → `upExc=2 >= R` → **Arch**.

Thresholds (`R`, and the Level `<=1` band) are tunable during implementation against audition;
the **five classes and Python-side classification are the fixed design**, the cutoffs are not.

### B. C++ — pool-backed selector

- **New `Contour` enum** (`Up, Down, Arch, Valley, Level`) and **`std::optional<Contour> contour`**
  added to `Constraints` (`figure_constraints.h`).
- **New `PoolFigureBuilder`** (sibling to `RandomFigureBuilder`, same `MelodicFigure build(const
  Constraints&)` interface). It:
  1. Loads `lib/figures/markov_pool.json` once (static cache, like `LibraryPassageStrategy`).
  2. Filters the pool by whichever of `count` / `length` / `contour` are **present** (absent =
     wildcard). `length` matches within a small tolerance (beats bucket).
  3. **Weighted-selects** among matches using each atom's `count` field — preserving the Markov
     idiom.
  4. **No match → throw** (`std::runtime_error`), parallel to `RandomFigureBuilder`'s throw, so a
     strategy can catch and relax a constraint rather than receive junk.
- `RandomFigureBuilder` is left untouched; `PoolFigureBuilder` is the Markov-backed alternative a
  strategy chooses. (Whether a strategy uses random vs pool is the strategy's call — out of scope
  here.)

## Pool JSON contract

```json
{
  "version": 1,
  "source": "mtd_markov_order2",
  "leapCapDegrees": 7,
  "atoms": [
    {
      "units": [{"duration":0.5,"step":0},{"duration":0.5,"step":1},{"duration":0.5,"step":1}],
      "noteCount": 3,
      "totalBeats": 1.5,
      "contour": "Up",
      "count": 412
    }
  ]
}
```

## Components / files

- Create: `corpus/mtd_seg/markov_pool.py` — sampler + leap-cap + classifier + weighted dedupe + emit.
- Create: `corpus/mtd_seg/test_markov_pool.py` — plain-assert tests (classifier cases, leap-cap,
  weight accumulation, schema).
- Create: `lib/figures/markov_pool.json` — generated artifact (committed or regenerated; decide at
  plan time).
- Modify: `engine/include/mforce/music/figure_constraints.h` — add `Contour` enum + `contour` field.
- Create: `engine/include/mforce/music/pool_figure_builder.h` — the selector.
- Test (C++): a small harness (mirror existing engine test style) — load a tiny fixture pool,
  assert filter + weighted-select + no-match-throws.

## Verifications required during implementation

- **Classifier vs ear:** spot-check that atoms tagged `Arch`/`Valley`/`Level` actually sound like
  it (render a few per class). Tune the `<=1` band if mis-binned.
- **Weight fidelity:** confirm within-bucket selection frequency tracks `count` (draw N, compare
  histogram to counts).
- **No-match path:** confirm an over-constrained request (e.g. `noteCount=2, contour=Arch` —
  impossible, a 2-note can't arch) throws cleanly rather than returning a wrong-shape atom.
- **Constraints reuse:** confirm `count`/`length` semantics match how `RandomFigureBuilder` already
  interprets them (so a strategy can swap builders without rethinking constraints).

## Non-goals

- The Phrase/Passage **strategies** that consume `build(constraints)` — separate downstream work.
- **Live** C++ Markov generation (Option B) — deferred; pool is the v1 supply.
- **`net`** as an atom constraint — excluded by design (seam-level).
- **Contrast-conditioned** generation (fig2 contrasts fig1) — separate backlog
  ([[project_contrast_figure_todo]]).
- **Order-3 / corpus expansion** — deferred ([[project_markov_figure_generator]]).

## Decision gate

Once the pool + selector land, a strategy can request constrained atoms. The open question that
answers: do constraint-driven requests (`Up`, `Arch`, busy vs sedate) give the strategy layer
enough expressive control to build good phrases — or do we need live generation (B) for coverage
the pool can't supply?
