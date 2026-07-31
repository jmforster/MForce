# PassageStrategy expansion — design

Date: 2026-07-31 · Lane: comp (Wolfie) · Backlog #6 (GOALS Wolfie G2)

Matt asked for passage strategies beyond thematic content: connective /
transitional passages, pedal-point buildups, discursive wandering, trips
around the circle of fifths. This spec turns the run-11 Python prototype
(`corpus/mtd_seg/passage_strategies.py`, 12 rendered passages in
`renders/passage_strategies/`) into an engine design.

## What the prototype settled

Everything below was rendered and measured, not reasoned about:

| strategy | shape that worked | measured |
|---|---|---|
| pedal buildup | levels of EQUAL span with DOUBLING density (`diminish(cell, 2^level)`, `2^level` restatements), climbing a third per level, arrival note held ≥2 beats, alternate restatements rotated | accel ratio 4–8×, selfsim 1.0 → 0.63 once alternate restatements rotate |
| wandering | 5–7 distinct cursor-continuous figures, no motif returns, soft register pull when the cursor passes ±5 degrees | selfsim 0.27 (lowest of the four), composite 0.94, repetition still above corpus p10 |
| connective | keep the antecedent's TAIL (last 3 units) as the link cell, sequence it 3× on interpolated anchors, augment the final entry; last anchor backed off by the cell's own net step so the FINAL NOTE lands on the target degree | landed == target in 3/3 takes |
| fifths sequence | one cell on anchors stepping −4 / +3 alternately → I-IV-vii-iii-vi-ii-V-I in diatonic space | 8 entries, range ≤19 semitones with the guard |

Two general lessons the engine port should keep:

1. **Anchors, not connectors, are the authoring unit.** Every strategy is
   naturally written as "entry *i* starts on scale-degree *a_i*"; the
   leadStep connectors are a mechanical `a_i − cursor` derivation
   (`anchor_connectors` in the prototype). The C++ strategies should build
   an anchor list and convert once.
2. **A range guard belongs at passage level.** Anchor-driven passages walk
   further than phrases; the prototype resamples while predicted span >
   corpus `range_p90` (19 semitones), best-of-8 kept. Without it,
   pedal buildup and connective both produced 23–24 semitone spans.

## Engine gap: keyContexts are inert

Probed, not assumed. `PieceTemplate::SectionTemplate::keyContexts` is
parsed (`templates_json.h`) and copied to `Section::keyContexts`
(`composer.h:275`), and `Section::active_scale_at(beat)` exists in
`structure.h:216` — with **zero callers**. Rendering one motif under three
sections keyed `C Major` / `G Major` / `D Major` produces byte-identical
note numbers (48/53/59/60 in each), including on a figure that traverses
degrees 0-3-6-7 where the key signatures differ (F vs F♯).

So a *real* modulating circle-of-fifths trip is blocked on making melody
realization key-aware. The diatonic sequence above is what is expressible
today, and it is the musically standard device — the modulating version is
a separate, larger change.

## Proposed C++ work, staged

**Stage 1 — anchor plumbing + two strategies (no key work).**
New `passage_strategies.h` with `PedalBuildupStrategy` and
`SequencePassageStrategy` (the fifths walk generalized to any anchor step
pair), both `PassageStrategy` subclasses registered in
`strategy_registry.h` alongside `PeriodPassageStrategy` /
`LibraryPassageStrategy`. Shared helper:

```
// anchors -> leadStep connectors; mirrors the engine's cursor math
std::vector<std::optional<int>> connectors_for(
    const std::vector<const MelodicFigure*>& figs,
    const std::vector<int>& anchors);
```

Pedal buildup needs a second Part. Today the prototype authors that in the
template; in the engine the strategy is passage-scoped and cannot add parts,
so stage 1 emits the melody only and the pedal stays a template-authored
part. (Deciding whether a strategy may request an accompanying part is a
separate architecture question — flagged, not answered here.)

**Stage 2 — connective strategy.** Needs the *previous* passage's last
figure to harvest a tail from, i.e. a passage-level "what came before"
input. `Locus` is the natural carrier; check what it already exposes before
adding to it.

**Stage 3 — key-aware realization (unblocks modulation).** Make the
scale-step → pitch conversion consult `Section::active_scale_at(beat)`
instead of the section scale, then verify with the same three-section probe:
identical motif under C/G/D must produce F, F♯, F♯/C♯ respectively. Only
then is a modulating circle-of-fifths trip authorable.

**Stage 4 — wandering.** Cheapest to port (no shared state, no key work),
listed last only because it is the least structurally interesting.

## Open questions for Matt

- Do the four prototypes read as their labels? (REVIEW listen item.)
- Pedal buildup currently pedals the DOMINANT. Tonic pedals and
  inverted (soprano) pedals are one-line variants — worth having?
- Wandering deliberately sits at the bottom of the repetition screen. Is
  "discursive" wanted as-is, or should it still return to something?
