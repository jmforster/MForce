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
2b. **[metric] (G1a) Calibrated motion mapping v2** — run-1 verification
   found the naive mapping overshoots: subtract the vibrato baseline in
   variance space (control already shows 5.5c resid / 38% amp from FM→AM
   through formant slopes), walk-RMS factor 0.74 not 0.55, rate centroid
   ≈ 0.5× segment rate. One cycle; superseded eventually by item 3.

3. **[metric] (G1a) CMA-ES optimizer core** — the ears project, staged:
   (a) spec (param vector over formant gains/motion/residue configs, scorer
   from ml_ears metrics, eval budget); (b) scorer CLI (render→score one
   candidate); (c) optimizer loop w/ checkpointing; (d) viola validation run.
4. **[metric] (G1b) Novelty metric** — timbre-feature embedding (MFCC stats,
   spectral flux/centroid trajectories) + distance-from-library scoring;
   wire into --explore-filter so novelty sweeps self-rank. Gate for G2/G3
   "crazy" sweeps.
5. **[metric] (G2) Recursive partial-expansion regime sweep** — expandRule
   recurse 2-4 at extreme spacings/dt/po (Matt's hint: under-explored);
   batch render, novelty-filter (item 4), queue survivors. No idea too crazy.
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
