# Status — open this file first

Updated: 2026-08-04 — dsp run 19 (Dipsy, scheduled) · comp run 15 (Wolfie, scheduled)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 16 (2026-08-04 pm, interactive): 5 verdicts folded; #12 closure screen verified+committed (and it exposed the ending rule overshooting, #16); **VoicingPin** landed (authored cadences outrank the selector); Bruckner v2 built to Matt's progressions with the pedal held under the Ger6; pedal_chords engine-voiced with the pinned seam; literal repeats transform (#10 closed); voicing A/B renders finally delivered (p05==p1 confirmed by hash) | #16 ending recalibration; #13 section key; #14 priority semantics (his call); scorer passage-mode | **4 items** — Bruckner v2 [listen], pedal_chords voiced [listen], repeat transforms A/B [listen], voicing_ab 9 WAVs [listen] |
| dsp | Dipsy | run 20 (2026-08-04 pm, interactive): 9 verdicts folded; PIANO FIRST PASS — per-partial decay + inharmonicity engine configs (bit-exact at defaults, stretch verified to 0.1 cents), config-driven reference pipeline adopted from the dead run + onset alignment, 18-dim encoder, 108-eval smoke 1.521 -> 0.900 with knock band converging to the measured 2.5-8 kHz; t3_23 clicks EXPLAINED (velvet-phase, controllable 5-193/s) + 3 noisy-attack PoCs; expand front retired; CombineTest fixed | full piano run (gated on A/B); scorer debt 3e-NEXT; sum-op lint 3f | **4 items** — piano A/B [listen], click PoCs [listen], power-renorm answer [read], F1-retuning answer [read] |

Reports: dsp/reports/2026-08-04-dipsy-run19.md ·
comp/reports/2026-08-04-wolfie-run15.md

