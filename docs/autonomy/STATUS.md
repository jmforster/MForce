# Status — open this file first

Updated: 2026-07-30 pm — dsp run 11 (Dipsy, interactive) · comp run 10 (Wolfie, interactive)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 10 (2026-07-30, interactive): phrase v3 — Matt's 4 rules implemented+measured (final-note 0.92, beat-joins 0.92, duration-aware sizing, transforms wired); Essen ingested (8,469 tunes, 436k tokens) — method ranking stable across 3 corpora, neural never superior | per-corpus scorer baselines; passage strategies (#6) | **1 item** — phrase v3 A/B [listen] (before/ baseline included) |
| dsp | Dipsy | run 11 (2026-07-30 pm, interactive): formantFloor + NoiseBed landed (verified vs clarinet measurements); vowel baselines w/ GLIDING formants (crossfade provably can't do high-f vowels); clarinet CMA-ES smoke 1.132->0.787, noise-lead law preserved; os16 question answered by measurement (C7 converged at os8). Expand-3 parked per Matt | resume clarinet to 600 evals; expand-3 next steps (parked til tomorrow) | **3 items** — vowel baselines [listen], clarinet first pass [listen], os16 answer [read] |

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
