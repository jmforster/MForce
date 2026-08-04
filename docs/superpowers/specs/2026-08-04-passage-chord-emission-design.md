# Passage-level chord emission — design

Wolfie, comp run 15 (2026-08-04). Backlog #6 follow-on ("chord emission from
the template layer", the gap run 14 documented).

## The gap, quoted from where it was found

`corpus/mtd_seg/passage_strategies.py`, inside `pedal_chords`:

> NOT role "harmony": that role produced ZERO events (the composer realizes
> harmony parts from a chord progression, not from phrases). Verified by
> inspecting the piece JSON, which is also why the voices are countermelody
> parts carrying explicit held notes.

So the prototype's "chords over a pedal" is three hand-spelled countermelody
parts, one per chord voice, each unit carrying a scale-degree DELTA from the
voice's previous note. Consequences:

- the voicing tier (VoicingSelector / VoicingProfile / ChordDictionary) is
  bypassed entirely — the chords are pre-voiced by the author;
- a passage cannot carry its own harmony at all: chords come only from the
  SECTION, so two passages in one section cannot have different progressions;
- an altered chord (the German 6th the cadence needs) is spelled as three
  independent accidentals rather than as one chord.

## Second finding — the harmony path is dead at HEAD

Measured before designing anything:

    mforce_cli --compose patches/Additive1.json ... \
        --template patches/test_jazz_turnaround_smooth.json
    peak=0 rms=0
    Harmony for 'Main': [0-64: 16 chords]
    Part 'chords': 0 events, 0 passages

The section timeline has all 16 chords and the part emits nothing. Cause:
`realize_chord_parts_` bails when a passage has no `rhythmPattern`
(*"Without one, no chord events are emitted (cfg-driven default-pattern
fallback was removed at Stage 11)"*), and the nine `test_jazz_turnaround_*`
patches — the entire voicing-selector A/B set — still carry the old
`chordConfig.defaultPattern`, which nothing reads. Every one of them is
silent.

## Design

One idea covers both: **an authored progression already carries durations, so
it should emit chords by itself. A `rhythmPattern` RE-articulates it.**

1. `PassageTemplate.chordProgression` (optional). When present it is the
   harmony source for that part in that section, overriding the section
   timeline. The section's own timeline is untouched — other parts and the
   melody's chord-tone lookups still see section harmony.

2. Emission gets two modes in `realize_chord_parts_`:
   - **span** (no `rhythmPattern`): one event per chord in the timeline, at
     the chord's own authored duration. This is the new default.
   - **pattern** (`rhythmPattern` present): current behaviour — the bar
     pattern strikes, each strike sampling the timeline at its position.
   Both go through the same voicing block (selector + profile selector, or
   the legacy inversion/spread path), so span mode is not a second-class path.

3. JSON: one shared parser for a progression, used by section AND passage,
   accepting both the flat authoring form `[{degree, alteration?, quality,
   beats}]` and the canonical `{chords:[...], pulses:[...]}` that
   `to_json(ChordProgression)` writes. `alteration` was silently unavailable
   in the flat form, which is what made bVI7 (= Ger6) unauthorable.

   This also fixes a latent round-trip bug: `to_json(SectionTemplate)` emits
   the canonical form and `from_json` only accepted the flat one, so a
   template that had been written back out could not be read again.

## What this buys immediately

`pedal_chords` becomes expressible as one harmony part with an inline
progression — including the German sixth as `{degree: 5, alteration: -1,
quality: "7"}` (Ab-C-Eb-Gb, enharmonically Ger6) — plus one bass part for the
pedal, instead of three hand-voiced countermelody parts. And the nine dead
voicing test patches come back.

## Verification plan

- build `mforce_cli` + `mforce_ui`; `mforce_ui --stamp` exits 0.
- `test_jazz_turnaround_smooth` goes from 0 events to 16, with pitches
  checked against the written progression (Dm7-G7-CM7-A7).
- a new template exercises the passage-level path with the Ger6 cadence over a
  pedal; assert the Ger6 bar's pitch set is {Ab, C, Eb, Gb}.
- regression: a template with a rhythmPattern renders byte-identical to HEAD
  (pattern mode untouched).
- the melody-only templates the comp lane renders (no harmony part) must be
  byte-identical — the change must not reach them at all.
