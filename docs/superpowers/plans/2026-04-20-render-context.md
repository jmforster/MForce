# RenderContext Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hardcoded sample rate (and similar ambient render-state) with a `RenderContext` struct propagated through `ValueSource::prepare()`. Fixes the `Partials` 48 kHz baked-in constant and inconsistent `WaveEvolution` SR handling; provides a growth point for future ambient state (offline/realtime flag, block size, master seed, scratch memory).

**Architecture:** New `struct RenderContext { int sampleRate; }` in a dedicated header. `ValueSource::prepare(int frames)` becomes `prepare(const RenderContext& ctx, int frames)`. Every override forwards ctx when it in turn calls prepare on child sources. SR-sensitive nodes (Partials, the affected WaveEvolutions) read `ctx.sampleRate` during prepare and cache what they need. Constructor-time `sampleRate` plumbing stays for nodes that genuinely want pre-prepared state (biquad coefs, ring-buffer sizing) — authoritative value is the one at prepare.

**Tech Stack:** C++17/20, CMake, no test framework (this codebase has none). Validation is build-clean + canonical-patch render + peak/rms comparison against pre-change baseline.

**Scope note:** This is a mechanical refactor across ~57 `prepare` overrides in 38 files and ~214 call sites in 39 files. Expect the compiler to drive most of the work. At 48 kHz the behavior must be bit-identical. At non-48 kHz, Partials and the affected WaveEvolutions will produce *different* output — that's the latent-bug fix we want.

**Not in scope:** Adding `offline`, `blockSize`, `masterSeed`, or other fields to `RenderContext` speculatively. YAGNI — add when a consumer needs it. The only field this plan introduces is `sampleRate`. Also not in scope: deferring constructor-time SR to prepare-time for filter coefficient calculation (separate concern).

---

## File Structure

**New files:**
- `engine/include/mforce/core/render_context.h` — the `RenderContext` struct.

**Modified headers (prepare signature + ctx forwarding):**
- `engine/include/mforce/core/dsp_value_source.h` — base `prepare()` signature.
- `engine/include/mforce/core/dsp_wave_source.h`
- `engine/include/mforce/core/envelope.h`
- `engine/include/mforce/core/multi_source.h`
- `engine/include/mforce/core/var_source.h`, `range_source.h`
- `engine/include/mforce/source/*.h` (all source headers with prepare overrides — full list via grep; ~30 files)
- `engine/include/mforce/source/additive/*.h` (basic/full/additive_source2, formant, partials)
- `engine/include/mforce/filter/filters.h`, `vibrato.h`
- `engine/include/mforce/render/instrument.h`
- `engine/include/mforce/music/pitch_bend.h`

**Modified C++ source files (call sites + implementations):**
- `engine/src/wavetable_source.cpp`
- `engine/src/mixer.cpp`
- `engine/src/red_noise_source.cpp`
- `engine/src/additive_source2.cpp`, `full_additive_source.cpp`
- `engine/src/hybrid_ks_source.cpp`
- `engine/src/patch_loader.cpp` — top-level prepare call(s) from render driver.
- `tools/mforce_cli/main.cpp` — constructs `RenderContext` before render.
- `tools/mforce_ui/main.cpp` — constructs `RenderContext` for preview/render paths.

**SR-specific behavior changes (not just mechanical):**
- `engine/include/mforce/source/additive/partials.h` — replace `constexpr RATE` with `ctx.sampleRate` captured in `partials_prepare`.
- `engine/include/mforce/source/wave_evolution.h` — `set_sample_rate()` virtual deleted; each SR-sensitive subclass reads SR from ctx during prepare via a new `fwd_sample_rate(ctx)` helper on the holder.

---

## Task 1: Introduce `RenderContext` header

**Files:**
- Create: `engine/include/mforce/core/render_context.h`

- [ ] **Step 1: Create the header**

```cpp
#pragma once

namespace mforce {

// Ambient render state propagated top-down through ValueSource::prepare.
// Intentionally minimal; add fields only when a consumer needs one.
struct RenderContext {
    int sampleRate;
};

} // namespace mforce
```

- [ ] **Step 2: Verify it compiles standalone**

Run: `"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine 2>&1 | tail -5`

Expected: PASS (header is unused but should parse).

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/core/render_context.h
git commit -m "feat(core): add RenderContext struct (sampleRate only for now)"
```

---

## Task 2: Change `ValueSource::prepare` signature

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h`

- [ ] **Step 1: Update the base class**

