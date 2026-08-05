# DSP lane — backlog (Dipsy)

Priority order. Tags per WORKFLOW.md. (G1)-(G4) = GOALS.md Dipsy goals.

1. **[build] Commit the fable1 thread + autonomy scaffold** — motion layer,
   cutoff fix, UI array-restore fix, patches/generators/docs, docs/autonomy/.
   Scoped commits per concern. Unblocks clean iteration on everything else.
2. **[metric] (G1a) Derive motion-layer params from Iowa samples** — measure
   per-harmonic line width (frequency jitter spectrum) and amplitude-
   fluctuation rate/depth/cross-harmonic correlation from the Iowa viola set;
   map measurements onto motionDepth/motionHz/motionCoherence/shimmer*;
   produce an "iowa_grounded" patch vs the hand-tuned v4 winner. Queue A/B.

3. **[metric] (G1a) CMA-ES optimizer core** — the ears project, staged:
   (a) spec ✓ run2; (b) scorer CLI ✓ run3 (refmetrics.py + iowa_reference.py +
   score_candidate.py; stage-a check passes); (c) optimizer loop ✓ run3
   (cmaes.py self-tested + optimize.py, 27-dim warm-started encoder, 100-eval
   smoke 1.28→0.945, every term improved); (d) **viola validation run** — the
   600-eval run whose best-of-run WAV goes to REVIEW. NOTE (run 4): Matt's freq-curve verdict adds curve knots as search dims (+4-6) once default curves are picked from the v6 audition. **Preliminary result
   queued** run3: smoke best (104 evals) re-rendered on the full ladder →
   REVIEW A/B (renders/cmaes_smoke/opt_best_104ev vs v5_04). **Blocker EASED
   run14:** option (iii) happened — v6_cmaes_best renders 16.1s → 8.9s (1.81x)
   from the truncf + fast_exp2 commits, so 600 evals is now ≈50 min rather
   than 1.5-2.7h. Options (i) --resume and (ii) maxPartials 48 remain
   available if that is still too long. **NEW blocker surfaced run14:** the
   cutoff-gate/shared-rng fragility (REVIEW 0b) makes the objective
   DISCONTINUOUS — a last-bit parameter change can re-roll the bandwidth noise
   the candidate is scored on. Worth settling before spending 50 min of
   search. Still GATED on Matt's A/B read (metric pointing right? re-pin
   vibrato?) before committing the full run.
3b. **[build] UI support for paramMap curves** — ✓ DONE run5 (2026-07-29).
   Minimum landed: load stashes the paramMap verbatim, save carries forward
   curve-bearing entries remapping node ids through the rename map, so
   load→save preserves curves + config targets and keyboard playback (temp
   file → CLI loader) honors them. Verified via new headless `--roundtrip`
   mode on all four v6 patches. v6+ patches are now safe to re-save from the
   UI. **Still deferred (Later):** a visual curve editor on Parameter node
   links — until then, editing a curve-bearing Parameter node's wiring in the
   UI is not reflected (verbatim entry wins).

3b2. **[metric] Shimmer gain floor** — Matt-approved (2026-08-01): the
   shimmer walk at optimizer-chosen depth visits near-silence mid-note
   (measured 14.6dB dip-and-reswell on v6_cmaes_best final note). Add a
   floor config (gain never below ~0.3-0.5), render a small ladder on
   the cmaes-best patch, queue A/B. Statistics note: keep total variance
   near the Iowa 50% target if possible (floor redistributes, not
   removes).
