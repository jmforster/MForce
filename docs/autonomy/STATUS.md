# Status — open this file first

Updated: 2026-07-31 — comp run 12 (Wolfie, scheduled) · dsp run 12 (Dipsy, scheduled)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 12 (2026-07-31, scheduled): per-corpus scorer anchors (#4 CLOSED — two measurement artifacts fixed, incl. Essen anchored on 2,246 Chinese tunes by alphabetical order); corpus-flavored phrase batches (controlled A/B, 36 WAVs); passage strategies #6 prototyped + rendered (15 WAVs) + C++ port spec'd in 4 stages | #6 C++ stage 1 (needs a tree without a live dsp build); #7 phrase-aware cadence | **4 items** — phrase v3 A/B, passage strategies, corpus flavor A/B [listen] · Essen note [read] |
| dsp | Dipsy | run 12 (2026-07-31, scheduled): additive hot loop **1.2-1.6x** (batched per-sample sum + hoisted scalars, bit-exact 14/14; fast sin, 1 LSB worst deviation); `load=` timer proves instrument patches are LOAD-bound not render-bound (viola 2288ms vs 16.6ms); 24-patch FM "modulate everything" matrix; oversample-default premise corrected by measurement | stage 2d SIMD/SoA (needs a spec, 17x headroom measured); clarinet 600-eval (gated) | **5 items** — vowel baselines [listen], clarinet first pass [listen], FM oversample default [read], FM matrix [listen], perf residual [listen, non-blocking] |

Reports: comp/reports/2026-07-31-wolfie-run12.md ·
dsp/reports/2026-07-31-dipsy-run12.md

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

Run-12 highlights (dsp): three fronts, all build/metric, no blind taste.
(1) **Additive hot loop, 1.2-1.6x** (item 8 stage 2, G4). The bit-exact half —
batching the partial loop behind one virtual call per sample, hoisting the
eight envelope reads that were being fetched once PER PARTIAL (768 virtual
calls per sample at 96 partials), caching pow-derived rolloff, fmod→truncf —
passes a 14-patch byte-identical null test. Then sinf was *measured* at ~40%
of what remained and replaced with a minimax polynomial in turns whose worst
deviation across all 14 patches is 1 LSB at 16 bit. Marginal cost
90.6→51.4 ns/sample/partial; ~437 partials/core real-time. Also closed the
run-8 loose end: `load=` timing proves instrument+score patches pre-render at
load, so viola_default is 2288ms load vs 16.6ms "render" — 0.94x realtime end
to end, not 150x.
(2) **FM "modulate everything"** (item 7 stage 2, G3) — 24 patches driving the
params a fixed-architecture FM synth can't reach (both ratios at audio rate,
phase-as-PM, FM driving FM's depth, modRatio swept through zero), 9 deliberate
rule-breakers. Wiring verified by measurement, not assumed. Novelty-ranked →
REVIEW.
(3) A **premise correction**: every alias number so far was taken at a high
carrier. At a low carrier with a huge index the oversample ladder does NOT
converge (M=1/2/4/8 → 0/-0.0/-2.4/-2.3 dB) — there it changes the sound rather
than cleaning it. The default rule should key on carrier frequency and
deviation, not index.
Anti-result recorded: a branchless full-period sin polynomial is a wash and
less accurate — don't retry.
Tree note: three tracked files were already modified at session start (a UI
re-save of v6_01, and the ml_ears config generalization); left untouched per
the guard, flagged in the report — HEAD currently can't run the clarinet
CMA-ES pipeline without them.

Next "go": comp = #6 C++ stage 1 (anchor plumbing + PedalBuildup/Sequence
PassageStrategy classes, per the run-12 spec) → stage 3 key-aware realization
if you want modulation → #7 phrase-aware cadence. Four comp review items
waiting (three listen, one read). dsp = item 8 stage 2d (explicit SIMD/SoA of
the partial loop — ablation measured a 3.3 ns memory floor against 57.3 ns
now, so there is 17x of headroom and it needs its own spec) → stage 2e
iFFT-OLA. Clarinet 600-eval stays gated on your ears. Five dsp review items
waiting. Verdicts fold in whenever you send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
