# Additive hot-loop optimization (item 8 stage 2a) — design

Date: 2026-07-31 · Dipsy · G4 (additive performance)

## Problem

Run-8 stage-1 profiling put the additive steady-state cost at ~64 ns per
sample per partial (~325 partials/core real-time single-thread). That is the
wall between MForce and real-time additive, and it also render-binds the
CMA-ES optimizer (item 3) and the recurse-2..4 expansion sweep (item 5).

The hot path is `Partials::get_partial_value()`, called once per partial per
sample through a virtual dispatch from `FullAdditiveSource::compute_wave_value()`.

## Where the time actually goes

Per partial, per sample, the current code does:

| # | Work | Notes |
|---|---|---|
| 1 | 5 virtual `ValueSource::current()` calls (mult/ampl/po/ro/dt) | value is **identical for every partial** — pure waste × N |
| 2 | +3 more virtual `current()` (bw, motion, shimmer) when those layers are on | same |
| 3 | `std::pow(pmult, ro)` | libm call; argument is invariant across samples whenever multEnv/roEnv are constants (the common case) |
| 4 | `std::pow(pmult, moScale)` | same, when motion scaling is on |
| 5 | `std::fmod(x, 1.0f)` | libm call; `x` is always in ≈(-2,2) here |
| 6 | virtual `get_partial_value` dispatch | blocks inlining of the whole body; also blocks vectorization |
| 7 | `MultiPartials::get_partial_value` linear scan over sets | O(sets) **per partial** |
| 8 | `std::sin` | irreducible without an approximation |

Items 1-7 are pure overhead. None of them require changing the math.

## Approach: stage 2a is strictly bit-exact

The stated plan for stage 2 flagged that a sin table/poly approximation
"would break bit-exactness → null test + Matt's ear". That is true of the
**sin** work, and it is deferred to stage 2b. Everything in 1-7 above can be
removed with *identical* float operations in an *identical* order, so the
rendered output must be byte-identical. That makes the whole of stage 2a
self-verifying: the acceptance test is a byte-for-byte WAV comparison across
a representative patch set, no ears needed.

### A. Batch entry point — `IPartials::sum_partials()`

New virtual on `IPartials`:

```cpp
virtual float sum_partials(float amplitude, float frequency, float phaseDiff,
                           IFormant* formant, float fmtWt, float fmtFloor);
```

Default implementation on the interface loops `get_partial_value()` exactly as
`FullAdditiveSource` does today (NaN → `continue`, else `+=`), so any
implementer that doesn't override keeps working unchanged.

- `Partials` overrides it with the loop inlined against a private
  `partial_value_impl()` — one virtual call per sample instead of N, and the
  body becomes inlinable/hoistable.
- `MultiPartials` overrides it to walk its sets once (kills the O(sets×N) scan).
- `FullAdditiveSource::compute_wave_value()` calls `sum_partials()`.

Summation order is unchanged, so the accumulated float sum is unchanged.

### B. Per-sample scalar cache

`partials_next()` already runs exactly once per sample, before the partial
loop. It caches the eight envelope `current()` values and the scalars derived
from them (`sDt_`, `sRo_`, `sBw_`, `sMd_`, `sSd_`) into plain floats.
`partials_prepare()` seeds the same cache so sample 0 is never stale.

The derived scalars are computed with the same expression and operand order as
today, so they are the same bits.

### C. Rolloff / pmult cache

`pmult_i = mult1_[i] + (mult2_[i] - mult1_[i]) * multE` and
`rolloff_i = 1 / pow(pmult_i, ro)` depend only on `(multE, roE)` — scalars.
Keep per-partial `pmultCache_` / `rolloffCache_` vectors plus the
`(multE, roE)` they were built from; rebuild only when either changes by exact
float comparison. `pow` is deterministic, so a cached value is bit-identical to
a recomputed one.

The same key covers `pow(pmult, moScale)` (motion frequency scaling), cached in
`moScaleCache_`.

Hit rate: 100% after the first sample whenever `multEnv`/`roEnv` are constants,
which is every patch in the repo today. When they are real envelopes the cache
misses every sample and costs one float compare — no regression.

Vectors are sized in `partials_prepare()`; nothing allocates in the render loop.

### D. `fmod` → `truncf`

`std::fmod(x, 1.0f)` with `x ∈ (-2, 2)` equals `x - std::truncf(x)` exactly —
both operands are floats and the difference is representable, so no rounding
occurs in either formulation. `truncf` compiles to a single `roundss`.

## Verification

1. Build.
2. **Null test**: render a representative patch set (viola default, clarinet
   template, a vowel-baseline glide, an expand-recursion patch, a multi-partials
   patch, the profiling ladder) before and after; assert byte-identical WAVs.
   Any difference is a bug, not a taste question.
3. Re-run `tools/prof_additive.py` for the ns/sample/partial delta.

## Not in this stage

- `std::sin` approximation / SIMD batching (**stage 2b**, not bit-exact →
  null-test residual + Matt's ear).
- iFFT overlap-add additive (**stage 2c**, structurally different synthesis).

## Measurement caveat

`mforce_ui` was resident during this run holding ~98% of one core on a 4-core
box, so absolute wall times are inflated and noisy relative to the run-8
baseline. Before/after ratios are taken from the same contended machine in the
same session; the null test carries the correctness claim regardless.
