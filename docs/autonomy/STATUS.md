# Status — open this file first

Updated: 2026-07-31 — comp run 12 (Wolfie, scheduled) · dsp run 11 (Dipsy, interactive)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 12 (2026-07-31, scheduled): per-corpus scorer anchors (#4 CLOSED — two measurement artifacts fixed, incl. Essen anchored on 2,246 Chinese tunes by alphabetical order); corpus-flavored phrase batches (controlled A/B, 36 WAVs); passage strategies #6 prototyped + rendered (15 WAVs) + C++ port spec'd in 4 stages | #6 C++ stage 1 (needs a tree without a live dsp build); #7 phrase-aware cadence | **4 items** — phrase v3 A/B, passage strategies, corpus flavor A/B [listen] · Essen note [read] |
| dsp | Dipsy | run 11 (2026-07-30 pm, interactive): formantFloor + NoiseBed landed (verified vs clarinet measurements); vowel baselines w/ GLIDING formants (crossfade provably can't do high-f vowels); clarinet CMA-ES smoke 1.132->0.787, noise-lead law preserved; os16 question answered by measurement (C7 converged at os8). Expand-3 parked per Matt | resume clarinet to 600 evals; expand-3 next steps (parked til tomorrow) | **3 items** — vowel baselines [listen], clarinet first pass [listen], os16 answer [read] |

Reports: comp/reports/2026-07-31-wolfie-run12.md ·
dsp/reports/2026-07-30-dipsy-run8.md

Run-12 highlights (comp): three fronts, all metric/build, no blind taste
iteration. (1) Per-corpus scorer anchors close backlog #4 — and finding them
required fixing two artifacts that had been silently distorting numbers: the
whole-tune vs theme length confound (Nottingham's repetition floor was 50 vs
MTD's 6, so generated phrases failed that screen by construction) and Essen's
alphabetical file order, which anchored "folk" on 2,246 Chinese tunes.
Corrected, the run-10 bake-off conclusion survives: n-gram backoff and the
neural LM stay in a ~0.02 cluster, order flips by corpus, nobody wins.
(2) Corpus-flavored phrase batches with structure rolls held identical across
corpora — metrics say provenance barely survives generation; your ears decide
whether it's audible. (3) All four #6 passage strategies prototyped as engine
templates, rendered, and chained into a suite; the C++ port is spec'd in four
stages. A probe found section keyContexts are inert in melody realization
(zero callers), which is exactly why the fifths trip is diatonic and not
modulating — that's stage 3.

Not done: #6 C++ stage 1. The dsp lane was live in this working copy
(partials.h touched 8 min before the check, build/ 6 min), and two builds in
one build dir isn't a verifiable state. Straight implementation task next run.

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

(Essen's data gate closed in run 10; #4 itself closed in run 12.)

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

Next "go": comp = #6 C++ stage 1 (anchor plumbing + PedalBuildup/Sequence
PassageStrategy classes, per the run-12 spec) → stage 3 key-aware realization
if you want modulation → #7 phrase-aware cadence. Four comp review items
waiting (three listen, one read). dsp = item 7
stage 2 (oversampled "modulate everything" FM sweep → novelty filter, review-
gated) and item 8 stage 2 (SIMD the partial loop / iFFT overlap-add). Five dsp
listen items waiting incl. the new FM alias A/B. Verdicts fold in whenever you
send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
