# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals. Wolfie's first run
should validate — deep context lives in the composition thread.

1. **[metric] Corpus-statistics scoring harness** — score generated output
   against MTD statistics (interval/contour/repetitiveness/cadence). The
   comp "ears": gates everything below.
3. **[metric] (G1) Corpus survey + acquisition** — beyond MTD: Essen
   folksong, ABC/folk collections, kern scores (KernScores/humdrum), jazz
   lead sheets; licensing + format notes; ingest pipeline for 1-2 best.
4. **[metric] (G1) FigureGenerator plugin structure + method bake-off** —
   formalize the generator interface; implement n-gram (existing Markov),
   higher-order/backoff variants, and a small neural next-note model
   (LLM-like prediction per Matt); compare on harness (item 1) per corpus.
4. **[build] (G2) PassageStrategy expansion** — new strategy types:
   connective/transitional passages, pedal-point pre-cadence buildup,
   discursive wandering, circle-of-fifths trips. Known principles + invented
   ones; each renders a demo passage for the review queue.
5. **[build] Per-phrase startingPitch in JSON** — period-parallelism
   structural fix.
6. **[build] Phrase-aware cadence placement** — replace strict A/B
   alternation (AFS impedance finding).
7. **[build] Voicing open items** — upward tendency, cadential chord role,
   boring-repeat, StagedVoicingProfileSelector.
8. **[build] Revisit PAC held-note workaround.**

9. **[metric] Scorer calibration — repetition ceiling + range weight** —
   run-3 outliers exposed that score_generated.py's repetitiveness screen
   is a one-sided floor (`rep ≥ p10`): a figure repeated 6× (rep 22–25)
   scores 0.74–0.84, as high as good phrases. Add an upper bound (corpus rep
   is a distribution) and reconsider the 1/5 range weight (a 4-octave span
   barely dents composite). Also fix the `--csv` arg-parse quirk (treats the
   csv path as an input melody). Feeds REVIEW item 2.

## Done

- **[metric] (G1) Phase-2 combination phrase — scored + guarded**
  (run 3, 2026-07-28). markov_phrase.py rendered + scored through the
  item-1 harness: long combos mean 0.705 (top AAA'B 0.83–0.865, corpus-
  anchor territory), bare AB weakest. Added range-runaway guard
  (eliminated 19% out-of-corpus-range rate, composite-neutral) and an
  outlier mode. Audition + scorer-calibration queued for review. See
  reports/2026-07-28-wolfie-run3.md.
- **[metric] Corpus-statistics scoring harness** (run 2). The comp "ears."