3c. **[build] UI engine-stamp guard** — ✓ DONE run14 (2026-08-01), commit
   cc4185d. **FALSE-POSITIVE FIXED run18 (2026-08-03), commit 9c2946e:** it
   compared the exe against all 116 engine sources, but mforce_ui depends on
   59, so a comp-lane edit to composer.h/passage_strategies.h shouted STALE
   with no rebuild able to clear it (Wolfie, 12d405e). Dependency set now
   comes from MSBuild's own CL.read tlogs for mforce_ui + mforce_engine
   (73 files, and it now covers tools/mforce_ui/main.cpp, which the
   engine-only scan never checked); falls back to the coarse scan when no
   tlogs exist, and --stamp names the mode. Detection extracted to
   tools/mforce_ui/build_stamp.h so a locked (running) exe can still be
   checked; new tools/stamp_test includes it directly + ab_stale_guard.ps1
   A/Bs old vs new on the live tree, 3 directions, restoring every mtime it
   touches. **✓ FULLY CLOSED run19 (2026-08-04)** — run 18's "NOT YET RELINKED"
   was pessimistic. No UI process was running at run-19 start and the on-disk
   exe already carried the fix (`--stamp`: `dep set : tlog (74 files)`,
   `stale : no`, exit 0), so the rename-then-link trick had worked; the
   `mforce_ui_running_backup2.exe` in the build dir is its residue. Both
   targets relinked and `--stamp` re-verified at every cycle close this run.
   Title bar carries `[build MM-DD HH:MM @sha]` always, plus
   `*** STALE - REBUILD ***` and a dismissable red banner naming the offending
   file when the exe is older than the newest engine .h/.hpp/.cpp. Commit read
   from .git/HEAD, repo root found by walking up from the exe — no build-system
   change, no git binary, so the check cannot itself go stale. New headless
   `mforce_ui.exe --stamp` (exit 1 when stale) makes it verifiable without the
   GUI; both directions tested. Also fixed: printf from --stamp/--roundtrip was
   invisible from a console (WIN32 subsystem app), now attaches the parent
   console unless stdout is already redirected.
3c2. **[build] FMSource phase param is dead** — ✓ DONE run19 (2026-08-04),
   commit fbbcb44. Inertness proven byte-exactly first: t1_06 and t1_07 each
   rendered IDENTICAL to a twin with `phase` deleted. Fixed by applying phase_
   as a carrier-side OFFSET (matches the base-class contract — currPos_
   telescopes to phase_current + sum(incr), so phase is an offset, not an
   integrated delta; never fed back into carrierPhase_, which would integrate
   the modulator twice). tools/null_test_fm.py PASS: 131 phase-unwired patches
   byte-identical (double(x)+0.0 is exact), all 4 wired ones differ. New
   research/fm_alias/measure_pm.py shows real sidebands, not just a different
   file: t1_07 centroid 1.40x / bw 1.19x; t1_06's slow sweep is a frequency
   deviation so it smears each partial into a 1.7 Hz cluster, 6 -> 109 peaks.
   Spec: specs/2026-08-04-fmsource-phase-pm-design.md.
   Also fixed t3_23_phase_velvet_jumps, which the null test caught as STILL
   DEAD — patch-side, not engine: RangeSource(-1,1,normalized=false) is the
   identity and VelvetNoiseSource emits {-1,0,+1}, so every offset was an
   INTEGER number of cycles. Now density=20 with +-0.5 cycle jumps.
   **REVIEW 7:** the run-12 audition verdict on t1_06/t1_07/t3_23 needs
   re-taking — those three were judged as something they were not.

14. **[build] 52 remaining silently-ignored params** (from run19's linter,
   `python tools/lint_patches.py`). First run said 62; 3 were FALSE POSITIVES
   (`RepeatingSource` `gap`, `PhasedValueSource` `overlap` — correct JSON
   consumed by JsonConfigurator lambdas in source_registrations.cpp, which the
   first SPECIAL_KEYS extraction never scanned), and 7 were the wander cells
   fixed in run19. None of the 52 fixed blind. Triage:
   (a) `AdditiveSource` fed rolloff/evenWeight/oddWeight/freqVar*/amplVar* in
   add_square_test + add_string_test — pre-migration debt (those belong on
   Partials), which contradicts the 2026-05-30 "no remaining debt" note;
   (b) `AdditiveSource2` fed amplEnvelopes/startPartials/endAmplitudes/... in
   5 as2_*_test patches — same species;
   (c) `Envelope` fed `releaseMax` in 6 algev_test_* — preset is `ar`, whose
   release stage always expands to fill the note, so release/releaseMax are
   both meaningless there. Either drop the keys or give `ar` a real release;
   Decide per group whether the patch or the engine is wrong; (a)/(b) are
   probably just stale test patches that should be regenerated or deleted.

