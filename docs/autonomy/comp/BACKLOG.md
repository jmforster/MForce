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
   **3a DOWNLOAD-GATED** (run 4: fetching the Nottingham-Jukedeck repo is a
   network download; unattended runs can't get the required go-ahead — queued
   in REVIEW as an operational unblock. bake_off.py is corpus-parametric via
   `--tokens`, so the token file drops straight in once fetched),
   3b Matt license call (REVIEW), 3c Essen ingest.
4. **[metric] (G1) FigureGenerator plugin structure + method bake-off** —
   INTERFACE + N-GRAM BAKE-OFF DONE (run 4, figuregen.py / bake_off.py /
   test_figuregen.py). FigureGenerator ABC; generic NGramModel (order-N
   backoff + add-k) proven == shipped Markov at order-2; roster uniform/
   unigram/ngram1/ngram2_backoff/ngram3_backoff/ngram2_addk scored on the
   harness (2-seed stable). Findings: context earns range control not interval
   fit; order-2 backoff is the validated sweet spot; add-k smoothing is a
   decisive negative; composite saturates across orders (evidence for #9).
   Remaining = **4c** small neural next-note model (numpy present; the LLM-like
   predictor per Matt) as the next bake-off entrant. Per corpus once 3a lands.
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

- **[metric] (G1) FigureGenerator interface + n-gram method bake-off**
  (run 4, 2026-07-29). figuregen.py (FigureGenerator ABC + generic NGramModel,
  proven == shipped Markov at order-2) + bake_off.py (engine-free realize +
  score, pooled JSD + composite vs corpus ceiling, corpus-parametric via
  `--tokens`) + test_figuregen.py. Order-2 backoff validated as the sweet spot;
  add-k smoothing a decisive negative; composite saturates across orders.
  Remaining #4 work = 4c neural next-note. See reports/2026-07-29-wolfie-run4.md.
- **[metric] (G1) Phase-2 combination phrase — scored + guarded**
  (run 3, 2026-07-28). markov_phrase.py rendered + scored through the
  item-1 harness: long combos mean 0.705 (top AAA'B 0.83–0.865, corpus-
  anchor territory), bare AB weakest. Added range-runaway guard
  (eliminated 19% out-of-corpus-range rate, composite-neutral) and an
  outlier mode. Audition + scorer-calibration queued for review. See
  reports/2026-07-28-wolfie-run3.md.
- **[metric] Corpus-statistics scoring harness** (run 2). The comp "ears."
