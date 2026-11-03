# Part 3, checkpoint 3.3: the shared DSP primitives in `core/dsp` — Implementation Plan

**Goal:** Seven DSP primitives that are written per node today — the band-limited step, the first-order tuning allpass, the DC blocker, the bow friction curve, the biquad section with its coefficient recipes, the one-pole and its time-constant formulas, the Butterworth section — are each written once under `engine/include/mforce/core/dsp/`, merged one at a time with every copy compared first; no render moves.

**Architecture:** Each stateful primitive is a small struct with no virtual functions, held by value inside the node that uses it, with `set…(...)` for its coefficients, `reset()` for its state and `process(x)` for one sample (Part 3 section 4: composition, not inheritance — a KS string *has* a DC blocker). A primitive with no state and no stored coefficient is a function, not a struct (`dsp::blep`, `dsp::bow_friction`): a struct with nothing in it is a namespace. The biquad is two structs — `BiquadCoefficients` (the five numbers, the named recipes, `process(state, x)`, the phase delay) and `BiquadState` (the four memories) — because the mesh runs 128 edge sections that share one coefficient set per sample; `Biquad` is the pair for the one-section case. Everything lives in `namespace mforce::dsp`, one header per primitive, with `constants.h` holding the one float `kPi` the sites' three π literals all round to. `PierceAllpass`, the one primitive that already has its own file, moves into the folder and the namespace. Byte-identity is by construction: every merge keeps the site's arithmetic in the same operations in the same order (the comparison tables in the Ledger say where the copies agree, where they differ and what the node keeps), and every merge is proved three ways — a twin test in `engine_tests` that retypes the deleted lines as a reference and compares the primitive bit for bit over a deterministic signal, the null gate after every task, and the A/B over `patches/library` + `patches/baselines` (plus a synthetic set for the node types the scope does not contain) against the pre-checkpoint binary.

**Tech Stack:** C++20 (MSVC, CMake 3.20+, `/fp:precise` — the default; no fast-math anywhere in the tree), the existing `engine_tests` harness (`CHECK`, `g_checks`), `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/alloc_gate.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/structure/check.py`.

**Spec:** `docs/architecture/part3-nodes.md` section 4 (the table of seven, "small struct with no virtual functions, held by value … `set(...)` for its coefficients and `process(x)` for one sample"; "the copies must be compared before they are merged. Where two copies compute the same formula the merge is exact. Where they differ … the primitive takes the difference as a parameter, so each node keeps its number, and the report says so. No node changes sound in this section") and section 10 row 3.3 ("none; a differing copy becomes a parameter"); `docs/architecture/target-architecture.md` section 3.1 ("New here: one home for the DSP primitives that are currently re-typed per node") and section 2 ("One place a DSP primitive is written"). Matt, 2026-11-02: proceed; easily-tweaked decisions are the session's (CLAUDE.md "Refactor verdicts"); the oboe1 A/B at 0 moved is accepted standing.

## Global Constraints

