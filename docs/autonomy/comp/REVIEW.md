# Composition lane — review queue

## Awaiting Matt

(run in progress: stage 3 key-aware realization + stage 4 wandering [C++],
final-note weighting v2, then the passage set v2 built on your musical
specs. New items land as fronts complete.)

## Resolved

2026-08-02 (Matt, folded):
- Phrase v3: good; final note needs MORE weight toward longest ->
  strengthening now.
- Passage strategies: "most musical/plausible output yet." Verbatim
  musical specs preserved for the v2 set:
  > CONNECTIVE end-transformations before arrival: (a) 4-note fig x7
  > ascending -> chromatic rise to peak, pause, descend to cadence;
  > (b) 4-note fig x6 descending to tonic -> rhythmic fanfare on arrival
  > (Cq Ce. Cs Cq Cq Ch Cq); (c) 6-note fig x3 ascending -> arpeggio
  > falls back almost to start, x3 cycles -> climactic cadence.
  > PEDAL: 8x accel too much. The cliche: chords over pedal with
  > INCREASING DISSONANCE (chords excluding the pedal tone) -> Fr/Ger 6th
  > over pedal -> V(7) -> full cadence; variant with melodic figure over
  > the chords; Bruckner extreme = modulating over the pedal (needs key
  > fix). WANDERING: wants modulation (sweet major theme -> sudden dim7
  > -> minor keys...) — gated on stage 3. SEQUENCE: sounds like
  > connective until key fix. SUITE: "exciting taste of things to come."
  Directive: stages 3+4 (new dev, not port), then v2 set from the specs.
- Corpus flavor: no action. Essen: order-2 backoff stays (no objection).

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
