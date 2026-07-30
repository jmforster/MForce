# Status — open this file first

Updated: 2026-07-30 — comp run 8 (Wolfie, scheduled) · dsp run 8 (Dipsy, scheduled)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 8 (2026-07-30, scheduled): 4 fronts. Contrast-aware fig B (#2) — B sampled in relation to A (closure + rhythm kinship + monotony veto); range 12.6→9.1, zero 0.278→0.251, composite a wash by design → audition. Neural next-note (#3, G1) — numpy Bengio LM, val_ppl 28.5, soft-imported into bake-off; competitive-not-superior vs n-grams on MTD+Nottingham. #9 v2 self-similarity screen + --csv fix (#5). figure_transforms.py library (Matt's fragment). Essen #4 found data-gated. | wire transforms into phrase-builder (#10), then passage-strategy expansion (#6) | **2 listen + 1 unblock** — repeat-contour/varytail phrases (#2), contrast-B A/B (NEW), Essen fetch (OP-1) |
| dsp | Dipsy | run 8 (2026-07-30, scheduled): 3 fronts. Item 7 oversampled FM LANDED — `oversample` config, sin() at M·SR + Butterworth decimation, measured 24.5/34.7/39.6 dB alias suppression @M=2/4/8, M=1 byte-identical (spacy safe). Item 8 additive-perf profiling stage 1 (CLI timer + sweep tool; baseline 64 ns/sample/partial, ~325 partials/core RT; resolved a 25x measurement artifact). Item 9 algev→instrument conversion done. | item 7 stage-2 (modulate-everything sweep) / item 8 stage-2 (SIMD·iFFT) — both staged | **5 listen items** — formantseq, expand-sweep, CMA-ES stage-d, v6 curve ladder, FM alias A/B (NEW) |

Reports: comp/reports/2026-07-30-wolfie-run8.md ·
dsp/reports/2026-07-30-dipsy-run8.md

Run-8 highlights (comp): four fronts, all metric/build, no blind taste
iteration. (1) Contrast-aware fig B (#2) closed the last "contrast = TODO" in
the combination layer — B is now sampled in relation to A on an
antecedent/consequent closure objective; the measurable win is range control
(12.6→9.1 semitones, runaways killed) and the audition question (does the A/B
balance read musical?) is queued, not guessed. (2) The neural next-note model
(#3, Matt G1) is in the bake-off — a numpy Bengio LM, competitive with but not
superior to count-based backoff on the first-order plausibility metric, which is
itself the honest finding (the composite saturates; a learned representation
doesn't beat backoff at this corpus scale). (3) #9 v2 self-similarity screen now
catches motif-level "one figure hammered N×" that the first-order screens miss.
(4) figure_transforms.py is the reusable substrate for Matt's spec-1/2/3 repeat
variety. Matt's non-literal-repeat verdict was already answered by run-7's three
families (markov_phrases2/) — mapped explicitly in REVIEW, awaiting ears.

Essen (#4) is DATA-gated: the folksong corpus isn't on disk (corpus/kern is 10
classical pieces). One-line fetch unblock queued as REVIEW OP-1.

Run-8 highlights (dsp): three fronts, all build/metric, no blind taste. (1)
Oversampled FM (item 7, G3) — a first, decisive dent in the FM-aliasing goal:
`oversample` config runs the sin() nonlinearity at M·SR then Butterworth-
decimates, measuring 24.5/34.7/39.6 dB alias suppression at M=2/4/8 (M=8 removes
99% of the in-band alias residual). Default 1 keeps the spacy-FM family byte-
identical. (2) Additive-perf profiling (item 8 stage 1, G4) — landed a CLI
render timer and a partial-count sweep tool; steady-state cost is ~64 ns per
sample per partial, linear, ~325 partials/core real-time single-thread. A 25x
"fast path" lead was chased down to a measurement artifact (instrument patches
pre-render at load, so the timer caught mixing not synthesis) — no phantom win;
stage 2 (SIMD / iFFT-OLA) is genuine work, staged. (3) 6 algev patches converted
to instrument-style, resolving the audition-path mismatch. Tree note: the comp
scheduled run shared the working copy this session — all commits scoped, no -u.

Next "go": comp = wire figure transforms into the phrase-builder (#10, uses the
run-8 library so repeated A occurrences can be transposed AND transform-varied at
full generality) → PassageStrategy expansion (#6, Matt re-raised). Essen stays
gated on the fetch. Two comp listen items + one unblock now waiting. dsp = item 7
stage 2 (oversampled "modulate everything" FM sweep → novelty filter, review-
gated) and item 8 stage 2 (SIMD the partial loop / iFFT overlap-add). Five dsp
listen items waiting incl. the new FM alias A/B. Verdicts fold in whenever you
send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