In `dsp_value_source.h`, add `#include "mforce/core/render_context.h"` at the top. Change:

```cpp
virtual void prepare(int frames) {}
```

to:

```cpp
virtual void prepare(const RenderContext& ctx, int frames) { (void)ctx; (void)frames; }
```

- [ ] **Step 2: Build and observe the cascade of errors**

Run: `"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine 2>&1 | tail -30`

Expected: FAIL with many "cannot override" / "no matching override" errors at each `prepare(int frames)` site. This is the worklist for Task 3.

- [ ] **Step 3: Do NOT commit yet**

The tree is in a non-compiling state; Task 3 finishes the sweep before committing.

---

## Task 3: Mechanical sweep — update every override and call site

**Files:**
- Modify: every file listed by the Task 2 build errors (~57 override sites, ~214 call sites). The mechanical rule is identical everywhere.

The mechanical rule has three variants:

**Variant A — override that ignores the new param:**

Before:
```cpp
void prepare(int frames) override {
    // ... body that doesn't call child prepare ...
}
```

After:
```cpp
void prepare(const RenderContext& ctx, int frames) override {
    (void)ctx;
    // ... body unchanged ...
}
```

**Variant B — override that forwards to child `ValueSource::prepare`:**

Before:
```cpp
void prepare(int frames) override {
    freq_->prepare(frames);
    ampl_->prepare(frames);
}
```

After:
```cpp
void prepare(const RenderContext& ctx, int frames) override {
    freq_->prepare(ctx, frames);
    ampl_->prepare(ctx, frames);
}
```

**Variant C — top-level caller (CLI / UI / instrument render loop):**

Before:
```cpp
src->prepare(frames);
```

After:
```cpp
RenderContext ctx{ sampleRate };
src->prepare(ctx, frames);
```

Where `sampleRate` is whatever local variable the caller already has (patch_loader has it; CLI main has it; UI has `DSP_SAMPLE_RATE`).

- [ ] **Step 1: Sweep each file in the Task 2 error list**

Work one file at a time. For every `prepare(int frames)` in the file, apply Variant A or B. For each outer caller, apply Variant C. Do not change behavior.

Files known to need updates (from grep):
- Core: `dsp_wave_source.h`, `envelope.h`, `multi_source.h`, `var_source.h`, `range_source.h`
- Sources: `triangle_source.h`, `sine_source.h` (if present), `saw_source.h`, `fm_source.h`, `pulse_source.h`, `pink_noise_source.h`, `red_noise_source.h` (+.cpp), `noise_sources.h`, `wander_noise_source.h`, `phased_value_source.h`, `repeating_source.h`, `segment_source.h`, `wavetable_source.h` (+ cpp), `hybrid_ks_source.h` (+ cpp), `combined_source.h`, `fitzhugh_nagumo_source.h`, `gray_scott_source.h`, `homotopy_source.h`, `ldpc_source.h`, `markov_ode_source.h`, `mass_spring_source.h`, `micro_nn_source.h`, `sat_dpll_source.h`, `self_avoiding_walk_source.h`, `self_rewriting_ast_source.h`, `sort_oscillator.h`, `wave_evolution.h`
- Additive: `basic_additive_source.h`, `full_additive_source.h` (+ cpp), `additive_source2.h` (+ cpp), `formant.h`, `partials.h`
- Filters: `filters.h`, `vibrato.h`
- Render: `instrument.h`, `mixer.cpp`
- Music: `pitch_bend.h`
- Loader / drivers: `patch_loader.cpp`, `tools/mforce_cli/main.cpp`, `tools/mforce_ui/main.cpp`

- [ ] **Step 2: Build clean**

Run: `"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli mforce_ui 2>&1 | tail -10`

Expected: PASS with no errors. The `(void)ctx;` usage will suppress any "unused param" warnings in Variant A overrides.

- [ ] **Step 3: Regression render (must be bit-identical at 48 kHz)**