15. **[build] 7 patches the CLI cannot render at all** — ✓ 6 of 7 DONE run19
   (2026-08-04), commit 6fc128b. Found while A/B-ing front 3. They were
   THREE distinct bugs, not one:
   (1) a bare mono source as `graph.output` was a hard error, though the
   instrument path already auto-wraps one — standard path now matches it
   (unity volume, centre pan, so it inherits the -3dB equal-power pan of
   item 3d). Fixes mux_{noise,rednoise,sine}_test;
   (2) `wire_params_generic` threw on any STRING in a pin/param slot, but a
   string there is always a legacy enum consumed by a hand-written branch
   further down — WavetableSource's `"evolution": "target"` is ALSO an input
   descriptor for the ref-wired form, so the generic loop killed the patch
   before its own special case ran. Strings skipped; other junk still throws.
   Fixes ks_morph_{flute,horn,saw}_test;
   (3) CombinedSource read `operation` as a string only, so legacy ordinals
   died on a raw json type_error. Both forms now accepted.
   **STILL OPEN — CombineTest.json**: it says `"operation": 3`, and no current
   CombineOp matches (0=add, 1=multiply, 2=fade). Deliberately NOT defaulted
   to Add — silent fallback is the exact failure mode this run kept finding.
   It now fails with a named, actionable error. **Needs one word from Matt**,
   who wrote the C#: what was ordinal 3? (REVIEW 10.)
   Regression-gated: 73/73 byte-identical vs the stored A/B arm, with exactly
   the 6 revived patches present only in the new arm.
3e-NEXT. **[metric] Piano: full run + scorer debt** — smoke landed run 20
   (1.521 -> 0.900). GATED on Matt's A/B (REVIEW 13): full 600-eval run.
   Scorer debt found by the smoke:
   (a) ✓ **DONE run21** (2026-08-05), commit 85a4015. broadband_ratios blew
   up when a band held no harmonic: `between_e / 1e-12`, a noise floor
   inflated by 12 decades. Live in the stored reference — piano C6 band0 was
   **4.63e+11** vs O(1) elsewhere, and C6 (1046.5 Hz) was the only eval note
   above BANDS[0]'s 700 Hz top, so the smoke's term3 was dominated by that
   one cell. Empty bands now return NaN, score_candidate nanmeans over them.
   Regenerating out/piano_reference.json changes C6 band0 to nan and leaves
   every other value identical to the digit.
   research/ml_ears/test_broadband_guard.py keeps the pre-fix formula inline
   so the blowup is shown, not asserted (1.325e13 at f0=880), and checks
   populated bands come back bit-identical.
   (b) **OPEN, and BIGGER than first written**: harmonic_env/motion
   heterodyne at k*f0, not the B-stretched positions. Run 21 found this also
   hits `broadband_ratios` — its line mask is at k*f0 too, so partials
   stretched by sqrt(1+B n^2) fall OFF the mask and get counted as
   inter-harmonic energy. That is the likely explanation for piano band2
   ratios of 120 (C3), 260 (C5), 2056 (G4). Stretch-aware heterodyne + a
   stretch-aware line mask. Top non-gated item.
   Velocity layers (mf-only today) stay Later.
3f. **[build] CombinedSource JSON op string "sum" silently falls back to
   Add** — ✓ DONE run21 (2026-08-05), commit b08d795. CombineOp gained Sum in
   run 20 but the loader never learned it: the string parser knew only
   add/multiply/fade and fell back to Add on anything else, and the ordinal
   switch threw on 3. Now add/mix/multiply/fade/sum parse, ordinal 3 maps to
   Sum, and anything unrecognised throws a NAMED error rather than quietly
   becoming Add (the rule run 19 set). Verified: "sum" and ordinal 3 render
   byte-identical (C45E9F8E) and differ from Add (BF396D58); "bogus" and
   ordinal 9 exit 1 with their error text. No existing patch affected — the
   only `operation` values in the repo are multiply (2), add (2), 0 (1).
   The separate linter check was dropped as redundant: the engine now fails
   loudly, which is strictly better than a static warning.
3g. **[build] SlewLimiterSource** — ✓ DONE run21 (2026-08-05), commits
   b08d795 (engine) + 5592354 (ladder). Spec:
   specs/2026-08-05-slew-limiter-design.md. Category Filter, modes
   Slew / Lag / **Peak**, `rate` + `fallRate` as ValueSources, UI menu entry
   under Filters.
   **The item's premise was wrong and measuring caught it**: the clicks are
   one-sample IMPULSES (0, ±0.5, 0), not steps, so a sign-keyed rate limiter
   just attenuates them to height rate*dt and they stay one sample wide.
   Proven in step_slew_impulse — Slew holds a POSITIVE impulse 10.00 ms and a
   negative one 0.02 ms (one sample). Peak mode is magnitude-keyed (attack
   while |in|>|held|, else decay toward zero at fallRate) and symmetric.
   tools/verify_slew.py vs patches/slew_test/ ALL PASS: Slew rise within
   0.5-1.9% of S/rate at 0.002% nonlinearity (a straight ramp); Lag tau
   within 0.05% at 33% nonlinearity (correctly not straight); fallRate
   asymmetry 10.05x vs 10x; Peak 10.00 ms glides both polarities.
   Ladder (patches/slew_clicks/, renders/slew_clicks/) → **REVIEW 15**:
   attack centroid 7116 → 3237 Hz (0.455x) as fallRate falls 20000 → 50,
   >8 kHz share 37.9% → 13.6%, monotonic, 6/6 distinct by sha256.
   Note the dial is NOT a lowpass: the glide is 0.5/fallRate s and a linear
   phase glide is a constant frequency offset, so fallRate trades chirp
   LENGTH against a fallRate/2 Hz deviation.
