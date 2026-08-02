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
   cc4185d. Title bar carries `[build MM-DD HH:MM @sha]` always, plus
   `*** STALE - REBUILD ***` and a dismissable red banner naming the offending
   file when the exe is older than the newest engine .h/.hpp/.cpp. Commit read
   from .git/HEAD, repo root found by walking up from the exe — no build-system
   change, no git binary, so the check cannot itself go stale. New headless
   `mforce_ui.exe --stamp` (exit 1 when stale) makes it verifiable without the
   GUI; both directions tested. Also fixed: printf from --stamp/--roundtrip was
   invisible from a console (WIN32 subsystem app), now attaches the parent
   console unless stdout is already redirected.
3c2. **[build] FMSource phase param is dead** — found by the matrix2
   batch: compute_wave_value uses its own carrierPhase_/modPhase_ and
   never reads the inherited phase_, so 'phase' modulation (t1_06, t1_07
   'PM' topologies) does nothing — those renders were plain FM,
   byte-identical across phase variants. Fix = apply phase_ to the
   carrier accumulator (true PM), THEN re-render the t1_06/t1_07
   topologies as actually designed and A/B.
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
   **recurse limited to 1** here — recurse 2-4 = base·(count·2+1)^(3..5) =
   thousands of partials, render-bound until item 8 lands. REOPEN at
   recurse 2-4 after additive perf.
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
11. **[build] Commit research/ml_ears/score_candidate.py + iowa_reference.py**
    — the config-driven instrument generalization is working-copy-only, but
    run 11 committed optimize.py and configs/clarinet_bb.json which DEPEND on
    `score_candidate.load_config`. HEAD alone cannot run the clarinet CMA-ES
    pipeline. Left untouched in run 12 per the tree guard (files modified at
    session start, not this run's work). One-line commit once Matt confirms
    nothing else is in flight on them.

## Done

- Partial motion layer + v1-v4 batches (docs/Fable1_results.md)
- 16 kHz cutoff break→continue fix; UI array-restore fix; 96-partial
  extrapolation
