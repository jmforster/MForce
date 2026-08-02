# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals.

6. **[build] (G2) PassageStrategy expansion** — Python prototype LANDED run 12
   (passage_strategies.py: pedal_buildup / wandering / connective /
   fifths_sequence + suite; 15 renders, listen item queued). Remaining = the
   C++ implementation (new dev, not legacy port), spec'd in 4 stages at
   docs/superpowers/specs/2026-07-31-passage-strategy-expansion-design.md:
   (1) anchor plumbing + PedalBuildup/Sequence strategies, (2) connective
   (needs prior-passage context via Locus), (3) key-aware realization —
   keyContexts are inert today, which BLOCKS real modulating
   circle-of-fifths, (4) wandering. Stage 1 was not started in run 12 because
   the dsp lane held the build dir; it needs a free tree or its own build dir.
11. **[metric] Representation ceiling for non-diatonic corpora** — run 12
    found that (scale-step, pulse) tokens realized in C major erase
    pentatonicism: training on Essen scores no better than training on MTD
    against the essen_asia anchor (int_jsd .087 vs .081). If corpus-specific
    modal/pentatonic flavor is wanted, the token alphabet (or realization)
    has to carry it. Design question, not yet a task.
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

- Run 12 (2026-07-31): #4 CLOSED — per-corpus scorer anchors
  (corpus_baseline.py; mtd/nottingham/essen/essen_europa/essen_asia), two
  measurement artifacts fixed (whole-tune length confound; Essen alphabetical
  sampling anchoring "folk" on 2,246 Chinese tunes — mtd-vs-essen JSD
  .128 -> .036). Bake-off re-run under proper anchors: run-10 ranking holds.
  Corpus-flavored phrase batches (markov_phrase --corpus, 36 WAVs, controlled
  structural A/B). #6 prototype + spec (above).
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