3e. **DONE run 20** (was: [build] Piano engine features (from measurement)) — (a)
   `inharmonicity` config on Partials: mults stretched by
   sqrt(1+B*n^2) at note-on; per-note B via the existing paramMap
   frequency-curve mechanism (B spans 225x across the keyboard,
   measured laws in out/piano_analysis_report.md). (b) per-partial
   decay: `decayRate`+`decayExp` configs (rate_i = rate*pmult^exp),
   prepare-cached per-sample gain. Then piano encoder (~14-18 dims,
   B+decay laws LOCKED from measurement per the clarinet-bed pattern;
   knock = graph-level noise burst, no engine feature) + first CMA-ES
   smoke. Prereq: iowa_reference onset-alignment (sample lead-ins vary
   0.04-0.54s).
3d. **[read] Pan-law question for Matt** — CLI WAVs are -3dB vs UI (equal-
   power center pan in StereoMixer vs unity mono). Option: mono patches
   write x1.0 to both channels so WAV loudness == UI loudness.

4. **[metric] (G1b) Novelty metric** — ✓ DONE run5 (2026-07-29).
   research/novelty/: embedding.py (36-dim MFCC + spectral-feature vector,
   numpy/scipy only) + novelty.py (build/score/manifest/selftest). Distance-
   to-nearest-library-neighbour in z-scored space. Wired into explore via the
   `manifest` subcommand (augments an --explore manifest.json with novelty +
   re-ranks; Python-side because the C++ filter carries no library ref).
   selftest PASS + real-render ordering validated. Gate for G2/G3 sweeps is
   now available.
5. **[metric] (G2) Recursive partial-expansion regime sweep** — ✓ FIRST BATCH
   done run5 (2026-07-29): 18 regimes (incl. 6 rule-breakers) generated
   (tools/gen_expand_sweep.py), rendered, novelty-ranked
   (tools/_run_expand_sweep.py); survivors queued for listen (REVIEW).
   Rounds 2-3 followed Matt's verdicts (gen_expand_sweep2/3.py).
   **✓ REOPEN DONE run18 (2026-08-03)** — depth 3-4 finally affordable
   (cost re-measured: LINEAR at ~17 ms/partial for a 4 s render, 6 cells
   216→5000 partials, which also confirms the base·(2c+1)^(r+1) formula).
   18 patches (tools/gen_expand_sweep4.py), 16 rendered in 285 s,
   novelty-ranked + a purpose-built depth-control comparison
   (tools/_run_expand_sweep4.py). **Answer: depth is second-order.**
   Holding partial count AND total spread fixed, halving depth moves the
   embedding 4.36, vs a batch median pair of 18.98 and loPct's 50.46 — i.e.
   about the size of a `power` tweak. Recurse 3-4 is available and cheap
   now, but it is not where the sound is; the levers are loPct/spacing/
   rule-breakers. AWAITING Matt's listen (REVIEW 4) to close the item —
   if he agrees, this front retires rather than getting a round 5.

12. **[build/review] `power` inert at count=1 in apply_expand_rule** — found
   run18: side-partial amplitude is `ampl*loPct + ampl*(1-loPct)*pow(t,power)`
   with `t = j/count` (left) / `(count-1-j)/count` (right), so at count=1
   t≡0, `pow(0,p)=0`, and power does nothing — every side partial lands on
   the loPct floor. Surfaced because peaked_deep rendered BYTE-IDENTICAL to
   micro_r3; proven live by the regression pair taper_flat_c2/taper_steep_c2
   (identical but for power, at count=2, and they differ: distinct sha256,
   distance 4.96). Not a coding bug — a defensible reading — but a trap that
   silently cost round 4 a cell. Options in REVIEW 5: leave it documented, or
   renormalise t (e.g. `(j+1)/(count+1)`) so count=1 is non-degenerate, which
   changes every existing expand patch. GATED on Matt; engine edit.

