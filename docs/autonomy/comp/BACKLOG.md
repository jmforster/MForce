# Composition lane — backlog (Wolfie)

Priority order. (G1)-(G2) = GOALS.md Wolfie goals.

18. **DONE run 18** — `passage_anchors::grid_complete` applied centrally in
    `Composer::compose_passage` (every strategy returns through there), NEAREST
    multiple not ceiling, knob `PassageTemplate::endGrid` (1.0 beat default /
    4.0 barline / 0 off). Measuring found a SECOND mechanism the backlog did
    not know about: a passage composed longer than its section was truncated
    mid-note and overhung the boundary, so quantizing its final unit could
    never help (`wandering_24x` composed a clean 49.0 into a 48-beat section).
    Notes now clamp to the section end. 5/21 → **21/21** on the beat.
    Blast radius: 31/38 templates byte-identical, the 7 that changed all moved
    onto the grid. Beat-vs-barline default queued as REVIEW 17.
    Also found: two STALE renders from an 08-01 `--takes 4` run were still in
    `renders/engine_passage_strategies/` and had been counted by every sweep
    since — including run 17's 37/74. Deleted.
19. **DONE run 18** — the empty render does NOT reproduce: `wandering_24x`
    renders 99 notes at HEAD (run 17 had no CLI to test with). On the open
    design question, `score()` now REFUSES rather than flags — `composite` is
    `None` below the 4-note threshold, so arithmetic raises instead of quietly
    averaging a placeholder. Verified safe first: no caller passes fewer than
    4 notes (`figure_transforms` samples kmin=5). Five aggregation sites in
    the batch drivers filter on `scorable`; `figure_transforms` deliberately
    does not, because a short figure there is a bug worth a traceback.
20. **[review:read] Passage-mode open semantics** — two questions the #15
    screen cannot answer by itself. (a) A DESCENDING sequence scores
    0.49–0.51 because the tension criterion asks "does it build", which is
    true but may be the wrong question for a relaxing passage — should a
    passage declare its intent (build / relax / static) and be scored against
    that? (b) `pedal_buildup` is 8/8 "non-tonic arrival" because it ends on
    its dominant pedal, which is arguably right — the tonic term needs to know
    what the passage was aiming at. See REVIEW 14.
13. **DONE run 18** — `sections[].keyName` desugars at COMPOSE time into a
    beat-0 KeyContext (parse stores / serialize writes back, so the template
    round-trips and the linter stops reporting it). Accepts `"G"` or
    `"G Minor"`; composes with LATER keyContexts; throws by name on a beat-0
    conflict and on an unknown key. Found and fixed a second half nobody had
    written down: `setup_piece_` realized a section's `scaleOverride` at the
    PIECE tonic, so a section could change its scale type but never its tonic.
    Null test 38/38 WAVs byte-identical (measured against a reverted build,
    not argued). Carried caveat CONFIRMED by measurement: it changes
    accidentals, it does not move the tonic — three-arm A/B in
    `renders/section_key/`, REVIEW 16.
14. **[build] Voicing priority ladder collapses** — with the nine
    `test_jazz_turnaround_*` patches rendering again (run 15 front 1),
    `_p05` and `_p1` are BYTE-IDENTICAL renders while `_p0` differs. Priority
    distinguishes 0 from nonzero and nothing in between. Belongs with #8; not
    touched blind because it is voicing-profile semantics.
6. **[build] (G2) PassageStrategy expansion** — CLOSED in run 15. All four
   C++ stages landed earlier; the two remaining Matt ideas are now built.
   The modulating wandering passage landed in run 14; the **Bruckner
   pedal-through-keys** landed in run 15 (`pedal_modulating`,
   `pedal_mod_minor3rds`) and needed two engine fixes to be possible at all:
   chord realization was not key-aware (stage 3 only covered melody), and a
   pedal had no way to opt OUT of key-awareness, so its held note was being
   snapped into each new scale. Both fixed. Anything further under this
   heading is new work, not leftovers.
   Old text kept below for the record:
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
15. **DONE run 17** — `score_passage.py`: tension (measured to the PEAK, not
    across the whole passage — a whole-passage slope reads negative on
    exactly the passages that work) / arrival / coherence, weights
    0.35/0.40/0.25, verified by a 4-way mechanical degradation harness rather
    than by taste. Uncorrelated with the phrase composite (Spearman 0.072
    over 74 renders) and much wider spread. The backlog's own example
    reproduces: `pedal_mod_minor3rds_1_1` 0.719 phrase vs 0.876 passage.
    Open semantics moved to #20.