Capture baseline hashes BEFORE any change (if you haven't already, do this before starting Task 2):

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/_baseline_pluck.wav
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_baseline_fadd.wav
```

Record the peak/rms lines. They should be:
- pluck_sanity: `peak=0.593099 rms=0.0525044 nonzero=191998/240000`
- fadd_formant_test: `peak=0.179758 rms=0.0151559 nonzero=383958/384000`

After Task 3, re-run the same two commands. Peak/rms must match exactly (to 6 decimals).

Expected: identical numbers. If they differ, something in Variant B was skipped (a child prepare that didn't get ctx forwarded) OR a node silently depended on the old signature somehow.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor(dsp): propagate RenderContext through ValueSource::prepare

Mechanical sweep: all prepare(int) overrides become prepare(const
RenderContext&, int), all call sites pass ctx. Behavior unchanged
at 48 kHz (bit-identical renders for pluck_sanity and
fadd_formant_test). Sets up Partials and WaveEvolution to use
ctx.sampleRate instead of hardcoded or cached-stale values."
```

---

## Task 4: `Partials` uses `ctx.sampleRate`

**Files:**
- Modify: `engine/include/mforce/source/additive/partials.h`

- [ ] **Step 1: Replace the constexpr with a captured member**

In `partials.h`, remove these lines near `CUTOFF`:
```cpp
static constexpr float CUTOFF = 16000.0f;
static constexpr float RATE   = 48000.0f;  // TODO: make configurable via sample rate
```

Replace with:
```cpp
static constexpr float CUTOFF = 16000.0f;  // Nyquist-ish fade threshold; remains fixed.
// RATE is now captured per-prepare from RenderContext.
```

Add a member variable in the protected section (near `rng_`):
```cpp
float rate_{48000.0f};  // captured from ctx.sampleRate at prepare()
```

In `partials_prepare(int frames)` — which becomes `partials_prepare(const RenderContext& ctx, int frames)` in Task 3 — add as the first line:
```cpp
rate_ = float(ctx.sampleRate);
```

Wait — `partials_prepare` is part of the `IPartials` interface, not `ValueSource::prepare`. Check the interface signature. If it's still `partials_prepare(int frames)`, update it too in this task:

- In `IPartials`: change `virtual void partials_prepare(int frames) = 0;` to `virtual void partials_prepare(const RenderContext& ctx, int frames) = 0;`
- In `Partials::partials_prepare`: update signature, add `rate_ = float(ctx.sampleRate);` as first line.
- In `Partials::prepare` (the ValueSource override from Task 3): forward to `partials_prepare(ctx, frames)`.
- In `CompositePartials::partials_prepare`: update signature, forward ctx.
- In `FullAdditiveSource` / `AdditiveSource2` call sites of `partials_prepare`: forward ctx.

In `Partials::get_partial_value`, replace every `RATE` with `rate_`:
```cpp
partialPos_[index] = std::fmod(
    partialPos_[index] + pfreq / rate_ + phaseDiff + (ppo - partialLPO_[index]),
    1.0f);
```

- [ ] **Step 2: Build clean**

Run: `"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli 2>&1 | tail -5`

Expected: PASS.

- [ ] **Step 3: Regression render at 48 kHz**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_t4_fadd.wav
```

Expected: `peak=0.179758 rms=0.0151559 nonzero=383958/384000` — bit-identical to Task 3 baseline.

- [ ] **Step 4: Non-48 kHz smoke test**

Create `patches/fadd_formant_test_96k.json` by copying `patches/fadd_formant_test.json` and changing the top-level `"sampleRate": 48000` to `"sampleRate": 96000`.

Run: `build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test_96k.json renders/_t4_fadd_96k.wav`

Expected: renders successfully (doesn't crash), peak > 0, rms > 0. The numbers will NOT match 48 kHz — that's the latent-bug fix. No listening judgment required at this step; just "does it render sensibly".

Delete the 96k test patch and its render afterwards — not keeping this in the repo.

```bash
rm patches/fadd_formant_test_96k.json renders/_t4_fadd_96k.wav
```

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/source/additive/partials.h
# (and any .cpp files modified in FullAdditiveSource / AdditiveSource2 / CompositePartials)
git commit -m "fix(additive): Partials reads sample rate from RenderContext

Removes the constexpr RATE = 48000 that silently mis-computed
partial phase advance and amplitude scaling at any sample rate
other than 48 kHz. 48k output is bit-identical to pre-change."
```

---

## Task 5: `WaveEvolution` — unified SR handling via RenderContext

**Files:**
- Modify: `engine/include/mforce/source/wave_evolution.h`
- Modify: `engine/src/wavetable_source.cpp`

- [ ] **Step 1: Deprecate `set_sample_rate`**

Currently `WaveEvolution` has:
```cpp
virtual void set_sample_rate(int /*sr*/) {}
```

And `WavetableSource::prepare` calls `evolution_->set_sample_rate(sampleRate_)` (per the Apr 18 edit). Replace with a prepare-time read:

In `wave_evolution.h`, change:
```cpp
virtual void set_sample_rate(int /*sr*/) {}
```
to:
```cpp
// Called by the holder during prepare(). SR-sensitive subclasses override.
virtual void on_prepare(const RenderContext& /*ctx*/) {}
```

For each subclass currently overriding `set_sample_rate` (BowedStringEvolution, BrassEvolution), rename the override to `on_prepare(const RenderContext& ctx)` and use `ctx.sampleRate`:

```cpp
void on_prepare(const RenderContext& ctx) override {
    sampleRate_ = ctx.sampleRate;
}
```

For subclasses that CURRENTLY ignore SR but SHOULD use it (PluckEvolution, AveragingEvolution, EKSEvolution, BlownTubeEvolution, ReedEvolution) — **do not add on_prepare in this task**. Their adjust() computes in terms of frequency; SR dependence is latent and fixing it requires per-subclass analysis. Log a follow-up TODO in `docs/Parked.txt`:

```
WaveEvolution SR dependence audit: PluckEvolution, AveragingEvolution,
EKSEvolution, BlownTubeEvolution, ReedEvolution all cache freq-dependent
state in adjust() without consulting sample rate. At non-48k, behavior
is subtly wrong. Not a blocker; audit per subclass and decide what
scales with SR vs. what stays constant.
```

- [ ] **Step 2: Update `WavetableSource::prepare` to call `on_prepare`**

In `engine/src/wavetable_source.cpp`, replace:
```cpp
if (evolution_) evolution_->set_sample_rate(sampleRate_);
```
with:
```cpp
if (evolution_) evolution_->on_prepare(ctx);
```

(`ctx` is already available in the updated `prepare` signature from Task 3. `sampleRate_` member can stay — it's used elsewhere in WavetableSource — but `on_prepare` now gets its value from ctx directly.)

- [ ] **Step 3: Build clean**

Run: `"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli mforce_ui 2>&1 | tail -10`

Expected: PASS. If there are remaining `set_sample_rate` call sites or overrides, rename them.

- [ ] **Step 4: Regression render at 48 kHz**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/_t5_pluck.wav
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_t5_fadd.wav
```

Expected: bit-identical to Task 4 baseline (peak/rms match).

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/source/wave_evolution.h engine/src/wavetable_source.cpp docs/Parked.txt
git commit -m "refactor(waveguide): WaveEvolution SR via on_prepare(ctx)

Replaces set_sample_rate(int) with on_prepare(const RenderContext&).
BowedString and Brass now read SR through ctx (same behavior at 48k).
Parked a follow-up to audit the evolutions that still cache freq-
dependent state in adjust() without SR awareness."
```

---

## Task 6: Final validation and cleanup

**Files:**
- No code changes. Final smoke test.

- [ ] **Step 1: Clean baseline renders**

Delete scratch baselines:
```bash
rm renders/_baseline_*.wav renders/_t3_*.wav renders/_t4_*.wav renders/_t5_*.wav
```

- [ ] **Step 2: Render a small canonical set at 48 kHz**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/_smoke_pluck.wav 2>&1 | tail -3
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_smoke_fadd.wav 2>&1 | tail -3
```

Expected:
- pluck_sanity: `peak=0.593099 rms=0.0525044 nonzero=191998/240000`
- fadd_formant_test: `peak=0.179758 rms=0.0151559 nonzero=383958/384000`

- [ ] **Step 3: UI smoke test**

Launch `build/tools/mforce_ui/Release/mforce_ui.exe`. Create a trivial patch (Sine → Output), save, reload. Confirm no crash. Close.

- [ ] **Step 4: Clean up scratch renders**

```bash
rm renders/_smoke_pluck.wav renders/_smoke_fadd.wav
```

- [ ] **Step 5: Final commit (if any stray changes)**

```bash
git status
# If nothing, done.
```

---

## Self-review notes

- Every step has exact code or exact commands. No "TBD" or "etc".
- The plan is driven by compiler errors in Task 3 — no need to enumerate all 57 override sites; the build tells you.
- Validation at 48 kHz is bit-identical by construction (old RATE=48000 literal becomes ctx.sampleRate=48000; mechanical refactor elsewhere). At non-48k, behavior changes in Partials — that's the latent-bug fix.
- Deferred work is explicitly parked (5 WaveEvolution subclasses that cache freq-dependent state) rather than silently skipped.
- `on_prepare` name chosen to avoid collision with the inherited `ValueSource::prepare`; WaveEvolution isn't itself a ValueSource.
