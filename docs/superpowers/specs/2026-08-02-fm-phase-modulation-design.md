# FMSource phase parameter — true PM (design)

Dipsy, 2026-08-02. Backlog dsp/3c2.

## Problem

`FMSource` advertises `phase` in `param_descriptors()` (inherited from
`WaveSource`) and `set_param` routes it to `WaveSource::set_phase`, so patches
can wire anything to it and the loader accepts it silently. But
`compute_wave_value()` runs its own `carrierPhase_`/`modPhase_` accumulators
and never reads `currPhase_`. The parameter is dead.

Consequence found in run 16: the two `fm_matrix` topologies designed as phase
modulation (`t1_06_phase_lfo`, `t1_07_phase_audiorate_pm`) rendered as plain
FM, byte-identical across phase variants. Two more patches
(`t2_10_all_lfo_incommensurate`, `t3_23_phase_velvet_jumps`) carry a wired
`phase` that contributes nothing. Any audition verdict on those four was a
verdict on something other than what the patch says it is.

## What the base class means by "phase"

`WaveSource::next()` maintains

```
currPos_ = fmod(currPos_ + currPhaseIncr_ + (currPhase_ - lastPhase_), 1)
```

which telescopes to `sum(incr) + currPhase_` (initial `currPos_` is seeded from
`phase_` on the first sample). So for every other oscillator, `phase` is an
**instantaneous offset in cycles applied to the read position** — not a
frequency contribution. Matching that semantic exactly is the whole fix.

## Change

In `FMSource::compute_wave_value()`, add `currPhase_` to the carrier's sin
argument at read time:

```cpp
float s = float(std::sin((double(carrierPhase_) + double(currPhase_)) * TAU_D));
```

Deliberate choices:

- **Read-time offset, not accumulator injection.** Adding to `carrierPhase_`
  would feed back into the wrap logic and the `unbounded_pos` legacy path
  (the spacy-FM precision-wall behaviour depends on that accumulator's exact
  float history). An offset at the read leaves the accumulator untouched.
- **Carrier only.** There is one `phase` param. Carrier-phase modulation is
  the standard meaning of PM (and what the DX-series architecture actually
  does). Modulating the modulator's phase is a different effect; if wanted it
  gets its own param later, not a silent reinterpretation of this one.
- **No config toggle.** Per project convention (no patch back-compat) the
  formula just changes.

## Bit-exactness

`phase` defaults to `ConstantSource(0.0f)`, so `double(currPhase_) == 0.0` and
the sum is the identical double. Every existing patch that does not wire
`phase` must be byte-identical. That is the null test: all 133 FM-bearing
patches minus the 4 that wire `phase`.

## Known limitation (documented, not fixed here)

`phase_->next()` is called once per **output** sample by `WaveSource::next()`,
so under `oversample > 1` the offset is held constant across the M sub-samples,
exactly like `depth`/`carrierRatio`/`modRatio` already are. Audio-rate PM at
M > 1 therefore modulates at the base rate. Lifting that means running the
param sources at the oversampled rate, which is a much bigger change (it
affects every param of every oversampled source). Out of scope; noted at the
call site.

## Why PM is worth having, not just a bug to close

With FM as implemented, `carrierFreq = base*cRatio*(1 + modVal*depth)` — the
modulation is proportional to the carrier frequency, so peak *phase* deviation
(the perceptual index) is constant with pitch only by accident of that
scaling. With true PM the index is the phase offset itself: **frequency-
independent**, which is why PM instruments keep a consistent spectrum across
the keyboard where naive FM brightens or dulls. That is a real, distinct
synthesis mode this engine did not have, not a variant of one it had.

## Verification

1. Build cli + ui; `mforce_ui --stamp` exits 0.
2. Null test: render every FM patch before/after, byte-compare. Expect
   identical on all patches that do not wire `phase`, and **differing** on the
   four that do (a differing result on those is the proof the fix took).
3. Spectral proof of PM: at fixed depth, sweep the carrier over octaves and
   measure spectral centroid / base-frequency ratio. FM's index-vs-pitch
   behaviour and PM's must differ measurably, or the parameter still is not
   doing what this doc claims.
4. Re-render the four affected `fm_matrix` topologies as designed, plus a PM
   batch, novelty-rank, queue for ears.
