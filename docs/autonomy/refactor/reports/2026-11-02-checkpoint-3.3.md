# Checkpoint 3.3 — the shared DSP primitives in `core/dsp`

Part 3 (`docs/architecture/part3-nodes.md`), checkpoint 3.3 of 3.8. Commits `0d500fc`..`b3c6160` on main (plan `docs/superpowers/plans/2026-11-02-part3-checkpoint-3.3.md`, committed as `5a07eeb`; execution ledger in the plan's Ledger). Report R10. Written 2026-11-02 at extra-high effort, inline execution with a Fable refuter.

## 1. Summary

Seven pieces of signal arithmetic were written per node: the band-limited step twice (saw, pulse); the first-order tuning allpass four times (the wavetable twice, the allpass resonator, EKS's stiffness); the DC blocker three times (the resonator, KSString's excitation and bow); the Friedlander bow friction curve three times (the bow table, KSString's bow junction, the bowed string); the second-order section with its coefficient recipes in five places (KSString's comb dispersion, the resonator's stiffness, the brass lip filter, the Biquad node, the mesh's Chafe edges); the leaky-integrator one-pole in four and its time-constant and corner-frequency formulas in five more (hammer bank, limiter, the performer's smoothers, the instrument's ring-out twice, KSString's inharmonic highpass pole); the Butterworth section as a base with a lowpass and a highpass subclass over two generic vector filters. The target's section 3.1 asks for "one home for the DSP primitives that are currently re-typed per node", and Part 3 section 4 says how: a small struct per primitive, no virtual functions, held by value, with `set…` for its coefficients and `process(x)` for one sample, merged one at a time with the copies compared first.

Now `engine/include/mforce/core/dsp/` holds nine headers in `namespace mforce::dsp`: `constants.h` (`kPi`, the one float the nodes' three π literals round to), `blep.h` (`blep(pos, phaseIncr)`), `dc_blocker.h` (`DcBlocker`), `tuning_allpass.h` (`TuningAllpass`), `one_pole.h` (`OnePole`, `pole_for_samples`, `pole_for_seconds`, `pole_for_cutoff_hz`), `bow_friction.h` (`bow_friction(dv, slope, offset, minOut, maxOut)`), `biquad.h` (`BiquadState`, `BiquadCoefficients` with five named recipes and the phase delay, `Biquad`), `butterworth_section.h` (`ButterworthSection` with the kind as a constructor parameter, `build_butterworth_sections`), and `pierce_allpass.h`, moved in from `core/` with the namespace. Twenty-three sites in fourteen node and render files hold or call them; `FIRFilter`, `IIRFilter`, `BWSection`, `BWLPSection` and `BWHPSection` are gone, as are KSString's `biquad_ap` and `BiquadState`, the resonator's `BiquadState`, the mesh's `allpass`, `PulseSource::get_blep` and about two hundred lines of per-node state and arithmetic.

Two shapes depart from the signed sentence, each with a reason in the plan's Ledger: a primitive with no state and no stored coefficient is a function, not a struct (`blep`, `bow_friction` — a struct with nothing in it is a namespace spelled longer), and the biquad is coefficients and state apart (`BiquadCoefficients::process(state, x)`) because the mesh runs up to 128 edge sections that share one coefficient set recomputed per sample; `Biquad` is the pair for the one-section case. `OnePole::process(x, a)` takes its coefficient per call because every owner reads it from a live setting.

**Behaviour.** No render moved: the null gate is 78/78 after every one of the seven task commits, the A/B over the 132 patches of `patches/library` and `patches/baselines` against the pre-checkpoint binary is 0 DIFFER, and a synthetic set of nine patches written for this checkpoint — for the node types the scope does not contain (`AllpassResonator`, `Mesh2D` in all three edge modes, `BrassEvolution`, `BWHighpassFilter`, `KSString` with its bow wired, `Biquad` in both modes) — is 0 DIFFER after every task that touched one of them and at the seal. Each merge keeps the deleted lines' float operations in the same order (section 6 has the seven comparison tables), and each primitive has a twin test that retypes the deleted lines as a reference and compares bit for bit over a deterministic signal salted with zeros of both signs. One departure exists by construction and is named here: EKS's stiffness allpass now clears both of its memories at a note's start where the old `adjust()` cleared the output memory only and let the previous note's last table value into the new note's first sample. The section sits inside EKS's recirculating loop, so the cleared memory is not an open-loop impulse: the refuter measured it on `hi_hat1` with stiffness 0.5 (`renders/scratch/ab33/synth/eks_stiffness.json`, four notes on one voice) — 2,486 of 153,600 16-bit samples differ, every one by a single LSB, from the second note on, where the first note is byte-identical; with stiffness 0 the render is byte-identical throughout. The only EKS patch in scope (`hi_hat1`) has stiffness 0, where the section is skipped, so the A/B over library and baselines cannot see it; the patch above is kept as a one-entry "departure" set beside the synthetic set and is A/B'd old against new at the seal (1 DIFFER, as listed), so the path is rendered rather than reasoned about. The keep-bytes alternative — a `reset_output()` on the primitive for EKS alone — was decided against under Matt's 2026-11-02 rule (understood, one LSB, no patch in scope, the honest behaviour); REVIEW R10 names it without a question. One class of bit difference exists and is bounded: `BiquadSource` in resonance mode now runs the general section with `b1 = b2 = 0`, whose `+ 0·x1 + 0·x2` differs from the old all-pole line only in the sign of a zero (the first divergence needs `b0·x` to be a negative zero; once a memory holds a zero of the other sign the sign can carry through further all-zero samples, the value never). The gates hash 16-bit WAVs, which fold the sign of a zero and any sub-LSB difference, so the twin test is the only evidence about this class: it asserts equality of value everywhere and bit-equality everywhere the bound allows; the eight resonance-mode Biquads in scope render byte-identical at 16 bits, as the class predicts. Two live-edit edges, neither a render: `BiquadSource`'s resonance mode now shifts its input memories, so a switch to Raw mid-note reads the last two inputs where the old node read memories frozen at the switch (the better behaviour, arguably); and `KSString` sets every comb's dispersion sections per note, so a mid-note increase of `numCombs` runs the added combs through the per-note pole as before (the review wave; the task commit set only the live count).

## 2. Types and files

27 files changed, 1190 insertions(+), 375 deletions(-) (`git diff --stat 5a07eeb b3c6160 -- engine tools`; nine commits: the folder, one per primitive, the review wave).

| Area | What changed |
|---|---|
| `core/dsp/constants.h` (new, 10 lines) | `inline constexpr float kPi`. `3.14159265f`, `3.1415926536f` and `3.14159265358979323846f` are one float (`0x40490FDB`); the scaffold test pins it. |
| `core/dsp/blep.h` (new, 24) | `float blep(float pos, float phaseIncr)`: the polynomial correction, non-zero within one increment of the wrap on either side. Callers: `SawSource::wave` (subtracts it), `PulseSource::wave` (adds it at the rise, subtracts it a duty cycle later). |
| `core/dsp/dc_blocker.h` (new, 27) | `struct DcBlocker { set(cornerHz, sampleRate); reset(); process(x); }` — `y = r·y1 + x − x1`, `r = 1 − 2π·corner/rate`. Held by `AllpassResonator` (`dc_`) and `KSString` (`dc_`, `bowDc_`); each names its corner `kDcCornerHz = 20.0f`. |
| `core/dsp/tuning_allpass.h` (new, 29) | `struct TuningAllpass { set_fraction(frac); set_coefficient(c); coefficient(); reset(); process(x); }` — `y = c·x + x1 − c·y1`. Held by `WavetableSource` (`tuning_`), `AllpassResonator` (`tuning_`), `EKSEvolution` (`stiffness_`). |
| `core/dsp/one_pole.h` (new, 39) | `struct OnePole { reset(); process(x, a); value(); }` — `y += a·(x − y)`; `pole_for_samples(n)` = `exp(−1/n)`, `pole_for_seconds(s, rate)`, `pole_for_cutoff_hz(hz, rate)` = `exp(−2π·hz/rate)`. Held by `KSString::Comb::lp`, `AllpassResonator::damping_`, `BowedStringEvolution::bridgeLp_`/`neckLp_`; the formulas called by `HammerBank`, `Limiter`, `PerformSource::prepare`, `PitchedInstrument` (twice), `KSString::init_note`. |
| `core/dsp/bow_friction.h` (new, 20) | `float bow_friction(dv, slope, offset, minOut, maxOut)` = `clamp((|(dv + offset)·slope| + 0.75)^−4, minOut, maxOut)`. Callers: `BowTableSource` (its pins and settings), `KSString` and `BowedStringEvolution` (offset 0, floor 0, top `kFrictionMax = 0.98f`). |
| `core/dsp/biquad.h` (new, 117) | `struct BiquadState {x1, x2, y1, y2; reset()}`; `struct BiquadCoefficients { set_raw; set_resonance (STK); set_resonant_lowpass (RBJ, peak-normalised); set_allpass_double_pole; set_allpass_chafe; process(state, x); phase_delay_at(hz, rate); b0()…a2() }` — Direct Form I, a0 normalised; `struct Biquad { coefficients; state; process(x); reset() }`. Held by `KSString::Comb::bq[2]`, `AllpassResonator::stiffness_`, `BrassEvolution::lip_`, `BiquadSource::section_`; `Mesh2DSource::edge_` (coefficients) over `fxAp_[64]`, `fyAp_[64]` (states). |
| `core/dsp/butterworth_section.h` (new, 90) | `struct ButterworthSection { enum class Kind { Lowpass, Highpass }; ButterworthSection(kind, k, n, rate); update(cutoff); process(x); reset(); }` over five float memories, the design in double with the file's own `kPiDesign` (section 7); `build_butterworth_sections(vector&, kind, count, rate)`. Held in `std::vector` by `BWLowpassFilter`, `BWHighpassFilter`, `BWBandpassFilter` (two vectors) and `FMSource` (the decimation cascade). |
| `core/dsp/pierce_allpass.h` (moved from `core/`, 94) | `dsp::PierceAllpass`, unchanged but for the namespace; `Mesh2DSource` and `PierceFilterSource` say `dsp::`. |
| `source/oscillator/saw_source.h` (−9), `pulse_source.h` (−11) | `wave()` calls `dsp::blep`; `get_blep` gone. |
| `source/physical/allpass_resonator.h` (−4) | `dc_`, `tuning_`, `damping_`, `stiffness_`; `BiquadState`, `dcR_/dcX_/dcY_`, `tuneA_/tuneX_/tuneY_`, `lp_`, `bq_` gone; `ap1_phase_delay` stays (a note-init formula). |
| `source/physical/ks_string.h` (−8) | `dc_`, `bowDc_`, `Comb::lp` a `OnePole`, `Comb::bq[2]` two `Biquad`s set per note, `hpR_` from `pole_for_cutoff_hz`, the bow junction through `bow_friction`; `biquad_ap`, `BiquadState`, `dcR_` and the five memories gone; `kDcCornerHz`, `kFrictionMax`. |
| `source/wavetable/wavetable_source.h` (−2), `src/wavetable_source.cpp` (−18) | `tuning_`; the two first-sample blocks set the fraction under the window and reset; the two per-sample blocks are one line each. |
| `source/wavetable/wave_evolution.h` (−21) | EKS: `stiffness_` set from a local coefficient and reset in `adjust`; the bowed string: `bridgeLp_`, `neckLp_`, `bow_friction`, `kFrictionMax`; brass: `lip_` with `set_resonant_lowpass(omega, q)` after the node's clamps, `values[index] = lip_.process(rawLoop_[index])`. |
| `source/filter/biquad_source.h` (−10) | `section_`; both modes load it from the five settings each sample; resonance mode fills `a1`/`a2` from `set_resonance` on an (f, r) change; `phase_delay_at` delegates, its cache kept. |
| `source/physical/mesh2d_source.h` (−8) | `edge_` set by `set_allpass_chafe` per sample in mode 1; `fxAp_`/`fyAp_` are `BiquadState[64]`; `allpass()` and `a1_/a2_` gone. |
| `source/physical/bow_table_source.h` (−1) | `dv * dsp::bow_friction(...)`. |
| `source/filter/hammer_bank.h` (+1), `limiter.h` (+1), `render/perform_source.h` (+1), `render/instrument.h` (+1) | the pole formulas; the scalar decays they apply are unchanged. |
| `source/filter/filters.h` (−108) | `FIRFilter`, `IIRFilter`, `BWSection`, `BWLPSection`, `BWHPSection`, `build_bw_sections` deleted; the three filters hold `std::vector<dsp::ButterworthSection>` built with a kind; `DelayFilter` untouched. |
| `source/oscillator/fm_source.h` (+2) | the decimation cascade is `ButterworthSection`s of kind Lowpass. |
| `tools/engine_tests/main.cpp` (+547) | the scaffolding (`same_bits`, `same_value`, `twin_signal`, `run_dsp_scaffold_tests`) and nine twin-test functions (`run_dsp_blep_tests`, `_dc_blocker_`, `_tuning_allpass_`, `_tuning_allpass_boundary_`, `_one_pole_`, `_bow_friction_`, `_biquad_`, `_biquad_recipe_`, `_butterworth_`), each retyping the deleted lines as its reference; 7359 → 7467 checks. |

## 3. Interfaces changed

| Interface | Before | After |
|---|---|---|
| `namespace mforce::dsp` | — | the nine headers under `core/dsp/` |
| `PierceAllpass` | `mforce::PierceAllpass` in `core/pierce_allpass.h` | `mforce::dsp::PierceAllpass` in `core/dsp/pierce_allpass.h` |
| `FIRFilter`, `IIRFilter`, `BWSection`, `BWLPSection`, `BWHPSection`, `build_bw_sections` | `source/filter/filters.h` | gone; `dsp::ButterworthSection`, `dsp::build_butterworth_sections` |
| `PulseSource::get_blep` (private) | two overloads | gone; `dsp::blep` |
| `KSString::biquad_ap`, `KSString::BiquadState`, `AllpassResonator::BiquadState`, `Mesh2DSource::allpass` (private) | per-node | gone; `dsp::Biquad`, `dsp::BiquadCoefficients`, `dsp::BiquadState` |
| `BiquadSource::phase_delay_at` | the closed form inline | delegates to `BiquadCoefficients::phase_delay_at`; same cache, same result bit for bit (the recipe test) |

No pin, no descriptor, no setting, no `set_param`, no registration and no patch key changed; nothing a patch or the editor names moved.

## 4. Duplicates collapsed

| Primitive | Copies before | Now |
|---|---|---|
| band-limited step | 2 (saw inline, pulse's `get_blep`) | `dsp::blep` |
| first-order tuning allpass | 4 (wavetable ×2, resonator, EKS) | `dsp::TuningAllpass` |
| DC blocker | 3 (resonator, KSString ×2) | `dsp::DcBlocker` |
| leaky-integrator one-pole | 4 (KSString comb, resonator, bowed string ×2) | `dsp::OnePole` |
| time-constant pole `exp(−1/(τ·sr))` | 5 (hammer bank, limiter, performer, instrument ×2) | `dsp::pole_for_seconds` / `pole_for_samples` |
| corner-frequency pole `exp(−2π f/sr)` | 1 (KSString) | `dsp::pole_for_cutoff_hz` |
| bow friction curve | 3 (bow table, KSString, bowed string) | `dsp::bow_friction` |
| second-order section (DF1) | 5 (KSString, resonator, brass, Biquad raw + resonance, mesh) | `dsp::BiquadCoefficients::process` |
| STK resonance recipe, RBJ resonant lowpass, double-real-pole allpass (×2), Chafe allpass | 5 coefficient blocks | five named recipes |
| Butterworth section | a base and two subclasses over two generic vector filters | `dsp::ButterworthSection` with `Kind` |

The meter's duplicate-block count fell by one (152 → 151); most copies were under its eight-line threshold, which is why they survived it. New duplication, acknowledged: the twin tests retype every deleted line once as a reference, by design — the only place the old forms survive, and the thing that bites when a primitive drifts.

## 5. Metrics

**Gates at the seal** (`python tools/gates.py`, every gate, at `2f0a755`):

| Gate | Result |
|---|---|
| ctest (engine_tests, test_figures, structure meter) | PASS (34 s); `engine_tests` ALL PASS (7467 checks) |
| `mforce_ui --stamp` | PASS (stale: no) |
| roundtrip smoke (one patch per shape) | FAIL: the known wiring diff (5/6 shapes clean), as at 2.4, 2.5, 2.6, 3.1 and 3.2 |
| allocation gate (library + baselines, `--jobs 3`) | PASS (61 s) |
| null gate (patches) | PASS: 78/78 manifest entries identical |
| roundtrip (UI save/load, whole corpus, 132 patches) | FAIL: the backlog-79 six (`double_advance_bug`, `double_advance_ok`, `feedback/loop_tap_counter`, `feedback/loop_tap_starved`, `perform/wiring_setting`, `perform/wiring_smoke`), 0 id changes — the same six as at 3.2, pre-existing, Part 4's codec |
| wiring null gate (paramMap conversion) | PASS: 78/78 identical after conversion (28 carried a paramMap) |

After every task commit: `engine_tests` ALL PASS (7372, 7377, 7382, 7399, 7411, 7418, 7452, 7467), null gate `78/78`, `gates.py --fast` (ctest, the stamp, the allocation gate; the roundtrip smoke's known wiring diff, 5/6, as at 3.2), and the synthetic A/B 9 identical. After the review wave (`b3c6160`): ALL PASS (7481), null gate `78/78`, `gates.py --fast` the same, the synthetic set (ten patches now, `fm_oversample` added) 10 identical, the departure set 1 DIFFER as listed, and the A/B over the 132 re-rendered (132 identical, 0 DIFFER).

**A/B** (`renders/scratch/ab33`): old arm from `renders/scratch/ab_tree/cli_pre33.exe` (the engine at `d5ebbf3`), new arm from the build at `2f0a755`: `compared 132 patches present in both arms — identical: 132, DIFFER: 0`. Synthetic set (`renders/scratch/ab33/synth.txt`, nine patches, two notes each): `identical: 9, DIFFER: 0` at the seal and after Tasks 2–7.

**Timing** (`python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre33.exe build/tools/mforce_cli/Release/mforce_cli.exe`):

One run, the machine quiet (nothing else of this session's running; the base arm's walls are within 1 percent of 3.1's clean run — oboe1 804 vs 803, piano_default 965 vs 990, viola 1188 vs 1185 — so the plan's rule asks for no second run). Rows 0.96x to 1.05x: noise around 1.0, as expected for a checkpoint whose per-sample arithmetic is unchanged and inlined.

```
patch             base wall   new wall   speedup  base rndr   new rndr
----------------------------------------------------------------------
oboe1                 804.1      793.8     1.01x       20.0       20.4
piano_default         965.3      938.2     1.03x       38.4       38.6
viola_default        1188.2     1189.5     1.00x        6.0        6.1
A sine                427.0      427.6     1.00x      124.3      123.4
B sine+env            441.2      442.6     1.00x      139.4      137.6
C filter x8           727.2      724.1     1.00x      422.4      420.7
D 8 sines            1189.3     1196.4     0.99x      884.3      894.3
E shared ref          495.6      473.7     1.05x      190.7      168.1
F 40 sines           2958.7     2914.9     1.02x     2753.5     2707.6
G 40 own env         3860.7     3847.5     1.00x     3654.3     3639.3
H 40 shared          2869.1     2800.6     1.02x     2648.2     2593.1
I 40 envs            1578.0     1637.6     0.96x     1369.1     1427.3
```

**The meter** (`python tools/structure/check.py --delta 5a07eeb`, pasted unedited):

# Structure delta

| Measure | Before | After | Change |
|---|---|---|---|
| files | 172 | 180 | +8 |
| lines | 60,819 | 61,528 | +709 |
| files_over_limit | 20 | 20 | +0 |
| functions_over_limit | 98 | 98 | +0 |
| file_level_mutable_vars | 120 | 120 | +0 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 60 | 60 | +0 |
| changelog_comments | 249 | 249 | +0 |
| duplicate_blocks | 152 | 151 | -1 |
| include_violations | 14 | 14 | +0 |
| unmapped_files | 0 | 0 | +0 |

## Files added

- `engine/include/mforce/core/dsp/biquad.h` (117 lines)
- `engine/include/mforce/core/dsp/blep.h` (24 lines)
- `engine/include/mforce/core/dsp/bow_friction.h` (20 lines)
- `engine/include/mforce/core/dsp/butterworth_section.h` (90 lines)
- `engine/include/mforce/core/dsp/constants.h` (10 lines)
- `engine/include/mforce/core/dsp/dc_blocker.h` (27 lines)
- `engine/include/mforce/core/dsp/one_pole.h` (39 lines)
- `engine/include/mforce/core/dsp/pierce_allpass.h` (94 lines)
- `engine/include/mforce/core/dsp/tuning_allpass.h` (29 lines)

## Files removed

- `engine/include/mforce/core/pierce_allpass.h`

## Types added or removed

- NEW `Biquad` in `engine/include/mforce/core/dsp/biquad.h`
- NEW `BiquadCoefficients` in `engine/include/mforce/core/dsp/biquad.h`
- NEW `BiquadState` in `engine/include/mforce/core/dsp/biquad.h`
- NEW `ButterworthSection` in `engine/include/mforce/core/dsp/butterworth_section.h`
- NEW `DcBlocker` in `engine/include/mforce/core/dsp/dc_blocker.h`
- NEW `OnePole` in `engine/include/mforce/core/dsp/one_pole.h`
- NEW `PierceAllpass` in `engine/include/mforce/core/dsp/pierce_allpass.h`
- NEW `TuningAllpass` in `engine/include/mforce/core/dsp/tuning_allpass.h`
- REMOVED `PierceAllpass` from `engine/include/mforce/core/pierce_allpass.h`
- REMOVED `BWHPSection` from `engine/include/mforce/source/filter/filters.h`
- REMOVED `BWLPSection` from `engine/include/mforce/source/filter/filters.h`
- REMOVED `BWSection` from `engine/include/mforce/source/filter/filters.h`
- REMOVED `FIRFilter` from `engine/include/mforce/source/filter/filters.h`
- REMOVED `IIRFilter` from `engine/include/mforce/source/filter/filters.h`
- NEW `BwRef` in `tools/engine_tests/main.cpp`
- NEW `ChafeRef` in `tools/engine_tests/main.cpp`
- NEW `DoublePoleRef` in `tools/engine_tests/main.cpp`
- NEW `RawRef` in `tools/engine_tests/main.cpp`
- NEW `ResonanceRef` in `tools/engine_tests/main.cpp`

## Files that grew or shrank

- `engine/include/mforce/render/instrument.h`: 839 -> 840 (+1)
- `engine/include/mforce/render/perform_source.h`: 139 -> 140 (+1)
- `engine/include/mforce/source/filter/biquad_source.h`: 105 -> 95 (-10)
- `engine/include/mforce/source/filter/filters.h`: 308 -> 200 (-108)
- `engine/include/mforce/source/filter/hammer_bank.h`: 109 -> 110 (+1)
- `engine/include/mforce/source/filter/limiter.h`: 85 -> 86 (+1)
- `engine/include/mforce/source/oscillator/fm_source.h`: 133 -> 135 (+2)
- `engine/include/mforce/source/oscillator/pulse_source.h`: 47 -> 36 (-11)
- `engine/include/mforce/source/oscillator/saw_source.h`: 30 -> 21 (-9)
- `engine/include/mforce/source/physical/allpass_resonator.h`: 189 -> 185 (-4)
- `engine/include/mforce/source/physical/bow_table_source.h`: 35 -> 34 (-1)
- `engine/include/mforce/source/physical/ks_string.h`: 526 -> 518 (-8)
- `engine/include/mforce/source/physical/mesh2d_source.h`: 326 -> 318 (-8)
- `engine/include/mforce/source/wavetable/wave_evolution.h`: 1304 -> 1283 (-21)
- `engine/include/mforce/source/wavetable/wavetable_source.h`: 75 -> 73 (-2)
- `engine/src/wavetable_source.cpp`: 157 -> 139 (-18)
- `tools/engine_tests/main.cpp`: 6949 -> 7496 (+547)

## 6. Decisions and rejected alternatives

The plan's eighteen rulings, then the execution rulings; the Ledger carries each with its reason.

1. **`namespace mforce::dsp`, one header per primitive under `core/dsp/`**; `PierceAllpass` moves in and takes the namespace. Alternative: leave the primitives in `core/` beside the contract. Rejected: the folder is the target's "one home", and the sub-namespace says "primitive, not node" at every use.
2. **A primitive without state and without a stored coefficient is a function** (`blep`, `bow_friction`). Alternative: a struct with a static method, to match Part 3 §4's sentence to the letter. Rejected: a struct with nothing in it is a namespace spelled longer (section 10, lesson 2).
3. **The biquad is coefficients and state apart.** Alternative: one `Biquad` struct only. Rejected: the mesh's 128 edge sections share one coefficient set recomputed per sample; one struct each would copy five floats into 128 objects per sample or hold the coefficients twice. The five numbers are private with getters, so a node that pokes a coefficient bypasses the recipe that names it.
4. **The bypass is the node's.** The wavetable skips its tuning section at coefficient 0; the resonator runs it (a unit delay then); EKS skips at 0; KSString's `dispActive_`, the resonator's `stiffness > 0.001`, the bowed string's `brightness < 1` all decide whether a section runs. A primitive that decided its own bypass would carry every node's threshold.
5. **The coefficient window (`0.001 < frac < 0.999`) is the wavetable's**, not the primitive's: the resonator has no window and its bytes depend on that.
6. **A setting read per sample stays read per sample.** `OnePole::process(x, a)` takes the coefficient per call; the resonator calls `set_allpass_double_pole(stiffness.value)` per sample, which is the `a·a` and `2·a` the old line computed per sample. Two conventions on purpose: the one-pole's coefficient *is* the setting, so a setter would store a number the owner already has; the biquad's setter earns its place with the `a·a`, `2a` and cosine it derives. None from the per-sample reads; section 1 names the two live-edit edges.
7. **`DcBlocker::set(cornerHz, sampleRate)` takes the corner; the 20 Hz is the node's constant.** The bow curve's top is the other way round: `dsp::kBowFrictionMax` (0.98, STK's maximum) sits beside the curve since the wave, because it is the curve's number, not a node's — three nodes had written it.
8. **EKS's stiffness allpass clears both memories at a note's start.** The old `adjust()` zeroed the output memory and left the input memory carrying the previous note's last table value into the new note's first sample. Alternative: a `reset_output()` kept for one caller's bytes. Rejected: a wart in a shared header for a stale-memory leak; the 2.5 precedent ("both improvements and both listed") under Matt's 11-02 rule. The plan called the difference one sample; the review measured it (section 1): one LSB over the rest of every note after the first on a voice, because the section recirculates. Invisible in scope (`hi_hat1` has stiffness 0); rendered in the departure set.
9. **`BiquadSource` in resonance mode uses the general `process` with `b1 = b2 = 0`** and accepts the sign-of-zero class; it keeps writing the derived `a1`/`a2` into its settings (CLEANUP P1).
10. **Forms that differ are not merged**: KSString's inharmonic highpass (`R·(y1 + x − x1)`), its nested allpasses and its allpass fractional read; the Reed and Brass loop one-poles (`c·x + (1−c)·y`); the SVF's 1P path; the mesh's STK edge one-pole (`b0·x + p·y1`); the additive noise bed (transposed direct form II). Each computes a different sequence of float operations; merging one is a render change for Part 8 or 3.8.
11. **Order of tasks** by blast radius: blep, DC blocker, tuning allpass, one-pole, bow friction, biquad, Butterworth.
12. **A synthetic A/B set for the node types the scope lacks** — nine patches written for this checkpoint under `renders/scratch/ab33/synth/`, this checkpoint's fixtures, not queue patches.
13. **The twin test retypes the deleted lines** as a lambda or a small struct and compares bits over a deterministic signal with exact and negative zeros salted in; `same_value` only where ruling 9 allows the sign of a zero to differ; each mutation-tested once.
14. **π.** The nodes' three float literals are one float (`kPi`); the Butterworth design computes in double with `3.14159265358979`, a different double from π to twenty-one digits, so that file keeps `kPiDesign` (section 7).
15. **A/B set, base and scope as 3.2's** plus the synthetic nine.
16. **`FIRFilter` and `IIRFilter` are deleted**, not moved: their only user was `BWSection`.
17. **The wavetable's two first-sample blocks stay two**; folding them is a structure change inside the node (3.6 or 3.7).
18. **REVIEW under the 11-02 rule**: no decision for Matt; R10 points at this report.

Execution rulings (the Ledger, by task): `PierceAllpass` takes the namespace with its move; the wavetable's node-level proof is the null gate's eleven wavetables and the A/B (the plan's "driven on the same read-head output" was not possible without exposing the head); the boundary test asserts a first-sample difference that never grows and dies away, and that c = 0 is a unit delay both owners skip (the plan's "exactly one sample" was wrong); `kFrictionMax = 0.98f` was each node's constant at the task commit (the wave moved it beside the curve as `dsp::kBowFrictionMax`); `BiquadSource` loads its section from the five settings each sample in both modes, the pole recipe filling `a1`/`a2` on an (f, r) change; `phase_delay_at` builds the coefficients from the five settings as the old closed form read them; KSString's dispersion sections are set in `init_note` after the shed loop has settled `dispEff_` (the live count's at the task commit, every comb's since the wave).

**The seven comparisons** (what each site computed, what it computes now, and what the node keeps):

| Primitive | Site | Deleted arithmetic | Now | Exact? |
|---|---|---|---|---|
| blep | saw | `t + t − t·t − 1` / `t·t + t + t + 1`, else 0; `clamp(val − blep)` | `clamp(val − blep(pos, incr))` | yes |
| blep | pulse | `get_blep` with the same two polynomials | `blep(pos) − blep(fmod(pos + 1 − duty, 1))` | yes |
| DcBlocker | resonator, KSString ×2 | `r·y1 + x − x1`, `r = 1 − 2·3.14159265f·20/sr` | `process(x)` after `set(20, sr)` | yes (`kPi` is the same float) |
| TuningAllpass | wavetable ×2 | `C·out + prevIn − C·state`, C from `(1−frac)/(1+frac)` under the window, else 0; skipped at 0 | `set_fraction`/`set_coefficient(0)` under the node's window; `if (coefficient() != 0) process` | yes |
| TuningAllpass | resonator | the same line, C unconditional, always run | `set_fraction(frac)`; `process` | yes |
| TuningAllpass | EKS | the same line, C from the stiffness model, skipped at 0; `adjust` cleared y1 only | `set_coefficient(coeff)`, `reset()` (both), `if (coefficient() > 0) process` | one memory at note start (ruling 8) |
| OnePole | KSString comb, resonator, bowed ×2 | `lp += c·(x − lp)` with the setting read per sample | `process(x, setting.value)` | yes |
| pole formulas | hammer, limiter, performer, instrument ×2, KSString `hpR_` | `exp(−1/(τ·sr))`, `exp(−1/max(1, s·sr))`, `1 − exp(…)`, `exp(−2·3.14159265f·f/sr)` | `pole_for_seconds`, `pole_for_samples(max(1, …))`, `1 − pole_for_seconds`, `pole_for_cutoff_hz` | yes |
| bow_friction | bow table | `fabs((dv + offset)·slope) + 0.75`, `pow(·, −4)`, `clamp(min, max)`, `dv·rc` | `dv · bow_friction(dv, slope, offset, min, max)` | yes |
| bow_friction | KSString, bowed string | `fabs(gain·vRel) + 0.75`, `pow`, `if (> 0.98) 0.98` (the bowed string also `< 0 → 0`) | `bow_friction(vRel, gain, 0, 0, 0.98)` | yes: `x + 0.0f` is `x` under `fabs`, a product commutes, `pow` of a positive is never negative |
| Biquad | brass, Biquad raw | `b0·x + b1·x1 + b2·x2 − a1·y1 − a2·y2` | `process` | yes |
| Biquad | KSString ×2 per comb, resonator | `a·a·x − 2a·x1 + x2 + 2a·y1 − a·a·y2` | `set_allpass_double_pole(a)`: `b0 = a·a, b1 = −2a, b2 = 1, a1 = −2a, a2 = a·a` | yes: `+ (−2a)·x1` is `− (2a)·x1`, `+ 1·x2` is `+ x2`, `− (−2a)·y1` is `+ (2a)·y1`, each exact |
| Biquad | mesh mode 1 | `a2·x + a1·s0 + s1 − a1·s2 − a2·s3`, `a1 = −2R cos(6.2831855f·fc/sr)` | `set_allpass_chafe(fc, R, sr)`: `b0 = a2, b1 = a1, b2 = 1`; `2·kPi` is that literal | yes |
| Biquad | Biquad resonance | `b0·x − a1·y1 − a2·y2`, no input memory | `set_raw(b0, 0, 0, a1, a2)`; `process` | the sign of a zero only (ruling 9) |
| ButterworthSection | LP, HP, FM decimation | `iir(fir(gain·x))` over vectors: `0 + a0·x0 + a1·x1 + a2·x2`, then `+ b0·y1 + b1·y2` | the same order over five floats | yes |

## 7. Debt knowingly left

- **The forms not merged** (ruling 10): KSString's inharmonic highpass, nested allpasses and allpass fractional read; the Reed and Brass loop one-poles; the SVF's 1P path; the mesh's STK edge one-pole; the additive noise bed. Each is a different difference equation; merging one moves a render. Part 8 (where renders may move for simplification) or 3.8 (the additive bed, with the Partials work).
- **`kPiDesign`** (3.14159265358979, double) in `butterworth_section.h`: the fifteen-digit π the Butterworth formulas were written with, a different double from π; replacing it with the twenty-one-digit constant moves every Butterworth render by a few bits. Part 8.
- **`BiquadSource` writes the derived `a1`/`a2` into its settings** in resonance mode (CLEANUP P1): the editor shows them and a saved patch carries them, as before; a node that mutates its own settings is a smell kept for byte- and editor-identity.
- **`ButterworthSection::process` starts its accumulator at `0.0f`** (CLEANUP P2) for the sign of a zero product, as the deleted FIR loop did; `a0·x0 + a1·x1 + a2·x2` would be one instruction fewer.
- **`BiquadSource` loads its section from the settings every sample** (five stores) where a setting hook would load it on change; the node has no hook today, and the old line read the settings per sample. Part 7's editor, or 3.6, may give it one.
- **The resonator sets its stiffness coefficients per sample** (ruling 6), the per-sample `a·a` and `2·a` the old line paid; a setting hook would set them once.
- **EKS's cleared input memory** (ruling 8) is the one behaviour difference by construction; invisible in scope.
- **The twin references live in the tests** as the only copies of the old forms; they are the oracle, and they age with the tests file (7,496 lines, the second-largest file in the tree; Part 8's split).
- **`OnePole::value()`** had no engine caller at the task commit (the resonator uses `process`'s return); the review wave gave it one, the performer's `wheel()`/`pressure()` getters. **`BiquadCoefficients::b0()/b1()/b2()`** are test-only, kept as the getter set beside the two the Biquad node reads.
- **Twenty-three π literals remain outside `core/dsp`** (`kPi`'s adoption stopped at the merged sites); each is a one-line change to a shared constant, and each must be checked to be the same float first.
- **Pre-existing, found by the review:** `FMSource` rebuilds its decimation cascade on a live `oversample` change (an allocation on the control side, under the audio mutex), and the limiter calls `exp` per sample for its release coefficient.
- **Coverage the scope and the synthetic set still lack:** `Mesh2D` mode 1 at a non-default `edgeFc`/`edgeR`, `KSString`'s top-octave dispersion shed (the same arithmetic, a different branch), the `PierceFilter` node (namespace only), a `Biquad` mode switch mid-note.
- Carried unchanged from 3.2's section 7: `Theme`'s remaining literals and JSON loading (Part 7), the six preset envelopes in the menu and the `KSPianoString` alias on the shelf (3.5), the `.cpp` files outside family folders (3.6), the `music/pitch_bend.h` edge (Part 5), registration order as menu order, `kFamilyOrder`, `Shelf` in the engine registry, `types_in` per frame, the editor's remaining name-matching classifiers, `denormals.h`'s encoding, the Part-number comments; and 3.1's: the holders as wrappers and the two prepares (3.4), `AdditiveSource2` (3.7), the three lifecycle trios, the editor's part strips, `signal()` casts, hand lists and silent tap-from-part (Part 7), the `expandRule` cast and the allowlist's descriptor keys (Part 4), `MultiSource`'s hand-written list, `Slot`'s public vectors, `nodesById` + `signals`.

## 8. Independent review

**Fable refuter, read-only, extra-high effort, on `5a07eeb..2f0a755`** with the ten-item brief and twelve checkpoint-specific questions (the plan's Review Focus, the per-site arithmetic, live edits, whether the twins bite, coverage). Its review follows verbatim (`renders/scratch/ab33/review_r10.md`), then what was done about each finding, then the re-review.

---

# Checkpoint 3.3 refutation (R10) — core/dsp primitives, commits 0d500fc..2f0a755

Reviewer: independent, read-only. Base read: diff 5a07eeb..2f0a755 over engine/ and tools/, every header under engine/include/mforce/core/dsp/, the plan's Ledger and Review Focus, Part 3 §4, target 3.1, modules.json. Ran engine_tests.exe (ALL PASS, 7467 checks). Rendered two EKS review patches with cli_pre33.exe and the current CLI into renders/scratch/ab33/review/ (see M1). HEAD = 2f0a755; working-tree engine/ and tests match it, so file:line cites are the working tree.

## 1. The ten brief items

| # | Item | Verdict | Evidence (what was checked) |
|---|---|---|---|
| 1 | Separation of concerns | PASS | One primitive per header. The only header with more than one type is `core/dsp/biquad.h` (State 9-12, Coefficients 19-107, pair 110-115): one section, three views. `one_pole.h:11-37` holds the integrator and its three pole formulas (one job: "the one-pole and its time constants"). `butterworth_section.h:16-88` the section and its vector builder. Nothing mixes drawing, persistence or model. `BiquadCoefficients::phase_delay_at` (biquad.h:84-97) is analysis on the coefficients, which is where it belongs. |
| 2 | Single responsibility | PASS | DcBlocker "removes DC with one pole" (dc_blocker.h:10-25); TuningAllpass "a first-order allpass" (tuning_allpass.h:12-27); OnePole "a leaky integrator" (one_pole.h:11-21); ButterworthSection "one bilinear second-order section of either kind" (butterworth_section.h:16-80, the kind branches only in `update`). BiquadCoefficients: "the five numbers, the recipes that fill them, the step over a state and the phase delay" — the "and" is one cohesive recipe set, not two jobs. |
| 3 | Dependency direction | PASS | Every core/dsp header includes only `core/dsp/constants.h` and the standard library: biquad.h:2-4, dc_blocker.h:2, one_pole.h:2-3, bow_friction.h:2-3, butterworth_section.h:2-3, pierce_allpass.h:2-3; blep.h and tuning_allpass.h include nothing. New node/render includes (ks_string.h:3-6, allpass_resonator.h:3-6, wave_evolution.h:2-5, mesh2d_source.h:3-4, biquad_source.h:3, filters.h:3, fm_source.h:2, saw/pulse, bow_table_source.h:3, hammer_bank.h:3, limiter.h:3, perform_source.h:3, instrument.h:8) all point at `core`, allowed for source and render. Meter: include_violations 14 -> 14, unmapped 0 (meter33_delta.txt). No cycle. |
| 4 | Duplication | FAIL (minor) | (a) `render/perform_source.h:78-81`: two copies of exactly the OnePole form `y += a*(x - y)` (`wheelSm_`, `pressSm_`), neither merged nor in ruling 10's list; the plan's Task 4 text calls them "scalar decays", they are leaky integrators on a control signal. (b) `kFrictionMax = 0.98f` written twice with the same comment (ks_string.h:310, wave_evolution.h:518), and BowTable's `maxOutput` default is the same 0.98 (bow_table_source.h:21). (c) `kDcCornerHz = 20.0f` twice (ks_string.h:308, allpass_resonator.h:121) — by ruling 7, accepted. Otherwise clean: grep over engine/ outside core/dsp finds no remaining `exp(-1.0f /`, DF1 biquad line, blep polynomial, `pow(., -4.0f)`, 20 Hz blocker or `+= a*(x - y)` on a single line. 23 pi literals remain outside core/dsp (svf 3, mixer 2, ks_string.h:363, allpass_resonator.h, hammer_bank.h, fm_source.h, ...) — not this checkpoint's scope, F7. |
| 5 | Encapsulation | PASS (noted) | `BiquadState`'s four floats and `Biquad`'s two members are public (biquad.h:9-12, 110-115). No owner touches a memory: grep for `fxAp_[..].x1`, `.state.x1` etc. finds nothing; owners reach the recipes through `.coefficients` (allpass_resonator.h:109, ks_string.h:441-442, wave_evolution.h:570, biquad_source.h:68,71). The other five primitives keep state private. See G. |
| 6 | Ownership | PASS | Every primitive is held by value: ks_string.h:321,327,513-514; allpass_resonator.h:178-182; mesh2d_source.h:297,312; wave_evolution.h:346,525-526,607; biquad_source.h:90; filters.h:53,94,142-143 (vectors of sections, trivially copyable now); fm_source.h:131. No pointer or reference member on any primitive; no cycle. |
| 7 | Abstraction fit | PASS (noted) | Part 3 §4's shape (small struct, no virtuals, by value, set/process) holds for DcBlocker, TuningAllpass, Biquad, ButterworthSection. The three departures are reasoned in the Ledger (2, 3, 6) and the reasons hold; see H for where I would differ (one convention for a live coefficient, F5) and for `float k, float n` (I4). |
| 8 | Real-time rule | PASS | No allocation, lock, throw or unbounded work added on a compute path: `BiquadSource::compute`'s `poles` is a stack local (biquad_source.h:62); `build_butterworth_sections` runs from `on_prepare` only (filters.h:26,70,112-114) and no longer allocates per section; FM's `build_decimation` from `on_prepare`/`on_setting_changed` (fm_source.h:35,40) is pre-existing; `std::cos` per sample in the mesh (mesh2d_source.h:160) and `std::pow` per sample in the bow sites were there before. Allocation gate PASS in gates_all_close.txt. |
| 9 | Names and comments | PASS (noted) | No date, REVIEW/Part/checkpoint number, backlog id or CLEANUP tag in any added line (grep of the diff's `+` lines). `kPiDesign` (butterworth_section.h:69-72) says what it is and why. One name says a use, not the thing: `TuningAllpass` holds EKS's dispersion (wave_evolution.h:346) — F4; the design table named it, so not a FAIL. |
| 10 | Tests | PASS (gaps) | Each primitive has a twin test that retypes the deleted lines (tests/main.cpp:52-555) and ALL PASS. Gaps: `phase_delay_at` has no reference (M2), `BiquadState::reset` untested, `ButterworthSection::reset` and `DcBlocker::set` under-tested (I2), one tautology (F2). |

## 2. Findings

### Critical
None. No render in patches/library + patches/baselines moves (132/132 identical, 78/78 null gate, synthetic 9/9 identical after every task — verified from hashes.txt in synth_old vs synth_t2..t7, synth_new). No crash path introduced.

### Major

**M1 — The one listed departure (ruling 8, EKS) is misdescribed: it is not "one sample", it recirculates for the rest of every legato note.**
`wave_evolution.h:337` runs the stiffness allpass inside EKS's loop and `:339` writes its output back into the table (`values[index] = avg`), so the cleared input memory is not an open-loop impulse that "shrinks by the coefficient each sample" (report §1 line 13; ruling 8 "a one-sample leak") — it enters the recirculating table and persists for the note's decay. Measured: `renders/scratch/ab33/review/hi_hat1_stiff05.json` (hi_hat1 with stiffness 0.5, four 0.2 s notes at 0.3 s spacing, polyphony 1) rendered by `cli_pre33.exe` and the current CLI: 2,486 of 153,600 int16 samples differ, all by 1 LSB, from 0.3007 s (the second note) to 1.2365 s (the fourth note's tail); `hi_hat1_stiff0.json` (stiffness 0) is byte-identical (sha256 74019338...). The twin boundary test (tests/main.cpp:216-253) proves the primitive's open-loop shape only; its comment "dies away" does not describe the node. The departure is still invisible in scope (hi_hat1 is the only EKS and has stiffness 0 — verified) and still the kind Matt's 11-02 rule lets the session decide, but the record is wrong about its extent.
Fix: (a) rewrite report §1 and ruling 8: "a 1-LSB-level difference over the remainder of every note after the first on a voice, for EKS with stiffness > 0; invisible in scope; measured in review/"; (b) add an EKS-with-stiffness synthetic to `synth.txt` (the review patch will do) so 3.4+ can see this path at all — today no render anywhere exercises `stiffness_.process`; (c) if Matt would rather keep bytes, the alternative is `TuningAllpass::reset_output()` used by EKS alone — I would not (the old behaviour leaked a stale sample into every new note; the new is the honest one), but it is his call, not the session's, now that the size is known.

**M2 — `phase_delay_at` was moved with no reference test; the test that claims to cover it cannot fail on the closed form.**
`biquad_source.h:77-85` now delegates to `BiquadCoefficients::phase_delay_at` (biquad.h:84-97); the old eleven lines are retyped nowhere. `tests/main.cpp:451-459` compares `node.phase_delay_at(hz)` with `c.phase_delay_at(hz, 48000)`, which after the merge is the same function on both sides — it tests the delegation and the cache, not the arithmetic (a sign flip in biquad.h:90 or :92 passes). Ruling 13 says every merge retypes the deleted lines; this one does not. The SVF measured-vs-predicted test (tests:823-854) does not touch BiquadSource. Whether any scope render asks a Biquad for its phase delay (loop compensation) I could not establish, so the null gate is not known to cover it.
Fix: retype the deleted closed form (from the diff's removed side, biquad_source.h at 5a07eeb) as `phase_delay_reference(b0..a2, hz, sr)` in `run_dsp_biquad_recipe_tests` and `same_bits` it against `c.phase_delay_at` at the four frequencies for two coefficient sets (one with a negative b0 to exercise the fold at :94-95); keep the delegation check as a second line.

### Minor

**I1 — PerformSource's two smoothers are the OnePole form, unmerged and unlisted.** `perform_source.h:78-81` (`wheelSm_ += smoothA_ * (load - wheelSm_)`, same for `pressSm_`). Merge: `dsp::OnePole wheelSm_, pressSm_; wheelSm_.process(inst_->wheel.load(..), smoothA_)` — same operations in the same order, exact — or add them to ruling 10 with the reason. (Brief item 4.)

**I2 — Three reset/set paths are under-tested.** (a) `BiquadState::reset` / `Biquad::reset` (biquad.h:11,114): no twin calls either; a reset that clears x1/x2 only passes `run_dsp_biquad_tests` (tests:384-433). (b) `ButterworthSection::reset` (butterworth_section.h:63-66): the test (tests:548-554) processes two samples before reset, so `x_[2]` is still 0 and a reset that forgets it passes. (c) `DcBlocker::set` is only ever called with 20 Hz (tests:157,167,173): a `set` that ignores `cornerHz` passes. (d) `TuningAllpass::reset` clearing x1 only (instead of both) passes the boundary test (tests:216-253): the reference clears y1 only, the mutant clears x1 only, the difference is `prevIn + C*y1`, still "first sample differs, never grows, dies away". Fix: a reset twin per stateful primitive (process >= 3 samples, reset, compare with a fresh one over 200 samples), and `DcBlocker::set` at a second corner.

**I3 — Unused API on the primitives.** `OnePole::value()` (one_pole.h:17) has no engine caller (the resonator uses `process`'s return, allpass_resonator.h:114; the plan said `value()`); `BiquadCoefficients::b0()/b1()/b2()` (biquad.h:99-101) are test-only (`a1()/a2()` are used, biquad_source.h:64-65); `PierceAllpass::energy()` test-only (pre-existing). Fix: delete `value()`; keep the five getters as a set or delete three — say which in the report.

**I4 — `ButterworthSection(Kind, float k, float n, float rate)` types two counts as float** (butterworth_section.h:19,87; fm_source.h:122-124 `float(i + 1), float(nSec * 2)`). `int k, int n` convert exactly into `2.0 * k + n - 1.0` (small ints are exact in double either way), so the change is byte-identical and better typed. Cheap; do it in the wave or list it.

**I5 — `kFrictionMax` and BowTable's 0.98 are three copies of the curve's top** (ks_string.h:310, wave_evolution.h:518, bow_table_source.h:21). The top belongs beside the curve (`dsp::kBowFrictionMax` in bow_friction.h), or ruling 7 should say why 0.98 is each node's number when 20 Hz is.

**I6 — BiquadSource resonance mode now shifts x1/x2** (biquad.h:72-73 via biquad_source.h:69) where the old line did not. Observable only on a live mode switch Resonance -> Raw mid-note: the raw line then reads the last two inputs instead of memories frozen at the switch into resonance. Not a render; arguably the better behaviour. Name it in §1 as a live-edit difference (the report lists none).

**I7 — KSString's dispersion sections are set for `i < numCombs` at `init_note`** (ks_string.h:436-442); `numCombs` is live (`on_setting_changed` ks_string.h:118, no re-init), so a mid-note increase now runs the added combs through identity sections (default coefficients b0 = 1) where the old `biquad_ap` applied `dispEff_` live. Edge of an edge (those combs' `len`/`gain` are stale in both), but it is a live-edit path that changed. Set all `kMaxCombs` sections, not `numCombs`, and it is gone.

### Flags

**F1 — The gates cannot see the sign-of-zero class, so they are not evidence about it.** The A/B and the null gate hash 16-bit WAVs (`write_wav_16le_stereo`, tools/mforce_cli/main.cpp:123..; ab_render_set.py:39). A zero of either sign is the same int16, as is any sub-LSB float difference. Report §1's "the eight resonance-mode Biquads in scope render byte-identical" is true and says nothing about Review Focus 1; only the twin test (tests:415-432) does. Also the Focus-1 wording "differs only when b0*x is a negative zero" is the first-divergence condition: once y1 is a zero of the other sign the next `- a1*y1` can carry the sign through further all-zero samples (value still equal; the test's `badBits <= negZero` bound holds for this signal because its zeros are never consecutive). Wording only.

**F2 — A check that cannot fail.** tests/main.cpp:338 `same_bits(rc * dv, dv * rc)`: IEEE multiplication is commutative; this CHECK is a tautology. Delete it or label it as documentation.

**F3 — Coverage of the departure path.** No render anywhere (scope or synth) runs EKS with stiffness > 0 (the departure's own path), and none runs FM with oversample > 1 (the decimation Butterworth: 4 sections, n = 8; the twin covers n = 4 only, same expression). Add both to the synthetic set.

**F4 — Name.** `TuningAllpass` is a first-order allpass; one of its four owners is a dispersion filter (wave_evolution.h:346). `FirstOrderAllpass` would say what it is; the design table chose `TuningAllpass`, so Matt's call.

**F5 — Two conventions for a setting read per sample.** OnePole takes the coefficient per call (one_pole.h:13; sites ks_string.h:279, allpass_resonator.h:114, wave_evolution.h:504-505); Biquad takes a per-sample `set_*` then `process` (allpass_resonator.h:109-110, mesh2d_source.h:160). Both exact. The biquad's setter earns its place (a*a, 2a, cos are derived work); the one-pole's coefficient is the setting itself. Defensible; the report should say it is two conventions on purpose.

**F6 — The closing gate run shows two FAIL lines** (gates_all_close.txt: roundtrip smoke "wiring RENDER DIFF"; roundtrip corpus "6 NEW render diffs" at double_advance_bug/ok, loop_tap_counter/starved, wiring_setting/smoke). These are the backlog-79 six that 3.2's report names (2026-10-31-checkpoint-3.2.md:81,84); the tool calls them NEW because its known-list is empty. The 3.3 report must cite them as pre-existing with that line, not paste "0 known diffs".

**F7 — kPi adoption** stops at the merged sites; 23 pi literals remain outside core/dsp (item 4). A section-7 line, not this checkpoint's work.

**F8 — Plan deviation acknowledged in the Ledger:** the Task 3 node-level wavetable test was dropped (no read-head access); the null gate's eleven wavetables stand in. Fine; noted so the report's §8 does not claim it.

## 3. Answers A–J

**A. Site-by-site arithmetic.** Every merged site computes the same float operations in the same order as its deleted lines; I found no operand-order, rounding, literal, clamp or reset difference other than ruling 8 (M1) and the sign-of-zero class (ruling 9). Checked:
- blep (saw_source.h:16-17, pulse_source.h:31-32 vs removed get_blep): same two polynomials, `val - 0.0f` for the out-of-window return, `(val + b1) - b2` order kept; `else if` vs two `if`s equivalent since the first returns.
- DcBlocker (dc_blocker.h:12,16): `r*y1 + x - x1` left-to-right; `1 - ((2*kPi)*corner)/sr` with kPi the same float as `3.14159265f` (constants.h:8; tests:65-68 prove the three literals and 2*kPi).
- TuningAllpass (tuning_allpass.h:13,18): `c*x + x1 - c*y1`; `(1-f)/(1+f)`; wavetable window and `!= 0` bypass kept at wavetable_source.cpp:66-68,97,111-113,134; resonator unconditional (allpass_resonator.h:103); EKS `> 0` guard (wave_evolution.h:337).
- OnePole (one_pole.h:14): `y += a*(x - y)`; sites ks_string.h:279 (`si + c.gain*damp*lp` order kept), allpass_resonator.h:114 (`feedback * lp`), wave_evolution.h:504-505. Pole formulas: `exp(-1/(s*sr))` (one_pole.h:31) matches instrument.h:694,744, hammer_bank.h:77, perform_source.h:71 (`1 - exp`); `exp(-1/n)` (one_pole.h:26) with the limiter's `max(1, ...)` applied by the caller (limiter.h:54); `exp(((-2*kPi)*hz)/sr)` (one_pole.h:36) = ks_string.h:484's old line.
- bow_friction (bow_friction.h:16-17): `(dv + 0.0f)*slope` equals `gain*vRel` under `fabs` for every input including -0 (x + 0 = x except -0 -> +0, erased by fabs; a product commutes exactly); `std::clamp(v, 0, 0.98)` equals `if (v > 0.98) v = 0.98` because `pow(s >= 0.75, -4)` is in (0, 3.16], +0 for s = inf, NaN for NaN — identical in every case; `dv * rc` order kept at bow_table_source.h:29, ks_string.h:235, wave_evolution.h:495.
- Biquad (biquad.h:71): the general line is `((((b0*x + b1*x1) + b2*x2) - a1*y1) - a2*y2)`, memories shifted after. Double-pole (biquad.h:52-58): `+ (-2a)*x1` is `- (2a)*x1` (negation commutes with the product, `p - q` is `p + (-q)` by definition), `+ 1*x2` is `+ x2`, `- (-2a)*y1` is `+ (2a)*y1` — exact; the resonator computes `a*a, -2a` per sample as the old line did (allpass_resonator.h:109); KSString's `dispEff_` is written only in `init_note` (ks_string.h:366,391) before the comb loop sets the sections (:441-442), so per-note set equals the old per-call read. Chafe (biquad.h:62-68): `b0 = a2, b1 = a1, b2 = 1`, state slots `s[0..3] -> x1, x2, y1, y2`, shift order `x2 = x1; x1 = x; y2 = y1; y1 = y` matches `s[1]=s[0]; s[0]=x; s[3]=s[2]; s[2]=y`; `-2R*cos(2*kPi*fc/sr)` with `2*kPi` = the old `6.2831853...f`. Brass (biquad.h:36-47): the coefficient block verbatim after the node's omega clamp and `max(0.5, q)` (wave_evolution.h:566-570), `2*kPi` = `3.1415926536f*2`. BiquadSource raw: `set_raw` from the five settings per sample (biquad_source.h:71). Resonance: `set_raw(b0, 0, 0, a1, a2)` (:68) — the argument "only the sign of a zero can differ" is right: `b0*x + (+-0) + (+-0)` is `b0*x` for any nonzero `b0*x`, and `+-0 - a1*y1 - a2*y2` is exact once any term is nonzero; `0*x1` is NaN only for an infinite input, where the old line was already inf/NaN. Downstream observers of a zero's sign: none found — grep for `signbit`, `copysign`, a division by a `.current()` pin finds nothing; the `atan2` calls are on coefficients, not signals; the int16 writer folds -0 to 0. The x1/x2 shift is I6. `a1 = -2*r*cos(2*kPi*f/sr)` (biquad.h:30) is the old biquad_source line's order.
- EKS reset (ruling 8): reading of the old code confirmed — at 5a07eeb `adjust()` zeroed `stiffnessState_` only (line 278) and `stiffnessPrevIn_` was written only inside the `> 0` block (334-336), so it carried the previous note's last `avg` on a reused voice (the evolution is a structural input that persists; `adjust` runs per note, wavetable_source.cpp:60,106). "Truly invisible in scope": yes, hi_hat1 has stiffness 0.0 and is the only EKS in library + baselines. Extent: M1.
- ButterworthSection (butterworth_section.h:21,24-44,46-60): zeta, both `update` branches and `process` are the deleted code verbatim including double design arithmetic, `kPiDesign`, `gain*input` first, `v = 0.0f` then three products in index order, then two feedback products, shift after.
- Mesh state layout: `float[64][4] -> BiquadState[64]` (mesh2d_source.h:312) with the slot mapping above; `clear_mesh` resets each (:286-287); coefficients not reset, as `a1_/a2_` were not.

**B. Live edit.** Every per-sample read is still per sample: resonator stiffness and damping (allpass_resonator.h:109,114), KSString brightness (ks_string.h:279), bowed brightness (wave_evolution.h:504-505), BiquadSource's five settings (biquad_source.h:68,71), mesh edgeFc/edgeR (mesh2d_source.h:160). Per-note things stayed per note (KSString dispersion via dispEff_, wavetable fraction, EKS stiffness, brass lipQ, BW cutoff on change). No coefficient is newly cached. Two live-edit edges changed: I6 (Biquad mode switch) and I7 (numCombs raised mid-note).

**C. Do the twins bite?** Each would catch its one flipped sign; what each misses:
- blep: a site mutation in pulse's `(1.0f - duty)` or the bend path (duty 0.5, bend 0 only); edge mutants `>` -> `>=` are genuinely equivalent (the polynomials are 0 at t = +-1).
- dc_blocker: `set` ignoring `cornerHz` (I2c).
- tuning_allpass: `reset` clearing x1 only (I2d); `set_fraction` outside (0, 1) untested (not a site need).
- one_pole: tight for the struct; `pole_for_cutoff_hz` at two points — fine.
- bow_friction: tight for the function; :338 is a tautology (F2).
- biquad: `set_resonance` not zeroing `b1_/b2_` on a reused object (fresh in the test and in BiquadSource, so not reachable today); `BiquadState::reset` (I2a); any mutation of `phase_delay_at` (M2); `set_allpass_chafe` at one (fc, R) only.
- biquad_recipe: brass recipe tight; phase delay circular (M2).
- butterworth: `reset` forgetting `x_[2]` (I2b); n = 8 (FM) untested, same expression.

**D. Real-time.** Nothing introduced; see item 8. Pre-existing and worth a section-7 line: FM allocates on a live `oversample` change (fm_source.h:35) and the limiter calls `exp` per sample (limiter.h:54).

**E. Dependency direction.** Clean; see item 3. No new include against the map.

**F. Names and comments.** No changelog-style comment added. Unused API: `OnePole::value()`, `BiquadCoefficients::b0()/b1()/b2()`, `PierceAllpass::energy()` (test-only) — I3. Name: F4. `set_allpass_chafe`, `set_allpass_double_pole`, `set_resonant_lowpass`, `pole_for_*`, `kPiDesign`, `build_butterworth_sections` say what they are.

**G. Encapsulation/ownership.** `BiquadState` public floats: defensible — it is four memories with no invariant between them, and only `reset()` and `BiquadCoefficients::process(state, x)` touch them; making them private would need a friend for nothing. `Biquad`'s public `coefficients`/`state`: the pair is a convenience over two values, and the sites use it as such (`x.coefficients.set_*`, `x.process`); forwarding five setters would duplicate signatures. No owner reaches into internals (grep). All ownership by value.

**H. Abstraction fit.** Functions for blep and bow_friction (ruling 2): right — a struct with no state and no stored coefficient is a namespace; the design's `Blep`/`BowFriction` types would have been empty. Coefficients/state split (ruling 3): right, forced by the mesh's 128 shared-coefficient sections; `Biquad` as the pair keeps the one-section sites to one member. OnePole coefficient per call (ruling 6): acceptable, but it is the one place a primitive has no `set`, and the resonator's biquad shows the other convention for the same situation (F5); I would have kept one convention — `set(a)` + `process(x)` on the one-pole with the sites calling `set` per sample as the resonator's biquad does — unless the per-call form is declared the rule for "coefficient is the setting itself". `float k, float n` (I4) is the one typing I would change. Deleting FIR/IIR (ruling 16) rather than moving them: right, no other user.

**I. Coverage.** Exercised by patches/library + patches/baselines (132): blep (3 Saw, 2 Pulse); DcBlocker, comb OnePole, double-pole Biquad, `pole_for_cutoff_hz` (4 KSPianoString, bow unwired); `pole_for_seconds` (3 HammerBank, instrument ring-out everywhere, performer everywhere); `pole_for_samples` (1 Limiter); bow_friction + OnePole (1 BowedString); bow_friction with offset (2 BowTable); TuningAllpass (11 Wavetable); Butterworth LP (11) and BP (6); Biquad raw with b2 = -1 (8, so the x2 path is in scope) and resonance (8). Synthetic only: AllpassResonator (all four primitives), Mesh2D mode 1 at the default fc/R, BrassEvolution (resonant lowpass recipe), BWHighpass, KSString bow (bowDc_, bow_friction without offset), Biquad raw with b1 != 0. Exercised by neither: EKS with stiffness > 0 (the departure's path — M1, now rendered in review/); FM oversample > 1 (the decimation cascade, n = 8); Mesh2D mode 1 at non-default edgeFc/edgeR (twin also at 1575/0.75 only); KSString top-octave dispersion shed (same arithmetic, only dispEff_ changes); PierceFilter node (namespace only); BiquadSource mode switch mid-note. The two that matter: EKS (add it to synth.txt) and FM oversample (one cheap patch).

**J. Tests file.** No new function over 80 lines: run_dsp_biquad_tests 49, run_dsp_butterworth_tests 43, run_dsp_tuning_allpass_boundary_tests 37, BwRef 37; functions_over_limit 98 -> 98. Checks that cannot fail: tests/main.cpp:338 (F2) and :459 (M2, the closed form against itself through the node).

## 4. Verdict

Not closeable as is; closeable after a short fix wave. The arithmetic claim holds: every merged site is the deleted lines' operations in the deleted lines' order, the twins prove it at the float level and the gates at the 16-bit level, and no render in scope moves. What must change first: M1 (the record of the one departure is wrong about its extent — one sample claimed, a 1-LSB tail over the rest of every legato note measured; correct §1 and ruling 8, add the EKS synthetic, and put the keep-bytes alternative in front of Matt now that its size is known) and M2 (retype the old `phase_delay_at` as a reference; the present check is circular). Cheap and clearly right for the same wave: I1 (merge or rule the performer's two smoothers), I2 (reset twins), I3 (delete `OnePole::value()`), F2 (drop the tautology), F6 (cite the backlog-79 six as pre-existing). I4, I5, I6, I7 and the flags can be listed in section 7 and left.

---

**What was done about each** (the review wave, `b3c6160`, tests first, mutation-tested):

| Finding | Done |
|---|---|
| M1 the departure's extent | The record corrected, not the code: section 1 and ruling 8 carry the measured extent; the refuter's patch is the departure set (`synth_departure.txt`), A/B'd old against new (1 DIFFER, as listed); the keep-bytes alternative decided under the 11-02 rule and named in REVIEW R10 without a question. |
| M2 `phase_delay_at` without a reference | The deleted closed form retyped as a lambda in `run_dsp_biquad_recipe_tests`, two coefficient sets (one with a negative b0 for the polarity fold), four frequencies, `same_bits`; the delegation check kept. Mutation: the sign of `ni` flipped → four FAILs (main.cpp:470). |
| I1 the performer's two smoothers | Merged: `dsp::OnePole wheelSm_, pressSm_`; `run_dsp_performer_smoother_tests` over 300 ticks against the deleted lines. |
| I2 reset and set under-tested | `run_dsp_reset_tests`: each stateful primitive driven five samples, reset, compared with a fresh one over 200; `DcBlocker::set` at 60 Hz / 44.1 kHz against the formula. Mutation: `TuningAllpass::reset` clearing y1 only → FAIL at main.cpp:611 (the boundary test caught it too). |
| I3 unused API | `OnePole::value()` gained the performer; the three unused `BiquadCoefficients` getters kept as the set, said so in section 7. |
| I4 `float k, float n` | `int k, int n`; the builder and `FMSource` pass ints; the twin test too (bit-identical: small integers convert exactly). |
| I5 three copies of 0.98 | `dsp::kBowFrictionMax` beside the curve; the two node constants deleted; the bow table's `maxOutput` defaults to it. |
| I6 resonance mode shifts x1/x2 | Named in section 1 as a live-edit difference. |
| I7 sections set for the live count | `init_note` sets all `kMaxCombs` combs' sections. |
| F1 the gates and the sign of zero | Section 1's wording: the gates hash 16-bit WAVs; the twin test is the evidence; the first-divergence condition stated as such. |
| F2 a check that cannot fail | Removed with its counter. |
| F3 coverage | `fm_oversample.json` in the synthetic set (identical); `eks_stiffness.json` as the departure set (differs, as listed). |
| F4 `TuningAllpass`'s name | CLEANUP row 15 (`FirstOrderAllpass` if Matt prefers the thing's name to its first use). |
| F5 two conventions | Ruling 6 says they are two on purpose. |
| F6 the gates' FAIL lines | Section 5 cites the backlog-79 six by name as pre-existing. |
| F7 the other π literals | Section 7. |
| F8 the dropped wavetable node test | The Ledger's Task 3 note; section 6's execution rulings. |

**Re-review** (the same refuter, by message, on `2f0a755..b3c6160`; `renders/scratch/ab33/review_r10_rereview.md`), verbatim:

---

# Checkpoint 3.3 re-review (R10) — the fix wave, commit b3c6160

Reviewer: the same independent reviewer, read-only. Read `git diff 2f0a755 b3c6160 -- engine tools` in full (8 files, +136/-30), the report `docs/autonomy/refactor/reports/2026-11-02-checkpoint-3.3.md` (untracked, being edited while I read: `{{AB2}}` at line 87 was present at my first read and gone at my second), the plan's Ledger, REVIEW.md R10, CLEANUP row 15. Ran `engine_tests.exe`: ALL PASS (7481). Working-tree engine/ and tests/ match b3c6160 (git status: only docs modified). Line cites are the working tree.

## 1. Each finding against its fix

| Finding | Asked | Done | Verified at | Verdict |
|---|---|---|---|---|
| M1 departure misdescribed | correct §1 and ruling 8; add the EKS synthetic; put the keep-bytes alternative to Matt | §1 (report:13) now carries the measurement (2,486 of 153,600 samples, 1 LSB, second note on, stiffness 0 identical) and the recirculation reason; ruling 8 (report:199) says the plan's "one sample" was wrong and cites the measured extent; `synth/eks_stiffness.json` is my patch verbatim (same params, score, polyphony 1) in `synth_departure.txt`, A/B'd: `departure_old` 6bd4a905…, `departure_new` 20b7154a… — the same two hashes my review renders produced; the keep-bytes alternative decided against under the 11-02 rule and named in REVIEW R10 ("Decided, named here and not asked: … (1) EKS's …", REVIEW.md:26-40) | report:13,199; synth_departure.txt; departure_*/hashes.txt; REVIEW.md:26 | Closed. The rule lets the session decide it; it is named, not hidden. |
| M2 phase_delay_at circular test | retype the old closed form as a reference; two sets incl. negative b0; four frequencies; keep the delegation check | `reference` lambda at tests:448-464 is the deleted biquad_source.h closed form line for line (`w = 2*pi*hz/sr`, c1/s1/c2/s2, nr/ni/dr/di, the two folds, `-ph/w`; `sr` in place of `float(sample_rate())`, which was 48000.0f); sets `{0.3,-0.6,0.3,-1.7,0.8}` and `{-0.4,0.2,0.1,-0.5,0.3}` (negative b0, exercising the fold) at 55/440/3000/12000 Hz with `same_bits` (tests:465-471); delegation check kept (tests:480) | tests:448-471,480 | Closed. The reference is a different function from the primitive, so it can fail; the stated mutation (sign of `ni`) would land on :470. |
| I1 performer smoothers | merge or rule | `dsp::OnePole wheelSm_, pressSm_` (perform_source.h:101-102); `tick()` calls `process(load, smoothA_)` (:78-79) — the same `y += a*(x - y)`; `wheel()`/`pressure()` return `value()` (:91-92); twin `run_dsp_performer_smoother_tests` (tests:637-658) drives 300 ticks through `InstrumentState`'s atomics against the deleted lines and the deleted coefficient line | perform_source.h:78-79,91-92,101-102; tests:637-658 | Closed. No scope patch wires wheel/pressure (grep), so the twin is the evidence; the form is exact. |
| I2 reset/set under-tested | reset twins (>= 3 samples driven), DcBlocker::set at a second corner | `run_dsp_reset_tests` (tests:596-635): `reset_mismatches` drives 5 samples (deeper than any memory: Butterworth x_[3]), resets, compares with a fresh instance over 200 — DcBlocker at 60 Hz/44100 (:598-601) plus the deleted formula at that corner (`r*1 + 0 - 1` after a 1.0 then 0.0 input, the primitive's operand order, :602-607), TuningAllpass (:609-611), Biquad (:613-617), ButterworthSection highpass (:619-623), OnePole (:625-634) | tests:585-635 | Closed. The TuningAllpass y1-only mutant now fails :611 (x1 differs from the first compared sample); the Butterworth x_[2] mutant fails :623 (five samples fill all three). |
| I3 unused API | delete `value()` or give it a caller; say what the getters are | `value()` has its caller (perform_source.h:91-92); `b0()/b1()/b2()` kept as the getter set, said so (report:243) | perform_source.h:91-92; report:243 | Closed. |
| I4 float counts | `int k, int n` | butterworth_section.h:19 `(Kind, int k, int n, float)`; builder :87 `i + 1, count * 2`; fm_source.h:123 `i + 1, nSec * 2`; the twin still compares against `BwRef`'s float form and passes, which is the bit-identity proof (int and float(int) are the same double in `2.0 * k + n - 1.0`) | butterworth_section.h:19,87; fm_source.h:123; tests:539-540 | Closed. |
| I5 three copies of 0.98 | one constant beside the curve | `dsp::kBowFrictionMax` (bow_friction.h:15-17); KSString (ks_string.h:234) and the bowed string (wave_evolution.h:493) use it, their constants deleted; BowTable's `maxOutput` default is it (bow_table_source.h:21); grep: no `kFrictionMax` left, the only other `0.98f` in engine/ is `hpR_`'s unrelated default (ks_string.h:512) | bow_friction.h:15-17; ks_string.h:234; wave_evolution.h:493; bow_table_source.h:21 | Closed. Same float, so the BowTable descriptor default is unchanged. |
| I6 resonance shifts x1/x2 | name it in §1 | report:13, last sentence ("Two live-edit edges, neither a render: …") | report:13 | Closed. |
| I7 sections set for the live count | set all `kMaxCombs` | ks_string.h:435-440 loops `kMaxCombs` before the `numCombs` loop (:442), after both `dispEff_` writes (:364,389) | ks_string.h:435-442 | Closed. For `i < numCombs` the set is the one the task commit made; for the rest the sections are set and not run, so no render moves (spot A/B below). |
| F1 gates and the sign of zero | wording | report:13 says the gates hash 16-bit WAVs, the twin is the only evidence, the first-divergence condition is stated as such | report:13 | Closed. |
| F2 tautology | drop | `badProduct` and its CHECK gone (tests:323-346) | tests:323-346 | Closed. |
| F3 coverage | EKS and FM oversample synthetics | `synth/fm_oversample.json` (FMSource, oversample 2, two notes) in `synth.txt` (ten entries); `synth_old` re-rendered 10/10 with cli_pre33.exe; `synth_t8` 10/10 identical to it (I diffed the hashes.txt files); EKS as the departure set | synth.txt; synth_old vs synth_t8 | Closed. |
| F4 name | Matt's call | CLEANUP row 15 (`FirstOrderAllpass`, "stands, 2026-11-02") | CLEANUP.md:28 | Closed as a recorded call. |
| F5 two conventions | say it is on purpose | ruling 6 (report:197) says so with the reason | report:197 | Closed — but see §2(b). |
| F6 backlog-79 six | cite as pre-existing | report:84 names the six, "the same six as at 3.2, pre-existing" | report:84 | Closed. |
| F7 pi literals, F8 wavetable test dropped | words | report:211 (read-head reason); the pi-literal line is in section 7 per the table at report:381-385 | report:211 | Closed. |

## 2. Anything introduced?

Code: nothing. The wave's engine edits are the six listed above; each is either a rename of a constant to the same float, an int for a float that converts to the same double, a reset-free member swap with the same per-sample arithmetic, or a superset of a per-note set. Evidence beyond the session's: (i) `synth_old` vs `synth_t8` identical (10 of 10, my diff); (ii) the departure arms' hashes equal the two my own review renders produced; (iii) my spot A/B of five scope patches with the post-commit CLI (mtime 20:15, after the 20:12 commit) against `cli_pre33.exe`, into `renders/scratch/ab33/review/`: `piano_default` (KSPianoString: the kMaxCombs loop, kBowFrictionMax), `bassoon1` (BW lowpass, int counts), `fm_bell_test` (FM, int counts), `bow_evolution_discovery` (the bowed string's kBowFrictionMax), `hi_hat1` — all five identical. The recorded `new` arm (render_new2.txt: 132 identical, 0 DIFFER) was rendered at 20:07, before the commit timestamp; the engine tree did not change between, and the spot A/B confirms with the later binary. No new function over 80 lines (reset tests 41, performer 22, biquad recipes 52). No new include against the map (the tests include `render/perform_source.h`, allowed).

Report (untracked; being edited): three small inconsistencies the wave left, each a one-line edit before the docs commit —
(a) report:211 still says "`kFrictionMax = 0.98f` is each node's constant, STK's maximum" and "KSString's dispersion sections are set in `init_note`'s comb loop" — both true at the task commit and both superseded by the wave (`dsp::kBowFrictionMax`; all `kMaxCombs` combs before the comb loop). Say "at the task commit" or strike.
(b) ruling 6 (report:197) still ends "No live-edit behaviour changes." while §1 (report:13) names two live-edit edges (the resonance-mode memories; the `numCombs` case, now closed by the wave). Make the sentence "none from the per-sample reads; §1 names the two edges".
(c) `{{AB2}}` at report:87 was present at my first read and gone at my second; the value it needs is render_new2.txt's "132 identical, 0 DIFFER".
Also for the record: `{{REREVIEW}}` at report:390 is this file's slot.

## 3. Verdict

Closeable. Every Major and Minor was closed as asked at the cited lines; nothing was introduced in code; the departure is now measured, rendered in a departure set and recorded with its true extent, and the decision not to keep bytes is named for Matt under the standing rule rather than hidden. Before the docs commit: the two stale clauses at report:211 and the tail of ruling 6 (report:197), and the `{{AB2}}` figure if it is not already in.

---

Its three one-line asks (two stale clauses in the execution rulings, ruling 6's tail, the post-wave A/B figure) were made in this file before the docs commit; nothing else changed after the wave, so the seal is `b3c6160`.

## 9. Reading guide

Ten minutes:

1. `engine/include/mforce/core/dsp/biquad.h`, all of it — the shape every primitive follows, the split (coefficients apart from state) and the five recipes with the formulas they came from.
2. `engine/include/mforce/source/physical/ks_string.h`, `compute()` from the bow junction to the comb loop — four primitives in a dozen lines where there were forty.
3. `engine/include/mforce/source/filter/filters.h` — what is left after the section moved out: three filters that build a cascade of a kind.
4. `tools/engine_tests/main.cpp`, `run_dsp_biquad_tests` — a twin test: the deleted lines as a struct, the primitive beside it, bits compared, and the one bounded class (`badBits <= negZero`).
5. Section 6's comparison table, for the sites where "exact" needed an argument.
6. Section 10, lessons 3 and 4.

## 10. C++ lessons

### Lesson 1: a member held by value, and why "has a" costs nothing here

`dsp::DcBlocker dc_;` inside `KSString` is the composition Part 3 §4 asked for — a string *has* a DC blocker — and in C++ it is also a statement about memory: the three floats of the blocker sit inside the string object, at a fixed offset, with no pointer, no allocation and no indirection. A C# `class DcBlocker` member would be a reference to a separate heap object; a C# `struct` would be the nearest match, with the difference that C++ makes no distinction between the two kinds of type — any class can be held by value, and the author chooses at the member, not at the type. That is why the real-time rule is met without a word: `KSString`'s constructor does not allocate for its primitives because they are not objects of their own. The alternative — `std::unique_ptr<DcBlocker>` — would buy nothing (there is no polymorphism to hide, no optional presence) and cost a heap allocation per node and a pointer chase per sample. The rule of thumb for a C# reader: a member that is always present, has no subtypes and is small is a value member; a pointer is for the cases where one of those is false.

### Lesson 2: a function in a header, `inline`, and the one-definition rule

`blep` and `bow_friction` are free functions, not structs, because they hold nothing between calls (ruling 2). A function defined in a header is included into every translation unit that uses it, and C++'s one-definition rule says a function may be defined once per program — so the linker would see `blep` five times and refuse. `inline` is the word that lifts that rule: it tells the compiler "every copy is the same; keep one". It is not a request to inline the call (the optimiser decides that on its own, and does so here). C# has no equivalent problem because methods live in exactly one assembly; Java the same. The nearest analogue is a `static` method on a `static class`, and the C++ idiom for that is a namespace: `dsp::blep(pos, incr)` reads like `Dsp.Blep(pos, incr)`, with the namespace doing the job the static class did and the function free to be a function. `inline constexpr float kPi` is the same word on a constant: one object across the program, usable in constant expressions.

### Lesson 3: what float arithmetic promises, and what a bit-for-bit test can ask

The whole checkpoint rests on four facts about IEEE arithmetic, each of which the comparison table in section 6 uses. Every operation rounds its exact result once to the nearest float, so two expressions with the same operations in the same order give the same bits on any machine. A product commutes exactly (`a·b` and `b·a` are the same rounding of the same exact value), which is why `gain·vRel` and `(vRel + 0)·gain` agree under `fabs`. Negation is exact and `x − y` is `x + (−y)` exactly, which is why `− (2a)·x1` and `+ (−2a)·x1` agree and the double-real-pole allpass could be written as the general section. Association does not hold: `(a + b) + c` and `a + (b + c)` round differently, and that is the whole reason the merges keep the deleted lines' order, why `ButterworthSection::process` still starts its accumulator at `0.0f`, and why the test's own expectation `r·0.5` for `(r·0.5 + 0.5) − 0.5` was wrong (Task 2's ledger note). And zero has a sign: `0·x1` is `−0` for a negative `x1`, `(−0) + (+0)` is `+0`, so adding a zero product can flip the sign of a zero and nothing else — which is why `BiquadSource`'s resonance mode is compared as values where it adds two such products and as bits everywhere else, and why `twin_signal` salts in zeros of both signs so that class is met rather than assumed. C# and Java use the same IEEE arithmetic; the difference is that neither offers `/fp:fast`, while a C++ build can ask the compiler to reassociate — this tree does not, and the plan says so in its Tech Stack line, because the day it did the comparison tables would stop being proofs.

### Lesson 4: two initialisation traps the tests met

`std::vector<float> v(size_t(n));` does not make a vector of `n` floats; it declares a function `v` taking a `size_t` named `n` — C++'s "most vexing parse", where anything that can be read as a declaration is one. The fix was `std::vector<float> v(static_cast<size_t>(n));`, which cannot be a parameter declaration. And `std::vector<float> fir{3, 0.0f};` does not make three zeros; braces prefer the initializer-list constructor when one fits, so it is a vector of two elements, `3.0f` and `0.0f` — the test then indexed `fir[2]` off the end and the binary died without a FAIL line (exit 127, Task 7's note). The fix was `std::vector<float>(3, 0.0f)`, the constructor call in parentheses. A C# reader expects `new List<float>(3)` to mean capacity and `new List<float> {3, 0}` to mean two elements, which is the same split; the trap is that C++ lets braces reach the sized constructor too when no list constructor matches, so the same `{3, 0.0f}` on a type without one would have meant "three, zero" — the meaning depends on the type, and the honest spelling is parentheses whenever a count is intended.
