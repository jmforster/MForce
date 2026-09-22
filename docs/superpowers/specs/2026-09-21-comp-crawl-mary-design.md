# Comp crawl — Mary from a fully-specified template

Date: 2026-09-21. Status: DESIGN, awaiting Matt's review.
Campaign: comp backlog 23 (steering 2026-09-13 decision 4).
Brainstormed interactively with Matt this session; this spec records the
decisions, it does not invent past them.

## 1. Ground rules (Matt, 2026-09-21 — standing law for the comp lane)

1. **We crawl beautifully before attempting to walk.**
2. **Harmonic content is the first thing that goes in the template, fully
   specified.**
3. **Figure generation can stay independent, but a figure gets fitted into
   the current underlying chord in a musical fashion** (with Mary in C,
   "dq dq" must never realize as F–E–D).
4. **The model stack is solid** — extended as needed, never rebuilt.
5. **All generator code except the Markov Figure generator is cataloged but
   shelved** until needed, or retired.

Correction on record: the Figure remains the atom, but "the atom was the
hard part and we solved it" is overstated. The old hope that a K467-class
opus falls out of combining Figures with enough code is dead.

## 2. Goal and non-goals

**Goal (the crawl):** a fully-specified template — harmony first, melody as
Matt's UI-verified passage string — renders through the existing model
stack into a WAV (melody on oboe1, chords on piano_default — see §5)
that is unmistakably Mary Had a Little Lamb, competently harmonized.
Zero generation anywhere in the pipeline.

This is where the lane started long ago and moved on without conquering
it. Conquering it proves the template schema and the
realize-against-a-chord path are sound — the floor the walk stage
(fitted figure generation, ground rule 3) will stand on.

**Non-goals, explicit:**
- No generation, and no plausibility bar. Mary only — no sibling tunes
  this round.
- No cadence machinery runs. Fully-specified means there is nothing for
  it to decide; the chords ARE the cadences.
- The walk stage (fitting independently generated figures onto the chord
  timeline) is out of scope; it gets its own brainstorm after the crawl
  verdict.
- Which tier owns voicing (score vs lead-sheet) is deliberately NOT
  decided — parked in docs/autonomy/IDEAS.md §comp, 2026-09-21 entry.
  The crawl's chord-symbols-plus-realization is expedience, not the
  decision.

## 3. Inputs and conventions

**Melody source of truth:** `passages/Comp_Mary.psg`, saved by Matt from
the UI transport (he verifies by ear before saving — this replaces
hand-edited DURN for comp material).

**NEW CONVENTION — comp-purpose passage saves:** `|` marks STRUCTURAL
phrase boundaries only. (In dsp/articulation usage `|` is breath/slur
grouping; comp saves collapse the two — a comp phrase mark is both the
structural boundary and the breath.) Mary carries exactly one `|`: after
bar 4's `Gh`, splitting the parallel period into antecedent (bars 1–4)
and consequent (bars 5–8).