- **Byte-identity is total.** Part 3 section 10 row 3.3 says "none". `python tools/null_gate_perform_source.py --jobs 3` reports `78/78 manifest entries identical` after EVERY task; the A/B at the close over `renders/scratch/ab31/library_baselines.txt` (132 patches) reports 0 DIFFER; the synthetic set `renders/scratch/ab33/synth.txt` (Task 0) reports 0 DIFFER after every task that touches one of its types. One departure is known and listed before any code moves: EKS's stiffness allpass clears both memories at a note's start where it cleared one (Ledger ruling 8) — invisible in scope (the one EKS patch, `hi_hat1`, has stiffness 0), named in the report's section 1.
- **Same operations, same order.** A merged site computes the same float operations in the same order as the deleted lines. Where a site's expression differs from the primitive's by more than commutation of one product or the sign of a zero, the site is not merged in this checkpoint and the Ledger says why (ruling 10).
- **Measurement scope is `patches/library` + `patches/baselines`** (CLAUDE.md "Measurement scope") plus the synthetic patches Task 0 writes for the node types the scope lacks (ruling 12) — those are this checkpoint's own fixtures, not queue patches.
- **No new file over 600 lines, no new function over 80** (`tools/structure/limits.json`); test functions included — one twin test function per primitive, split at 80. `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report; `duplicate_blocks` should fall, `include_violations` and `unmapped_files` must not rise (`core/dsp/` is under the `core` module's path; no map change).
- **No dated changelog, backlog id, REVIEW, Part or checkpoint number in code comments.**
- **Commit after every task; `python tools/gates.py --fast` before every commit** (ctest, the stamp, the roundtrip smoke with its known wiring diff, the allocation gate). The UI is relinked before a stamp run whenever a header the UI compiles has changed (every task here touches a node header the UI includes).
- **Work on main, no worktree.** The in-tree build is `build/`.
- **The A/B base** is `renders/scratch/ab_tree/cli_pre33.exe`: Task 0 copies the current build's CLI (its engine is the tree at `d5ebbf3`, 3.2's seal) before any code moves and renders the old arm into `renders/scratch/ab33/old` and `renders/scratch/ab33/synth_old`.
- **Edits go through script files**, never heredocs with backslashes (memory `feedback_bash_heredoc_limits`).
- **Dates:** `date` first (the machine is HST); this plan is dated 2026-11-02.

## Review Focus

The five places a byte could move that no stored patch exercises, each pinned by the task named.

1. **The sign of a zero product** where the primitive's general form adds a term the deleted line did not have: `BiquadSource`'s resonance mode (`b0·x − a1·y1 − a2·y2`, no input history) becomes `BiquadCoefficients::process` with `b1 = b2 = 0`, whose `+ 0·x1 + 0·x2` differs from the old line only when `b0·x` is a negative zero. Task 6's twin test compares the two over a signal that contains negative zeros and asserts equality of value (±0 equal) and bit-equality everywhere else; the eight resonance-mode Biquad nodes in scope render byte-identical.
2. **EKS's stiffness allpass memory at a note's start** (ruling 8): the old code cleared `y1` only; `TuningAllpass::reset()` clears both. Task 3's twin test shows the one-sample difference with stiffness > 0 and the equality with stiffness = 0; the A/B over `hi_hat1` is 0 moved.
3. **The Butterworth section's operation order** (`gain·x` first, the accumulator started at `0.0f`, the three products summed in index order, then the two feedback products): Task 7 keeps the deleted FIR/IIR loops' order verbatim and the twin test compares bits over white noise for both kinds and several cutoffs; the two `BWLowpass` and `BWBandpass` sites in scope render byte-identical; `BWHighpass` is in the synthetic set.
4. **A setting read per sample versus set once**: the resonator's `stiffness` and `damping`, KSString's `brightness`, the bowed string's `brightness` are read live. The merged sites keep the live read — `OnePole::process(x, coefficient)` takes the coefficient per call, and the resonator calls `set_allpass_double_pole(stiffness.value)` per sample as the old line computed `a·a` and `2·a` per sample (ruling 6) — so no behaviour question arises; Task 4 and Task 6 pin the per-call forms by test.
5. **The twin tests bite.** Each twin test is mutation-tested at the task's close: one sign or one memory assignment in the primitive flipped, the test must fail naming the sample; restored, ALL PASS. The Ledger records each mutation and its failure line.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/core/dsp/constants.h` (new, Task 0) | `inline constexpr float kPi` — the one float the sites' three literals (`3.14159265f`, `3.1415926536f`, `3.14159265358979323846f`) round to; the Butterworth's double π is its own (ruling 14). |
| `engine/include/mforce/core/dsp/pierce_allpass.h` (moved, Task 0) | `dsp::PierceAllpass`, unchanged but for the namespace. |
| `engine/include/mforce/core/dsp/blep.h` (new, Task 1) | `float blep(float pos, float phaseIncr)`: the polynomial band-limited step correction, 0 outside the two windows. |
| `engine/include/mforce/core/dsp/dc_blocker.h` (new, Task 2) | `struct DcBlocker { set(cornerHz, sampleRate); reset(); process(x); }` — `y = r·y1 + x − x1`, `r = 1 − 2π·corner/rate`. |
| `engine/include/mforce/core/dsp/tuning_allpass.h` (new, Task 3) | `struct TuningAllpass { set_fraction(frac); set_coefficient(c); coefficient(); reset(); process(x); }` — `y = c·x + x1 − c·y1`. |
| `engine/include/mforce/core/dsp/one_pole.h` (new, Task 4) | `struct OnePole { reset(); process(x, a); value(); }` — `y += a·(x − y)`; `pole_for_samples(n)`, `pole_for_seconds(seconds, rate)` (= `exp(−1/n)`), `pole_for_cutoff_hz(hz, rate)` (= `exp(−2π·hz/rate)`). |
| `engine/include/mforce/core/dsp/bow_friction.h` (new, Task 5) | `float bow_friction(dv, slope, offset, minOut, maxOut)`: the Friedlander/Smith reflection coefficient `clamp((|(dv+offset)·slope| + 0.75)^−4, min, max)`. |
| `engine/include/mforce/core/dsp/biquad.h` (new, Task 6) | `struct BiquadState { reset(); }`; `struct BiquadCoefficients { set_raw(b0,b1,b2,a1,a2); set_resonance(hz, radius, rate, gain); set_resonant_lowpass(omega, q); set_allpass_double_pole(a); set_allpass_chafe(fc, R, rate); process(state, x); phase_delay_at(hz, rate); b0()…a2(); }`; `struct Biquad { coefficients; state; process(x); reset(); }`. |
| `engine/include/mforce/core/dsp/butterworth_section.h` (new, Task 7) | `struct ButterworthSection { enum class Kind { Lowpass, Highpass }; ButterworthSection(kind, k, n, rate); update(cutoff); process(x); reset(); }`; `build_butterworth_sections(vector&, kind, count, rate)`. |
| `source/oscillator/saw_source.h`, `pulse_source.h` (modify, Task 1) | `wave()` calls `dsp::blep`; `PulseSource::get_blep` gone. |
| `source/physical/allpass_resonator.h` (modify, Tasks 2, 3, 4, 6) | `dsp::DcBlocker dc_`, `dsp::TuningAllpass tuning_`, `dsp::OnePole damping_`, `dsp::Biquad stiffness_`; `BiquadState`, `dcX_/dcY_/dcR_`, `tuneA_/tuneX_/tuneY_`, `lp_`, `bq_` gone; `ap1_phase_delay` stays (a note-init formula, not a per-sample section). |
| `source/physical/ks_string.h` (modify, Tasks 2, 4, 5, 6) | `dsp::DcBlocker dc_, bowDc_`; `Comb::lp` a `dsp::OnePole`, `Comb::bq[2]` two `dsp::Biquad`s set at note init; `hpR_` from `pole_for_cutoff_hz`; the bow junction calls `dsp::bow_friction`; `biquad_ap`, `BiquadState`, `dcR_` gone. The inharmonic highpass, the nested allpasses and the allpass fractional read stay as they are (ruling 10). |
| `source/wavetable/wavetable_source.h`, `src/wavetable_source.cpp` (modify, Task 3) | `dsp::TuningAllpass tuning_`; `tuningCoeff_/tuningState_/tuningPrevIn_` gone; the window and the zero-coefficient bypass stay the node's. |
| `source/wavetable/wave_evolution.h` (modify, Tasks 3, 4, 5, 6) | EKS: `dsp::TuningAllpass stiffness_`; bowed string: two `dsp::OnePole`s and `dsp::bow_friction`; brass: `dsp::Biquad lip_` with `set_resonant_lowpass`. |
| `source/filter/biquad_source.h` (modify, Task 6) | `dsp::Biquad section_`; raw mode `set_raw` from the settings, resonance mode `set_resonance` (the derived `a1`/`a2` still written into the settings — CLEANUP P1); `phase_delay_at` delegates to the coefficients, the cache stays. |
| `source/physical/mesh2d_source.h` (modify, Tasks 0, 6) | `dsp::PierceAllpass` by its new path; `dsp::BiquadCoefficients edge_` (mode 1, set per sample) over `dsp::BiquadState fxAp_[64], fyAp_[64]`; `allpass()` and `a1_/a2_` gone. |
| `source/physical/pierce_filter.h` (modify, Task 0) | the include and the namespace. |
| `source/filter/hammer_bank.h`, `limiter.h` (modify, Task 4) | `qDecayMul_` and `releaseCoef` from `pole_for_seconds` / `pole_for_samples`. |
| `render/perform_source.h`, `render/instrument.h` (modify, Task 4) | `smoothA_ = 1 − pole_for_seconds(0.010f, rate)`; `ringDecay = pole_for_seconds(0.05f, rate)` (twice). |
| `source/filter/filters.h` (modify, Task 7) | `FIRFilter`, `IIRFilter`, `BWSection`, `BWLPSection`, `BWHPSection`, `build_bw_sections` gone; the three filters hold `std::vector<dsp::ButterworthSection>` built with a kind. |
| `source/oscillator/fm_source.h` (modify, Task 7) | `decimSections_` is `std::vector<dsp::ButterworthSection>` of kind Lowpass. |
| `tools/engine_tests/main.cpp` (modify, every task) | `run_dsp_<primitive>_tests` per primitive (the twin tests), `same_bits`/`same_value` helpers and the deterministic signal in Task 0; the PierceAllpass tests follow the namespace. |
| `renders/scratch/ab33/` (Task 0, gitignored) | `cli_pre33.exe` under `ab_tree/`; `synth/*.json` and `synth.txt`; `old`, `synth_old`, and per-task `synth_t<N>` arms. |
| `docs/architecture/part3-nodes.md`, `target-architecture.md` (modify, Task 8) | section 4 gains the landed line and the list of what each site keeps; section 10 row 3.3's result; the target's 3.1 landed line. |

