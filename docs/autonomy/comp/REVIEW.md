# Composition lane — review queue

## Awaiting Matt

### 1. Phrase v3 — your four rules implemented [listen] (run 10)
renders/markov_phrases3/ (24 WAVs; before/ = 24-phrase baseline for
direct A/B; scores.csv, mean 0.83, none <0.6). What changed, verified
mechanically on the rendered output:
- final notes now >= median pulse in 92% of phrases (was 62%)
- figure joins land on integer beats 92% (was 62%); elision is now the
  ~20% exception, not the norm
- short note durations buy MORE notes instead of ultra-short phrases
  (correlation pulse vs note-count: -0.44, was none); ultra-short tail
  gone, top end now 25 beats, median unchanged per your "don't make them
  ALL longer"
- the 9-transform library (augment/diminish/expand/compress/rotate/
  ornament/...) now drives A-variants; families in filenames
Verdict decides: do the four rules read as musical; max length cap
(25 beats — one-line raise if you want longer outliers); which transforms
earn default rotation.

### 2. Essen bake-off note [read, optional]
8,469 tunes ingested; ranking stable across 3 corpora. Only decision
embedded: within the top cluster (ngram2/3/neural, ~0.02 apart, order
flips by corpus) there is NO winner to pick — order-2 backoff stays the
workhorse by parsimony unless you object.

## Resolved

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
