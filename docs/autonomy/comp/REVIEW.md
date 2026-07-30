# Composition lane — review queue

## Awaiting Matt

### 2. Repeat-contour + vary_tail phrases [listen] — (run 7; answers your spec)
renders/markov_phrases2/ (24 WAVs; filenames encode family: _lit/_varytail,
stepup/zigzag/etc; scores.csv). This is the direct answer to your non-literal
repeat spec — the three families map to your three cases:
  1. repeat on different pitches  -> `transposed` family (stepup/down/leap/zigzag
     contours anchor each A occurrence at a new pitch)
  2. same pitch but modified      -> `varytail` family (G-F#-G / G-F#-G-A-D shape:
     head kept, tail resampled per repeat)
  3. both                          -> `combined` family (moved AND modified)
Combined scored highest (0.86 mean). Verdict decides: contour/transform defaults
for the combination layer.

### 3. Contrast-aware fig B — independent vs contrast A/B [listen] — NEW (run 8)
renders/markov_contrast/independent/ vs renders/markov_contrast/contrast/
(14 paired phrases, same figA + pattern per pair; only how fig B is sampled
differs) + scores.csv. Different axis from #2: this is about how the *second*
figure relates to the *first* (antecedent/consequent), not repeats of A.
Contrast-B answers a rising A with a falling B, shares A's rhythm, avoids
monotone. Metric: range 12.6->9.1 semitones (runaways gone), zero_rate
0.278->0.251; composite is a wash because it can't measure A/B balance — that's
the question for your ears. Verdict decides: whether contrast-B becomes the
combination-layer default, and whether the closure/kinship weighting reads as
musical or too "tidy".

### OP-1. Essen folksong data fetch [unblock] — NEW (run 8)
Backlog #4 (Essen ingest) is blocked purely on data: corpus/kern holds only 10
classical pieces, not the Essen folksong collection. Same fetch-authorization
gate Nottingham had. If you drop the Essen **kern (or clone the repo) into a
corpus/ subdir, the parser + tokenize + bake-off run are unattended-doable next
session. One line here unblocks it.

## Resolved

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
