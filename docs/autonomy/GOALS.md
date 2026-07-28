# Goals (Matt's space)

Brain-dump big, ambitious, or vague goals here in any form — sentences,
fragments, "wouldn't it be cool if". Decomposing them into prioritized
backlog items (dsp/ or comp/ lanes, or new lanes) is the autonomous
session's job, done at the start of the next run. Items derived from a goal
reference it, so you can trace what happened to each ambition.

## Goals
Dipsy:
1 Pursue the goal of ML ears, with one goal being matching known samples
to assemple a library of usable instrument patches, and another being
measuring "novelty" for unique patches.
2 Make Additive synthesis do things I haven't even thought of, via creating
innovative patches and/or adding engine features (one hint: (recursive)
partial expansion seems potentially rich and fairly under-explored.. ditto
for FormantSequences).
3 Revisit FM (and its cousin PM) and raise it to a new level - can aliasing be
conquered via oversampling or other means? Try new things. Modulate literally
everything and see what happens!
4 Additive synth is of course very costly and therefore not able to rendered
real-time on a modest cpu. It would be awesome to conquer or at least improve
that issue.

Wolfie:
1 Pursue the goal of "plausible" melodies that don't sound computer-generated.
The key to this is the atomic Figures, and the Markov chain derived from the
classical corpus was a breakthrough. Find other corpi, use other methods,
including the "LLM-like prediction of next note" vs. Markov. Make separate
FigureGenerators using these methods.
2 Flesh out the Phrase and PassageStrategy levels, using known musical principles
and approaches you make up. Make strategies not just for thematic content (like a
classical period) but connective passages, big "pedal tone" pre-cadence buildup
passages, discursive wandering passages, trips around the circle of fifths, etc.

## Decomposed

2026-07-27 (Dipsy): Dipsy goals 1-4 → dsp/BACKLOG.md items 2-8 (G-refs
inline): ML ears = items 2-3 (matching) + 4 (novelty); additive-beyond =
items 5-6; FM/PM revival = item 7; performance = item 8. Wolfie goals 1-2 →
comp/BACKLOG.md items 2-4 (corpus survey, generator bake-off incl. neural
next-note, passage-strategy expansion). Originals kept above for reference —
strike them when satisfied with the decomposition.
