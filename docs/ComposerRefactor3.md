* Refactor of MForce Composer (/Conductor) tier

* Motivations

Original stated separation of concerns was:
  - Compose tier produces everything needed to produce a score (ElementSequence)
  - A PieceTemplate is the blueprint for a piece
  - A Piece has 2 sets of state:
    + Parts (horiz), Sections (vertical), Passages/Phrases/Figures per Part
    + An ElementSequence - the discrete notes, rests, ornaments, dynamic markings, etc.
  - Composer constructs the Piece components from the template, then walks to realize the ElementSequence
  - With the ElementSequence containing everything needed to produce a score, the Composer's work is done
  - Conductor takes Piece in perform() signature, but *only consumes ElementSequence*
  - Conductor passes Elements to Performers, who do 2 things:
    - realize articulations, ornaments, add humanization, etc.
    - call an Instrument to render sound
  - A distinction was made between "score" type music and "lead sheet" type music, implication being:
    + "Lead sheet" just has chord symbols, so voicing and realization of chord tones delegated to ChordPerformer (the "Josie" approach)

In implementing, major drift occurred where:
 - the Conductor was walking the Passages, Phrases, etc. and constructing the Elements to pass to Performer
 - in other words, doing the Composer's job

* Work thus far - decisions

We recently completed a refactor of both tiers to make the separation clean, as intended
In the process, we decided the "lead" sheet distinction, important in real life, was not an important distinction for MForce, namely:
  - Let the Composer choose chord voicings *and* chord note realization (think, Josie on a score will all the individual notes on a staff)
  - Limit Performers to tempo alterations (swing, humanization), and articulation and ornament realization
  - A "lead sheet" with chord symbols could still be built by reading chord progression and ignoring the realization of those chords
  - We allowed that in future the voicing select and/or realization might be allowed to take place in both tiers, but only if strong motivation to blur that separation

* Work thus far - specifics

We moved code walking Piece componenents to Composer from Performer

We left Conductor'signature (taking Piece) intact pending gaining more experience:
  - Conductor might need to know what Section we're in, etc.
  - Probable outcome, deferred for now:
    + anything "missing" from a score that the Conductor needs to know?
    + add it as an Element (with flag to exclude from an exported score, if necessary)
    + revise Conductor's perform() signature to take ElementSequence only

We then jumped down to the "atomic" level and completely rewrote FigureBuilder as:
  - RandomFigureBuilder - construction and combination of atomic Figures
  - FigureTransforms - invert, retrograde, etc.

We very lightly tested the FigureBuilder work via a bespoke test harness (new param in mforce_cli)

* Next steps

1. Write a trival WrapperPhraseStrategy
   - wire in the new RandomFigureBuilder and FigureTransforms
   - simply makes a one-figure Phrase
   DONE 2026-04-24: wrapper_phrase strategy + round-trip smoke test
   (spec: docs/superpowers/specs/2026-04-24-wrapper-phrase-strategy-design.md)
   (plan: docs/superpowers/plans/2026-04-24-wrapper-phrase-strategy.md)
   Note: RFB/FigureTransforms are called at test-build time (pending decision D2,
   default kept); strategy itself just dispatches through default_figure. Revisit
   if a phrase-level "build me an RFB figure" path is needed before step 3.

