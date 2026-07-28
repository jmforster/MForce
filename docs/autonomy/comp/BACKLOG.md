# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals. Wolfie's first run
should validate — deep context lives in the composition thread.

1. **[metric] Corpus-statistics scoring harness** — score generated output
   against MTD statistics (interval/contour/repetitiveness/cadence). The
   comp "ears": gates everything below.
2. **[metric] (G1) Corpus survey + acquisition** — beyond MTD: Essen
   folksong, ABC/folk collections, kern scores (KernScores/humdrum), jazz
   lead sheets; licensing + format notes; ingest pipeline for 1-2 best.
3. **[metric] (G1) FigureGenerator plugin structure + method bake-off** —
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

## Done

(none under this workflow yet)
