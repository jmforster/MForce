# Articulations and Ornaments Design

## Status
Data model changes for Ornaments are already implemented. Articulation realization and Ornament expansion in the Performer are next.

## Summary

Ornaments and Articulations are *declarative instructions* on the score. They tell the Performer what to do, but how (or whether) they are realized depends on the Performer and ultimately the Instrument. This design covers:

1. Ornament data model (done) -- variant structs replacing the old enum
2. Articulation realization in the Performer -- universal parameter transforms
3. Ornament expansion in the Performer -- breaking one note into multiple play_note calls

## Key Design Decisions

### Ornaments are variant value types, not enums

Old: `enum class Ornament { None, MordentAbove, MordentBelow, ... }`

New:
```cpp
struct Mordent { int direction; int semitones; std::vector<Articulation> articulations; };
struct Trill   { int direction; int semitones; std::vector<Articulation> articulations; };
struct Turn    { int direction; int semitonesAbove; int semitonesBelow; std::vector<Articulation> articulations; };
using Ornament = std::variant<std::monostate, Mordent, Trill, Turn>;
```

Rationale:
- Ornaments carry sub-articulations (e.g., a mordent whose constituent notes use HammerOn + PullOff)
- Different ornament types need different fields (Turn needs two intervals, others need one)
- Value types: no heap allocation, copyable, stored inline on FigureUnit/Note

### Articulations stay as enums (for now)

Articulation properties like "how much to shorten" are Performer decisions, not inherent to the articulation. No need for struct state yet. Can revisit if needed.

### Ornaments carry semitone intervals, not scale steps

The event-list path has no scale context. By resolving intervals to semitones at composition time, ornaments are self-contained. The compositional path resolves scale steps to semitones when building ornaments; the event-list path specifies semitones directly.

Default: 2 semitones (whole step). Legacy JSON ("MordentAbove" etc.) maps to 2 semitones.

### Sub-note timing is a Performer decision

The Performer already receives `durationBeats` and `bpm`, which is sufficient to derive appropriate sub-note durations. The ornament does not carry timing. The Performer uses a heuristic based on available time (e.g., ornament sub-notes are short relative to the parent, clamped to a sensible absolute range).

### Pitch-bending ornaments are deferred

Glissando, portamento, slides -- anything that bends pitch continuously within a single note -- requires wiring DSP-level frequency envelopes. This is a fundamentally different mechanism from "expand one note into N play_note calls" and is deferred to a future design.

## Articulation Realization

The Performer modifies `play_note` parameters based on the note's articulation. Universal transforms that work for any PitchedInstrument:

| Articulation | Effect |
|---|---|
| Staccato | Shorten duration (e.g., 50%) |
| Marcato | Boost velocity |
| Sforzando | Boost velocity (stronger) |
| Mute | Reduce velocity / shorten duration |

Instrument-specific articulations (Pizzicato, HammerOn, PullOff, Bow, etc.) require instrument classification to be meaningful. For now, unrecognized articulations fall through to Default behavior. Future work: add instrument family/capability metadata.

### Where the logic lives

`NotePerformer::perform_note` is the single point where articulations are applied and ornaments are expanded. It receives the full note data (including articulation and ornament) and makes all realization decisions before calling `instrument.play_note`.

Current signature takes raw floats. New signature takes a `Note`, which already carries both Articulation and Ornament.

## Ornament Expansion

The Performer expands ornaments by calling `play_note` multiple times with adjusted pitch, timing, and articulation.

### Mordent (3 notes: main, neighbor, main)

Given a note at pitch P with `Mordent{direction=1, semitones=2, articulations=[HammerOn, PullOff]}`:

1. Play P for short duration (articulation: the Note's own articulation)
2. Play P + 2 semitones for short duration (articulation: HammerOn)
3. Play P for remaining duration (articulation: PullOff)

Sub-note duration heuristic: a fraction of the parent note, clamped to a reasonable absolute range based on tempo.

### Trill (alternating: main, neighbor, main, neighbor, ...)

Given a note with `Trill{direction=1, semitones=2, articulations=[...]}`:

1. Alternate between P and P + semitones for the full duration
2. Number of alternations determined by parent duration and tempo
3. Sub-articulations applied cyclically to alternating notes

### Turn (4 notes: above, main, below, main -- or reverse)

Given a note with `Turn{direction=1, semitonesAbove=2, semitonesBelow=1, articulations=[...]}`:

1. P + semitonesAbove (short)
2. P (short)
3. P - semitonesBelow (short)
4. P for remaining duration

If direction=-1, reverse: below first, then main, then above, then main.

## JSON Format

New format (object with type):
```json
{
  "type": "Mordent",
  "direction": 1,
  "semitones": 2,
  "articulations": ["HammerOn", "PullOff"]
}
```

Turn uses `semitonesAbove` and `semitonesBelow` instead of `semitones`.

Legacy format (bare string) is still parsed for backwards compatibility:
`"MordentAbove"` -> `Mordent{1, 2, {}}`, `"TurnAB"` -> `Turn{1, 2, 2, {}}`, etc.

## What's Already Done

- Ornament variant structs in `basics.h`
- `has_ornament()` helper
- JSON serialization with legacy compat in `music_json.h`
- All field defaults updated across `structure.h`, `figures.h`
- Builds clean, existing renders unaffected

## What's Next

1. Update `NotePerformer::perform_note` signature to accept articulation + ornament
2. Implement articulation parameter transforms (staccato, marcato, sforzando, mute)
3. Implement ornament expansion (mordent, trill, turn)
4. Update Conductor call sites (`perform_events`, `perform_phrase`) to pass the new data through
5. Test with a composition that includes ornaments
