# Composition lane — review queue

## Awaiting Matt

### 5. The #12 implementation is sitting uncommitted [one word] (run 15)
`corpus/mtd_seg/score_generated.py` (+181 lines) and `corpus_baseline.py`
(+12) were modified 2026-08-03 07:10 by a scheduled run that died before
committing or reporting. They implement backlog #12 — the phrase-ending
closure screen you needed after the final-note verdict. Per the tree guard I
did not touch, run, or commit them.

Reading the diff (not running it), it looks complete and self-justifying: a
`closure` screen using final-note duration ratio against the corpus band plus
where the final onset sits on the beat grid, a batch-level `closure_batch`
reported separately (the run-13 defect — every phrase ending at exactly 1.0 —
is invisible per-melody because 1.0 is MTD's p25, and only shows up in the
batch distribution), and a `LONGEST_TOL` of 0.98 argued from a measured
plateau (0.115 exact vs 0.351 at any tolerance from 1% to 10%).

Verdict decides: adopt (I verify and commit it next run) or discard (I
reimplement from scratch). Until then #12 is blocked and the composite still
cannot see endings.

### 6. Bruckner — modulating over a stationary pedal [listen] (run 15)
`renders/passage_bruckner/` — 4 renders, two chromatic-third rings.
Your run-12 spec, finally buildable. The same I-vi-IV-V re-lit in each key
while the pedal does not move, then Ger6 → I(6/4) → V7 → I back home:

    take 0   C  →  E flat  →  G flat  →  C     (G = 5th, 3rd, alien, 5th)
    take 1   C  →  A flat  →  E       →  C     (G = 5th, 7th, #4, 5th)

The ring is picked so the held G *changes meaning without moving*, which is
the reason to hold it. Two engine gaps had to close first: chord realization
ignored key contexts entirely (stage 3 only covered melody), and the pedal
itself was key-aware, so it drifted 43-43-42-41 — G is not in G-flat major.

Verdict decides: (a) does the harmonic motion read as *going somewhere* over a
static bass, or as unrelated chords; (b) is 8 beats per key station right, or
too fast to register; (c) is G-flat (where the pedal is alien to every chord)
a peak or just wrong.

### 7. Chords by hand vs chords by engine [listen] (run 15)
`renders/passage_chords_ab/` — `ab_hand_N` / `ab_voiced_N` / `ab_smooth_N`,
3 takes. Controlled: identical melody, identical progression, identical
tension curve, identical pedal. The ONLY difference is who spells the chords.

- `hand` — what `pedal_chords` has always done: 3 countermelody parts,
  every note fixed by the generator.
- `voiced` — one harmony part carrying its own progression; engine default
  voicing (root position throughout).
- `smooth` — same, with the `smooth` voicing selector, which inverts to
  minimize motion (E3m/i2, G3M/i1, A3m/i1, C3M/i2).

Verdict decides: whether the voiced path replaces the hand-voiced one in
`pedal_chords` (and by extension whether new strategies should author chords
as chords), and whether `smooth` or plain root position suits this texture.
Note the Ger6 differs slightly between arms: hand spells Ab-Eb-F# (no third),
the named bVI7 gives Ab-C-Eb-Gb.

### 8. Nine voicing test patches were silent; now they aren't [listen + read]
`patches/test_jazz_turnaround_*.json` (9) rendered `peak=0 rms=0` at HEAD —
the whole voicing-selector A/B set from the chord-walker work has been dead
since the Stage-11 rhythm-pattern migration, which no template ever completed.
They now render (16 chords, peak 0.68-0.91), so the A/B you never got to hear
is available: `flat` / `smooth` / `drift` / `random` / `scripted` / `rock` and
the priority ladder `p0` / `p05` / `p1`.

**One read**: the priority ladder collapses — `p05` and `p1` are
BYTE-IDENTICAL renders while `p0` differs. Priority separates zero from
nonzero and nothing in between. Backlog #14; not touched blind because it is
voicing-profile semantics, and what "priority 0.5" should mean is your call.

### 1. Final-note weighting — A/B [listen] (run 13)
`renders/markov_phrases4/` vs `renders/markov_phrases4/before/` — 24 phrases
each, SAME seed, and the final-note draws run on their own rng stream, so
`p07` in one directory is `p07` in the other with a different ending and
nothing else changed (verified: identical spec list, identical note counts).

Measured what real themes do first (`final_note_stats.py`, MTD n=1632 /
Nottingham n=1024): the final note is **2.0x** the median pulse in both
corpora, and is >= every other note in 35% / 53% of themes. The old rule set
it to exactly 1.0x — MTD's p25, Nottingham's p10. Now drawn from the corpus
histogram, snapped to the grid, extend-only.

| | before | after | corpus |
|---|---|---|---|
| median final ratio | 1.00 | 2.50 | 2.00 |
| ratio >= 2 | 0.25 | 0.79 | .51 / .85 |
| final is longest | 0.12 | 0.67 | .35 / .53 |
| ends on an integer beat | 4/24 | 24/24 | — |

Verdict decides: is 2.5x right, too much, or still short; and whether
phrase-end grid completion should stay unconditional (it is what took
ends-on-a-beat from 4/24 to 24/24).

### 2. Passage strategies v2 — built from your specs [listen] (run 13)
`renders/passage_strategies2/` — 30 WAVs, 3 takes x 8 strategies, plus
`suite_*` and a new `chain_suite_*` order. Start with `chain_suite_0`.

- `chain_chromatic_peak_*`, `chain_tonic_fanfare_*`,
  `chain_arpeggio_fallback_*` — your three connective end-transformations,
  each as CHAIN -> GOAL. The chromatic rise is genuine chromaticism (verified
  in rendered pitches: 57-58-59-60-61-62-63); the fanfare is your
  Cq Ce. Cs Cq Cq Ch Cq on the arrived tonic; the arpeggio falls back one
  degree short per cycle, 3 cycles.
- `pedal_chords_*` — chords over the pedal with dissonance rising *by
  measurement* (tension curve 0-0-0-3-3-4), into Ger6 -> I(6/4) -> V7 -> I,
  with the pedal releasing to the tonic for the final chord. Includes the
  melodic-figure-over-chords variant.
- `pedal_buildup_*` — acceleration now caps at 4x.

Verdict decides: (a) do the chain shapes read as *arriving* — is the terminal
transformation doing what you described; (b) is 4x the right ceiling; (c) is
the diatonic buildup in `pedal_chords` too plain before the Ger6; (d) which
of the eight earn the C++ port first.

Two notes: the tonic over a held pedal renders as 6/4, not first inversion —
the pedal is the fifth, so it has to be unless the pedal moves. And part of
the "8x" you reacted to was a measurement artifact, not audio: the old
accel_ratio compared level 0's mean *including* its long final note against
the last level's mean *excluding* its held note, reporting 8x for a real 4x.

### 3. Modulation is live — first real one [listen] (run 13)
`renders/passage_strategies2/modulating_fifths_*` (3 takes). #6 stage 3 landed
this run, so the fifths trip actually modulates now:

    entry 0 key C -> C-E-E-C     entry 2 key D -> D-F#-F#-D
    entry 1 key G -> G-B-B-G     entry 3 key A -> A-C#-C#-A

The F# and C# are the new keys' signatures — pitches this template could not
produce yesterday. This is what "Sequence sounds a lot like Connective due to
key center limitation" was waiting on.

One semantic to be aware of before you spec the Bruckner pedal or the
modulating wandering passage: stage 3 snaps the cursor's PITCH into the new
scale, it does NOT move it to the new tonic. Anchoring each entry at degree 0
renders C and G *identically* (both scales contain C and E). A real sequence
has to offset each entry by the key distance — so neither of those two ideas
will modulate audibly just by adding keyContexts.
Verdict decides: does the modulation read as going somewhere, and do you want
the pitch-preserving reading (same melody re-harmonized) available as a
separate option alongside the transposing one.

### 4. Scorer is blind to the final note [read] (run 13)
`scores.csv` for the two arms of item 1 is **byte-identical** — mean, min and
max composite unchanged to three decimals across a change you could hear
immediately. The composite cannot see phrase endings at all, which is why
this needed your ears rather than a metric. Filed as backlog #12; flagging it
because it means "composite went up" has never been evidence about endings.

## Resolved

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
