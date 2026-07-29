# FigureGenerator plugin interface + method bake-off — design

Wolfie, 2026-07-29. Comp backlog #4 (G1). Brief spec for a Python-tooling
subsystem (no C++ engine change).

## Problem

The Markov figure generator (order-2 stupid-backoff) was a breakthrough over
random figure content, but it's one hard-coded method. G1 wants *multiple*
FigureGenerators — n-gram variants, and eventually an LLM-like next-note model —
compared apples-to-apples on the corpus "ears" (score_generated.py), per corpus.
Today there is no interface to swap methods behind and no head-to-head harness.

## Design

**One interface, `FigureGenerator` (`figuregen.py`):**
- `figure(k, rng) -> (steps, pulses)` — k-note anchored atom, `steps[0]==0`.
  This is exactly what `PoolFigureBuilder` / `markov_phrase` already consume, so
  any generator is drop-in for the composer.
- `transitions(n, rng) -> [(dstep, pulse), ...]` — free-run stream for
  distributional scoring.

**Generic n-gram model, `NGramModel(order, thresh, add_k)`:**
- `tables[n]` = n-token-context → `Counter(next)`; `tables[0][()]` is the
  unigram marginal. BOS (`streams[*][0]`, sentinel step) is context-only, never
  emitted (loop skips `i==0`), matching `markov_model`.
- Backoff: longest context with `>= thresh` obs, else shorter, else unigram.
- `add_k > 0`: additive (Laplace) smoothing of the chosen context over the full
  alphabet — a variant to test, not a default.
- **Equivalence requirement:** `NGramModel(order=2, thresh=3)` must reproduce the
  shipped `markov_model.MarkovModel` counters and `dist()` exactly, so the
  bake-off's "existing Markov" row is the real baseline. (Asserted in
  `test_figuregen.test_backoff_matches_shipped_markov`.)

**Roster (`build_methods`):** `uniform` (alphabet-uniform floor), `unigram`
(order-0), `ngram1`, `ngram2_backoff` (== shipped), `ngram3_backoff`,
`ngram2_addk` (smoothed). Registry is the report order.

**Bake-off (`bake_off.py`):** per method, generate K melodies of L notes via
`figure(L)`, realize engine-free (scale-degree→semitone in C major, the same
math `markov_phrase.predict_relative_semitones` uses — run-3-verified against the
engine), and score. Two views: **pooled JSD** (all intervals/contours pooled per
method → one JSD vs corpus; stable fidelity number) and **composite**
(per-melody `score()` aggregated mean/median/p10; end-to-end filter view). A
`corpus` row (real MTD themes) is the ceiling. `--tokens` points the whole
bake-off at another corpus's token file → "compare per corpus" for free when
Nottingham/Essen land.

## Why engine-free

Scoring needs realized pitches; the degree→semitone realizer is deterministic
and already validated against the engine, so the bake-off is pure Python and
freely iterable — no build/render in the loop. `[metric]`, iterate at will.

## Non-goals (this cycle)

- Neural next-note model (backlog #4c) — needs a trained model; numpy is
  present (1.26.4) so it's feasible, next cycle.
- Higher-order self-similarity screen (backlog #9) — the bake-off gives it its
  first evidence (all orders saturate the composite) but does not implement it.
