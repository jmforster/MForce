# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals.

4. **[metric] (G1) Essen ingest (3c)** — DATA-GATED (run 8): the Essen folksong
   collection is NOT on disk (corpus/kern = 10 classical polyphonic pieces only,
   not Essen). Needs a network fetch an unattended run can't authorize — unblock
   queued in REVIEW. Kern parser deferred with it (low value against 10 classical
   soprano lines). When data lands: kern parser -> tokenize_notes ->
   essen_tokens.json; re-run bake-off. Also still open: Nottingham-anchored scorer
   baseline (corpus_stats per corpus) — bake-off Nottingham numbers are still
   MTD-anchored.
6. **[build] (G2) PassageStrategy expansion** — connective/transitional,
   pedal-point buildup, discursive wandering, circle-of-fifths trips;
   demo passage per strategy to REVIEW. (Matt re-raised in run-7 REVIEW:
   "New passage strategies".)
10. **[build] Wire figure transforms into the phrase-builder** — the transform
    library landed run 8 (figure_transforms.py); next is USING it in
    markov_phrase so repeated A-family occurrences can be transposed AND
    transform-varied (invert/rotate/ornament/expand), not just contour-anchored
    + vary_tail. Serves Matt's spec-1/2/3 repeat variety at full generality.
7. **[build] Phrase-aware cadence placement** (AFS impedance finding).
8. **[build] Voicing open items** — upward tendency, cadential chord role,
   boring-repeat, StagedVoicingProfileSelector.
9. **[build] Revisit PAC held-note workaround.**

## Done

- Run 8 (2026-07-30): #2 contrast-aware fig B (closure objective + monotony
  veto; range 12.6->9.1, zero 0.278->0.251, composite wash by design ->
  audition queued); #3 neural next-note model (numpy Bengio LM, val_ppl 28.5,
  competitive-not-superior in bake-off on MTD+Nottingham); #5 #9 v2
  self-similarity screen + --csv fix; figure_transforms.py library (Matt's new
  fragment). Essen #4 found data-gated.
- Run 5 (2026-07-29): monotone-B selection bias fixed (0.38->0.19);
  Nottingham ingested (1024 tunes) + folk bake-off (method ranking
  corpus-stable); #9 v1 monotony screens in scorer. Norm-breakers dropped
  per Matt.
- Run 4: FigureGenerator interface + n-gram bake-off (order-2 backoff =
  sweet spot; add-k negative; composite saturates -> #9).
- Run 3: Phase-2 combination phrases scored + guarded; corpus survey.
- Run 2: scoring harness (the comp "ears"); parallel-period flag
  ("parallel": true — per-phrase startingPitch mechanism menu complete).
