# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals.

6. **[build] (G2) PassageStrategy expansion** — ALL FOUR C++ STAGES LANDED.
   Stages 1-2 committed by the 2026-08-01 run; stages 3-4 adopted, verified
   and committed in run 13 (key-aware realization + WanderingPassageStrategy,
   plus a retry fix for a silent-render bug the 24-entry outlier exposed).
   keyContexts are LIVE — modulation verified C/G/D 3/3 distinct, and
   `modulating_fifths` renders a real circle-of-fifths trip with each key's
   accidentals. Python prototype now carries 9 strategies.
   Remaining under this item: Matt's two modulation ideas that stage 3
   unblocked but that nobody has built — Bruckner pedal-through-keys, and the
   modulating wandering passage (sweet major theme -> sudden dim7 -> minor
   keys). NOTE for both: stage 3 snaps the cursor's PITCH into the new scale,
   it does NOT move it to the new tonic, so adding keyContexts alone will not
   modulate audibly; the entry must be offset by the key distance.
12. **[build] Scorer is blind to the final note** — run 13 changed every
    phrase ending in a 24-phrase batch (final ratio 1.00 -> 2.50, ends-on-beat
    4/24 -> 24/24) and `scores.csv` came out BYTE-IDENTICAL. The composite has
    no phrase-ending term at all, so no metric could ever have caught the
    defect Matt heard. Add a closure screen: final-note ratio vs the corpus
    histogram (`final_note_profile.json` already has it) and phrase-end grid
    landing. Until then, "composite went up" is not evidence about endings.
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

- Run 13 (2026-08-02): Matt's run-12 verdicts, three fronts.
  Final-note rule corpus-calibrated (final_note_stats.py profiles MTD n=1632 /
  Nottingham n=1024: median ratio 2.0 in both; the old rule sat at 1.0 = MTD
  p25). Ratio now drawn from the corpus histogram; controlled A/B in
  renders/markov_phrases4/{,before} — ratio 1.00 -> 2.50, final-is-longest
  0.12 -> 0.67, ends-on-beat 4/24 -> 24/24. Exposed #12 (scorer blind to
  endings). Three chain->goal connective strategies from Matt's three worked
  examples, incl. real chromaticism via FigureUnit.accidental (no engine
  change needed — verified in rendered pitches). pedal_chords: dissonance
  ordered by measurement into a Ger6 -> I(6/4) -> V7 -> I cadence with the
  pedal releasing to the tonic. pedal_buildup acceleration capped at 4x, and
  its accel metric found asymmetric (reported 8x for a real 4x). 30 renders in
  renders/passage_strategies2/.
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