2. Exhaustively test figures
   - remove the CLI hacks put in yesterday
   - build Pieces with a single Passage containing a single Phrase containing a single Figure
  ┌─────────────────────────────┬────────────────┬─────────────────────────────────────────────────────┐
  │            Layer            │   Test kind    │                   What you assert                   │
  ├─────────────────────────────┼────────────────┼─────────────────────────────────────────────────────┤
  │ figure_transforms::*        │ pure unit test │ step sequences in/out                               │
  ├─────────────────────────────┼────────────────┼─────────────────────────────────────────────────────┤
  │ Composer locked-figure path │ integration    │ piece.parts[...].passages[...] figure matches input │
  ├─────────────────────────────┼────────────────┼─────────────────────────────────────────────────────┤
  │ Pitch realization           │ integration    │ element.pitch after scale walk                      │
  ├─────────────────────────────┼────────────────┼─────────────────────────────────────────────────────┤
  │ End-to-end                  │ listen         │ sanity check                                        │
  └─────────────────────────────┴────────────────┴─────────────────────────────────────────────────────┘

  For transforms uses assert() in a tools/test_figures/ binary and upgrade later if needed.

  DONE 2026-04-24: test_figures binary now covers figure_transforms (unit +
  integration) and the fig/invert/retrograde/retro-invert listening exercise
  via `test_figures --render`. --test-rfb / --test-replicate retired.
  (spec: docs/superpowers/specs/2026-04-24-figure-testing-harness-design.md)
  (plan: docs/superpowers/plans/2026-04-24-figure-testing-harness.md)

3. Rewrite DefaultPhraseStrategy
   - wire in the new RandomFigureBuilder and FigureTransforms
   - produces a 2-figure Phrase
   - test a few Phrases
   DONE 2026-04-24: landed as additive sibling two_figure_phrase strategy
   (interpretation B — leaves existing default_phrase untouched). Uses RFB +
   figure_transforms::apply directly; TwoFigurePhraseConfig carries the
   base-build method + Constraints + transform op. JSON round-trip + four
   integration tests in test_figures. Rename-to-default deferred to step 6.
   (spec: docs/superpowers/specs/2026-04-24-two-figure-phrase-strategy-design.md)
   (plan: docs/superpowers/plans/2026-04-24-two-figure-phrase-strategy.md)

4. Brainstorm what "middle tiers" as a whole - namely, Phrase and Passage strategies and related classes - should actually look like
  - Currently there are a bunch of "shape" PhraseStrategies that should go away
  - Input into brainstorming is the universe of strategies we can imagine, informed by music theory and literature research
   DONE 2026-04-24: ElaboratedPhraseStrategy Phase 1 — first concrete
   middle-tier strategy emerging from this brainstorm. Recursive
   elaboration / Auskomponierung framing: skeleton (RFB-built or literal)
   + per-anchor random Leave/Generate; FC.leadStep math FC[0]=0 /
   FC[i>0] = skel.step[i] - E_{i-1}.net_step bridges the cursor across
   figure boundaries.
   Foundation refactor came with it: Phrase gained parallel
   std::vector<FigureConnector> connectors; figures are no longer
   mutated to absorb leadStep; realize_phrase_to_events_ /
   apply_cadence / pitch_before / range queries / Default- and
   PeriodPassageStrategy cursor accumulators / build_melody_profile all
   updated to read FC.leadStep from phrase.connectors at walk time.
   K467 walker / harmony / period goldens all bit-identical to
   pre-refactor branch tip (walker also matches the 2026-04-22 pinned
   baseline; harmony's pinned baseline is pre-existing stale on this
   branch independent of this work).
   3 integration tests in test_figures.
   (spec: docs/superpowers/specs/2026-04-24-elaborated-phrase-strategy-design.md)
   (plan: docs/superpowers/plans/2026-04-24-elaborated-phrase-strategy.md)

5. Redesign of the "middle tiers" - if Shapes go away so does FigureSource.shape, do we even need FigureSource, etc.

6. Refactor "middle tiers" - PhraseStrategy, PassageStrategy, FigureSource, etc.
   Revise template JSONs and 467 as needed.

7. Pick 3-5 PhraseStrategies and 2-3 PassageStrategies to implement, test, and perfect 
   (one being the existsing PeriodPassageStrategy)

* Longer term

Once all the plumbing is working, brainstorm how we can guide construction of Figures so they are musically interesting.
- Better "shaping" algos
- Expand FigureTemplate to have more "shape" information vs. basic stats
- Possibly create library of FigureTemplates (maybe by genre)
 