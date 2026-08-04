# FMSource `phase` → true phase modulation

Dipsy · 2026-08-04 · dsp backlog 3c2

## Problem

`FMSource` advertises `phase` in `param_descriptors()`, and `WaveSource::set_param`
routes it to `phase_`, so patches can wire it and the loader accepts it silently.
But `FMSource::compute_wave_value()` runs private `carrierPhase_`/`modPhase_`
accumulators and never reads `phase_` (nor `currPos_`, which every other
`WaveSource` subclass uses). The parameter is inert.

Measured, not asserted (pre-fix, 2026-08-04):

    fm_matrix__t1_06_phase_lfo.wav        86e2d1b9…  (±1 cycle @ 1.7 Hz on phase)
    fm_matrix___ctrl_t1_06_phase_lfo.wav  86e2d1b9…  (phase param DELETED)
    fm_matrix__t1_07_phase_audiorate_pm   3fa49bbb…  (±0.5 cycle @ 220 Hz)
    fm_matrix___ctrl_t1_07_…              3fa49bbb…  (phase param DELETED)

So the two "PM" topologies in the run-12 matrix batch were plain FM, and any
audition verdict on them was a verdict on the wrong signal.

## Semantics to match

`WaveSource::next()` maintains
`currPos_ = phase_initial + Σ incr + Σ (phase_i − phase_{i−1})`, which telescopes
to `phase_current + Σ incr`. The base-class contract is therefore a **phase
offset**, not an integrated delta — a constant `phase` is a static offset, and a
time-varying `phase` is phase modulation. FMSource should match that, so `phase`
means the same thing on every oscillator in the engine.

## Change

In `compute_wave_value()`, add the per-sample phase value to the **carrier's**
sin argument only:

    const float ph = phase_->current();
    float s = float(std::sin((double(carrierPhase_) + double(ph)) * TAU_D));

- Carrier only. In FM/PM convention `phase` is the carrier's phase; the
  modulator has no separate phase parameter to collide with.
- Offset, never fed back into `carrierPhase_` — accumulating it would integrate
  the modulator a second time and turn PM into (another) FM.
- Read once per sample, outside the oversample loop: `phase_->next()` is driven
  by the base class per sample, so it is constant across the M sub-steps. The
  offset is a sample-rate control signal by construction.
- `unbounded_pos` and the float-mantissa precision wall are untouched: the
  accumulator update is unchanged, only the sin argument moves.

## Bit-exactness

Default `phase` is `ConstantSource(0.0f)`. `double(x) + 0.0` is exact for every
finite float, so patches that do not wire `phase` must render **byte-identical**.
That is the null-test partition, not an expectation.

## Verification

`tools/null_test_fm.py` (already present, purpose-built for this item) renders
every patch in `patches/` containing an `FMSource` and asserts a partition:

- phase-unwired patches → byte-identical hash
- phase-wired patches   → hash MUST differ (the proof the fix took)

Plus: re-render t1_06/t1_07 as designed and confirm they now diverge from their
`_ctrl_` twins, with a spectral measurement showing PM sidebands where the
control has none.
