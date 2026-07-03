# MTD Markov Figure Generator — Phase 1 Design

**Date:** 2026-06-22
**Status:** Approved (design); pre-implementation
**Scope:** Python-only. Ends at an audition deliverable. No engine / C++ changes.

## Problem

`LibraryPassageStrategy` deploys figure shapes in musically sensible places, but fills
each cell with `RandomFigureBuilder` random content — so structure is fine and the notes
inside aren't ("computer music"). The planned fix was mining discrete figure atoms from the
MTD corpus, but that stalled: figure boundaries are genuinely arguable even in famous themes,
and hand-annotating or hand-authoring puts the user on the critical path, which is not viable.

## Approach

Stop treating the corpus as a library of discrete figures to *retrieve*, and treat it as
statistics to *generate* from. Train a Markov model on tokenized melodic streams; sample new
figure cells. **Boundaries never need to be found** — a figure's length is *imposed* at sample
time (2–9 notes), and the model fills it. This is the project's "AI flavor, lite."

Phase 1 validates one hypothesis only: **does corpus-trained cell content sound more like real
figures than uniform-random content?** It answers that with bare, isolated cells before any
engine integration, so a result is unambiguous and cheap.

## Corpus

- Source: all of `corpus/mtd_full` (1,638 themes; prefer `MTD####_score.mid`).
- Barlow & Morgenstern themes are single-line incipits; the manifest `Polyphony` column
  describes the *source work*, not the theme. Do **not** assume monophony — the tokenizer
  detects note overlaps and skips (or monophonic-reduces) any non-single-line theme, logging
  how many survive. Survivors are the training set (expected ~20–28k transitions).

## Locked settings

| Knob | Value | Rationale |
|---|---|---|
| Chains | **1 joint** chain over `(step-delta, pulse)` tokens | Captures step↔rhythm correlation; corpus is large enough to support it. |
| Order | **2 with backoff** (2 → 1 → unigram) | Captures contour shapes; backoff handles sparse contexts. |
| Chromatics | sidecar, **ignored** in Phase 1 | Diatonic skeleton only; matches existing extract/audition convention. |
| Figure length | imposed, **2–9 notes**, spread across range | A figure atom is a few notes ("dun dun dun DUN"). |
| Seeding | bootstrap from a randomly chosen **observed** context | Figures begin like real melodic starts, not cold draws. |

## Components

All new files live in `corpus/mtd_seg/` to reuse existing modules by import.

### 1. Tokenizer (`markov_tokenize.py`)
Reuses `prep_groundtruth.parse_midi` and `extract.py`'s `degree_and_accidental`,
`scale_step_index`, `snap_pulse`, `GRID`. For each surviving theme, walk the **whole** line
(no boundaries) into a token stream of `(step_delta, snapped_pulse)` per note, plus a parallel
chromatic-accidental sidecar (carried, unused). Emits one flat token corpus + a survivor log.

### 2. Model (`markov_model.py`)
Builds an order-2 joint Markov model with Katz-style backoff (2 → 1 → unigram). Exposes:
`next_token(context)` (weighted sample with backoff) and `top_paths(k, n)` (n highest-
probability k-note paths, for the diagnostic).

### 3. Generator + baseline (`markov_generate.py`)
- **Model pool:** for each length k in 2–9, sample several figures. A k-note figure =
  synthetic anchor `step[0]=0` + (k−1) sampled deltas; `pulse` = k sampled durations.
- **Random pool:** length-matched figures from a uniform-random generator over the *same*
  token alphabet (the A/B baseline standing in for `RandomFigureBuilder`).
- Both written as pool entries in the **existing `audition.py` schema**
  (`{id, num_notes, total_beats, pulse, step, chromatic, source}`) →
  `pools/generated.json` and `pools/random.json`.

### 4. Audition (reuse `audition.py`, one small addition)
Add a `--pool PATH` argument so it can target `generated.json` / `random.json` without
overwriting `paired.json`. A thin batch driver renders several cells per length from both
pools to `auditions/`. Existing `build_midi` is otherwise unchanged.

## `step=0` resolution (correctness note)

In a continuous theme stream, `step_delta == 0` legitimately means "repeat the same pitch."
The figure convention *also* uses `step[0]=0` to mean "anchor / start here." These are distinct.
Resolution: the model trains on real deltas (0 = repeat); when sampling a figure we **prepend**
a synthetic anchor-0 for note 0, then sample k−1 deltas. In an emitted figure, `step[0]` =
anchor and `step[1..]` = real deltas (which may themselves be 0). Consistent with `audition.py`
and the engine.

## Deliverable

- `pools/generated.json`, `pools/random.json` (length-matched).
- A batch of `.mid` auditions: several model cells and several random cells per length 2–9.
- `top_paths` dump: the N most-probable k-note paths (the figures the corpus "recognizes" —
  expected to surface scale runs, turns, arpeggios).
- Format is **MIDI**, played in an external player/DAW. (WAV via the engine is Phase 2.)

## Explicit non-goals (Phase 1)

- No engine / C++ changes; no `LibraryPassageStrategy` wiring.
- No passage stitching, accompaniment, or composer involvement.
- No chromatic realization.
- No phrase-arc handling — low-order Markov melody wanders; that does not matter at the cell
  scale being tested, and is a separate, higher-level problem if stitched phrases later wander.

## Decision gate after Phase 1

- **Cells sound better than random** → Phase 2: wire the modeled generator into the engine's
  figure path, render a real WAV passage (the Option B listen).
- **Cells do not sound better** → important negative result: the problem lives above the cell
  level (phrase arc / motif development), and the corpus-generation track is parked.