16. **DONE run 17** — v4 `calib` arm, default. The draw was already landing
    on the corpus median; the overshoot was mostly GRID COMPLETION ceiling
    every phrase onto the next barline (+0.25 beats at p50, on top of the
    draw). n=200: ratio p50 3.00 → 2.50, p95 11.00 → 7.00, over-corpus-p95
    0.170 → 0.065, final-is-longest 0.785 → 0.620 (corpus 0.354).
    A/B in `renders/markov_phrases6/{,longest}` — REVIEW 13.
17. **DONE run 17** — offender-first reversion; `range_guard_ab.py`
    reproduces the reported p18 case and fixes it. cap 12 / prob 1.0, n=300:
    transforms surviving the guard 3 → 15. Production defaults: 0 → 3.
12. **DONE run 16** — closure screen verified against the run-13 A/B
    (byte-identical scores.csv now separates) and committed as 0fc3f29.
    Was: BLOCKED at run 15 by the
    tree guard: an uncommitted implementation of exactly this is sitting in
    the working copy (`score_generated.py` +181, `corpus_baseline.py` +12,
    stamped 2026-08-03 07:10, no report, no commit — a run that died
    mid-cycle). Not touched, not run, not committed. Needs one word from Matt
    to adopt or discard; see REVIEW 5. Original text:
    run 13 changed every
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
10. **DONE run 16 (second half: literal repeats)** — run 15 found only the
   PRIME path wired; literal-repeat occurrences now transform too (A/B in
   renders/markov_phrases5/). Was: Wire figure transforms into
    the phrase-builder. `markov_phrase.py` routes A-family primes through
    `figure_transforms` (INDEPENDENT_OPS / INVOLUTION_OPS / a "mixed" menu
    that draws a different op per occurrence), and enforces that each repeat
    actually differs via a rotate-then-ornament fallback when an
    occurrence-parameterized op saturates. The backlog entry was stale.
7. **[build] Phrase-aware cadence placement** (AFS impedance finding).
8. **[build] Voicing open items** — upward tendency, cadential chord role,
   boring-repeat, StagedVoicingProfileSelector.
9. **[build] Revisit PAC held-note workaround.**

## Done

- Run 17 (2026-08-05): four fronts, all Python-side — the dsp lane was live in
  the working copy, so #13 (engine) was not attempted. #16 final-note
  recalibration (the barline pad, not the draw, was the overshoot), #17
  offender-first range guard, #15 passage-mode scorer + its degradation
  harness, and passage mode wired into the strategy driver — which turned up
  37/74 passage renders ending off the beat (#18), an empty render scoring
  0.407 (#19), and two KeyError holes in the batch path.
- Run 15 (2026-08-04): four fronts. Passage-level chord emission
  (`PassageTemplate.chordProgression` + span/pattern modes + one shared
  progression parser that accepts both authoring forms and carries
  `alteration`) — which also revived the harmony path, DEAD at HEAD: all nine
  `test_jazz_turnaround_*` rendered peak=0 with 0 chord events. Prototype A/B
  (`pedal_chords_voiced` / `_smooth`, controlled at fixed seed).
  `mforce_cli --lint-template`: 236 templates swept, `chordConfig` found
  parseable-but-not-serializable, 9 dead `defaultPattern` keys removed.
  Bruckner pedal-through-keys (`pedal_modulating`) closing #6, needing
  key-aware chord realization + `PassageTemplate.scaleOverride` so a pedal can
  refuse to modulate. Renders: passage_chords/, passage_chords_ab/,
  passage_bruckner/.
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