**Known fix pending on the file (Matt's, 10 seconds):** as first saved it
carries a leftover second `|` after bar 3 AND a missing space (`|Eq`),
which the whitespace-splitting parser rejects
(engine/include/mforce/music/parse_util.h — token `|Eq` throws). Target
content:

```
Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw
```

**`/` is reserved, unused:** it parses nowhere in melodic passage strings
today (only inside drum tokens). If a future need arises to mark
structure separately from articulation in one string, `/` is the token
to claim. Not claimed now.

## 4. Template schema, crawl form

A new template file (working name `scores/baselines/template_mary_crawl.json`;
the loader-era `template_mary.json` stays put — see §7). New/extended
fields, harmony FIRST in the file as a statement of ground rule 2:

- **Harmony block** on the section: the chord timeline, fully specified,
  bar by bar. Mary in C:
  `C | C | G7 | C | C | C | G7 | C` (one chord per bar, 4/4, 8 bars).
  Spelling reuses the EXISTING chord-token vocabulary
  (`parse_chord_token`, `Root:Type` + duration — exact field syntax
  settled at implementation against the parser; no new chord grammar is
  invented). Structural facts — bar count, where the inconclusive and
  conclusive arrivals fall — are implied by the chords. Harmony is
  levels 3/4 of the four-level model living where they always were
  (Section.chordProgression); no model reshape.
- **Melody block** on the melody part: a reference to the passage file
  plus an anchor octave (`.psg` notes carry no octave; house convention,
  octave*12). Example shape:
  `"melody": {"passageFile": "passages/Comp_Mary.psg", "octave": 5}`.
- **Loader derivation** (compose time, no offline converter tool): the
  passage string becomes Figures/Phrases through the existing model —
  barline = figure boundary (by beat count against the meter), `|` =
  structural phrase boundary, startingPitch derived from the first note
  + anchor octave. The Figure machinery is exercised for real; that is
  the point of the crawl.
- Passage phrase marks flow through to onsets as they already do
  (breath at each phrase start — two breaths in Mary), so the comp lane
  inherits the onsets-v2 articulation work for free.

## 5. Realization

- **Harmony part** (role `harmony`): block chords, one voicing per chord
  change, held for the chord's duration. Root-position triads/sevenths
  to start (Matt's explicit license: "any voicing you want, even all
  uninverted"); the run-16 engine-voiced path with smooth voicing stays
  available behind a flag, not default. No accompaniment pattern
  language — whole-bar blocks.
- **Melody part**: the derived phrases, exactly as authored.
- **Instruments — standing rule (Matt 2026-09-21, until otherwise
  stated): melody on oboe1** (patches/library/winds/oboe1.json),
  **accompaniment on piano_default**
  (patches/library/keys/acoustic_piano/piano_default.json). This
  supersedes the 2026-09-05 all-piano_default comp rule. Side benefit:
  the melody rides the wind loop's onsets machinery, so the two breath
  phrases are audible as breaths.

## 6. Provenance

`passages/` is Matt's UI bench and stays gitignored (same standing as
pending/). When a template freezes as a baseline, its `.psg` gets a
committed copy under `scores/baselines/passages/` so no baseline ever
depends on an ignored file. The template references the committed copy
once frozen.

## 7. The old form

`scores/baselines/template_mary.json` (figure-JSON melody, no harmony)
stays put as a loader-era baseline with null-test coverage. The
passage-string melody block is an ADDITIONAL melody source, not a
replacement; the figure-JSON form remains legal. No migration.

## 8. Shelve catalog (ground rule 5)

One new doc, `docs/autonomy/comp/GENERATORS.md`: every generator
enumerated with status LIVE / SHELVED / RETIRED and a one-line what-it-was.
- LIVE: Markov Figure generator (the atom source).
- SHELVED until recalled: AFS, Wandering/Bruckner/sequence/suite passage
  strategies, phrase-builder transform wiring, cadence generation
  (apply_cadence and arrival shaping), passage-mode scorer, voicing
  selector machinery beyond what §5 uses.
- Code untouched — cataloging is a declaration of service status, not a
  deletion. Retirement is Matt's call per entry, later.

## 9. Gates and exit

- Render lands in `renders/comp/audition/crawl1/` with a plain-language
  REVIEW entry (REVIEW is Matt's interface).
- **Backlog 21 rides along**: zero-event renders become failures in
  `null_test_templates.py` and the render harness — cheap, and this is
  the harness it belongs in.
- Roundtrip: the crawl template survives load→save→load.
- Existing comp null tests stay green (template_mary.json unchanged).
- **Exit = Matt's verdict**: "that's Mary, competently harmonized."
  On the verdict, template + `.psg` copy + render freeze as the crawl
  baseline that all walk-stage work regresses against.

## 10. What follows (recorded, not scoped)

The walk stage: independently generated figures (Markov atoms) fitted
onto the fully-specified chord timeline "in a musical fashion" — the
fit rule (strong-position chord-tone discipline) gets its own brainstorm
→ spec once the crawl is conquered.
