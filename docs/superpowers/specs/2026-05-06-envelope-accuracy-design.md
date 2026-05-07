# Envelope stage_accuracy & ramp_accuracy — Design Spec

**Date:** 2026-05-06
**Status:** Approved (Matt)
**Predecessor:** Envelope multi-stage editor on `main` through `8691a7a`

---

## Motivation

In multiplexed patches (e.g. `Additive3.json` with `count=50`) the sustain phase decorrelates across instances because wandering modulators (RedNoise, etc.) take divergent paths from differently-seeded RNGs. Attack and decay phases, however, are sample-exact across all 50 voices — every clone fires its envelope ramp at the same time with the same shape, producing a phase-coherent transient that doesn't sound very different from a single voice.

Two new floats on the base `Envelope` add per-instance variation to the envelope itself.

## stage_accuracy ∈ [0, 1]

- `1.0` → exact stage durations (current behavior).
- `s`   → each non-expand stage's duration is multiplied by a random factor in `[s, 1]` before the existing min/max clamp. The expand stage absorbs the resulting slack as today.
- `0.0` → durations vary in `[0, 1]× spec`.

Each Envelope instance has its own RNG (seeded via `seed_`), so multiplex clones get different durations.

## ramp_accuracy ∈ [0, 1]

- `1.0` → unmodulated (current behavior).
- `a`   → `next()` returns `cur * (1 + (1 − a) · lfo)`, where `lfo` is a smoothed random signal in `[−1, 1]`.

LFO design: cosine-smoothed segments between random targets. Period base = `totalFrames / 10` (Matt's `freq = 10/note_dur`), with ±50% per-segment jitter (matches `rampVariation = 0.5`). Each Envelope instance has its own LFO RNG (seeded from `seed_ ^ 0xDEADBEEF`).

## Why an inline LFO instead of `RedNoiseSource`

`RedNoiseSource` lives in `engine/include/mforce/source/`. Including it from `engine/include/mforce/core/envelope.h` would invert the layering (core → source). The full RN surface (density, smoothness, rampVariation, boost, zct) is overkill for this use case — we just need a smooth bumpy LFO. ~30 lines inline using `std::mt19937` + cosine interpolation. If broader RN flexibility is wanted later, we revisit.

## Seed flow (multiplex decorrelation)

The Multiplex loader already injects a perturbed `seed` field into every node's params (`patch_loader.cpp:586-590`). For Envelope to participate:

1. **Bare Envelope** — the `stages` branch in `patch_loader.cpp` reads `params.seed` and calls `env->set_seed(...)`.
2. **Preset envelopes** — the registry create lambdas pass through the `seed` arg they already receive: `if (seed) e->set_seed(*seed);`. 6 lambdas to update.

Each instance ends up with a different seed → different stage durations and different LFO signal.

## Preset rebuild

Each preset's `set_config` rebuilds via `*static_cast<Envelope*>(this) = make_xxx(...)`. That slice-assign clobbers `stage_accuracy`, `ramp_accuracy`, and `seed_` back to defaults whenever the user edits e.g. AR's `attack`.

Fix: each preset's `rebuild()` saves these three before the slice-assign and restores after. ~3 lines × 6 presets.

## Public surface

- Two new `ConfigDescriptor` entries (`stage_accuracy`, `ramp_accuracy`) appear on the bare `Envelope` and on all 6 preset envelopes. The UI inspector picks them up automatically via the existing Settings panel; JSON save preserves them automatically via `configValues`.
- Each preset's `set_config` delegates unhandled names to `Envelope::set_config(...)`. Symmetric `get_config` delegation.
- `seed` is **not** a public config (matches every other source that has one — they take seed via the registry create lambda or via params.seed in the loader). Invisible in the inspector; flows through the multiplex perturbation.

## Backward compat

Default values (`1.0`) reproduce today's behavior. Existing patches without these fields render identically (parity-tested on `Additive1.json`).

## Verification

1. Build mforce_cli; render `Additive3.json` (50-voice multiplex) before/after the change with all defaults. Output must be bit-identical (peak / RMS).
2. Set `stage_accuracy = 0.5` on env1 in a copy patch; render → audible attack jitter across instances (softer / wider transient).
3. Set `ramp_accuracy = 0.9` on env1 in a copy patch; render → subtle amplitude wobble in sustain that varies per instance.

## Out of scope

- Stand-alone "VariedValue" / "RNVar" node — see Matt's side question. Legacy doesn't have one (Vibrato is the closest, and it wraps RN for the specific pitch-modulation use case). Recommendation: keep the RN+Var math internal to Envelope; revisit a generic node only if the pattern starts appearing in 3+ patches.
- Per-stage `ramp_accuracy` (i.e. independent modulation per stage). Could be added later by moving the LFO segment-frequency calc per stage; today it's a single LFO across the whole envelope.
