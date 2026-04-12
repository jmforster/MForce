# Composition Quality — Brainstorm Expansion

> Pre-design brainstorming material for Matt to review and redirect. Written
> 2026-04-10 late session, intended for iteration in a fresh session
> tomorrow. NOT a spec — ideas are wild-to-conservative, most will be
> trimmed, some will be rejected outright.

## Matt's two corrections from the session wind-down

Captured before I forget:

**1. Outline tail = backward encroachment, not append.**

**Important context:** the three-phrase description is specifically the
Billy Joel "My Life" chorus, NOT a general pattern Matt invented. Design
work here should first and foremost faithfully reproduce the My Life
chorus construction; generalization to other patterns is a secondary
concern once the specific case works.

The tail figure isn't a separate thing tacked onto the end of the outline's
last anchor. It *displaces* the final notes of the repeated surface figure.
Across the three chorus phrases in My Life, the encroachment deepens:

- Phrase 1: [statement 1][statement 2][statement 3 full][tag]
- Phrase 2: [statement 1][statement 2][statement 3 cut after 2 notes][tag]
- Phrase 3: [statement 1][statement 2][statement 3 cut after 1 note][final cadence]

That last phrase uses a *different* tag (the "final cadence") than the
earlier phrases' regular tag. The encroachment parameter varies across
phrases, which means it lives at the passage level, not inside a single
phrase strategy.

The "3 statements of a 3-repeated-note figure plus tag" structure is
itself Joel-specific (the outline anchors are the target notes of the
three descending-backbone anchors in that chorus; the surface figure is
the hook "don't care what you say any-more"-shaped cell). Treat those
numbers (3 statements, 3-note surface cell) as authored values in the
template, not as fixed framework constants.

**2. Harmony-first vs melody-first is a BIFURCATION, not a detail.**
The "add harmonic awareness to the melody composer" framing is wrong.
Matt's point: harmony-first ("strum some cool chords, then fit a melody")
and melody-first ("create a killer melody, then harmonize") are two
different composers. They have different top-level pipelines, different
template fields, different per-figure decisions. Not one composer with a
knob — two composers in the IComposer registry.

---

## A. Outline strategies (expanded)

### The encroachment model

The original "Schenkerian ur-line with surface figures" model is still
correct for a SINGLE phrase. But the passage-level pattern Matt describes
is a second, orthogonal structure: across multiple phrases that share the
same outline template, the surface figure progressively compresses to make
room for the tag. The encroachment is a passage-level schedule.

**Proposed data model:**

```cpp
struct OutlinePhraseConfig {
  StepSequence outline;          // scale-degree deltas for outline anchors
  MelodicFigure surfaceFigure;   // repeating surface pattern at each anchor
  MelodicFigure tailFigure;      // the tag — always present
  int encroachmentNotes{0};      // how many notes of the final surface
                                 // statement to skip (they're replaced by
                                 // the first encroachmentNotes of tailFigure)
};

// At the passage level, the OutlinePassageStrategy carries an encroachment
// schedule — a list of ints, one per phrase, applied by substituting each
// value into the corresponding phrase's OutlinePhraseConfig.encroachmentNotes
// before realization.
struct OutlinePassageConfig {
  StepSequence outline;                        // passage-level anchors
  PhraseTemplate surfacePhrase;                // the outline phrase template
                                               // that gets repeated
  std::optional<PhraseTemplate> finalPhrase;   // optional distinct final
                                               // phrase (uses the "final
                                               // cadence" tag); if empty,
                                               // use surfacePhrase with the
                                               // deepest encroachment value
  std::vector<int> encroachmentSchedule;       // one int per phrase
};
```

The passage walks its outline, instantiates `surfacePhrase` at each anchor,
and BEFORE calling `realize_phrase` on each instance, stamps
`encroachmentNotes` from the schedule into the phrase's periodConfig. The
last phrase uses `finalPhrase` if present.

### Variations worth considering

- **Linear encroachment curve.** Instead of an explicit list, just
  `{starting: 0, ending: N, curveType: Linear|Exponential|Stepped}`. Less
  verbose for typical "progressively intensify" patterns.
- **Encroachment from the other side.** Cut the FIRST N notes of the final
  statement instead of the last. Rare in tonal music but appears in folk
  and blues traditions.
- **Generalized "intensification curve"** applied to any repeated phrase,
  not just outlines. Parameters vary per phrase: tempo, dynamic, register,
  figure density, tag length, pitch range. Encroachment is one knob;
  intensification is the broader concept.
- **Recursive outlines** — the surface figure of an outline is itself an
  outline. A turn-figure inside "My Life" could have its own micro-outline
  (upper-neighbor approach + resolution). Fractal expressive authoring.
  Probably YAGNI for v1 but worth keeping in mind.
