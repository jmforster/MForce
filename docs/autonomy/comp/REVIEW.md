# Composition lane — review queue

## Awaiting Matt

### 16. Should a section key move the tonic? [listen] (run 18)
`renders/section_key/` — three arms, same music, same seed, sections C / G / D.
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

### 13. Phrase endings, recalibrated [listen] (run 17)
`renders/markov_phrases6/` (new default `calib`) vs
`renders/markov_phrases6/longest/` (the v3 rule you called "fine for now").
Same seed, same 24 phrases, **only the last note differs** (README inside).
You said fine; the measurement said the rule was running at ratio p50 3.0 /
p95 11.0 against a corpus of 2.0 / 6.0. It turned out the DRAW was already
right — grid completion was ceiling every phrase onto the next barline and
adding a further quarter-beat at the median. v4 keeps the final note as the
phrase's longest just as often in spirit (0.62 vs 0.785 measured, corpus
0.354) but it can no longer invent a length the phrase never contained.
Verdict decides: does the shorter ending still land, or did v3's extra length
carry something? `--final-rule longest` restores v3 exactly.

### 14. Passage-mode scoring — two semantics questions [read] (run 17)
`renders/passage_scores/` (74 passages scored, README explains the screen).
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
`renders/passage_bruckner2/` — `bruckner2_ger6_0/1`, `bruckner2_neap_0/1`.
Your run-15 spec exactly: G Em A7 D Bm Bdim7 -> Ger6 (or Neapolitan Ab) ->
C(6/4) -> G -> C. Point (d) verified in every event dump: the G3 pedal now
holds under the Ger6, the C(6/4) AND the plain G (no G7 anywhere); the bass
moves to C only on the final downbeat ([43 x9, 36]). Take 0 = 4 beats/chord
(~29s), take 1 = 8 beats/chord (~57s, the v1 "station" pacing) — 10 chords
made one default feel wrong, so both are here.
Verdict decides: Ger6 vs Neapolitan flavor, 4 vs 8 beats pacing, and whether
this cadence becomes the pedal-family default.

### 10. pedal_chords is now engine-voiced [listen] (run 16)
`renders/passage_chords_ab2/` — 3 takes. `pedal_chords` routes through the
harmony part with smooth voicing; your caveat is enforced by a new per-chord
VoicingPin: Ger6 root position -> I(6/4), bass Ab->G, top Gb->G (verified
[44,48,51,54] -> [43,48,52,55] in all dumps, register matched to the smooth
arm you picked). Hand-voiced path survives as `pedal_chords_hand`.
Verdict decides: does the pinned arrival read as you intended, and does the
hand path stay reachable or retire.

### 11. Literal repeats can now transform [listen] (run 16)
`renders/markov_phrases5/` vs `renders/markov_phrases5/before/` — same-seed
A/B, backlog #10 closed. Repeated A/B occurrences can invert / retrograde /
rotate / ornament instead of only transposing; first occurrence of a family
never transformed; a range guard reverts span-breakers. Conservative 0.3
probability -> exactly 4 of 24 pairs differ: p00 (retrograde), p01 + p17
(rotate), p19 (invert); the other 20 are byte-identical on purpose.
Composite flat (0.815 vs 0.814) — this change is for ears, not metrics.
Verdict decides: do the transformed repeats read as variation or as wrong
notes; and whether you want a denser batch (0.6 probability) to audition
more instances.

### 12. Voicing A/B renders — now actually delivered [listen] (run 16)
`renders/voicing_ab/` — the 9 WAVs item 8 promised and did not deliver
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
