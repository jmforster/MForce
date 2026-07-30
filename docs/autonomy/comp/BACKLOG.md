# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals.

2. **[metric] (G1) Contrast-aware fig B** — promoted by Matt's Phase-2
   verdict: fig B currently sampled independent of fig A; sample it in
   relation (register continuation, rhythmic kinship, complementary
   contour). Gate through the scorer incl. monotony screens; re-audition
   against the regenerated independent-B set.
3. **[metric] (G1) 4c neural next-note model** — small numpy next-note
   predictor (the LLM-like method per GOALS) as the next bake-off entrant;
   run on both MTD and Nottingham tokens.
4. **[metric] (G1) Essen ingest (3c)** — license cleared by Matt 2026-07-29.
   Kern parser -> tokenize_notes -> essen_tokens.json; re-run bake-off.
   Also: Nottingham-anchored scorer baseline (corpus_stats per corpus).
5. **[metric] #9 v2 — figure-level self-similarity** — v1 screens (zero
   rate, same-note runs) landed run 5; v2 = motif-level "one figure
   hammered Nx" detection (autocorrelation / variation diversity).
   Also fix score_generated --csv arg-parse quirk.
6. **[build] (G2) PassageStrategy expansion** — connective/transitional,
   pedal-point buildup, discursive wandering, circle-of-fifths trips;
   demo passage per strategy to REVIEW.
7. **[build] Phrase-aware cadence placement** (AFS impedance finding).
8. **[build] Voicing open items** — upward tendency, cadential chord role,
   boring-repeat, StagedVoicingProfileSelector.
9. **[build] Revisit PAC held-note workaround.**

## Done

- Run 5 (2026-07-29): monotone-B selection bias fixed (0.38->0.19);
  Nottingham ingested (1024 tunes) + folk bake-off (method ranking
  corpus-stable); #9 v1 monotony screens in scorer. Norm-breakers dropped
  per Matt.
- Run 4: FigureGenerator interface + n-gram bake-off (order-2 backoff =
  sweet spot; add-k negative; composite saturates -> #9).
- Run 3: Phase-2 combination phrases scored + guarded; corpus survey.
- Run 2: scoring harness (the comp "ears"); parallel-period flag
  ("parallel": true — per-phrase startingPitch mechanism menu complete).