13. **[metric] Piano unison beat RATE — attempt 3, different method** —
   run18 attempts 1 and 2 both failed to resolve the rate (2-attempt rule
   → backlogged, not retried inline). Attempt 1: 3 s window, every partial
   "beat" at FFT bins 1-2 (decay curvature). Attempt 2: 8 s window +
   4-cycle floor + high-passed residual — depth control passes cleanly but
   a built-in floor-sensitivity check shows 9 of 11 rates CLIMB when the
   floor is raised 0.50→0.75→1.12 Hz, so the rate is a window artifact.
   Only C5/C6 hold (~1.9-2.0 Hz). Envelope-domain inference is the wrong
   tool. NEXT METHOD: resolve the unison strings as SEPARATE spectral lines
   — they are split by the beat frequency (0.6-2 Hz), which a ≥2 s coherent
   FFT resolves directly in the bass/mid — and read the split, don't infer
   it. What DID survive run18 and is usable now: single-strung B0/C1
   modulate at 0.016-0.029 vs 0.106-0.285 multi-strung (5-9x), so
   shimmerDepth ≈ 0.15 is a defensible SEED. shimmerHz/shimmerCoherence
   stay searchable in the piano encoder unless Matt redirects (REVIEW 6).
   research/ml_ears/piano_beating.py, out/piano_beating.json + .png.
6. DONE run 6 — see reports/2026-07-29-dipsy-run6.md. **(was: [metric] (G2) FormantSequence deep-dive)** — modulated/sequenced formant
   motion as a first-class timbre animator; sweep + novelty-filter.
7. **[build→metric] (G3) Oversampled FM render path** — ✓ CORE DONE run8
   (2026-07-30). FMSource `oversample` config (Int 1..16, default 1);
   carrier+mod sin() at M·SR + 8th-order Butterworth decimation (BWLPSection).
   M=1 byte-identical (spacy family safe). Measured suppression M=2/4/8 =
   24.5/34.7/39.6 dB (research/fm_alias/measure.py; M=8 kills 99% of in-band
   alias residual). Spec: specs/2026-07-30-oversampled-fm-design.md.
   **STAGE 2 ✓ DONE run12 (2026-07-31):** 24-patch "modulate everything"
   matrix (tools/gen_fm_matrix.py → patches/fm_matrix/), 9 rule-breakers;
   both ratios / phase / frequency / amplitude modulated at sub-audio AND
   audio rate, FM driving FM's depth, modRatio swept through zero. Wiring
   verified by measurement (spectral centroid sd 99..2719 Hz across the
   batch), novelty-ranked, survivors → REVIEW 4.
   Also research/fm_alias/measure_patch.py — alias measurement for ANY FM
   patch. It found the oversample-default question has a wrong premise: the
   ladder converges at high CARRIER (M=1/2/4/8 → 0/2.7/5.7/12.5 dB) but does
   NOT converge at high INDEX on a low carrier (0/-0.0/-2.4/-2.3 dB). REVIEW 3.
   **Open:** whichever patch family the audition verdict picks out.
