# Composition lane — review queue

## Awaiting Matt

### 1. Regenerated Phase-2 phrases after the monotone-B fix [listen]
renders/markov_phrases/ (same paths, regenerated). The "second figure all
one note" bias is fixed (0.38 -> 0.19 zero-step; your ear caught what the
composite missed). Question: with the bug gone, do the combinations still
read as "computer music," or was the monotone tail a big part of it?
Verdict decides: whether contrast-aware fig B (next combination front)
builds on this pattern set or the combination layer needs rethinking.

MATT: Yes, better. But in all phrases with repeated figures, the figure repeats
literally, on the same starting tone. This is very rare in real music. We want:
1. figures that repeat on different starting pitches
   (step up, step down, step up then down, leap up, step down, etc. etc.)
2. figures that repeat on same pitch but are modified
   (famous example: G-F#-G-D-Eb / G-F#-G-A-D / G-F#-G-A-C-D-Eb)
   (tho you could view this as an ABAB'AB'' phrase where all A figures are identical,
    which SHOULD be allowed)
3. both of the above (repeat on different pitches *and* modify)

[scolding]
The guidance for these scheduled sessions is to do a volume of work that I can review the
the next day, moving ball forward on multiple fronts. Your last session was a scant
few minutes in duration, and consisted of 1 bug fix w/ re-renders, and ingestion of
Essen but not Nottingham (even tho I unblocked both). All sessions thus far
have been far short of me stating a general goal and you running with it. Please see
GOALS.md and strive for a large number of deliverables for review in each session.
Session duration should be hours, not a couple of minutes. Items in BACKLOG are
fairly independent so not sure why you keep adding items there instead of tackling
them.

New passage strategies,
Figure transform operations, 
### 2. Repeat-contour + vary_tail phrases [listen] — NEW (run 7)
renders/markov_phrases2/ (24 WAVs; filenames encode family: _lit/_varytail,
stepup/zigzag/etc; scores.csv). Combined family scored highest (0.86 mean).
Verdict decides: contour/transform defaults for the combination layer, and
whether contrast-aware fig B builds on top of this (next front).

## Resolved

2026-07-29 (Matt, run-5 folding):
- Parallel A/B: no single default — per-phrase choice (running pitch /
  explicit / parallel restart) stands as the complete mechanism menu.
- Scorer sanity: "second figure all one note" -> root-caused (range-guard
  selection bias) + fixed + monotony screens added to composite.
- Phase-2 phrases: "computer music; forget norm-breakers" -> outlier
  patterns dropped; re-listen queued post-fix.
- Essen license: cleared by Matt -> ingest queued (backlog 3c).
- Nottingham: cloned by Matt -> ingested (1024 tunes, folk bake-off run).
