# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals. Wolfie's first run
should validate — deep context lives in the composition thread.

1. **[metric] Corpus-statistics scoring harness** — score generated output
   against MTD statistics (interval/contour/repetitiveness/cadence). The
   comp "ears": gates everything below.
3. **[metric] (G1) Corpus survey + acquisition** — beyond MTD: Essen
   folksong, ABC/folk collections, kern scores (KernScores/humdrum), jazz
   lead sheets; licensing + format notes; ingest pipeline for 1-2 best.
   SURVEY DONE (run 3, corpus_survey.md): recommend ingest order (3a)
   Nottingham-Jukedeck (GPLv3, MIDI ready — fastest), (3b, review) Essen
   license call, (3c) Essen ingest (6k monophonic folk, kern parser).
   KernScores-classical deprioritized (idiom overlaps MTD). Remaining =
   3a ingest (next cycle), 3b Matt license call (REVIEW), 3c Essen ingest.
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

9. **[metric] Scorer — higher-order monotony screen** — run-3 first
   thought a rep *ceiling* was needed (outliers scoring 0.74–0.84), but
   measuring the corpus killed that: corpus repetition-coverage
   (rep_LxCount/n_notes) runs p50=0.59, **p90=0.86, p95=0.92, max=0.97**,
   and rep_LxCount p95=21/max=44 — the corpus is genuinely that repetitive,
   so a naive ceiling would penalize corpus-faithful themes. The real gap is
   that first-order interval/contour/rep distributions can't see *monotony*
   (a whole phrase that is one figure hammered N×): p04 (fig ×6) is only
   corpus-p90 on coverage, so it "looks" plausible per stats. Needs a
   higher-order feature — motif/variation diversity, or an autocorrelation/
   self-similarity screen — not a threshold tweak. Separately: fix the
   `--csv` arg-parse quirk (treats the csv path as an input melody; harmless
   stderr warning). Feeds REVIEW item 2.

## Done

- **[metric] (G1) Phase-2 combination phrase — scored + guarded**
  (run 3, 2026-07-28). markov_phrase.py rendered + scored through the
  item-1 harness: long combos mean 0.705 (top AAA'B 0.83–0.865, corpus-
  anchor territory), bare AB weakest. Added range-runaway guard
  (eliminated 19% out-of-corpus-range rate, composite-neutral) and an
  outlier mode. Audition + scorer-calibration queued for review. See
  reports/2026-07-28-wolfie-run3.md.
- **[metric] Corpus-statistics scoring harness** (run 2). The comp "ears."