8. **[metric] (G4) Additive performance** — ✓ STAGE 1 DONE run8 (2026-07-30):
   CLI render timer + tools/prof_additive.py sweep.
   **✓ STAGE 2a+2b DONE run12 (2026-07-31)** — 1.2-1.6x wall clock, commit
   aafbe9b, spec specs/2026-07-31-additive-hot-loop-design.md.
   2a (bit-exact, 14/14 byte-identical null test): IPartials::sum_partials
   batches the loop behind ONE virtual call per sample (accumulator threaded
   so nested sets keep the addition order); per-sample scalar cache kills 8
   virtual current() calls PER PARTIAL; per-partial pmult/rolloff/motion-scale
   cache keyed on (multE,roE); fmod→truncf.
   2b (fast sin): sinf measured at ~40% of the remaining loop, replaced by a
   folded degree-9 minimax polynomial in turns (core/fast_math.h). Worst
   deviation 1 LSB @16-bit, residual RMS -125..-140 dBFS; MORE accurate in
   float32 than the std::sin(x*TAU) it replaces. No toggle.
   2c (timer) ✓: mforce_cli prints load= and total=. viola_default is
   load=2288ms vs render=16.6ms — profile instrument patches by load=.
   Gates: tools/null_test_additive.py, residual_test_additive.py,
   ab_render_time.py, ablate_additive.py.
   Now: marginal 47.6 ns/sample/partial, ~437 partials/core RT (on a box with
   mforce_ui holding a core — better when quiet).
   **✓ STAGE 2d PHASE 1 DONE run14 (2026-08-01)** — isolated AVX2 prototype
   (research/additive_perf/simd_proto.cpp) beats scalar **18.4-19.1x** on
   synthetic arrays, vs a 2x abort criterion. Stage greenlit but RESTAGED,
   because tools/ablate_layers.py (new) showed a layer-free vector path
   reaches only ~51% of viola_default's loop: the flagship patches run
   motion+shimmer+onset+bandwidth together, and bandwidth's SHARED-rng walk
   re-rolls (not perturbs) the noise if vectorized. New sub-stages 2d-1..2d-4
   in the spec. **2d-3 (per-partial rng streams) is gated on REVIEW 0b** and
   is arguably a prerequisite for the rest.
   **✓ ALSO run14 — two libm calls in the hot body, both landed:**
   `truncf` was a CRT call under SSE2-baseline flags → int round-trip, 1.6x,
   BIT-EXACT 14/14 (commit e31d936, marginal 41.7→26.1 ns/sample/partial);
   `std::exp2` in the motion path was 65.6% of viola_default's entire loop →
   core/fast_math.h fast_exp2 (0.88 float32 eps), 1.7x on motion-bearing
   patches, v6_cmaes_best 16.1s→8.9s (commit 8396d3a). Residual 1 LSB on
   13/15; the exception is REVIEW 0a.
   **STAGE 2d (open, >1 session):** explicit SIMD of the partial loop. Spec:
   specs/2026-07-31-additive-simd-soa-design.md. Ablation says the
   memory-bound floor for the current layout is 3.3 ns/sample/partial vs
   26.1 now (was 57.3 before run 14) — and that the gap is NOT explainable by
   arithmetic count. FOUR ANTI-RESULTS, don't retry any (all on controlled
   A/B; (iii)-(iv) from run 14):
   (i) branchless degree-13 full-period sin polynomial, no quadrant folding —
   a WASH (MSVC already compiles the folded selects branchlessly) and less
   accurate (-123.4 vs -133.3 dB);
   (ii) 4x unroll with 4 independent accumulators — consistently SLOWER
   (0.88/0.94/0.93x on the 32/96/200p ladder). The compiler was already
   scheduling across iterations; the unroll only cost register pressure.
   (iii) `pfreq / rate_` → multiply by a cached reciprocal: 1.10x in the
   isolated prototype but 0.89x IN THE ENGINE (25.0→29.6 ns/sample/partial at
   200 partials), and not bit-exact. Rejected on both counts; noted inline.
   (iv) bit-exact `t*t` fast path for `Formant::get_gain`'s `std::pow` (all
   579 formant instances use power 2): 1.00-1.01x, i.e. nothing. The
   `contains()` gate means only in-band partials reach it. Noted at the call
   site in formant.h.
   Consequence: the cheap explanations are closed off. Anything that moves
   this number has to be real vector arithmetic or nothing — so phase 1 is an
   isolated SIMD prototype on synthetic arrays with a 2x abort criterion,
   BEFORE touching Partials. Weigh against 2e before committing.
   **STAGE 2e (open):** iFFT overlap-add additive (1000s of partials cheap;
   structurally different synthesis, NOT bit-identical → review:listen).
9. **[build] Convert 6 algev patches to instrument-style** — ✓ DONE run8
   (2026-07-30). instrument+score added + output rerouted to bare source;
   all 6 render with sound via the shared instrument path.
10. **[review:listen] Vowel/formant re-tune after formantWeight refactor** —
    prep render set + gain sweep, queue for ears. (Largely overtaken by the
    run-11 vowel BASELINE set, REVIEW 1.)
11. **[build] Two standing dirty patch files** — ✓ **SHRUNK run21**: the
    Python half of this item is GONE. `score_candidate.py` and
    `iowa_reference.py` were committed by run 20 (72defb9), so HEAD can now
    run the clarinet CMA-ES pipeline on its own. What remains is only
    `patches/clarinet_c2/c2c_quiet.json` and
    `patches/fable1_v6/v6_01_res_curve_lo.json`, dirty at session start for
    seven runs now (a UI re-save of v6_01 and a clarinet variant). Left
    untouched again per the tree guard. One word from Matt: commit or revert.

## Done

- Partial motion layer + v1-v4 batches (docs/Fable1_results.md)
- 16 kHz cutoff break→continue fix; UI array-restore fix; 96-partial
  extrapolation
