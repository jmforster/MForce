# Status — open this file first

Updated: 2026-08-03 — dsp run 18 (Dipsy, scheduled) · comp run 14 (Wolfie, 2026-08-02)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 14 (2026-08-02, interactive): final-note v2 (longest 96%); stages 3+4 VERIFIED (modulation real: F/F#/C# literal test); passage set v2 from Matt's specs — 58/58 checks, suite_v2 68.75 beats, modulating wander/fifths with proven accidentals | chord emission from template layer; stages 1-2 C++ strategies; scorer passage-mode | **3 items** — passage set v2 [listen], final-note A/B [listen], stage-3 acceptance [read] |
| dsp | Dipsy | run 18 (2026-08-03, scheduled): UI stale-guard FALSE POSITIVE fixed (real dep set, 73 files not 116, A/B-proven 3 ways); expand depth 3-4 finally run — depth measures SECOND-ORDER; `power` found inert at count=1; piano beat RATE measured and shown NOT lockable | 3c2 FMSource dead phase, then 2d-1 SIMD — both need mforce_ui relinkable | **6 items** — 3 from run 17 + expand round 4 [listen], power-at-count-1 [read], shimmer dims [read] |

Reports: dsp/reports/2026-08-03-dipsy-run18.md ·
comp/reports/2026-08-02-wolfie-run13.md

Run-18 highlights (dsp): three fronts, no engine edits — and that last part
is the headline constraint. **mforce_ui.exe was locked all run** (your UI up
since 08-02 19:23, pid 18556); I did not kill it, so the target could not be
relinked, so the "engine edits rebuild both targets" rule ruled engine work
out entirely. Items 3c2 and 2d are consequently untouched and are first up
next run — **please close the UI when convenient so mforce_ui can relink**;
until then your binary still carries the old stale-guard.
(1) **Your stale-guard false positive is fixed** (9c2946e), and it was worse
than reported: the guard compared the exe against all **116** engine sources
when mforce_ui depends on **59**. It now reads MSBuild's own CL.read tlogs
for mforce_ui + mforce_engine — 73 files, and it newly covers
tools/mforce_ui/main.cpp, which the engine-only scan never looked at.
A/B-proven on the live tree in three directions: control (both quiet),
composer.h newer → old STALE / new quiet, partials.h newer → new still
STALE. The script restores every mtime and SHA256-checks content. Writing
the test caught a real bug in the fix (case folded, separators not, so a
forward-slash path matched nothing and the dep set silently went empty).
(2) **Depth is second-order, measured not guessed.** Item 5's parked
recurse 3-4 is affordable now (cost re-measured: linear, ~17 ms/partial per
4 s render). Holding partial COUNT and total SPREAD fixed, halving the depth
moves the embedding **4.36** — against a batch median pair of 18.98 and
loPct's 50.46. So depth ≈ the size of a `power` tweak. My recommendation is
to retire the expand front rather than run a round 5; your ears decide.
The batch also caught a live trap: **`power` does nothing at count=1**
(taper is pow(t,power) with t≡0 there), found because two cells rendered
byte-identical, and proven by a regression pair at count=2 that differs.
(3) **A measurement I could not make, shown rather than asserted.** Piano
unison beat RATE: attempt 1 failed its own control (every "beat" was FFT bin
1-2 = decay curvature). Attempt 2 looked plausible, so I tested it — raising
the analysis floor 0.50→0.75→1.12 Hz makes **9 of 11 rates climb with it**.
Artifact. Only C5/C6 hold (~1.9-2.0 Hz). What survived is structural and
usable: single-strung B0/C1 modulate at 0.016-0.029 vs 0.106-0.285
multi-strung, a 5-9x contrast, so shimmerDepth ≈ 0.15 is a good seed while
shimmerHz/Coherence stay searchable. The data also reclassified F1 as a
bichord — my register label was wrong, not the measurement. Two attempts, so
it is backlogged (item 13) with the method that should work, not retried.

Run-13 highlights (comp): five fronts, all metric/build, no blind taste
iteration. Every one of them started from something you said.
(1) **Your final-note verdict was a measurable distributional gap, not a
preference.** `final_note_stats.py` profiled the last note of every usable
theme (MTD n=1632, Nottingham n=1024): the final note runs **2.0x the median
pulse in both corpora** and is >= every other note in 35% / 53% of themes. The
old rule set it to exactly 1.0x — MTD's p25, Nottingham's p10. The ratio is
now drawn from the corpus histogram; controlled A/B (same seed, own rng
stream, so only the ending differs) moves median ratio 1.00 -> 2.50,
final-is-longest 0.12 -> 0.67, ends-on-an-integer-beat 4/24 -> 24/24.
**A scorer gap fell out of it**: `scores.csv` is byte-identical between the
two arms. The composite cannot see phrase endings at all — which is why this
needed your ears and no metric ever caught it. Backlog #12.
(2) **Your three connective examples are three strategies**, all shaped
CHAIN -> GOAL, which was the half the old `connective` was missing. The
chromatic rise is *real* chromaticism and needed no engine change:
`FigureUnit.accidental` shifts the pitch without moving the scale-degree
cursor, so a raised passing tone is `(step 0, accidental +1)` — verified in
rendered pitches (57-58-59-60-61-62-63), not assumed.
(3) **Chords over the pedal**, with the triads ordered by *measured*
dissonance against the pedal, into Ger6 -> I(6/4) -> V7 -> I, the pedal
releasing to the tonic so the "full cadence" actually lands. On the 8x: part
of it was never in the audio — the accel metric compared level 0's mean
*including* its long final note against the last level's mean *excluding* its
held note, reporting 8x for a real 4x. Fixed and capped.
(4) **#6 stages 3+4 landed.** The 2026-08-01 comp run committed stages 1-2 and
then died mid-cycle, leaving stages 3-4 uncommitted; your 11:17 fold directed
them, so I confirmed nothing was live (28h untouched, no build activity), then
verified rather than trusted. Run 12's own probe now reports 3/3 distinct
across C/G/D with the no-keyContexts control still byte-identical. The
24-entry outlier case exposed a real bug — one failed cell killed the whole
candidate, so the passage rendered **silent**; fixed with a cell retry.
(5) **The first real modulating circle-of-fifths**: C-E -> G-B -> D-F# ->
A-C#, the new keys' signatures appearing. One semantic that matters for your
Bruckner idea: stage 3 snaps the cursor's PITCH into the new scale, it does
NOT move it to the new tonic, so keyContexts alone will not modulate audibly —
the entry has to be offset by the key distance.

Run-14 highlights (dsp): three fronts, all build/metric, no blind taste
iteration. Both wins are the same bug class.
(1) **`std::truncf` was a CRT call.** A comment in the hot body asserted it was
a single instruction; `roundss` is SSE4.1 and the engine builds SSE2-baseline,
so MSVC emitted a function call — once per partial per sample. The int
round-trip is bit-identical for every |x| < 2^31 and worth **1.6x**
(41.7 → 26.1 ns/sample/partial). Null test 14/14 byte-identical.
(2) **`std::exp2` in the motion path was 65.6% of viola_default's entire
loop** — found by a new tool (`tools/ablate_layers.py`) that profiles layers on
a REAL patch instead of on the layer-free profiling patches. `fast_exp2`
(0.88 float32 eps, at the rounding limit of the type) gives **1.7x on every
motion-bearing patch**; `v6_cmaes_best` 16.1s → 8.9s, which is the item-3d
blocker by name — 600 evals is now ~50 min, not 1.5-2.7h.
(3) The SIMD prototype **passed its abort criterion 18-19x**, but the same
sizing measurement says a layer-free vector path only reaches ~51% of the
flagship patch, because those patches all run the bandwidth layer and its
shared-rng walk re-rolls rather than perturbs the noise when reordered. Stage
2d restaged into 2d-1..2d-4 rather than built on a wrong premise.
**One thing genuinely needs you:** the exp2 change re-rolled the bandwidth
noise on ONE patch (`v6_cmaes_best`), and bisecting it exposed a pre-existing
fragility — the cutoff gate sits before the bandwidth block, so any 1-ulp
`pfreq` change can flip a partial and desync its noise for the rest of the
render. That also makes **the CMA-ES objective discontinuous**, which matters
for the 600-eval run regardless of run 14. A/B rendered, both questions in
REVIEW (0a listen, 0b read).
Two anti-results recorded rather than left as folklore: the reciprocal-instead
-of-divide substitution measures 1.10x in isolation and 0.89x in the engine,
and a bit-exact fast path for `Formant::get_gain`'s `pow` buys exactly nothing
(the `contains()` gate keeps it cold). That is four dead theories on this loop
against three live wins.
(4) **Backlog 3c closed** — the UI now shows `[build MM-DD HH:MM @sha]` in the
title bar and shouts `*** STALE - REBUILD ***` with a red banner when the exe
predates the newest engine source. Both phantom bug reports so far were stale
binaries; this makes that visible instead of silent. New `mforce_ui --stamp`
verifies it headlessly (exit 1 when stale), tested in both directions.
Tree guard honoured: the four files modified at session start were left
untouched; backlog item 11 still stands.

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
Two anti-results recorded rather than left as folklore: a branchless
full-period sin polynomial is a wash and less accurate, and a 4x unrolled loop
with split accumulators is *slower* (0.88-0.94x). Between them they rule out
branch misprediction, the accumulator chain, and ILP starvation — so stage 2d
needs real SIMD or nothing, and its spec now opens with an isolated prototype
and a 2x abort criterion instead of a refactor.
Tree note: three tracked files were already modified at session start (a UI
re-save of v6_01, and the ml_ears config generalization); left untouched per
the guard, flagged in the report — HEAD currently can't run the clarinet
CMA-ES pipeline without them.

**For Wolfie:** the stale-guard false positive you reported is FIXED in run 18
(commit 9c2946e) — you were right on both the cause and the remedy; it now
keys off the target's real dependency set. One catch: `mforce_ui.exe` could
not be relinked (Matt's UI held it locked), so the running binary still has
the old behaviour. Once it is rebuilt, a comp-only engine edit will no longer
make it shout STALE.

Next "go": comp = the two modulation passages stage 3 unblocked (Bruckner
pedal-through-keys, modulating wandering) → #12 give the scorer a phrase-
ending screen → #7 phrase-aware cadence. Five comp review items waiting
(three listen, two read). dsp = **rebuild mforce_ui first and confirm
`--stamp` exits 0**, then item 3c2 (FMSource's `phase` param is dead — apply
phase_ to the carrier for true PM, then re-render the t1_06/t1_07 topologies
as designed), then 2d-1 (vector path, needs an AVX2-availability decision).
All three are engine edits and all three were blocked in run 18 by the locked
exe. Clarinet 600-eval stays gated on your ears. Six dsp review items waiting
(three listen/look, three read). Verdicts fold in whenever you send them.

Standing tree note: the same four tracked files have now been dirty at
session start across six runs (c2c_quiet.json, v6_01_res_curve_lo.json,
iowa_reference.py, score_candidate.py). Backlog 11 wants one word from Matt —
HEAD alone still cannot run the clarinet CMA-ES pipeline without them, and
the piano onset-alignment prereq lives in iowa_reference.py, so this is now
blocking piano work too.

How this works: [WORKFLOW.md](WORKFLOW.md)
