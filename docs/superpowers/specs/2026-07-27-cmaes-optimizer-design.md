# CMA-ES patch optimizer — design spec (dsp backlog #3, GOALS G1a)

Goal: close the loop that runs 1-2 did by hand — search additive-patch
parameter space to minimize measured distance to a real-instrument
reference set. First target: Iowa viola. Output: an optimized patch +
the reusable optimizer for any sampled instrument (G1a "library of usable
instrument patches").

## Why CMA-ES

Run-2's manual calibration showed the problem shape: parameters interact
through the render (vibrato leaks into motion/AM measurements), gradients
are unavailable, evaluations are expensive-ish (~10 s render+analysis),
dimensionality is modest (20-40). CMA-ES is the standard tool for exactly
this regime. Pure-numpy implementation (~120 lines, no new deps) or
`cma` package if Matt approves a pip install — spec assumes pure-numpy.

## Parameter vector (v1: 24 dims, all bounded, sigmoid-mapped)

- Harmonic envelope: 8 knots, log-spaced over harmonics 1-48, interpolated
  multiplicatively over the measured ampl1 baseline (NOT free per-partial
  gains — 96 dims of mush; knots preserve the measured spectrum's shape).
- Formant/body: fmtWt, plus 4 gain knots over the BandSpectrum (160-6k).
- Motion layer: motionDepth, motionHz, motionEvolve (coherence pinned 0 —
  measured), shimmerDepth, shimmerHz, shimmerCoherence, shimmerEvolve.
- Residue/attack: bandwidth floor, burst level, burst decay, cluster
  sustain, onsetSpread, onsetTilt.
- Vibrato: depth, speed (Iowa refs barely vibrate — let the optimizer
  decide; Matt's aesthetic preference can re-pin these later).

## Scorer (per candidate; note set C3/G3/D4/A4 ~ the Iowa SOURCE_SET)

Weighted sum, all terms already implemented or trivial extensions of
research/ml_ears/ code:
1. Harmonic envelope distance — log-amp L2 over harmonics 1-32 vs the
   per-note Iowa reference (derive_formant/build_warmstart machinery).
2. Motion stats distance — derive_motion metrics (resid cents RMS, rates,
   amp fluct, coherences) vs Iowa medians, normalized per-metric.
3. Inter-harmonic broadband — ratio of between-line energy to line energy
   in 3 bands vs reference (the ml_ears quantified gap).
4. Attack envelope — per-band (lo/mid/hi) onset-lag + rise-time match vs
   reference attacks.
Weights v1: 0.35 / 0.25 / 0.2 / 0.2. Report per-term always — weight
tuning will need Matt's ears exactly once the winners start sounding
close.

## Loop mechanics

- Population lambda=12, mu=6, sigma0=0.25 (normalized space); budget
  ~600 evals (~2 h wall at 10 s/eval, embarrassingly parallelizable later).
- Each eval: patch JSON from vector -> mforce_cli render (4 notes, 2 s
  each) -> score. Renders to a scratch dir, best-so-far patch + WAV
  checkpointed every generation to research/ml_ears/cmaes_runs/<run>/.
- Resumable: generation state (mean, cov, sigma, rng) pickled per gen.
- Determinism: fixed masterSeed per candidate index (CLAUDE.md seeds rule).

## Staging (each lands independently)

a. `score_candidate.py` — render+score one patch, print per-term breakdown.
   Validate: score(v5_04) < score(v3_00 control) on motion terms.
b. Pure-numpy CMA-ES module + 2D toy test (converges on Rosenbrock).
c. `optimize.py` — full loop, 100-eval smoke run.
d. 600-eval viola run -> REVIEW queue: best-of-run WAV vs v5 hand-tuned.
e. Generalize: instrument config file (sample dir, note set) -> flute or
   cello second target.

## Non-goals (v1)

Realtime perf, novelty scoring (separate backlog item), per-note-register
param variation (one global vector; register-dependence is v2).