---

### Task 0: Base, old arm, the folder, the twin-test scaffolding

**Files:**
- Create: `engine/include/mforce/core/dsp/constants.h`
- Move: `engine/include/mforce/core/pierce_allpass.h` → `engine/include/mforce/core/dsp/pierce_allpass.h` (`git mv`), namespace `mforce::dsp`
- Modify: `engine/include/mforce/source/physical/mesh2d_source.h` (include, `dsp::PierceAllpass`), `engine/include/mforce/source/physical/pierce_filter.h` (same), `tools/engine_tests/main.cpp` (the Pierce tests' names; the new helpers)
- Create: `renders/scratch/ab33/synth/*.json`, `renders/scratch/ab33/synth.txt`

**Interfaces:**
- Produces:
  ```cpp
  // core/dsp/constants.h
  namespace mforce::dsp { inline constexpr float kPi = 3.14159265358979323846f; }
  // engine_tests (file-local)
  static bool same_bits(float a, float b);                 // std::bit_cast<uint32_t> equality
  static bool same_value(float a, float b);                // same_bits, or both zero of either sign
  static std::vector<float> twin_signal(uint32_t seed, int n);   // Randomizer::valuePN() per sample, a few exact zeros and negative zeros salted in at fixed indices
  ```
- The synthetic set (one short note each, 48 kHz, the shape of `patches/baselines/<a physical baseline>.json`): `allpass_resonator.json` (an envelope into `AllpassResonator`, stiffness 0.15, tension 0.35), `mesh2d_stk.json`, `mesh2d_chafe.json`, `mesh2d_pierce.json` (an envelope impulse into `Mesh2D`, edgeMode 0, 1, 2), `brass_evolution.json` (`WavetableSource` + `BrassEvolution` with a breath envelope and lipQ 2), `bw_highpass.json` (white noise into `BWHighpassFilter`), `ks_string_bow.json` (`KSString` by its real name, `bow` wired to an envelope, `frictionGain` 4), `biquad_raw.json` and `biquad_resonance.json` (white noise into `Biquad`, mode 0 with a fitted section, mode 1 at 440 Hz radius 0.997).

- [ ] **Step 0: Date, base, old arms** — `date`; `git rev-parse --short HEAD`; `python tools/gates.py --fast` (the stamp says the build is current at `d5ebbf3`'s engine; rebuild if not); copy `build/tools/mforce_cli/Release/mforce_cli.exe` to `renders/scratch/ab_tree/cli_pre33.exe`; write the nine synthetic patches and `synth.txt`; `python tools/ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab33 old --cli renders/scratch/ab_tree/cli_pre33.exe` (132/132) and `… render renders/scratch/ab33/synth.txt renders/scratch/ab33 synth_old --cli …` (9/9, every patch renders: a synthetic patch that fails to render is fixed before the old arm is kept).
- [ ] **Step 1: The folder and the move** — `constants.h`; `git mv` the Pierce header; `namespace mforce::dsp`; the two node headers and the tests say `dsp::PierceAllpass`; a script rewrites the three include lines.
- [ ] **Step 2: The helpers, with a test that uses them** — `same_bits`, `same_value`, `twin_signal` and `run_dsp_scaffold_tests` (asserts `same_value(-0.0f, 0.0f)`, `!same_bits(-0.0f, 0.0f)`, the signal's salted zeros at their indices, and `dsp::kPi` bit-equal to each of the three literals).
- [ ] **Step 3: Build everything and run the gates** — `cmake --build build --config Release`; `engine_tests` ALL PASS; null gate `78/78`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: the core/dsp folder; kPi; PierceAllpass moves in; the twin-test scaffolding"`.

---

### Task 1: `blep`

**Files:**
- Create: `engine/include/mforce/core/dsp/blep.h`
- Modify: `engine/include/mforce/source/oscillator/saw_source.h`, `engine/include/mforce/source/oscillator/pulse_source.h`
- Test: `run_dsp_blep_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
// The polynomial band-limited step: the correction added at a discontinuity of a
// phase-accumulated waveform, non-zero only within one phase increment of 0 and 1.
inline float blep(float pos, float phaseIncr) {
  if (pos < phaseIncr) { const float t = pos / phaseIncr; return t + t - t * t - 1.0f; }
  if (pos > 1.0f - phaseIncr) { const float t = (pos - 1.0f) / phaseIncr; return t * t + t + t + 1.0f; }
  return 0.0f;
}
}
```
**Comparison (Ledger table 1):** saw's inline body and pulse's `get_blep` compute the same two polynomials in the same operations; saw's `blep = 0.0f` outside the windows and `val - blep` equal `val - dsp::blep(...)` with `blep = 0.0f` returned. Exact.

- [ ] **Step 1: The twin test (RED)** — the reference lambda is pulse's `get_blep` retyped; over `twin_signal` mapped into [0,1) as positions and five increments (1e-4, 0.01, 0.1, 0.45, 0.5), `CHECK(same_bits(ref(pos, inc), dsp::blep(pos, inc)))` per sample; a `SawSource` and a `PulseSource` prepared at 48 kHz and rendered 4,800 samples at 440 Hz against a copy of their old `wave()` retyped as a lambda over `currPos_`/`currPhaseIncr_` (the test drives the lambda with the same accumulator arithmetic as `WaveSource::compute`). RED: `dsp::blep` undeclared.
- [ ] **Step 2: The header; the two sites** — saw: `return std::clamp(val - dsp::blep(currPos_, currPhaseIncr_), -1.0f, 1.0f);`; pulse: `val + dsp::blep(currPos_, currPhaseIncr_) - dsp::blep(std::fmod(currPos_ + (1.0f - duty), 1.0f), currPhaseIncr_)`; `get_blep` deleted.
- [ ] **Step 3: GREEN, mutation, gates** — `engine_tests` ALL PASS; flip `t + t - t * t - 1.0f` to `t + t - t * t + 1.0f`, the twin test fails naming the sample, restore; null gate `78/78`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: blep written once; saw and pulse call it"`.

---

### Task 2: `DcBlocker`

**Files:**
- Create: `engine/include/mforce/core/dsp/dc_blocker.h`
- Modify: `engine/include/mforce/source/physical/allpass_resonator.h`, `engine/include/mforce/source/physical/ks_string.h`
- Test: `run_dsp_dc_blocker_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
struct DcBlocker {
  void set(float cornerHz, float sampleRate) { r_ = 1.0f - 2.0f * kPi * cornerHz / sampleRate; }
  void reset() { x1_ = y1_ = 0.0f; }
  float process(float x) { const float y = r_ * y1_ + x - x1_; x1_ = x; y1_ = y; return y; }
private: float r_{0.0f}, x1_{0.0f}, y1_{0.0f};
};
}
```
**Comparison (Ledger table 2):** the resonator's excitation blocker, KSString's excitation blocker and KSString's bow blocker all compute `r·y1 + x − x1` in that order with `r = 1 − 2·π·20/sr` (`3.14159265f`, which is `kPi`). Exact. The corner stays each node's constant (`kDcCornerHz = 20.0f`), passed to `set` at note init where `dcR_` was computed. KSString's inharmonic highpass (`hp = R·(y1 + x − x1)`) is a different form and is not merged (ruling 10).

- [ ] **Step 1: The twin test (RED)** — reference: `float r = 1.0f - 2.0f * 3.14159265f * 20.0f / 48000.0f; x = r*y1 + exc - x1; x1 = exc; y1 = x;` retyped; `same_bits` over `twin_signal`; a second run at 44100 and 96000. RED: `dsp::DcBlocker` undeclared.
- [ ] **Step 2: The header; the three sites** — the resonator: `dc_.set(kDcCornerHz, sr)` in `init_note`, `dc_.reset()` in `on_start`, `float x = dc_.process(exc);`; KSString: `dc_` and `bowDc_` likewise (`fb = bowDc_.process(f)`); the five members gone.
- [ ] **Step 3: GREEN, mutation, gates** — mutation: `r_ * y1_ + x - x1_` → `r_ * y1_ + x + x1_`; null gate; synth A/B for `allpass_resonator` and `ks_string_bow` (`ab_render_set.py render renders/scratch/ab33/synth.txt renders/scratch/ab33 synth_t2`, `compare … synth_old synth_t2` → 0 DIFFER); `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: DcBlocker written once; the resonator and KSString hold one each (KSString two)"`.

---

### Task 3: `TuningAllpass`

**Files:**
- Create: `engine/include/mforce/core/dsp/tuning_allpass.h`
- Modify: `engine/include/mforce/source/wavetable/wavetable_source.h`, `engine/src/wavetable_source.cpp`, `engine/include/mforce/source/physical/allpass_resonator.h`, `engine/include/mforce/source/wavetable/wave_evolution.h` (EKS)
- Test: `run_dsp_tuning_allpass_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
// First-order allpass y = c·x + x1 − c·y1: a fractional delay of (1−c)/(1+c)
// samples at low frequency (Thiran, first order).
struct TuningAllpass {
  void set_fraction(float frac) { c_ = (1.0f - frac) / (1.0f + frac); }
  void set_coefficient(float c) { c_ = c; }
  float coefficient() const { return c_; }
  void reset() { x1_ = y1_ = 0.0f; }
  float process(float x) { const float y = c_ * x + x1_ - c_ * y1_; x1_ = x; y1_ = y; return y; }
private: float c_{0.0f}, x1_{0.0f}, y1_{0.0f};
};
}
```
**Comparison (Ledger table 3):** the wavetable's two sites (`compute_raw`, `compute_interpolated`), the resonator and EKS all compute `c·x + x1 − c·y1` in that order. The wavetable sets `c` only when `0.001 < frac < 0.999` (else 0) and skips the section when `c == 0`; the resonator sets `c` from the fraction unconditionally and always runs the section (at `c == 0` it is a unit delay); EKS sets `c` from its stiffness model and skips the section when `c == 0`. The window and the bypass are the nodes' and stay there; the primitive computes. EKS's `adjust()` cleared `y1` only — the primitive's `reset()` clears both (ruling 8).

- [ ] **Step 1: The twin test (RED)** — reference `y = C*x + prevIn - C*state; prevIn = x; state = y;` for `C` in {0.2, 0.5, 0.9, 0.0}; `set_fraction` against `(1.0f - frac) / (1.0f + frac)` for twenty fractions; a `WavetableSource` at 48 kHz and 441 Hz (frac 0.84…) rendered 4,800 samples against a copy of its old tuning lines driven on the same read-head output; the EKS case: two runs of the primitive across a "note boundary" — one with `reset()`, one with only `y1` cleared — equal when `c == 0` for the whole run, differing in exactly one sample when `c > 0` (Review Focus 2). RED: `dsp::TuningAllpass` undeclared.
- [ ] **Step 2: The header; the four sites** — wavetable: `tuning_.set_fraction(frac)` or `tuning_.set_coefficient(0.0f)` under its window, `tuning_.reset()`, `if (tuning_.coefficient() != 0.0f) out = tuning_.process(out);` at both sites (the duplicated block shrinks to one line each; folding the two `compute_*` first-sample blocks into one function is 3.6's, not this task's); the resonator: `tuning_.set_fraction(frac)` in `init_note`, `reset()` in `on_start`, `float yt = tuning_.process(y);`; EKS: `stiffness_.set_coefficient(stiffnessCoeff_)` and `stiffness_.reset()` in `adjust`, `if (stiffness_.coefficient() > 0.0f) avg = stiffness_.process(avg);`.
- [ ] **Step 3: GREEN, mutation, gates** — mutation: `c_ * x + x1_ - c_ * y1_` → `c_ * x + x1_ + c_ * y1_`; null gate `78/78` (eleven wavetables and `hi_hat1` in scope); synth A/B `allpass_resonator`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: TuningAllpass written once; the wavetable, the resonator and EKS hold one"`.

---

### Task 4: `OnePole` and the pole formulas

**Files:**
- Create: `engine/include/mforce/core/dsp/one_pole.h`
- Modify: `engine/include/mforce/source/physical/ks_string.h` (`Comb::lp`, `hpR_`), `engine/include/mforce/source/physical/allpass_resonator.h` (`lp_`), `engine/include/mforce/source/wavetable/wave_evolution.h` (bowed string's two), `engine/include/mforce/source/filter/hammer_bank.h`, `engine/include/mforce/source/filter/limiter.h`, `engine/include/mforce/render/perform_source.h`, `engine/include/mforce/render/instrument.h`
- Test: `run_dsp_one_pole_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
// The leaky integrator y += a·(x − y): a one-pole lowpass whose coefficient the
// caller supplies per call, because every site reads a live setting.
struct OnePole {
  void reset() { y_ = 0.0f; }
  float process(float x, float a) { y_ += a * (x - y_); return y_; }
  float value() const { return y_; }
private: float y_{0.0f};
};
inline float pole_for_samples(float samples) { return std::exp(-1.0f / samples); }      // the per-sample multiplier that decays to e⁻¹ in `samples`
inline float pole_for_seconds(float seconds, float sampleRate) { return std::exp(-1.0f / (seconds * sampleRate)); }
inline float pole_for_cutoff_hz(float hz, float sampleRate) { return std::exp(-2.0f * kPi * hz / sampleRate); }
}
```
**Comparison (Ledger table 4):** KSString's comb damping (`c.lp += brightness·(v − c.lp)`), the resonator's damping (`lp_ += damping·(ys − lp_)`) and the bowed string's two line filters (`bridgeLp_ += brightness·(out − bridgeLp_)`) are the one form `y += a·(x − y)`; the Reed and Brass loops (`c·x + (1−c)·y`), the SVF's 1P modes and the mesh's STK edge one-pole (`b0·x + p·y1`) are other forms and are not merged (ruling 10). The time-constant formula `exp(−1/(τ·sr))` is written in `HammerBank` (`resDecay·sr`), `Limiter` (`max(1, relSec·sr)` — the guard is the limiter's, applied to the sample count), `PerformSource` (`1 − exp(…)`) and `PitchedInstrument` (twice, `0.05·sr`); `pole_for_seconds` computes `seconds * sampleRate` then `-1.0f / (…)` as every site does. KSString's `hpR_ = exp(−2π·inharmHp/sr)` is `pole_for_cutoff_hz` (`-2.0f * kPi * hz / sr` in the site's order). The scalar decays those sites apply (`qDelta_ *= mul`, `peak_ * coef`, `gain_ + (target − gain_)·(1 − coef)`) are not signal filters and stay as written.

- [ ] **Step 1: The twin test (RED)** — reference `lp += c*(x - lp)` over `twin_signal` for `c` in {0.05, 0.6, 1.0}; `pole_for_seconds(0.05f, 48000.0f)` bit-equal to `std::exp(-1.0f / (0.05f * 48000.0f))`; `pole_for_samples(std::max(1.0f, 0.2f * 48000.0f))` bit-equal to the limiter's line; `pole_for_cutoff_hz(150.0f, 48000.0f)` bit-equal to `std::exp(-2.0f * 3.14159265f * 150.0f / 48000.0f)`. RED.
- [ ] **Step 2: The header; the nine sites** — each site keeps its operand order (`c.lp.process(v, brightness.value)`; the resonator `damping_.process(ys, damping.value)` then `fbSample_ = feedback.value * damping_.value()`; the bowed string `outBridge = bridgeLp_.process(outBridge, brightness.value)`); `reset()` where the members were zeroed.
- [ ] **Step 3: GREEN, mutation, gates** — mutation: `y_ += a * (x - y_)` → `y_ += a * (x + y_)`; null gate (oboe1 and the whole physical family in scope); synth A/B `allpass_resonator`, `ks_string_bow`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: OnePole and the pole formulas written once; nine sites call them"`.

---

### Task 5: `bow_friction`

**Files:**
- Create: `engine/include/mforce/core/dsp/bow_friction.h`
- Modify: `engine/include/mforce/source/physical/bow_table_source.h`, `engine/include/mforce/source/physical/ks_string.h`, `engine/include/mforce/source/wavetable/wave_evolution.h` (bowed string)
- Test: `run_dsp_bow_friction_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
// Smith's memoryless bow friction curve (Friedlander; STK BowTable): the fraction of
// the differential velocity dv (bow minus string) the bow hands back to the string.
inline float bow_friction(float dv, float slope, float offset, float minOut, float maxOut) {
  const float s = std::fabs((dv + offset) * slope) + 0.75f;
  return std::clamp(std::pow(s, -4.0f), minOut, maxOut);
}
}
```
**Comparison (Ledger table 5):** `BowTableSource` computes exactly this with its pins and settings. KSString computes `|gain·vRel| + 0.75`, `pow(·, −4)`, `if > 0.98 → 0.98`, no lower bound; the bowed string the same with `else if < 0 → 0`. `(vRel + 0.0f) * gain` equals `gain * vRel` bit for bit (`x + 0.0f` is `x` for every non-negative-zero `x`, and the absolute value erases the sign of a zero; a product commutes), `pow` of a positive number is never negative so `minOut = 0` is the absent lower bound, and `std::clamp(rc, 0, 0.98)` is `if (rc > 0.98) rc = 0.98`. Exact. The nodes multiply `dv * rc` themselves in their own order (`fricCoef * vRel * pressure * bowGain` — the product commutes bit for bit, and the test checks it).

- [ ] **Step 1: The twin test (RED)** — three references retyped (the table's, KSString's, the bowed string's) over `twin_signal` scaled to ±3 for `slope` in {0.05, 3, 4, 20} and `offset` in {0, 0.3}; `same_bits` per sample, and `same_bits(fric * v, v * fric)`. RED.
- [ ] **Step 2: The header; the three sites** — `rc = dsp::bow_friction(dv, slope.current(), offset.current(), minOutput.value, maxOutput.value)`; KSString `float fricCoef = dsp::bow_friction(vRel, frictionGain.value, 0.0f, 0.0f, 0.98f);`; the bowed string likewise with `gain`.
- [ ] **Step 3: GREEN, mutation, gates** — mutation: `+ 0.75f` → `+ 0.7f`; null gate (`bow_evolution_discovery` and two BowTables in scope); synth A/B `ks_string_bow`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: bow_friction written once; the bow table, KSString and the bowed string call it"`.

---

### Task 6: `Biquad` with named recipes

**Files:**
- Create: `engine/include/mforce/core/dsp/biquad.h`
- Modify: `engine/include/mforce/source/physical/ks_string.h` (`Comb::bq`), `engine/include/mforce/source/physical/allpass_resonator.h` (stiffness), `engine/include/mforce/source/wavetable/wave_evolution.h` (brass lip), `engine/include/mforce/source/filter/biquad_source.h`, `engine/include/mforce/source/physical/mesh2d_source.h` (mode 1 edges)
- Test: `run_dsp_biquad_tests`, `run_dsp_biquad_recipe_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
struct BiquadState { float x1{0}, x2{0}, y1{0}, y2{0}; void reset() { x1 = x2 = y1 = y2 = 0.0f; } };
// Direct Form I, a0 normalised: y = b0·x + b1·x1 + b2·x2 − a1·y1 − a2·y2.
struct BiquadCoefficients {
  void set_raw(float b0, float b1, float b2, float a1, float a2);
  void set_resonance(float hz, float radius, float sampleRate, float gain);   // STK BiQuad::setResonance: a1 = −2 r cos(2π f/sr), a2 = r², b0 = gain, b1 = b2 = 0
  void set_resonant_lowpass(float omega, float q);                            // RBJ lowpass at `omega` radians per sample, peak-normalised to unity (the brass lip)
  void set_allpass_double_pole(float a);                                      // (a² − 2a z⁻¹ + z⁻²)/(1 − 2a z⁻¹ + a² z⁻²): b0 = a², b1 = −2a, b2 = 1, a1 = −2a, a2 = a²
  void set_allpass_chafe(float fc, float R, float sampleRate);                // (a2 + a1 z⁻¹ + z⁻²)/(1 + a1 z⁻¹ + a2 z⁻²): a1 = −2R cos(2π fc/sr), a2 = R²
  float process(BiquadState& s, float x) const;
  float phase_delay_at(float hz, float sampleRate) const;                     // closed form, polarity folded (from BiquadSource)
  float b0() const; float b1() const; float b2() const; float a1() const; float a2() const;
private: float b0_{1}, b1_{0}, b2_{0}, a1_{0}, a2_{0};
};
struct Biquad { BiquadCoefficients coefficients; BiquadState state; float process(float x) { return coefficients.process(state, x); } void reset() { state.reset(); } };
}
```
**Comparison (Ledger table 6):** every site's per-sample line is `((((b0·x + b1·x1) + b2·x2) − a1·y1) − a2·y2)` with the memories shifted `x2 = x1; x1 = x; y2 = y1; y1 = y` after — brass and `BiquadSource`'s raw mode literally; KSString's and the resonator's double-real-pole allpass with `b0 = a·a`, `b1 = −2·a`, `b2 = 1`, `a1 = −2·a`, `a2 = a·a` (`− (2a)·x1` is `+ (−2a)·x1`, `+ x2` is `+ 1·x2`, `+ (2a)·y1` is `− (−2a)·y1`: each a negation or a multiplication by one, exact); the mesh's Chafe allpass with `b0 = a2, b1 = a1, b2 = 1` over its four-float state in the same slot order. `BiquadSource`'s resonance mode is the one site whose line is shorter (`b0·x − a1·y1 − a2·y2`, no input memory): with `b1 = b2 = 0` the general form adds `0·x1 + 0·x2`, which differs only in the sign of a zero (Review Focus 1); the node still writes the derived `a1`/`a2` into its settings (CLEANUP P1). The resonator computes `a·a` and `2·a` per sample from the live setting; it calls `set_allpass_double_pole(stiffness.value)` per sample (ruling 6). KSString's `dispEff_` is per note; its two sections per comb are set in `init_note`. The additive noise bed (transposed direct form II) is not merged (ruling 10).

- [ ] **Step 1: The twin tests (RED)** — `run_dsp_biquad_tests`: the raw DF1 line retyped against `process` over `twin_signal` for three coefficient sets; the double-real-pole line (`a*a*x - 2.0f*a*x1 + x2 + 2.0f*a*y1 - a*a*y2`) against `set_allpass_double_pole(a)` + `process` for `a` in {0.12, 0.5, 0.95}; the Chafe line against `set_allpass_chafe`; the resonance line (`b0*x - a1*y1 - a2*y2`, memories `y` only) against `set_resonance` + `process` with `same_value` per sample and `same_bits` where `b0*x` is not a negative zero. `run_dsp_biquad_recipe_tests`: the brass coefficient block retyped (omega, q → b0..a2) bit-equal to `set_resonant_lowpass`; `set_resonance(440, 0.997, 48000, 1)`'s `a1` bit-equal to `-2.0f * 0.997f * std::cos(2.0f * 3.14159265358979323846f * 440.0f / 48000.0f)`; `phase_delay_at` bit-equal to `BiquadSource::phase_delay_at` on a node with the same settings. RED.
- [ ] **Step 2: The header; the five sites** — KSString: `Comb::bq` is `dsp::Biquad[2]`, `init_note` sets both from `dispEff_`, the loop `v = c.bq[0].process(v); v = c.bq[1].process(v);`, `biquad_ap` and `BiquadState` gone; the resonator: `stiffness_.coefficients.set_allpass_double_pole(stiffness.value); ys = stiffness_.process(yt);` under its `> 0.001f` guard; brass: `lip_.coefficients.set_resonant_lowpass(omega, q)` after its clamps, `values[index] = lip_.process(x)`; `BiquadSource`: raw `section_.coefficients.set_raw(b0.value, …)` when a setting changes (`on_setting_changed`) and at prepare, resonance `set_resonance(f, r, sr, b0.value)` under the existing change check with `a1.value = section_.coefficients.a1(); a2.value = …;`, `phase_delay_at` delegates; mesh: `edge_.set_allpass_chafe(fc, R, sr_)` where `a1_`/`a2_` were computed, `edge_.process(fxAp_[i], gain_ * x)`, `fxAp_`/`fyAp_` are `dsp::BiquadState[kMaxN]`, `clear_mesh` resets them.
- [ ] **Step 3: GREEN, mutation, gates** — mutation: `- a2_ * s.y2` → `+ a2_ * s.y2`; null gate (sixteen Biquads, four KSPianoStrings in scope); synth A/B `allpass_resonator`, `ks_string_bow`, the three `mesh2d_*`, `brass_evolution`, `biquad_raw`, `biquad_resonance`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: Biquad with named recipes written once; KSString, the resonator, the brass lip, Biquad and the mesh edges use it"`.

---

### Task 7: `ButterworthSection`

**Files:**
- Create: `engine/include/mforce/core/dsp/butterworth_section.h`
- Modify: `engine/include/mforce/source/filter/filters.h`, `engine/include/mforce/source/oscillator/fm_source.h`
- Test: `run_dsp_butterworth_tests`

**Interfaces:**
```cpp
namespace mforce::dsp {
// One second-order Butterworth section (bilinear design), lowpass or highpass by
// a parameter; k of n sections of a cascade sets its damping zeta.
struct ButterworthSection {
  enum class Kind { Lowpass, Highpass };
  ButterworthSection(Kind kind, float k, float n, float sampleRate);
  void update(float cutoff);     // the kind's coefficient formula, verbatim from today's two
  float process(float x);        // gain·x first; the accumulator starts at 0.0f; x0, x1, x2 then y1, y2 in that order
  void reset();
private: Kind kind_; float rate_, zeta_, a_[3]{}, b_[2]{}, gain_{0}, x_[3]{}, y_[2]{};
};
void build_butterworth_sections(std::vector<ButterworthSection>& sections, ButterworthSection::Kind kind, int count, float rate);
}
```
**Comparison (Ledger table 7):** today's `BWLPSection` and `BWHPSection` share `BWSection`'s state and `process` and differ only in `update(cutoff)`; the kind becomes a constructor parameter and `update` a two-branch function holding both formulas verbatim (double arithmetic, the fifteen-digit π literal kept as the file's own `kPiDesign` — ruling 14). `process` was `iir.process(fir.process(gain*input, a, 3), b, 2)` over two `std::vector`s: the FIR shifted its history then summed `0.0f + a0·x0 + a1·x1 + a2·x2`; the IIR summed `fir + b0·y1 + b1·y2` then shifted. The new `process` keeps that order on five floats (CLEANUP P2 names the `0.0f +` start). `FIRFilter` and `IIRFilter` have no other user and are deleted with `BWSection`; construction no longer allocates (the vectors were per section).

- [ ] **Step 1: The twin test (RED)** — the old `BWSection`/`BWLPSection`/`BWHPSection` retyped in the test with their vectors; both kinds, `k`/`n` for a two-section cascade, cutoffs {100, 1000, 12000} at 48 kHz, `same_bits` over `twin_signal` for 10,000 samples; a `BWLowpassFilter(2)` and a `BWHighpassFilter(2)` rendered on white noise against a cascade of the retyped sections. RED.
- [ ] **Step 2: The header; the sites** — `filters.h` keeps `BWLowpassFilter`, `BWHighpassFilter`, `BWBandpassFilter`, `DelayFilter`; their vectors are `std::vector<dsp::ButterworthSection>` built with `build_butterworth_sections(…, Kind::Lowpass/Highpass, …)`; `fm_source.h`'s decimation the same with Lowpass.
- [ ] **Step 3: GREEN, mutation, gates** — mutation: `v += a_[1] * x_[1]` → `v += a_[1] * x_[2]`; null gate (eleven BW lowpass, six bandpass, fourteen FM in scope); synth A/B `bw_highpass`; `gates.py --fast`.
- [ ] **Step 4: Commit** — `git commit -m "dsp: one ButterworthSection with the kind as a parameter; the FIR and IIR helpers are gone"`.

---

### Task 8: Close the checkpoint

- [ ] **Step 1: Every gate** — `python tools/gates.py` (all): ctest, stamp, roundtrip smoke (the known wiring diff), the allocation gate, null gate `78/78`, roundtrip (the backlog-79 six), wiring null gate `78/78`.
- [ ] **Step 2: The A/B** — `python tools/ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab33 new` then `compare renders/scratch/ab33 old new` → `0 DIFFER` of 132; the synthetic set `compare … synth_old synth_new` → `0 DIFFER` of 9.
- [ ] **Step 3: Timing** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre33.exe build/tools/mforce_cli/Release/mforce_cli.exe` with nothing else of this session's running; the per-sample arithmetic is unchanged and inlined, so the expectation is noise; if the base arm's walls are not within 5 percent of a previous clean run, say so and run once more.
- [ ] **Step 4: The meter** — `python tools/structure/check.py --delta <Task 0 sha>`; paste it (`duplicate_blocks` expected to fall; `include_violations` +0; `unmapped_files` 0); `--snapshot`; `--modules-md`.
- [ ] **Step 5: The refuter, the wave, the re-review** — the Fable refuter (read-only, extra-high, the ten-item brief with checkpoint-specific questions: each Ledger comparison table against the deleted lines; the five Review Focus items; whether any merged site changed an operand order; whether a primitive carries a reason-less method; the twin tests' bite), a fix wave with tests first (Majors and the cheap, clearly-right Minors), the same refuter's re-review by message, a re-check of the wave's follow-up.
- [ ] **Step 6: The report and the docs** — `docs/autonomy/refactor/reports/2026-11-02-checkpoint-3.3.md` in the ten-section format (section 1 names the one departure by construction — EKS's cleared input memory — and the sign-of-zero class; section 4 the copies collapsed by count; section 5 the gates, the A/B, the synthetic A/B, the timing, the meter; section 6 the rulings and the seven comparison tables; section 7 the debt: the forms not merged (ruling 10), the Butterworth's own π, the preservations P1 and P2; section 8 the refutation; section 10 lessons — a struct held by value versus a pointer to one, a function versus an empty struct, why `inline` on a free function in a header, what IEEE arithmetic guarantees and does not); Part 3 section 4 gains the landed line and section 10 row 3.3 its result; the target's 3.1 gains a landed line; REVIEW R10 — under the 11-02 rule, expected to carry nothing for Matt beyond the report's existence unless the refuter finds a far-reaching item; CLEANUP rows as the Ledger named them; BACKLOG row 2 (3.3 DONE, 3.4 next); STATUS.
- [ ] **Step 7: Commit** — `git commit -m "docs: checkpoint 3.3 report (R10), Part 3 row 3.3 and the section 4 landed line, the target's landed line, backlog, STATUS handoff, the plan's Ledger, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **`namespace mforce::dsp`, one header per primitive under `core/dsp/`.** The sub-namespace says "a primitive, not a node" at every use (`dsp::DcBlocker`), and the folder is the target's "one home". `core`'s module path already covers it; no map change. `PierceAllpass` moves in by `git mv` and takes the namespace, so the folder holds every primitive from the first commit.
2. **A primitive without state and without a stored coefficient is a function.** `blep(pos, incr)` and `bow_friction(dv, slope, offset, min, max)` have nothing to hold; Part 3 section 4's shape (`set`, `process` on a struct) is for the primitives that have memory. A struct with only a static function is a namespace spelled longer. Cost if wrong: two functions become two structs in an afternoon.
3. **The biquad is coefficients and state apart.** The mesh runs up to 128 edge sections that share one coefficient set recomputed per sample; a struct that owned both would copy five floats into 128 objects per sample or hold the coefficients twice. `BiquadCoefficients::process(state, x)` serves the mesh; `Biquad` (the pair) serves every one-section site. The five numbers are private with getters, because a node that pokes a coefficient bypasses the recipe that names it.
4. **The bypass is the node's.** The wavetable skips its tuning section when the coefficient is 0; the resonator runs it (a unit delay then); EKS skips when its coefficient is 0; KSString's `dispActive_`, the resonator's `stiffness > 0.001`, the bowed string's `brightness < 1` all decide whether a section runs. Those decisions are what the node means; the primitive computes one sample. A primitive that decided its own bypass would have to carry every node's threshold.
5. **The coefficient window is the wavetable's** (`0.001 < frac < 0.999`), not the primitive's: the resonator has no window and its bytes depend on that.
6. **A setting read per sample stays read per sample.** The resonator reads `stiffness.value` and `damping.value` per sample, KSString `brightness.value`, the bowed string `brightness.value`; `OnePole::process(x, a)` takes the coefficient per call (every site reads a live setting; a stored coefficient would need a per-sample `set` or a setting hook the sites do not have), and the resonator calls `set_allpass_double_pole(stiffness.value)` per sample, which is the `a·a` and `2·a` the old line computed per sample. No live-edit behaviour changes.
7. **`DcBlocker::set(cornerHz, sampleRate)` takes the corner; the 20 Hz is the node's constant.** Both nodes name it (`kDcCornerHz`); the primitive does not know what a KS string wants.
8. **EKS's stiffness allpass clears both memories at a note's start.** The old `adjust()` zeroed `stiffnessState_` (y1) and left `stiffnessPrevIn_` (x1) carrying the previous note's last table value into the new note's first sample. A `reset()` that clears both is the primitive's one honest reset; a `reset_output()` kept for one caller's bytes would be a wart in a shared header. The departure is one sample of one filter's input memory per note with stiffness > 0; the only EKS patch in scope (`hi_hat1`) has stiffness 0, so the A/B is 0 moved; the report's section 1 names it and the twin test shows it (Review Focus 2). This is the 2.5 precedent ("both improvements and both listed") applied under Matt's 11-02 rule.
9. **`BiquadSource` in resonance mode uses the general `process` with `b1 = b2 = 0`** and accepts the sign-of-zero class (Review Focus 1); it keeps writing the derived `a1`/`a2` into its settings, since the editor shows them and a saved patch carries them (CLEANUP P1 retires the write after Part 8 if wanted).
10. **Forms that differ are not merged.** KSString's inharmonic highpass (`R·(y1 + x − x1)`), its nested allpasses and its allpass fractional read (`eta·(x − y1) + x1`); the Reed and Brass loop one-poles (`c·x + (1−c)·y`); the SVF's 1P path; the mesh's STK edge one-pole (`b0·x + p·y1`); the additive noise bed (transposed direct form II). Each computes a different sequence of float operations from the primitive's; merging one is a render change, which is Part 8's (where renders may move for simplification) or 3.8's (the additive bed, with the Partials work). Section 7 lists them.
11. **Order of tasks**: by blast radius ascending and by dependency — `blep` (two files), `DcBlocker` (two), `TuningAllpass` (four), `OnePole` (nine sites, including two in `render`), `bow_friction` (three), `Biquad` (five, the widest), `ButterworthSection` (two files, deleting five helpers). Each is one commit under the null gate.
12. **A synthetic A/B set for the node types the scope lacks.** `patches/library` + `patches/baselines` contain no `AllpassResonator`, `Mesh2D`, `BrassEvolution`, `BWHighpassFilter`, no `KSString` with its bow wired, and no raw-mode `Biquad`; the twin tests prove each primitive, but a node-level render is the proof the protocol asks for. Nine small patches written for this checkpoint under `renders/scratch/ab33/synth/` (gitignored; the plan lists them) are rendered by both binaries. They are this checkpoint's fixtures, not queue patches, so Matt's measurement-scope rule (no time on audition/pending/sweep/old) is not touched.
13. **The twin test retypes the deleted lines.** Each reference is the old site's arithmetic copied into the test as a lambda (the only place the old form survives), compared bit for bit (`std::bit_cast`) over a deterministic signal with exact and negative zeros salted in. Where a site's own bits may legitimately differ in the sign of a zero (ruling 9) the comparison is `same_value`, and the test says why. Each twin test is mutation-tested once at its task's close.
14. **π.** The sites' three float literals are one float (`kPi`); the Butterworth design computes in double with `3.14159265358979`, which is a different double from π to twenty-one digits, so that file keeps its own `kPiDesign` with a comment, and the report's section 7 lists it (a one-line change after Part 8 moves every Butterworth render by a few bits).
15. **A/B set, base and scope as 3.2's** (library + baselines, 132; the old arm from the build at `d5ebbf3`; 0 DIFFER expected) plus the synthetic nine.
16. **`FIRFilter` and `IIRFilter` are deleted**, not moved to `core/dsp`: their only user was `BWSection`, and a generic order-n filter over `std::vector` is a primitive nobody asked for.
17. **The wavetable's two first-sample blocks stay two.** Each shrinks by five lines in Task 3; folding `compute_raw`/`compute_interpolated`'s shared first-sample work into one function is a structure change inside the node (3.6's one-type-per-file pass, or 3.7's real-time pass), not a primitive merge.
18. **REVIEW under the 11-02 rule.** Expected to carry no decision for Matt: every call here is either exact by construction or the kind the rule says the session decides (rulings 8, 9, 14). An R10 entry still points at the report so the chain of reports is unbroken.

## Self-review notes

- **Spec coverage.** Part 3 section 4's seven rows — Tasks 1 to 7 in the order blep, DC blocker, tuning allpass, one-pole, bow friction, biquad, Butterworth (the table's order differs; ruling 11 says why); "small struct, no virtual functions, held by value, `set`/`process`" — the Interfaces blocks (rulings 2 and 3 are the two shapes that depart, with reasons); "copies compared before they are merged" — the seven comparison paragraphs, one per task, and ruling 13's twin tests; "where they differ the primitive takes the difference as a parameter" — `DcBlocker::set(cornerHz, …)`, `bow_friction`'s five parameters, `ButterworthSection::Kind`, `OnePole::process(x, a)`; "no node changes sound" — the Global Constraints, with ruling 8 the one listed departure; the target's "one home" — ruling 1; section 10 row 3.3 — Task 8 steps 1 and 2.
- **Step scan.** Every code step names the file, the signature or the rule; every twin test names its reference lines and its signal; every verification step has its command and expected output; every mutation names the flip. Open by design: the exact shape of the nine synthetic patches (Task 0 reads a baseline of the same family and copies its instrument block), and which `Biquad` baseline rows are raw-mode (Task 6's A/B lists them).
- **Type consistency.** `dsp::kPi`, `dsp::blep`, `dsp::DcBlocker`, `dsp::TuningAllpass`, `dsp::OnePole`, `dsp::pole_for_samples/seconds/cutoff_hz`, `dsp::bow_friction`, `dsp::BiquadState`, `dsp::BiquadCoefficients`, `dsp::Biquad`, `dsp::ButterworthSection`, `dsp::build_butterworth_sections`, `dsp::PierceAllpass`, `same_bits`, `same_value`, `twin_signal`, `cli_pre33.exe`, `renders/scratch/ab33/{old,new,synth_old,synth_t<N>,synth_new}` are used with those names throughout.
- **Review Focus.** 1 — Task 6 step 1; 2 — Task 3 step 1; 3 — Task 7 steps 1–2; 4 — Tasks 4 and 6 (ruling 6); 5 — every task's step 3.
- **Proportion.** Code appears for each primitive's interface (the whole of it, since the arithmetic is the deliverable) and nothing else; the rest is comparison prose, commands and rulings. The engine change is seven small headers in, about 250 lines of per-node copies out; no pin, descriptor or render-path signature changes.