- **Outline *is* a chord progression.** Not metaphorically — literally. Each
  outline anchor is a chord, the outline steps are the bass motion between
  chords, and the surface phrases are melodies fitting each chord. This
  unifies outline strategies with harmony-first composition.

### Passage-level outline design questions for tomorrow

- **Cursor control at phrase boundaries.** When the passage-level outline
  reaches a new anchor, does it FORCE the next phrase to start there
  (overriding cursor inheritance), or does it only constrain the cadence
  target and let the cursor flow naturally? Earlier I assumed the former;
  Matt's encroachment pattern might work either way.
- **Anchors = pitches vs anchors = chord roots.** If the outline is a chord
  progression, each anchor is a chord (multiple pitches). Phrase-level
  outline = scale-degree sequence. Passage-level outline = what?
- **What does the "final phrase" override look like?** If phrase 3 has
  distinct content (different tag, more dramatic cadence), is it a
  separate PhraseTemplate (in `finalPhrase` field) or just different
  values for the same template's encroachment/tag parameters?

---

## B. Shape selection intelligence

Current state: `DefaultFigureStrategy::choose_shape` picks randomly from a
hardcoded shortlist per MelodicFunction. No context awareness.

### Phrase-planner pre-pass (my recommended starting point)

Before generating any figures, a planning step walks the phrase template
and produces a shape plan: one `FigureShape` enum value per figure. The
planner considers:

