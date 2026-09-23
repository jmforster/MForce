# Composition lane — review queue

## Awaiting Matt

### 22. Walk round 2 — forks ANSWERED; walk3 spec awaits your read [read] (2026-09-22)
**Update 2026-09-22 (brainstorm with Matt):** both forks resolved.
(a) is a budget, but over-budget candidates are ranked last, never
eliminated. (b) is data now: `styles/nursery_v1.json`. The design grew:
the rules become readable odds in the genre file, a phrase critic is
added, and best-of-N runs at phrase and passage level. vary_steps may
now change the final step. **Read
`docs/superpowers/specs/2026-09-22-comp-walk3-design.md` and say go or
change.** Deferred: figure-fit rules (backlog 24), melody-first test
(backlog 25). Original entry below.

#### (original) Walk round 2 — ANNOTATED; two design forks await your call [decide] (2026-09-22)
Your annotations are in (docs/matt/comp_walk2_annotated.txt) and
distilled: the round's complaints are one missing grammar — non-chord
tones need stepwise approach AND departure (appoggiatura ban, no
leap-from-passing-tone, extensions like the 13th are NCT-class and fine
only when they step down — Mary's own E-over-G7 proves it), plus the
leading-tone tendency (B→C, B-D-C ok, no downward leaps), plus
compounding ("taken together = computer"). Your F-G-over-C question:
not dice — that case was genuinely uncovered (suspension term needs a
chord change, 96:4 needs V; a fresh NCT left upward in-bar scored zero).
**Before walk3 is specced, two forks need your word:**
(a) Compounding: hard per-phrase departure BUDGET (my lean — one
    licensed oddity per phrase, rest eliminated) or super-additive
    penalties (each further violation costs more)?
(b) Genre profile as DATA now ("NRS v1" ruleset file the template names;
    compound-melody = a future profile level) or keep constants in code
    until the ruleset stabilizes? My lean: data now.
Build will be an OPUS DISPATCH (quota). Original round-2 entry below for
the record.

### (superseded same-day by the annotations above) Walk round 2 — same ten seeds, your ruleset applied [listen] (2026-09-21)
`renders/comp/audition/walk2/` — the annotation round built and re-run
the same evening (spec 2026-09-21-comp-walk2-design.md). Everything you
annotated in 21 became mechanism: the elaboration ladder (late-bar,
additive, sub-half splits rare and mostly dotted, fast notes always
move); seventh-of-V down 96:4; suspension discipline; long notes police
extensions; repetition caps across harmony; penultimate≠final; gap-fill
upvoted; leaps cheap early / dear late; final note must be an
ALREADY-VISITED pitch (hard) and regresses toward the melody's mean
(soft — your "regression to the mean" note, mid-build). 10/10 pass the
full validator including your three rhythm rules; per-seed decision
logs (`mary_walk_s<seed>.log`) answer "why did it do that" with score
breakdowns. Side effect worth knowing: at rep3_b's target of 4 the
ladder is deterministic (= Mary's own h→q q), so this batch has MARY'S
RHYTHM everywhere — all variation is pitch, which isolates your pitch
rules cleanly; rhythm variety returns by raising the target (one number)
when you want it back.
`walk2_passages.txt` inside for annotation, same vocabulary.
**Score each seed against your own walk1 annotations: fixed / missed /
new smells.** My own suspect for a new smell: early-register wandering
(cheap early leaps) — s109 visits G3, s104 hits C6 in bar 2.
Gates: engine_tests 3342; template null gate 20/20 byte-identical.


### 21. Walk round 1 — ten generated siblings [listen] (2026-09-21)
`renders/comp/audition/walk1/mary_walk_s100..s109_1.wav` — the first
GENERATED output of the reset. Same Mary harmony and figure slots, but:
only `head`, `rep3` and the closing whole note are authored notes;
`rep3_a`/`rep3_b`/`head_a` are synthesized from their parents by the
declared transforms (your naming recipe, verbatim); and every figure's
starting degree is chosen against the chords by the new anchor selector —
your three rules hard (phrase-opening downbeat / phrase-final note =
chord tone; passage-final = 1), chord-tone preference with the stacked
bar-final/figure-final/long-note boosts, cursor proximity, and the
parallel-repeat intent pinning the consequent's opening to the
antecedent's. All ten passed the rules validator mechanically (10/10)
before reaching you; engine gates: engine_tests 754, template null gate
19/19 byte-identical (everything is opt-in).

Known and accepted (spec §7): exact Mary is not in this batch's support.

**The questions: legal nursery tunes? Which siblings please you, which
fall down, and how do the falls sound?** Verdicts steer round 2 — keepers
calibrate the weights, wincers name the next rule (the interior
non-chord-tone hole and re-shape-to-fit are the queued suspects).

**FIRST VERDICT (Matt, same day, overall): "almost brain-destroying to
listen to because each is so close to Mary" — ear-known rules broken that
the system lacks, some broken in a GOOD way. STANDING ORDER: the lane
does not move off these ten until all ten are perfect; per-sibling
annotations in progress (passage strings emitted to
renders/comp/audition/walk1/walk1_passages.txt).**

Transform-specific verdict, recorded verbatim-adjacent:
- Transforms MOSTLY SUCCESSFUL except **head_a — the worst offender**
  (vary_steps on the identity motif; his original intent was a
  REARRANGEMENT of head's cell, not random perturbation).
- **Non-idiomatic: super-fast-note injections in the FIRST half of a
  bar.** Rhythmic speed-up idiomatically belongs in the SECOND half,
  leading into the concluding bit.
- His elaboration ladder, least→most altered (E E Eh family):
  1. split the half into 2 quarters → E E E E (Mary's own move)
  2. split a quarter into 2 eighths → E E-D E E — ADDITIVE: the last
     quarter STAYS. Not E E-D E(h): "we accelerate then put the brakes
     on, doesn't lead into the concluding bit."
  3. split again, and again, LEAVING THE LAST MOD → E E-D E F-E (or
     E E-F E E-D) — the newest subdivision rides the bar's end.
- Derivable for round 2 (once annotations are in): elaboration placement
  is late-bar-weighted, depth is progressive (quarters→eighths before
  sixteenths anywhere), never lengthen after a split (no brakes), and
  the ladder is one operator applied cumulatively — not complexify's
  uniform-random placement.


### 20. The crawl: Mary from a fully-specified template [listen] (2026-09-21)
`renders/comp/audition/crawl1/mary_crawl_1.wav` — the comp-restart campaign's
first artifact (backlog 23, your five ground rules from this morning).
Harmony went into the template first and fully specified (C | C | G7 | C,
twice, root-position piano blocks at octave 3); the melody is your own
UI-saved passage string (`scores/baselines/passages/Comp_Mary.psg`, your `|`
between the two structural phrases) run through the new loader derivation —
Locked figures split at barlines, connectors carrying the bridges. Melody on
oboe1, chords on piano_default per the new instrument rule. No generation,
no cadence machinery, nothing drawn from a seed.

Verified mechanically before it reached you: all 26 melody events match the
transcription beat-for-beat and pitch-for-pitch; the 8 chords are C-E-G and
G-B-D-F at the right bars; the two-instrument mix is normalized (raw sum
peaked 1.19, scaled to 0.98 — single-instrument renders untouched, null gate
27/27 byte-identical).

**The question: is this Mary, competently harmonized?**
YES → template + passage + render freeze as the crawl baseline, and the walk
stage (generated figures fitted onto a chord timeline) gets its brainstorm.
NO → diagnosis round.

Round-2 addendum (same day, no new listen needed): the STRUCTURAL template
(`scores/baselines/template_mary_structural.json` — named motifs, `head`
referenced by both phrases = the parallel period as data, connectors
carrying the bridges, zero spelled-out notes) renders **byte-identical**
audio to this WAV. Your verdict on the sound covers both templates. En
route it proved the reference+connector path correct for the first time
(the old template_mary's connectors never parsed) and pinned the authoring
convention: template connectors are dense per figure, `connectors[i]` =
bridge INTO figure i, `[0]` null/dummy — not "between figures".


### 19. Octave naming convention — scientific or house? [decide] (2026-08-12)
The music model's core (Pitch::note_number, parse_note_input, passage
parser, pitch_reader) uses octave*12: "C4" = MIDI 48, one octave below
scientific pitch (Iowa/MIDI standard C4 = 60 = middle C). The UI keyboard
panel alone uses scientific ((octave+1)*12) — internally inconsistent.
Numeric-MIDI paths (CMA-ES pipeline, reference scoring) are unaffected.
2026-08-12 pm: keyboard panel aligned to HOUSE convention (octave*12) —
the app now agrees with itself; Matt not necessarily opposed to option
(a) migration later. SWEEP LIST if we migrate to scientific (everything
that encodes a note NAME or an octave number; raw-MIDI artifacts are
safe):
- scores/baselines/*.dun (DURN: note names + header octave), and any
  future .dun
- scores/baselines + scores/pending templates/specs that set
  startingPitch/entry pitches by name or octave field (test_k467_*,
  template_*, test_passage_*, test_jazz_*)
- passage strings anywhere ("C4 e D4 e" — docs, tools, transport
  defaults)
- chord octave fields (parse_chord_token --octave semantics, chord
  test patches)
- PatternLibrary .ppl sources in lib/ppl (if pitched by name)
- mforce_cli flag defaults (--octave 3/4), docs/examples using names
- UI transport noteStr defaults + saved g_transport state in patches
- CLAUDE.md/docs examples with note names
Raw-MIDI (safe, no sweep): patch score blocks, paramMap curves,
CMA-ES pipeline, Iowa reference, renders.
Surfaced by Matt comparing renders against Iowa file labels.

> PATHS 2026-08-10: housekeeping moved comp render dirs to
> renders/comp/pending/ and the live template/jazz-turnaround inputs to
> scores/pending/ (patch/score split, docs/patch_triage_2026_08_10.md).
> UNBLOCKED 2026-09-05 (run 27): items 11/13/17, whose renders died in the
> 2026-08-08 purge, are re-rendered at HEAD with the same seeds/recipes
> under **renders/comp/audition/** (the post-09-03 queue location; the
> pre-purge items above still say pending/). All seven items here are now
> verdictable.

### 18. Cadential arrival — held, or approached? [listen] (run 26)
`renders/comp/pending/cadential_arrival_ab/` — `template_golden_phase1a_{held,approach}`
and `template_shaped_test_{held,approach}`. Same template, same seed; the only
difference is how the final figure of the *cadential* phrase is shaped.

`held` is what you have been hearing since 2026-04: the arrival is forced to a
single long note. That workaround predates `apply_cadence` growing a real tail
rebuild, and a single note defeats it — there is nothing to reshape.
`approach` hands it a real figure, so it steps into the target and settles.

Two things worth knowing before you listen, because both contradict what I
expected:
- The leap the workaround is usually blamed for **does not happen**. Both arms
  enter the final note by +1 semitone on both templates.
- The held arm does **not** deliver the longer arrival it exists for. Final
  note 1.00 / 0.91 beats held, against **1.54 / 1.74** approach.

So the audible difference is the approach contour and a noticeably longer
arrival, not a leap being removed.

Verdict decides the default for `PhraseTemplate.cadentialArrival`. Both stay
authorable per phrase either way. (`template_shaped_test` was rendering pure
silence until this run — it is one of six that were; see the run-26 report.)

### 17. Passage endings — beat or barline? [listen] (run 18; re-rendered run 27)
`renders/comp/audition/passage_end_grid/{off,beat,bar}` — the same 21 passages
three times, `endGrid` the only difference. Re-rendered 2026-09-05 at HEAD,
same seeds (README inside); faithful to run 18 down to the individual
off-grid end times (9.375, 37.250, 50.750...). `off` is what you had been
hearing (6/21 end on a beat), `beat` is the new default (21/21 on a beat,
8/21 on a barline), `bar` quantizes to the 4/4 barline (21/21 both).

Passage endings landed wherever the arithmetic left them — 9.38, 37.25, 50.75
— because the phrase path has grid-completed since run 13 and the passage path
never got the equivalent. That is fixed and is not the question. The question
is how far to round: to the beat, which moves the ending as little as
possible, or to the barline, which is where a phrase actually stops but can
add up to three beats of held final note.

Verdict decides the default for `PassageTemplate.endGrid` (1.0 vs 4.0). Both
stay authorable either way, and `0` turns it off for a pickup or an elided
handoff.

### 16. Should a section key move the tonic? [listen] (run 18)
`renders/comp/pending/section_key/` — three arms, same music, same seed, sections C / G / D.
`control` (no section keys, everything in C) · `accidentals_only` (what
landed) · `transposed` (same plus a per-passage `startingPitch` offset by the
key distance). Three distinct renders by sha256.

`sections[].keyName` now works — it was silently dropped before. But it does
what the key-aware path has always done: it changes which **accidentals**
appear, it does not move the cursor to the new tonic. So `accidentals_only`
is still six notes hovering around C, with an F# in G and an F#/C# in D — and
in D major the C3 entry is not even a scale tone, so it snaps to C#.
`transposed` is what most people mean by "this section is in G".

Verdict decides: does `keyName` on a section stay a pure key-signature
statement (author transposes the entry themselves, which is what run 13
concluded), or does it also offset the passage entry by the key distance —
and if so, is that the default or an opt-in flag? Note the second one has a
sharp edge: a template that sets both would get transposed twice.

### 13. Phrase endings, recalibrated [listen] (run 17; re-rendered run 27)
`renders/comp/audition/markov_phrases6/` (new default `calib`) vs
`renders/comp/audition/markov_phrases6/longest/` (the v3 rule you called
"fine for now"). Re-rendered 2026-09-05 at HEAD, same seed/recipe (README
inside). Same seed, same 24 phrases, **only the last note differs** —
re-verified event-by-event on this render, 24/24 pairs.
You said fine; the measurement said the rule was running at ratio p50 3.0 /
p95 11.0 against a corpus of 2.0 / 6.0. It turned out the DRAW was already
right — grid completion was ceiling every phrase onto the next barline and
adding a further quarter-beat at the median. v4 keeps the final note as the
phrase's longest just as often in spirit (17/24 vs 24/24 this render, corpus
0.354) but it can no longer invent a length the phrase never contained
(ratio median 2.50 vs 4.00, max 19 vs 27).
Verdict decides: does the shorter ending still land, or did v3's extra length
carry something? `--final-rule longest` restores v3 exactly.

### 14. Passage-mode scoring — two semantics questions [read] (run 17)
`renders/comp/pending/passage_scores/` (74 passages scored, README explains the screen).
The screen itself is verified mechanically — I am not asking you to bless the
numbers. Two things it cannot decide on its own:
(a) A **descending** sequence scores 0.49–0.51 because the tension criterion
asks "does this build". True, but a descending sequence into a cadence is a
legitimate passage. Should a passage declare its INTENT (build / relax /
static) and be scored against that intent instead of against "build"?
(b) `pedal_buildup` is 8/8 "does not land on the tonic" — it ends on its
dominant pedal, which is arguably exactly right. Should the arrival term know
what the passage was aiming at rather than always meaning the piece tonic?
Verdict decides whether passage mode grows a passage-type parameter, or stays
one fixed set of criteria that some passage types will always score low on.

### 9. Bruckner v2 — your two progressions, pedal fixed [listen] (run 16)
`renders/comp/pending/passage_bruckner2/` — `bruckner2_ger6_0/1`, `bruckner2_neap_0/1`.
Your run-15 spec exactly: G Em A7 D Bm Bdim7 -> Ger6 (or Neapolitan Ab) ->
C(6/4) -> G -> C. Point (d) verified in every event dump: the G3 pedal now
holds under the Ger6, the C(6/4) AND the plain G (no G7 anywhere); the bass
moves to C only on the final downbeat ([43 x9, 36]). Take 0 = 4 beats/chord
(~29s), take 1 = 8 beats/chord (~57s, the v1 "station" pacing) — 10 chords
made one default feel wrong, so both are here.
Verdict decides: Ger6 vs Neapolitan flavor, 4 vs 8 beats pacing, and whether
this cadence becomes the pedal-family default.

### 10. pedal_chords is now engine-voiced [listen] (run 16)
`renders/comp/pending/passage_chords_ab2/` — 3 takes. `pedal_chords` routes through the
harmony part with smooth voicing; your caveat is enforced by a new per-chord
VoicingPin: Ger6 root position -> I(6/4), bass Ab->G, top Gb->G (verified
[44,48,51,54] -> [43,48,52,55] in all dumps, register matched to the smooth
arm you picked). Hand-voiced path survives as `pedal_chords_hand`.
Verdict decides: does the pinned arrival read as you intended, and does the
hand path stay reachable or retire.

### 11. Literal repeats can now transform [listen] (run 16; re-rendered run 27)
`renders/comp/audition/markov_phrases5/` vs
`renders/comp/audition/markov_phrases5/before/` — same-seed A/B, backlog #10
closed. Re-rendered 2026-09-05 at HEAD, same seed/recipe (README inside).
Repeated A/B occurrences can invert / retrograde / rotate / ornament instead
of only transposing; first occurrence of a family never transformed; a range
guard reverts span-breakers. Conservative 0.3 probability -> **6 of 24 pairs
differ** at HEAD: run 16's four (p00 retrograde, p01 + p17 rotate, p19
invert) plus p14 (invert) and p18 (ornament), which run 17's offender-first
range guard recovered; the other 18 are byte-identical on purpose (verified
by sha256 on this render).
Composite flat (0.822 vs 0.814) — this change is for ears, not metrics.
Verdict decides: do the transformed repeats read as variation or as wrong
notes; and whether you want a denser batch (0.6 probability) to audition
more instances.

### 12. Voicing A/B renders — now actually delivered [listen] (run 16)
`renders/comp/pending/voicing_ab/` — the 9 WAVs item 8 promised and did not deliver
(README inside). flat / smooth / drift / random / scripted / rock, plus the
priority ladder p0 / p05 / p1. Confirmed by hash: p05 == p1 byte-identical,
only p0 differs — "priority" today separates zero from nonzero and nothing
else. What 0.5 SHOULD mean is backlog #14, awaiting your semantics.

## Resolved

2026-08-04 (Matt, folded in run 16): #12 closure screen "verify and commit"
-> verified (the run-13 byte-identical A/B now separates: ratio-med 1.00 /
longest 0.125 vs 4.00 / 0.958) and committed. NEW MEASUREMENT it exposed:
the ending rule OVERSHOOTS the corpus (gen median 4.0x vs corpus 2.0x, 25%
past corpus p95) -> backlog #16. Bruckner (a) minor3rds_0_1 best, (b) 8
beats ok, (c) Gb aggressive -> two authored progressions replace the rings
(item 9); (d) pedal must hold under Ger6, no G7 -> VoicingPin + fix (item
9). Chords A/B: smooth wins with root-position-bVI7 caveat -> pins (item
10). Final-note weighting: "fine for now."

2026-08-04 (Matt, folded in run 16):
- #12 closure screen: "Verify and commit" -> verified live (the byte-identical
  run-13 A/B now separates: ratio med 1.0 vs 4.0, longest 0.125 vs 0.958),
  one --csv crash fixed, all 5 corpus baselines rebuilt with whole-tune
  closure anchors. COMMITTED 0fc3f29. Batch view also shows the new ending
  rule OVERSHOOTS the corpus (med 4.0 vs 2.0 by raw-IOI convention, 25% over
  p95) — noted, no action while "fine for now" stands.
- Final-note A/B: "Fine for now" -> closed.
- Bruckner: minor3rds_0_1 best; 8 beats/station right; Gb "aggressive";
  two new progressions specced + pedal-under-Ger6 bug called out -> v2 in
  flight (item 6).
- Chords A/B: smooth wins with the Ger6-arrival caveat -> adoption in
  flight (item 7).
- Voicing patches: no renders delivered -> being rendered (item 8).

2026-08-02 (Matt, folded):
- Phrase v3: good; final note needs MORE weight toward longest ->
  strengthening now.
- Passage strategies: "most musical/plausible output yet." Verbatim
  musical specs preserved for the v2 set:
  > CONNECTIVE end-transformations before arrival: (a) 4-note fig x7
  > ascending -> chromatic rise to peak, pause, descend to cadence;
  > (b) 4-note fig x6 descending to tonic -> rhythmic fanfare on arrival
  > (Cq Ce. Cs Cq Cq Ch Cq); (c) 6-note fig x3 ascending -> arpeggio
  > falls back almost to start, x3 cycles -> climactic cadence.
  > PEDAL: 8x accel too much. The cliche: chords over pedal with
  > INCREASING DISSONANCE (chords excluding the pedal tone) -> Fr/Ger 6th
  > over pedal -> V(7) -> full cadence; variant with melodic figure over
  > the chords; Bruckner extreme = modulating over the pedal (needs key
  > fix). WANDERING: wants modulation (sweet major theme -> sudden dim7
  > -> minor keys...) — gated on stage 3. SEQUENCE: sounds like
  > connective until key fix. SUITE: "exciting taste of things to come."
  Directive: stages 3+4 (new dev, not port), then v2 set from the specs.
- Corpus flavor: no action. Essen: order-2 backoff stays (no objection).
- Run-13 status against the directive: the v2 set is DONE (item 2 above) and
  the final-note weighting is DONE (item 1). Stages 3+4 were found already
  written but uncommitted in the tree by the run that died on 2026-08-01 —
  see the run-13 report; adopted and verified in the same run.

2026-07-30 (Matt, folded in run 10):
- Repeat/varytail phrases: "some progress, a few close to coherent;
  varytail is a BIG improvement in musicality." Complaints -> run-10
  fixes: phrase-length clamp too short (raise the TOP of the range, keep
  short end); short note durations should mean MORE notes, not
  ultra-short phrases (duration-aware sizing).
- Contrast-B A/B: contrast wins by a little; independent stays as an
  occasional option. Two rule gaps flagged and under mechanical
  investigation in run 10: (a) no final-note lengthening rule in this
  path (phrases ending in a flurry / long-then-short — "very unlikely in
  real music"); (b) figure joins never land on downbeats (everything
  reads as elision; alignment should be the default, elision occasional).
- Essen: cloned by Matt to corpus/essen-folksong-collection -> ingest
  running (run 10).


2026-07-30 (Matt verdict on old item #1, folded run 8):
- "Repeated figures repeat literally on the same tone; want repeat on different
  pitches / same-pitch-modified / both." -> ALREADY implemented run-7 as the
  three families in renders/markov_phrases2/ (transposed / varytail / combined);
  now mapped explicitly as review item #2 above, awaiting your ears. Old item #1
  (literal-repeat markov_phrases/ re-listen) is superseded by #2.
- New-goal fragments "New passage strategies" / "Figure transform operations"
  decomposed to backlog: passage strategies = #6 (re-raised); figure transforms =
  #10 (library landed run 8, wiring into phrase-builder queued).
- Volume note: taken. Run 8 ran 4 fronts (contrast-B, neural next-note,
  self-similarity screen, transform library).


2026-07-29 (Matt, run-5 folding):
- Parallel A/B: no single default — per-phrase choice (running pitch /
  explicit / parallel restart) stands as the complete mechanism menu.
- Scorer sanity: "second figure all one note" -> root-caused (range-guard
  selection bias) + fixed + monotony screens added to composite.
- Phase-2 phrases: "computer music; forget norm-breakers" -> outlier
  patterns dropped; re-listen queued post-fix.
- Essen license: cleared by Matt -> ingest queued (backlog 3c).
- Nottingham: cloned by Matt -> ingested (1024 tunes, folk bake-off run).
