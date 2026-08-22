# Composition lane — backlog (Wolfie)

Housekept 2026-08-22 (with Matt). Item ids are STABLE (reports/REVIEW cite
them); order = priority. Done items are one-liners in the ledger at the
bottom; full text in run reports + this file's git history. Tags per
WORKFLOW.md. (G1)-(G2) = GOALS.md Wolfie goals.

## Next up

22. **[build] Re-render the three purge-casualty review items** — REVIEW
    11 (literal repeats, `renders/markov_phrases5/{,before}`), 13 (phrase
    endings, `renders/markov_phrases6/{,longest}`), 17 (passage endings,
    `renders/passage_end_grid/{off,beat,bar}`) all cite renders deleted in
    the 2026-08-08 render purge — Matt can never verdict them as they
    stand. Re-render each A/B with the same seeds/recipes (READMEs and
    generator scripts document them), place under renders/comp/pending/,
    and update the REVIEW entries' paths. FIRST task of the next comp run.

21. **[build] Zero-event renders are never treated as failures** —
    building #9's harness found six of 38 committed templates rendering
    pure silence (empty passage when no `startingPitch`, under a comment
    claiming the loader refused those). The generator bug is FIXED (run
    26: inherit first phrase's pitch, refuse by name when none), but
    nothing in the batch/sweep path treats 0 events as a failure — which
    is why it survived indefinitely and why run 17 hit the same class with
    `wandering_24x`. Add a zero-event check to `null_test_templates.py`
    and the strategy sweeps. Cheap; closes a whole class.

8. **[build] Voicing open items — register drift + selector** — register
    drift ("upward tendency") is back on the backlog after two failed
    attempts (both measured WORSE than nothing; no dead knob left behind —
    see note in `voicing_profile.h`). **Do not attempt a third without
    re-observing first: the symptom does not reproduce at HEAD** (on the
    2026-04-20 observation patch, base top voice drifts 0 semitones over
    16 chords; likely changed by the run-15 harmony-path revival). If it
    IS still audible, target SPREAD, not the octave search. Still
    untouched under this heading: StagedVoicingProfileSelector.
    (`repeatPenalty` + `cadential` terms shipped run 26, default-off.)

11. **[metric] Representation ceiling for non-diatonic corpora** — run 12
    found (scale-step, pulse) tokens realized in C major erase
    pentatonicism: training on Essen scores no better than MTD against the
    essen_asia anchor (int_jsd .087 vs .081). If corpus-specific
    modal/pentatonic flavor is wanted, the token alphabet (or realization)
    has to carry it. Design question, wants Matt's steer before it becomes
    a task.

## Gated on Matt

14. **[build] Voicing priority ladder collapses** — `_p05` and `_p1` are
    byte-identical renders while `_p0` differs: priority distinguishes 0
    from nonzero and nothing in between. What SHOULD 0.5 mean? Voicing-
    profile semantics — not touched blind. Evidence: REVIEW 12.

20. **[review:read] Passage-mode open semantics** — (a) a DESCENDING
    sequence scores 0.49-0.51 because the tension criterion asks "does it
    build" — should a passage declare intent (build/relax/static) and be
    scored against it? (b) `pedal_buildup` is 8/8 "non-tonic arrival"
    because it ends on its dominant pedal, arguably exactly right — should
    the arrival term know the passage's aim? See REVIEW 14.

## Done ledger

One line each; full text in run reports + this file's git history.

- 6. PassageStrategy expansion (G2) — closed run 15: all four C++ stages,
  key-aware realization, WanderingPassageStrategy, Bruckner
  pedal-through-keys (`scaleOverride` so a pedal refuses to modulate).
- 7. Phrase-aware cadence — run 26: per-phrase chord spans, cadence only
  at phrase boundaries; real defect was the composer/realizer pitch-cursor
  disagreement (+ unbounded-descent crash AFS shares); shared
  `chord_walk::advance` now mirrors realization. Arrivals 1/4 → 4/4.
- 9. `cadentialArrival: "approach"` — run 26; default unchanged (taste,
  REVIEW 18); two anti-results recorded (no leap; held arm is SHORTER).
- 10. Figure transforms wired into phrase-builder — runs 15/16 (primes,
  then literal repeats; A/B renders/markov_phrases5/).
- 12. Phrase-closure screen — verified + committed run 16 (0fc3f29); the
  run-13 byte-identical A/B now separates.
- 13. Section `keyName` desugars at COMPOSE time — run 18; second half
  found (scaleOverride realized at piece tonic); null test 38/38;
  accidentals-not-tonic caveat CONFIRMED → REVIEW 16.
- 15. Passage-mode scorer — run 17; tension measured to the PEAK;
  verified by mechanical degradation; uncorrelated with phrase composite
  (Spearman 0.072). Open semantics → #20.
- 16. Final-note recalibration v4 `calib` — run 17; the overshoot was grid
  completion ceiling onto the barline, not the draw. A/B → REVIEW 13.
- 17. Offender-first range guard — run 17; reproduces and fixes the p18
  case; surviving transforms 3 → 15 at tight cap.
- 18. Central `grid_complete` + section-end clamp — run 18; 5/21 → 21/21
  endings on the beat; two stale renders found and deleted; default →
  REVIEW 17.
- 19. Empty-render refusal — run 18; `score()` returns None below 4 notes
  and arithmetic raises; five batch aggregation sites filter on
  `scorable`.
- Runs 2-5, 8, 12 foundations (scoring harness, n-gram bake-off, neural
  next-note, corpus anchors, monotony screens, contrast-aware fig B) —
  see reports/.