- Phrase function (Statement → opening shapes; Cadential → resolving shapes)
- Position within phrase (first, middle, last)
- Cursor position at each point (avoid runaway register)
- Cadence target (last figure's shape must end on a step-bridge to target)
- Transition plausibility (don't put two ScalarRuns in a row)

Then figure generation consults the plan instead of running `choose_shape`
per-figure. Gives coherent per-phrase shape arcs.

### Other ideas, escalating in craziness

1. **Markov chain of shape transitions.** Each shape has a probability
   distribution over "what tends to come next." Hand-tuned or derived from
   corpus analysis. Gives statistical realism.

2. **Contour-driven selection.** Define a target contour for the phrase
   ("arch: rise to peak then fall"). Each figure position gets a desired
   cursor-movement direction. Shapes are filtered to those that produce
   that direction. Contours come from phrase function, user spec, or
   corpus-derived per-style shapes.

3. **Genre templates.** "Classical" picks different shapes than "folk" or
   "jazz." Genre = weighted distribution over shape probabilities.

4. **Pitch-context-aware.** The selector considers where the cursor IS —
   near the top of the range, prefer descending shapes; near the bottom,
   prefer ascending. Prevents runaway pitch excursions without requiring
   a range-enforcement pass.

5. **Goal-oriented backward planning.** Given a phrase-start pitch and a
   cadence target, work backward: which shape sequences end at the target?
   Filter to those. Pick from the filtered list.

6. **Wild idea: shape = mood.** Each shape has an emotional descriptor
   (RepeatedNote = stasis, Sigh = melancholy, Fanfare = triumph,
   LeapAndFill = drama). Template declares a mood arc and the selector
   picks shapes whose mood fits each position. Anthropomorphizes shape
   semantics but gives users an intuitive authoring knob.

7. **Wilder idea: differentiable shape selection.** Each shape has a
   learned embedding. The planner treats figure selection as a sequence
   prediction problem; optional ML hookup trains a tiny model on a corpus
   of hand-auditioned phrases to predict "what shape would go here." This
   is where mforce's AI-composer roadmap converges with the procedural
   composer.

---

## C. Motivic development

Current state: motifs can be `Reference`'d (copy verbatim) or `Transform`'d
(invert, reverse, stretch, compress, replicate, vary_steps). The primitives
exist but nothing orchestrates *when* to use them, so the same motif tends
to appear identically every time.

### Core concept: development schedule

A passage-level schedule that says "at position N, apply transform T to
motif M." The schedule is expressed in the template; the composer executes
it at realization time.

```cpp
struct MotifDevelopmentEvent {
  std::string motifName;
  int phraseIndex;           // 0-based position in the passage
  int figureIndex;           // 0-based position within the phrase
  TransformOp transform;     // Invert, Reverse, Stretch, etc.
  int transformParam{0};     // e.g. +2 for "sequence up a second"
};

struct PassageTemplate {
  // ... existing fields ...
  std::vector<MotifDevelopmentEvent> motifDevelopment;
};
```

Example: "phrase 2 figure 0: motif_a sequenced up a second." At
realization, when the composer would normally instantiate motif_a
untransformed at that position, it applies the Transform per the event.

### Ideas

1. **Sequential repetition (classical "sequence").** Motif A is stated at
   the tonic in phrase 1, up a step in phrase 2, up a step again in
   phrase 3. Canonical classical development; should be one of the first
   supported patterns.

2. **Fragmentation arc.** Motif starts whole, progressively fragments.
   Phrase 1: full motif. Phrase 2: first half + rest + first half.
   Phrase 3: just the first two notes, repeated. The opposite of the
   encroachment pattern — here the motif SHRINKS instead of the tail
   growing.

3. **Inversion at climax.** When the passage's dynamic/contour peak is
   reached, automatically invert the motif. Gives a "peak moment" that's
   audibly different.

4. **Retrograde return.** Final statement of the motif is the reverse of
   the opening statement. Cyclic symmetry. Rare but beautiful.

5. **Motif as accompaniment vs as melody.** Motifs can be "foreground"
   (as melody) or "background" (broken into arpeggios beneath another
   line). A strategy that places motifs in both roles gives coherent
   texture. This is a multi-part idea though — requires accompaniment
   support that we don't have yet.

6. **Motif combination.** Phrase 1 introduces motif A. Phrase 2 introduces
   motif B. Phrase 3 combines them — either simultaneously (A as
   counterpoint against B), interleaved (A B A B), or fused (A's rhythm +
   B's pitch contour). Requires voice-leading + meter awareness.

7. **Wild idea: derivation graph.** Each motif knows its "parents" — how
   it was derived from other motifs. A directed graph of motif
   relationships. The composer can reason about "what's the family tree
   of this piece?" and make sure motif reuse is musically motivated.
   Close to Schenkerian analysis applied forward as a generation rule.

8. **Wilder idea: thematic transformation à la Liszt.** A single "germ"
   motif is the seed for every theme in the piece. Every motif in the
   motif pool is derived from the germ via transformations. The piece
   has radical unity even across very different-sounding passages.

---

## D. Harmonic awareness — the pipeline bifurcation

Matt's correction made this clear: harmony-first and melody-first are two
different composers, not one composer with a harmony knob. Both should
live in the IComposer registry.

### Melody-first composer (mostly current behavior, cleaned up)

1. **Generate melody as today.** No harmonic context.
2. **Post-hoc harmonization.** A new harmonizer pass analyzes the melody
   and writes a chord progression that fits. Heuristics:
   - Common-tone preservation between chords
   - Avoid V-I with 7 in melody moving up (resolves wrong way)
   - Cadence on V-I where cadence target is tonic
   - Simple rules first; corpus-learned rules later
3. **Implied harmony analysis.** Infer what chord each beat "implies" by
   looking at the downbeat pitch + surrounding pitches. Emit a
   `ChordProgression` derived from the melody. Lets downstream consumers
   use the inferred harmony (for accompaniment generation, for harmonic
   reduction displays, etc.).

### Harmony-first composer (new)

1. **ChordProgression is the primary input.** Before any melodic figure
   is generated, the piece has a chord progression. Can come from:
   - User-authored (pianist starts with a progression in mind)
   - Procedurally generated from a progression library
   - Derived from a style (blues = I-IV-V, jazz = ii-V-I, etc.)

2. **Phrase boundaries align with harmonic resolution points.** A phrase
   ends where a cadence (authentic, half, deceptive) lands. The phrase
   length is determined by the progression, not a fixed beat count.

3. **Figure generation consults the current chord.** When generating a
   figure, the composer knows what chord is sounding over it. Strong beats
   prefer chord tones (root/3rd/5th). Weak beats allow non-chord tones
   (passing, neighbor, suspension).

4. **Voice leading between chords.** When the progression changes, the
   figure's last note before the change smoothly leads to the next chord's
   root or 3rd. Prevents the "modal jumping" feel of purely scalar melody.

5. **Chord-tone cadence math.** The existing `apply_cadence` re-pitches
   to a scale degree. Harmony-first cadence re-pitches to a CHORD TONE of
   the destination chord. The target isn't "scale degree 1" generically,
   it's "the tonic chord's root" which might be on scale degree 1 in C
   major but scale degree 5 if the tonic chord is G over a modulation.

### Hybrid / user choice

1. **Harmony sketches the arc, melody fills it in.** A loose chord
   progression gives the global tension/release shape; the melody-first
   composer uses it only to pick starting pitches and cadence targets.
   Most melodic content is still melody-first. Matt's earlier note about
   `harmonySeeds` fits this model.

2. **Per-phrase override.** Some phrases are harmony-first (e.g., a hook
   that must sit on a specific progression). Others are melody-first
   (e.g., a freely improvised middle section). A phrase-level
   `harmonyMode` enum: Ignore | Consult | Constrain.

3. **Wild idea: negotiation.** The composer generates a melody candidate
   AND a harmony candidate independently, then scores how well they
   match. If the score is below threshold, one or both regenerate.
   Internal "does this sound coherent" check. Probably overkill for v1.

### What goes in the IComposer registry

- **`ClassicalMelodyFirstComposer`** — what we have now, renamed/cleaned
  up. No harmony awareness beyond post-hoc harmonization.
- **`ClassicalHarmonyFirstComposer`** — new. Takes `ChordProgression` as
  primary input. Figure generation is chord-aware. Phrase boundaries
  align with cadences.
- **`ClassicalHybridComposer`** — takes both a melody seed and a harmony
  progression and reconciles them.

Users select their composing approach at the piece level. The existing
`ClassicalComposer` facade becomes `ClassicalMelodyFirstComposer` (or
stays as an alias).

---

## E. Metrical rhythm

Current state: `vary_rhythm` randomly splits or dots notes with 40%
probability. No meter awareness. No sense of strong/weak beats. No
intentional syncopation.

### Ideas, conservative to wild

1. **Beat-hierarchy awareness.** For a 4/4 meter, beat 1 = strong,
   beat 3 = medium, beats 2 and 4 = weak. Subdivisions follow the same
   pattern. Generator places longer durations and contour peaks on strong
   beats by preference.

2. **Meter-pattern library.** A library of rhythm templates per meter:
   `quarter-quarter-half`, `eighth-eighth-quarter-quarter`,
   `dotted-quarter-eighth-half`, etc. Each has a "weight" per context.
   Pick from the library during figure generation instead of random
   splitting. Much more predictable and authorable than `vary_rhythm`.

3. **Syncopation as intentional.** Add an `allowSyncopation` flag per
   figure/phrase/section. When true, rhythm generator can place a long
   note starting on a weak beat and tying across a strong beat. When
   false, all long notes start on strong beats. The current composer is
   effectively "syncopation always allowed, uniformly."

4. **Accent marks on strong-beat notes.** Figures carry accent indicators
   that the conductor uses to vary dynamics (accented notes play at
   +0.15 velocity, non-accented at baseline). Gives "breathing" to a
   phrase without requiring explicit dynamic markings.

5. **Rubato at phrase ends.** Phrases naturally slow down approaching
   the cadence. Add a per-phrase "rallentando curve" that scales unit
   durations larger as the phrase progresses. Classical expressive
   convention; easy to implement at the conductor level.

6. **Groove patterns.** Genre-specific rhythm characters (swing = 2:1
   subdivision, reggae = upbeat emphasis, bossa nova = clave pattern,
   shuffle = triplet subdivision). Baked in as "groove" parameters on
   Section or Passage. Rhythm generator consults groove when picking
   subdivisions.

7. **Wild idea: polyrhythm hooks.** Allow a part to play 3 against 4, or
   2 against 3. Rare in most tonal music but a knob worth having for
   specific genres and moments of complexity.

8. **Wilder idea: rhythm as its own strategy level.** The current
   composer treats rhythm as a sub-property of figure generation.
   Separate it out: a "rhythm strategy" generates a rhythm (a sequence of
   durations) given a meter and length, independent of pitch; a "pitch
   strategy" generates pitches given a rhythm (as a step sequence fitting
   the note count). Two-pass figure generation. Gives fine-grained
   control over each dimension and lets each evolve independently.

---

## What to pick first (my ranking for tomorrow)

If we can only do ONE in the near term:

1. **A — Outline strategies with encroachment model.** You already have a
   concrete use case (My Life) and explicit direction. It's the most
   tractable and ships a visible feature that you can audition. Plus it
   unblocks Phase 4 (OutlinePassageStrategy).

If we have TIME for a second:

2. **D — Harmony-first composer.** This is the largest architectural
   change and has the biggest potential impact on how "musical" the
   output sounds. It's also the one that introduces a whole new
   top-level `IComposer` rather than just tweaking the current one.
   Worth scoping before it gets blocked behind smaller work.

If we have time for a third:

3. **B — Phrase planner pre-pass.** Smaller scope than D, and the biggest
   quality win per unit of effort among the melody-first improvements.
   It's a mostly-local change to `DefaultPhraseStrategy` plus a new
   helper class.

C (motivic development) and E (metrical rhythm) can wait — they're
substantial quality wins but they depend on having a planner (B) and a
more structured composer pipeline (D) to slot into. Do them after
A/D/B so they have a place to live.

---

## Cross-cutting observation

All five areas touch the same underlying pattern: **the composer needs to
plan before it generates.** The current composer is purely procedural —
it generates each figure with only local context. Every improvement we
discussed involves some form of pre-planning: shape plans, encroachment
schedules, chord progressions, motif development events, meter patterns.
That's a natural evolution, but it means the composer's architecture
shifts from "walk and emit" to "plan then walk then emit." Worth keeping
in mind as we decide on the order — if we commit to a planner pass in
area A or B, areas C/D/E all benefit from the same infrastructure.

---

End of brainstorming expansion. Review when you're fresh; redirect freely.
