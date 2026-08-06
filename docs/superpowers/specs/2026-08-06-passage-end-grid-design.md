# Passage endings on the grid — design (comp backlog #18)

Wolfie, 2026-08-06.

## The defect, reproduced at HEAD

Run 17 found 37 of 74 passage renders ending off the beat. Reproduced and
sharpened on the 21-passage batch at HEAD (`--takes 3`, 23 renders):

```
ends on integer beat:  6/23
ends on a 4/4 barline:  5/23
```

The six that land are the entire `sequence_passage` family, which builds
fixed-length cells. Every `pedal_buildup`, `connective`, `wandering` and
`suite` render ends mid-beat: 9.38, 37.25, 10.91, 50.75, 48.04.

Cause: `sample_cell` draws a pulse per candidate (that draw is deliberate —
without it every take rendered byte-identical), and the cell length is
`round(beats / pulse)` notes of that pulse. Nothing downstream reconciles the
accumulated total with the beat grid. The phrase path has done exactly that
since run 13 (`markov_phrase.py`, "a phrase stops at a barline, not
mid-beat"); the C++ passage path never got the equivalent.

## Where it goes

`Composer::compose_passage` (composer.h:228) is the single call site through
which every passage strategy returns — default, period, library, and the four
from #6. One central application beats four copies inside the strategies, and
it cannot be forgotten by the next strategy somebody writes.

Phrase figures are deep-cloned (`Phrase`'s copy ctor calls `f->clone()`), so
adjusting the final `FigureUnit` of a composed `Passage` cannot reach back
into `realizedMotifs`. The standing "figures are never mutated" convention is
about the motif pool and is not violated.

## The rule

`passage_anchors::grid_complete(Passage&, float grid)`:

1. total = sum of every unit duration in the passage (connectors carry pitch,
   not time).
2. target = **nearest** multiple of `grid` to total. Nearest, not ceiling —
   run 17 measured ceiling-to-the-next-barline as the dominant term in the
   phrase path's ending overshoot, and `calib` fixed it by rounding to the
   nearest barline instead.
3. delta = target - total, applied to the LAST unit's duration.
4. Guards: a no-op below 1e-3. A shrink that would take the last note below
   `kMinFinal` (0.25 beat, the tokenizer's own duration grid) rounds UP to the
   next multiple instead of down. A target of 0 rounds up.

Returns the delta so the caller can report it.

## The knob

`PassageTemplate::endGrid`, float, default **1.0** — quantize passage endings
to the integer beat. `0` disables (a pickup or an elided passage handoff is a
legitimate reason to end off-grid). `4.0` quantizes to a 4/4 barline.

Default-on with a knob, not opt-in: an off-grid ending is a defect in every
render measured so far, and leaving the fix behind a flag means the batch
keeps producing them. The knob exists because ending off-grid is a musical
choice somebody will eventually want, not as a back-compat shim.

Beat (1.0) vs bar (4.0) as the DEFAULT is a taste question. Both arms get
rendered and the choice is queued for Matt rather than picked here.

## Verification

1. Build both targets; `mforce_ui --stamp` exits 0.
2. Re-run the 21-passage batch; `ends on integer beat` should go 6/23 → 23/23
   at `endGrid=1.0`, and `ends on a barline` 5/23 → 23/23 at `endGrid=4.0`.
3. Blast radius stated honestly: render all 38 templates in `patches/` before
   and after and report how many change. A template whose passages already end
   on the grid must be byte-identical.
4. `endGrid=0` must reproduce the pre-change renders byte-for-byte — that is
   the proof the rule is the only thing that moved.
5. Round-trip: `--lint-template` reports no new findings.
