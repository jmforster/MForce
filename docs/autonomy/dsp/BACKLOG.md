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
   REVIEW A/B (renders/cmaes_smoke/opt_best_104ev vs v5_04). **Blocker:** eval
   render-bound ~9-16s (96 partials × ~12s ≈ real-time); 600 evals ≈ 1.5-2.7h
   and a bg run dies at session end. Options: (i) long run split across
   sessions via --resume; (ii) maxPartials 48 during search + re-score at 96;
   (iii) render-speed (item 8) first. GATED on Matt's A/B read (metric pointing
   right? re-pin vibrato?) before committing the full run.
3b. **[build] UI support for paramMap curves** — ✓ DONE run5 (2026-07-29).
   Minimum landed: load stashes the paramMap verbatim, save carries forward
   curve-bearing entries remapping node ids through the rename map, so
   load→save preserves curves + config targets and keyboard playback (temp
   file → CLI loader) honors them. Verified via new headless `--roundtrip`
   mode on all four v6 patches. v6+ patches are now safe to re-save from the
   UI. **Still deferred (Later):** a visual curve editor on Parameter node
   links — until then, editing a curve-bearing Parameter node's wiring in the
   UI is not reflected (verbatim entry wins).

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
   **REOPEN (stage 2, [review:listen]):** the "modulate everything" FM matrix
   sweep through the novelty filter — taste-gated, not done blind. Also a small
   A/B (M=1 vs M=8 alias tone) is queued in REVIEW for the default decision.
8. **[metric] (G4) Additive performance** — ✓ STAGE 1 DONE run8 (2026-07-30):
   CLI render timer (run_patch stderr) + tools/prof_additive.py sweep. Baseline
   ~65-90 ns/sample/partial (marginal 64 ns), linear, ~325 partials/core RT
   @48kHz single-thread; hot path = per-partial std::sin + fmod in
   Partials::get_partial_value. Note: instrument+score patches pre-render at
   load, so run_patch's timer measures MIXING not synthesis (25x artifact
   resolved) — stage-2 profiling must time the pre-render.
   **STAGE 2 (open):** (a) SIMD/vectorize the get_partial_value partial loop
   (batch the sin — a table or poly approx would break bit-exactness → null
   test + Matt's ear); (b) investigate iFFT overlap-add additive (1000s of
   partials cheap; NOT bit-identical → review:listen); (c) add a timer at the
   instrument pre-render. >1 session; stage it.
9. **[build] Convert 6 algev patches to instrument-style** — ✓ DONE run8
   (2026-07-30). instrument+score added + output rerouted to bare source;
   all 6 render with sound via the shared instrument path.
10. **[review:listen] Vowel/formant re-tune after formantWeight refactor** —
    prep render set + gain sweep, queue for ears.

## Done

- Partial motion layer + v1-v4 batches (docs/Fable1_results.md)
- 16 kHz cutoff break→continue fix; UI array-restore fix; 96-partial
  extrapolation