Run-15 highlights (comp): four fronts, all build/metric, no blind taste
iteration. The through-line is **capabilities that existed but could not be
reached from a template**.
(0) **Tree guard, and it matters this time.** Two comp-lane files were dirty
at session start — `score_generated.py` (+181) and `corpus_baseline.py` (+12),
stamped 08-03 07:10, no report, no commit. They are a complete-looking
implementation of **backlog #12** (the phrase-ending screen you needed) left
by a run that died mid-cycle, the same way stages 3+4 were orphaned on 08-01.
Untouched, unrun, uncommitted per the guard. **One word adopts or discards
them** (REVIEW 5); until then #12 is blocked and the composite still cannot
see endings.
(1) **A passage can now carry its own chords** — and finding out why it
couldn't turned up something worse: **the harmony path was dead at HEAD**. All
nine `test_jazz_turnaround_*` patches, the entire voicing-selector A/B set,
rendered `peak=0` with **0 chord events** while the section timeline held all
16 chords. Nothing in the repo had ever been migrated to the `rhythmPattern`
that Stage 11 made mandatory. One idea fixed both: an authored progression
already carries durations, so it emits chords by itself and a rhythmPattern
merely RE-articulates it. All 9 revived (peak 0.68-0.91, selectors live).
`alteration` is now authorable, which is what makes bVI7 — a German sixth —
expressible at all. Pattern mode and melody-only templates byte-identical.
(2) **The prototype uses it, as a controlled A/B**: `pedal_chords_voiced` /
`_smooth` are the same music as `pedal_chords` with only the plumbing changed
— proven, not asserted (old-vs-new templates identical across 3 takes; melody,
progression and tension curve identical in the rendered pairs). Measured
payoff: `smooth` inverts to minimize motion (E3m/i2 G3M/i1 A3m/i1 C3M/i2)
where the hand-voiced path can only ever emit what the author already fixed.
(3) **`mforce_cli --lint-template`** — the comp analogue of the dsp patch
linter, built because the round-trip bug in front 1 is a class, not an
incident. 236 templates → 26 hard findings after classification, 12 of them
type-defaults. Real: `chordConfig` was parseable but NOT serializable (a
template through the engine lost its chord octave); 9 dead `defaultPattern`
keys removed (render byte-identical after — the proof they were dead);
`sections[].keyName` does nothing in a run-13 probe. **The tool cried wolf
twice before it was trustworthy** — an all-null connectors list was 50 of the
first 77 findings — and one apparent bug is an anti-result: flat
`voicingPriority` is renamed on output, not lost.
(4) **The Bruckner pedal lands, closing backlog #6.** Your "modulating over
the pedal" needed TWO key fixes, not the one stage 3 delivered: chord
realization ignored key contexts entirely, and then — found by rendering, not
by reading — the **pedal itself drifted 43-43-42-41**, because key-awareness
applied to it too and G is not in G-flat major. A pedal that moves is not a
pedal. `PassageTemplate.scaleOverride` now lets a part refuse to modulate.
The passage re-lights I-vi-IV-V in each key of a chromatic-third ring over a
stationary G, closing Ger6 → I(6/4) → V7 → I at home.
**A finding for your ears elsewhere**: with the voicing patches alive again,
the priority ladder collapses — `p05` and `p1` are byte-identical renders,
`p0` differs (REVIEW 8, backlog #14).
Also corrected: backlog **#10 was already done** — markov_phrase has been
routing A-family primes through the transform library, with a fallback that
enforces each repeat actually differing. Stale entry, not new work.

Run-19 highlights (dsp): four fronts, all build/metric, no blind taste
iteration. The through-line is **things that were silently doing nothing**, and
each front was found by the previous one's tooling rather than picked off the
backlog.
(0) **The blocker that stopped run 18 is gone.** No UI process was running,
and the on-disk `mforce_ui.exe` already carried run 18's stale-guard fix
(`--stamp`: `dep set : tlog (74 files)`, `stale : no`, exit 0) — run 18's
rename-then-link trick had in fact worked, so its "NOT YET RELINKED" was
pessimistic. Backlog 3c fully closed; engine work unblocked.
(1) **FMSource's `phase` param was dead.** It was advertised, the loader
wired it, and `compute_wave_value` never read it. Proven byte-exactly before
touching anything: t1_06 (±1 cycle @ 1.7 Hz) and t1_07 (±0.5 cycle @ 220 Hz)
each rendered IDENTICAL to a twin with `phase` deleted — so **the two "PM"
cells you auditioned in the run-12 FM matrix were plain FM**. Fixed as a
carrier-side offset, which is the base class's own contract rather than an
invented one. Null test PASSES a partition, not a sweep: 131 phase-unwired
patches byte-identical, all 4 wired ones differ. And the fix makes real
sidebands, measured not asserted — t1_07 centroid 1.40x; t1_06 splits every
partial into a 1.7 Hz cluster, 6 → 109 peaks, without brightening (correct: a
slow phase sweep is a frequency deviation). The null test then caught a THIRD
patch the engine fix did not resurrect, and it was patch-side both ways —
t3_23's offsets were whole cycles (inert by construction) and it set
`"frequency"` on a source that has `density`.
(2) **That last bug is a hole in the loader, so I went looking.**
`wire_params_generic` iterates descriptors and picks matching JSON keys — any
key matching nothing is dropped **without a word**. New
`mforce_cli --dump-descriptors` (71 types) + `tools/lint_patches.py` check
patches against the engine's own truth. First run: **62 silently-ignored
params in 27 files.** Fixed the 7 live in your FM review batch —
`WanderNoiseSource`'s rate param is `speed`, not `frequency`, so every
`t2_11_all_noise_wander` cell ran at 1.0 instead of 7.0. The other 55 are
triaged in backlog 14, none fixed blind. Also `--dups`: 13 duplicate groups
across 1003 patches, and **all 13 are intentional** (gen_fm_matrix2 defines
its `med` rung AS the original patch) — an anti-result, recorded so nobody
re-derives it. I called them defects on first sight and was wrong. The tool
also cried wolf on its first run: 3 of the 62 were false positives, because
JsonConfigurator lambdas in `source_registrations.cpp` consume keys that no
descriptor set mentions (`gap` legitimately sets a member called
`gapDuration`). Allowlist now reads both files. Corrected: 59 real, **52
remaining**.
(3) **Two gaps the linter exposed, both closed.** White/Pink/Blue/Violet noise
had NO `amplitude` param at all — setting noise level required an extra
multiplier node. And the `adsr` preset dropped all six of `make_adsr`'s
randomization ranges, so an adsr envelope could not express stage jitter.
Blast radius established honestly before the change: 375 adsr nodes exist and
**0** set those keys (a file-level grep said 106; false positive from `ar`
nodes, which already worked). A/B-verified bit-exact — 327/327 identical
across the renderable affected patches — with linearity proved separately,
because the one patch that SHOULD have moved turns out to be one of **7 that
the CLI cannot render at all** ("Only StereoMixer output supported"). That is
pre-existing, is the same species as the RD audition-path mismatch, and is now
backlog 15.
(4) **Seven patches the CLI could not render at all**, found while A/B-ing
front 3 — the same UI/CLI mismatch class as the run-8 algev conversion. Three
distinct bugs, not one: a bare mono `graph.output` was a hard error though the
instrument path already auto-wraps one; `wire_params_generic` threw on any
STRING in a param slot, though a string there is always a legacy enum a
later branch handles (`WavetableSource`'s `"evolution": "target"` is ALSO an
input descriptor, so the generic loop killed the patch before its own special
case ran); and `CombinedSource` read `operation` as a string only. **6 of 7
revived**, 73/73 byte-identical regression check.

**Three things want you:** the run-12 FM matrix verdict on t1_06/t1_07/t3_23
should be treated as void — those were judged as something they weren't — and
the same for the t2_11 wander row (REVIEW 7, 8). And **one word**: CombineTest
sets `"operation": 3` as a legacy C# enum ordinal, which no current CombineOp
matches. I refused to default it to Add, because silently substituting an
operation nobody asked for is the exact bug class this whole run was pulling
out of the loader — so it fails with a named error instead. What was ordinal 3?
(REVIEW 10.)

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

Next "go": comp = #12 (one word from you unblocks it — the code is written and
sitting in the tree) → scorer passage-mode (phrase screens mis-score passages
by design: a Bruckner take with 16 notes over 50 beats scores 0.719 for
reasons unrelated to whether it works) → #7 phrase-aware cadence. Eight comp
review items waiting (five listen, two read, one word). dsp = **rebuild mforce_ui first and confirm
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
blocking piano work too. As of run 15 there are **six**: the two comp scorer
files from the 08-03 orphan run join the list (REVIEW 5 — that one is a
separate decision, adopt or discard).

How this works: [WORKFLOW.md](WORKFLOW.md)
