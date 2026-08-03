# Piano unison-string beating — measured 2026-08-03 (Dipsy, dsp run 18)

Closes open item #1 of `piano_analysis_report.md` ("Unison-string beating rates
unmeasured (needed to lock shimmer dims)"). Script: `research/ml_ears/piano_beating.py`.
Raw numbers: `out/piano_beating.json`; figure `out/piano_beating.png` (both gitignored,
regenerate with `python research/ml_ears/piano_beating.py`).

**Headline: the beat RATE is not measurable from the envelope and must stay
searchable in the piano encoder. The beat DEPTH is real, ~0.15, and can be seeded.**

## Why there was anything to measure

Mid/treble keys carry 2-3 slightly-detuned strings, so each partial is a sum of
near-equal frequencies and its AMPLITUDE beats at the difference. That is what the
engine's shimmer layer already does, so the mapping is direct: beat rate → `shimmerHz`,
beat depth → `shimmerDepth1/2`, rate agreement across partials → `shimmerCoherence`.

## Method and the control

Heterodyne each partial to baseband, take |z|, remove the decay (two-segment fit —
double decay is universal per the main report), spectrum of the residual, peak = beat
rate, prominence over the off-peak median = accept/reject.

Built-in control: **single-strung bass must not beat like a trichord.** This is what
caught both failures below; without it the first pass would have shipped confident
numbers that were pure artifact.

## Attempt 1 — failed its own control

3 s window. Every partial of every note, single-strung bass included, "beat" at 0.35 or
0.69 Hz — i.e. FFT bins 1 and 2 at that window length. Leftover decay curvature the
piecewise detrend did not flatten, not beating.

## Attempt 2 — depth control passes, rate does not survive testing

8 s window, 4-cycle floor (0.5 Hz), residual high-passed at the floor, depth taken from
the narrowband component rather than the total residual std.

| note | strung | f0 | beating | rate (Hz) | depth | coherence |
|---|---|---|---|---|---|---|
| B0 | single | 30.6 | 10/10 | 0.51 | **0.029** | 0.00 |
| C1 | single | 32.4 | 10/10 | 2.02 | **0.016** | 0.00 |
| F1 | double | 43.4 | 10/10 | 0.52 | 0.150 | 0.69 |
| C2 | double | 65.3 | 10/10 | 0.63 | 0.113 | 0.99 |
| G2 | double | 97.9 | 10/10 | 0.63 | 0.216 | 0.82 |
| C3 | double | 130.9 | 9/10 | 0.56 | 0.148 | 0.74 |
| G3 | triple | 196.4 | 9/10 | 0.63 | 0.106 | 0.84 |
| C4 | triple | 262.2 | 8/10 | 0.65 | 0.174 | 0.81 |
| G4 | triple | 392.9 | 5/10 | 0.96 | 0.160 | 0.80 |
| C5 | triple | 522.7 | 7/10 | 1.98 | 0.123 | 0.00 |
| C6 | triple | 1041.0 | 1/10 | 1.90 | 0.285 | 1.00 |
| C7 | triple | 2082.7 | 0/5 | — | — | — |

The rates cluster at 0.51-0.65 with the floor at 0.50, which is what a boundary
artifact looks like — so it was tested rather than trusted. Re-running with the floor
raised:

| note | floor 0.50 | floor 0.75 | floor 1.12 |
|---|---|---|---|
| B0 | 0.51 | 2.79 | 4.78 |
| F1 | 0.52 | 0.84 | 1.59 |
| C2 | 0.63 | 0.96 | 3.16 |
| G2 | 0.63 | 1.01 | 1.39 |
| C3 | 0.56 | 0.89 | 1.36 |
| G3 | 0.63 | 0.93 | 1.49 |
| C4 | 0.65 | 1.04 | 1.52 |
| G4 | 0.96 | 0.96 | 1.27 |
| C5 | 1.98 | 1.98 | 1.98 |
| C6 | 1.90 | 1.90 | 1.90 |

**9 of 11 climb with the floor; 2 hold.** The rate is a window artifact except at
C5/C6, which sit dead steady at ~1.9-2.0 Hz across all three floors. `piano_beating.py`
now runs this check itself and refuses to recommend locking `shimmerHz`.

## What is usable

- **Depth contrast is real and structural.** Genuinely single-strung B0/C1 modulate at
  0.029/0.016; everything multi-strung sits at 0.106-0.285, median **0.150** — a 5-9x
  separation. Per-partial envelope modulation in the unison registers is worth
  modelling, at order 0.1-0.2.
- **F1 was mislabelled, and the data said so.** Called single-strung on the first pass,
  it measures 0.150 — squarely multi-strung. Real pianos go single only for the lowest
  few keys; F1 is a bichord. The control is B0/C1.
- `shimmerCoherence` (0.81) is computed FROM the rates and inherits their
  unreliability. Not proposed as a locked value.

## Recommendation for the piano encoder

| dim | disposition |
|---|---|
| `shimmerDepth1/2` | seed ≈ 0.15, searchable in a narrow band |
| `shimmerHz` | **fully searchable** — not lockable from this pass |
| `shimmerCoherence` | **fully searchable** |

## Next method (dsp backlog 13)

Two attempts, so this stops here rather than getting a third inline. The envelope
domain is the wrong tool. The unison strings are split in FREQUENCY by exactly the beat
rate (0.6-2 Hz), which a ≥2 s coherent FFT resolves directly in the bass/mid — resolve
them as separate spectral lines and read the split instead of inferring it.
