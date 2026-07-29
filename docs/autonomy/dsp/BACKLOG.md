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
6. **[metric] (G2) FormantSequence deep-dive** — modulated/sequenced formant
   motion as a first-class timbre animator; sweep + novelty-filter.
7. **[build→metric] (G3) Oversampled FM render path** — render FM/PM patches
   at 4-8x SR with decimation; measure alias-product suppression on the
   spacy-FM family; then "modulate everything" matrix sweeps through the
   novelty filter.
8. **[metric] (G4) Additive performance** — profile partial loop; vectorize
   (SIMD) get_partial_value hot path; investigate iFFT overlap-add additive
   synthesis (classic route to 1000s of partials cheap). Target: measured
   samples/sec, no audible diff (null test vs reference render).
9. **[build] Convert 6 algev patches to instrument-style** — audition-path
   mismatch cleanup; scripts exist.
10. **[review:listen] Vowel/formant re-tune after formantWeight refactor** —
    prep render set + gain sweep, queue for ears.

## Done

- Partial motion layer + v1-v4 batches (docs/Fable1_results.md)
- 16 kHz cutoff break→continue fix; UI array-restore fix; 96-partial
  extrapolation
