# Additive partial loop — SoA / SIMD restructuring (item 8 stage 2d) — design

Date: 2026-07-31 · Dipsy · G4 (additive performance)

Stage 2a-2c landed in run 12 (commit `aafbe9b`) and took the loop from
90.6 to 51.4 ns per sample per partial. This spec covers the next stage, which
is a restructuring rather than another round of micro-optimization, and is
sized at more than one session.

## The measurement that motivates it

`tools/ablate_additive.py`, run 12:

| variant | marginal ns/sample/partial |
|---|---|
| full | 57.3 |
| no_sin (polynomial replaced by a passthrough) | 29.0 |
| loads_only (two array loads and a multiply) | 3.3 |

Two readings matter.

**There is 17x of headroom.** The loop reduced to array traffic costs 3.3 ns.
Everything above that is arithmetic and scheduling, not memory bandwidth.

**The gap is not explainable by instruction count.** The full body is roughly
25 float operations plus a 6-FMA polynomial. On a 3 GHz core that should be a
handful of nanoseconds if it pipelined; it costs 57. Something is serializing.
The two candidates:

- **Loop-carried dependency chains.** `val += v` is a single serial float
  accumulator: 200 partials × ~4 cycles of add latency = 800 cycles per sample
  that cannot overlap. That alone is ~4 ns/partial at N=200.
- **No vectorization.** The body reads nine separate arrays and writes two,
  with a NaN test and an early return in the middle. MSVC will not
  auto-vectorize across that control flow, so every partial is scalar.

Run 12 also produced an anti-result worth honoring: the sin polynomial's
quadrant folds were *suspected* to be mispredicting branches, a branchless
degree-13 replacement was fitted and measured, and it was **a wash** on
controlled A/B. MSVC already compiles those selects branchlessly. The lesson
for this stage: hypotheses about where cycles go are cheap and usually wrong
here — every step below is gated on a measurement, and the ablation harness
already exists to take it.

## Approach

### Phase 1 — cheap scheduling fix: TRIED AND REJECTED (run 12)

The obvious first move was to give the scheduler independent work: unroll the
partial loop 4x with four independent accumulators, removing the one
loop-carried dependency (`val +=`) and putting four of the long per-partial
dependency chains in flight at once. Fifteen lines, no SIMD, no layout change.

Measured on controlled A/B: **consistently slower** — 0.88x / 0.94x / 0.93x on
the 32/96/200-partial ladder, neutral on everything else. Reverted.

So the remaining 57 ns is *not* the accumulator chain and *not* simple ILP
starvation — MSVC was already scheduling across iterations, and the manual
unroll only cost register pressure. Together with the branchless-sin
anti-result this closes off the cheap explanations: **there is no scheduling
trick left, and anything that moves this number has to be real vector
arithmetic — 4-8 lanes of genuine SIMD — or nothing.** That raises the cost of
this stage and should be weighed against stage 2e before committing to it.

Phase 1 is therefore now: a standalone throwaway benchmark over synthetic
arrays comparing the current scalar body against an explicitly vectorized one
(SSE2/AVX intrinsics, or `std::experimental::simd` if the toolchain has it)
processing 4-8 partials per iteration. **Do not touch `Partials` until that
prototype shows a real win in isolation** — two theories have already died
here, and the prototype is far cheaper than a partial refactor.

Vectorizing changes the float addition order, so the result is not bit-exact —
it lands in the same "measured residual, not asserted identity" category as
the sin polynomial, and `tools/residual_test_additive.py` is the gate.

### Phase 2 — data layout

The body currently reads `mult1_`, `mult2_`(via cache), `po1_`, `po2_`,
`dtVals_`, `ampl1_`, `ampl2_`, `partialPos_`, `partialLPO_`, `pmultCache_`,
`rolloffCache_` — eleven independent streams. For SIMD each must be
contiguous and ideally aligned. Two options:

- **Keep SoA, align the vectors.** Least disruptive: the arrays already are
  structure-of-arrays, they just need aligned allocation and padding to a
  multiple of the vector width so the tail needs no scalar remainder loop.
- **Pack into a single interleaved block.** Better locality, much more
  invasive, and it fights the existing `get_array()` / UI accessors.

Start with the first. The padding trick (round `n` up to a multiple of 8 and
zero the tail amplitudes) removes the remainder loop and is worth doing even
without SIMD.

### Phase 3 — the branches inside the body

Vectorizing requires the per-partial control flow to become arithmetic:

- `if (pfreq > CUTOFF) return NaN` and the caller's `isnan` skip → replace with
  a mask multiply. The NaN protocol exists only to communicate "past cutoff"
  across the old per-partial interface; inside a batched loop it can just be a
  zero-weight lane. **This changes nothing audibly but must be checked against
  the null test** — a partial exactly at the cutoff boundary must keep its
  current treatment.
- The optional layers (`motionActive_`, `shimmerActive_`, `tradeActive_`,
  `onsetActive_`, `sBwActive_`) are per-*sample* booleans, not per-partial, so
  they should be hoisted into separate specialized loop bodies rather than
  tested inside. A template parameter or a small dispatch on the active-flag
  combination keeps one clean vector loop per configuration.
- The bandwidth layer's per-partial RNG walk (`rng_.valuePN()` on segment
  boundaries) is inherently serial and per-partial-stateful. If it resists
  vectorization, specialize it out: patches with `bandwidth == 0` — most of
  them — take the fast loop.

### Phase 4 — the sin

`fast_sin_turns` is already a branchless-in-practice polynomial and vectorizes
trivially (it is 6 FMAs on a folded argument). The folds become select
intrinsics. No accuracy change; the existing residual measurement still
applies.

## Verification

Unchanged from stage 2a, and non-negotiable:

1. `tools/null_test_additive.py` for anything claiming bit-exactness.
2. `tools/residual_test_additive.py` for anything that isn't — report peak and
   RMS residual in dBFS, do not assert inaudibility without it.
3. `tools/ab_render_time.py` with both binaries alternated, never a
   before-number from one session against an after-number from another.
   `mforce_ui` holding a core makes single-run absolute timings useless.
4. `tools/ablate_additive.py` to confirm the marginal cost actually moved.

## Success criterion

The target is the region the ablation opened up: **under 20 ns/sample/partial**,
i.e. >1000 partials/core real-time, which would put recurse-2 partial expansion
(backlog item 5) and the CMA-ES eval loop (item 3) in reach without further
work.

The abort criterion matters just as much, given two dead theories: if the
isolated SIMD prototype in phase 1 does not beat the scalar control by at
least 2x on synthetic arrays — where there is no engine structure in the way —
then the loop is limited by something neither this spec nor the ablation has
identified, and the honest move is to stop, write down the number, and put the
effort into stage 2e instead.

## Explicitly out of scope

iFFT overlap-add additive synthesis is stage 2e. It is a different synthesis
algorithm rather than a faster implementation of this one — thousands of
partials become cheap, but per-partial frequency motion, bandwidth noise and
onset dispersion all have to be re-expressed in the frequency domain, and the
result is not comparable sample-for-sample. It needs its own spec and a
listen gate.
