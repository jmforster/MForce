# Phrase-aware cadence placement (comp backlog #7)

Wolfie · 2026-08-10 · engine

## The defect

`AlternatingFigureStrategy` (AFS) is the only chord-progression-driven passage
strategy. It alternates two figure templates **strictly per chord** across the
whole progression, and it emits **one Phrase** regardless of how many
`PhraseTemplate`s the passage carries:

```
for (int ci = 0; ci < chordProg.count(); ++ci) {
  bool isA = (ci % 2 == 0);            // global chord index
  ...
  if (!isA && adjusted.figureCadenceType > 0) {
    int bIndex = ci / 2;
    adjusted.figureCadenceType = (bIndex % 2 == 0) ? 1 : 2;   // HC, PAC, HC, PAC...
  }
}
```

Two consequences:

1. **A cadence lands on every other chord.** Real phrases cadence at phrase
   *boundaries*. K467's opening is 2+2+4+4 bars: AFS emits A-B-A-B where the
   third and fourth phrases want A-A-A-B.
2. **`pt.phrases[1..]` are ignored entirely** — only `phrases[0].figures[0..1]`
   is read. So `PhraseTemplate.cadenceType` / `cadenceTarget` / `parallel` /
   `startingPitch`, and `DefaultPhraseStrategy::apply_cadence`, are all
   unreachable from a chord-driven passage.

This is the standing "AFS impedance" finding (2026-04-13), backlog #7, the
oldest untouched comp item.

## What lands

A new passage strategy `phrase_aware_figure`. AFS is left exactly as it is —
it is referenced by two committed patches and is a legitimate texture
(continuous alternation) in its own right.

### Chord → phrase assignment

The progression is partitioned into contiguous spans, one per non-locked
`PassageTemplate.phrases[]` entry.

- **Beat-budgeted** (preferred): a phrase's budget is `ph.totalBeats` when
  > 0, else the sum of its `figures[].totalBeats`. If *every* phrase has a
  positive budget, walk the chords accumulating `chordProg.pulses`, closing a
  span once its accumulated beats reach the budget.
- **Even split** (fallback, any budget missing): boundaries at
  `chords * p / N`.

Both paths are then repaired identically: every phrase gets at least one
chord, and the final phrase absorbs any remainder. A passage with more
phrases than chords gets one chord per phrase until the chords run out, and
the surplus phrases are dropped (not silently emitted empty).

### Per-phrase composition

For phrase `p` with span `[c0, c1)`, index within the phrase `j = ci - c0`,
`isLast = (ci == c1 - 1)`:

- **Body figures** (`!isLast`): alternate A/B on **`j`, the position within
  the phrase**, not on the global chord index. `j` even → template
  `figures[0]`, wrapped as `ChordFigure` (chord-tone stepping, running pitch
  advanced through the resolved chord tones — the AFS mechanism, unchanged).
  `j` odd → template `figures[1]` if present else `figures[0]`, emitted as
  `MelodicFigure` (scale-step).
- **Cadence figure** (`isLast` and `ph.cadenceType > 0`): always a
  **`MelodicFigure`**, never a `ChordFigure`. `apply_cadence` accounts degrees
  by summing `figures[f]->net_step()` in scale steps, so a chord-tone final
  figure would desynchronise its arithmetic. `figureCadenceType` is set from
  the phrase's own `cadenceType` (1 = HC, 2 = full).
- After the span, `DefaultPhraseStrategy::apply_cadence` runs on the phrase
  when `cadenceType > 0`, with `cadenceTarget` defaulted from the type when
  the template leaves it at -1: **HC → degree 4, full → degree 0**. This is
  the same mapping `compose_figure` already uses for `figureCadenceType`
  (composer.h:979), stated in one place instead of two.

### Pitch continuity

Same semantics as `DefaultPassageStrategy::compose_passage`: explicit
`startingPitch` wins; else `parallel` restarts at the passage's
`startingPitch`; else the cursor continues from the phrases realised so far in
the local passage. AFS's single-phrase form had no continuity question to
answer; a multi-phrase strategy does, and it should answer it the same way the
default path does rather than inventing a third convention.

## Verification

- Build `mforce_cli` **and** `mforce_ui`; `mforce_ui --stamp` exit 0.
- **Null test**: every existing template renders byte-identical. Nothing
  dispatches to `phrase_aware_figure` until a patch asks for it, so the
  expected result is 100%, and any diff means the edit leaked.
- **A/B**: one K467-shaped template (2+2+4+4 bars over 12 chords) rendered
  through `alternating_figure` and `phrase_aware_figure` at a fixed seed.
  Measured from the event dump, not asserted:
  - number of cadential arrivals (figures landing on degree 0 or 4 at a
    phrase boundary) — AFS should show one per two chords, phrase-aware one
    per phrase;
  - each phrase's final pitch vs its declared `cadenceTarget`;
  - phrase count in the composed passage (1 vs 4).
- A deliberate norm-breaker in the batch per the standing rule: one passage
  whose phrase budgets are 24 + 8 bars.

## Not in scope

- Cadence *type* selection (which phrase gets HC vs IAC vs PAC) stays with
  the author, in each `PhraseTemplate.cadenceType`. Auto-assigning a
  HC-IAC-HC-PAC arc across a passage is a separate item.
- AFS is not deprecated or changed.
