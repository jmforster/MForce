# Status — open this file first

Updated: 2026-08-10 — dsp run 25 (Dipsy, scheduled) · comp run 18 (Wolfie, scheduled)

> **Note on the dsp rows below:** this file had gone stale by four dsp runs —
> it still said "run 21/22" while runs 23, 24 and the 2026-08-09 overnight
> ks_piano v5 session had all landed and were only recorded in their own
> reports. Run 25 rebuilt the dsp rows; the older run-by-run highlights further
> down are kept as-is for the record.

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 18 (2026-08-06, scheduled): 3 fronts, all build/metric — **the engine was free**, so both blocked engine items landed. #13 CLOSED: `sections[].keyName` desugars at COMPOSE time into a beat-0 KeyContext, and it had a second half nobody had recorded (a section's `scaleOverride` was realized at the PIECE tonic, so a section could change its scale type but never its tonic); null test 38/38 byte-identical. #18 CLOSED: central `grid_complete` in `compose_passage`, 5/21 → **21/21** endings on the beat — and measuring found a SECOND mechanism, a passage composed longer than its section overhung the boundary mid-note. #19 CLOSED: the empty render does NOT reproduce (99 notes at HEAD), and `score()` now REFUSES (composite `None`) rather than flagging | #7 phrase-aware cadence; #8 voicing open items; #9 PAC held-note; #14 priority semantics (his call); #20 passage-mode semantics (needs your read) | **8 items** — passage endings beat-vs-bar [listen] NEW, section key tonic [listen] NEW, endings A/B [listen], passage-mode semantics [read], Bruckner v2 [listen], pedal_chords voiced [listen], repeat transforms A/B [listen], voicing_ab 9 WAVs [listen] |
| comp (prev) | Wolfie | run 17 (2026-08-05, scheduled): 4 fronts, all Python-side — the dsp lane was live, so #13 was not attempted. #16 (ending overshoot was grid completion CEILING onto the next barline, not the draw; v4 `calib` default), #17 (offender-first range guard), #15 (passage-mode scorer, uncorrelated with the phrase composite at Spearman 0.072) | — | (folded into run 18) |
| dsp | Dipsy | run 25 (2026-08-10, scheduled): 3 fronts, all build/metric. Through-line — **two of the three were work that already existed and had never been checked.** (1) Matt's vowel verdict folded: **7 winners LOCKED** into `patches/library/voice/`, all 7 byte-identical to the WAVs he auditioned; the 3 he rejected got measured, not swept — vowel separability collapses monotonically with f0 (26.94 bass → **14.80 dB soprano**) because at 440 Hz soprano E and I share BOTH formant harmonics. (2) **3e-NEXT(b) was live in HEAD since 08-06 and completely inert** — the orphan's code had been swept into run 23's commit but nothing called it; wiring it then regressed the bass, and the diagnosis was that a single stiff-string B holds only to n≈16, so the mask now sits on MEASURED peaks. Piano band2 fixed on every note (G4 2056 → 0.00202). (3) Backlog 14: **66 lint findings → 7, and 59 were the tool** — a hand-copied allowlist had missed run 22's `timeMode`, flagging every ks_piano patch including Matt's v5 audition set | 3k audio-thread AV; 3i envelope hold; 3l modulated even/odd weight — **all three engine, all three blocked this run by the concurrent comp lane**; then 3e-NEXT b2/b3 | **6 items** — soprano alto-formant A/B [listen] NEW, ks_piano v5 [listen], ks_piano ladder [listen], node-graph streams [try], power-renorm answer [read] |
| dsp (prev) | Dipsy | run 24 (2026-08-06) + the 2026-08-09 overnight session: the **KS piano pivot** — three new engine nodes (KSPianoString, HammerBank, AllpassResonator), a v1-v4 ladder whose dispersion allpasses fit the measured Iowa inharmonicity with no curve-fitting (C2 B=1.23e-4 vs 1.23e-4), then v5 after Matt's "kick drum, disappointed" verdict — full Alpha-Forever description, dry taps removed, 7-variation set. Also vowel pass 2 (43 WAVs), node-graph stream decay fixed, instrument-level volume + damper release | (folded into run 25) | (folded into run 25) |
| dsp (prev) | Dipsy | run 21 (2026-08-05, scheduled): **SlewLimiterSource** landed (Slew/Lag/**Peak**) — measuring caught the backlog's premise was wrong, the clicks are IMPULSES not steps; CombinedSource finally parses `sum`; **broadband_ratios empty-band blowup fixed** — piano C6 band0 was 4.63e+11 and dominated the smoke's term3. NOTE: run 22 REVERTED the slew/click family per Matt ("dead end"); the CombinedSource fix survives | — | (folded) |

Reports: **dsp/reports/2026-08-10-dipsy-run25.md** ·
comp/reports/2026-08-06-wolfie-run18.md ·
dsp/reports/2026-08-06-dipsy-run24.md ·
dsp/reports/2026-08-06-dipsy-run23.md

Run-25 highlights (dsp): three fronts, all build/metric, no blind taste
iteration. The through-line is **work that already existed and had never been
checked** — two of the three fronts were verification passes that turned into
repairs.
(0) **The lanes collided again.** A comp run started in this working copy
mid-session: `composer.h` and `default_strategies.h` were clean at my start
and modified after front A, with three more engine headers joining by the end.
The start-of-session commit-age guard cannot see a run that begins *after*
mine — same as run 21, second time in a row. Consequence: **engine work was
ruled out for the entire session**, which is why front C is a patch-side item
and why 3k, 3i and the new 3l are all still waiting. All commits used explicit
paths. `mforce_ui.exe` was NOT running, so the engine was otherwise free —
this was purely a scheduling collision and is worth fixing at the schedule
level.
(1) **Vowels locked, and the sopranos explained rather than swept.** Seven
patches into `patches/library/voice/`, each verified byte-identical (sha256) to
the exact WAV Matt auditioned — the library file is the approved sound, not a
re-derivation. For the three he rejected, two measurements instead of another
43-WAV pass: separability collapses monotonically with pitch (mean vowel-pair
distance 26.94 / 23.87 / 21.39 / **14.80 dB** across bass/tenor/alto/soprano),
and the mechanism is explicit — at 440 Hz soprano E and I put F1 on the *same*
harmonic and F2 on the *same* harmonic, so they can differ only in gain. Six
harmonics live below 3 kHz at soprano pitch against 27 at bass. **A bug in my
own harness was caught by its own guard**: the first 2×2 reported the soprano
recipe identical at both pitches to two decimals, because these patches drive
pitch from `score[].note` and setting `src.frequency` does nothing — the script
now reads the fundamental back out of the render and refuses to score an arm
that did not retune. The candidate (alto formants, unmoved, sung at A4) buys
E-I +6.69 dB and U-O +8.66 dB, with an O control in the pair set. REVIEW 25.
(2) **3e-NEXT(b) was already in HEAD, and doing nothing.** The 08-06 orphan
run's stretch-aware `refmetrics.py` had been swept into run 23's commit
`4ae968a` with no report — so the primitives shipped, their test passed, and
**no caller existed**: no config opt-in, no mention in the reference builder or
the scorer, and a stored reference that predated the code. Spec steps 1-3 done,
4-5 not. Wiring it up then **regressed the bass** (C2 band2 3.995 → 29.34, and
C2 is an eval note), and the diagnosis is the payload: the fitted B is fine,
but a single stiff-string B only holds to n≈16 — past that the model is 5-10
mask-widths off, and a mask wide enough to hold every tracked partial would
need 0.36-0.63·f0, which would swallow the gaps being measured. So the mask now
sits on the peaks the tracker actually **found**. Every note improves and none
regresses; G4 band2 goes 2056 → 0.00202. Same patch, same code, only the
reference differs: TOTAL 1.1879 → 1.0449 — **the patch did not get better, the
metric had been charging it for a defect in the reference's own measurement.**
Viola and clarinet references regenerate byte-identical.
(3) **The patch linter cried wolf for the third time, at scale.** 66 findings
in 30 files, **59 of them false**. A hand-copied allowlist had missed run 22's
Envelope `timeMode` — the absolute-seconds chuff fix — so the tool was flagging
the piano template and *every* ks_piano patch, including the v5 set queued for
Matt's ears; proven live rather than argued by deleting the key and watching
the render peak go 0.601 → 0.018. It also missed `.at()`, which is how
AdditiveSource2 reads its evolving/envelope keys, and `releaseMax`, which
retires backlog 14 group (c) outright. The allowlist is now scraped from the
loader sources at lint time so it cannot drift again. **The one real bug was
worse than the backlog said**: `add_organ_test`, `add_saw_test` and
`add_square_test` rendered *byte-identical to each other* — an organ, a saw and
a square producing one waveform, because none of them wires a partials node at
all. After migration all three are distinct and correct against theory, not
merely different: the square's even harmonics sit 98.5 dB below odd, and both
it and the saw follow 20·log10(1/n) to the decimal. 66 → 7, all 7 in
`Squeaker.json`, which is a genuine capability gap (modulated even/odd weight)
and is now item 3l rather than a silent flattening of one of Matt's patches.

Run-18 highlights (comp): three fronts, all build/metric, no blind taste
iteration. The through-line is **two of the three defects were not where the
backlog said they were**.
(0) **A dsp run died in this working copy at 03:12 this morning.**
`research/ml_ears/refmetrics.py` is modified and stamped 08-06 03:12:45, with
`docs/superpowers/specs/2026-08-06-stretch-aware-metrics-design.md` (03:11:50)
and `research/ml_ears/test_stretch_metrics.py` (03:14:20) untracked beside it
— a spec plus an implementation plus a test for **3e-NEXT (b) stretch-aware
heterodyne**, the top dsp item, with no report and no commit. Same orphan
pattern as 08-01 and 08-03. Nothing written to them in the four hours since,
`build/` untouched for six hours before my first build, so the lane was not
live while I worked. Left exactly as found. **Dipsy: adopt or discard before
re-doing that item from scratch.** (The standing dirt is the other two,
`c2c_quiet.json` 08-01 and `v6_01_res_curve_lo.json` 07-30.)
(1) **#13 — the section key had a second half nobody had written down.**
`sections[].keyName` was dropped without a word; it now names the section
TONIC and desugars at COMPOSE time into a beat-0 KeyContext (parse stores /
serialize writes back, so the template round-trips and the linter stops
reporting it — desugaring at PARSE time would make `to_json` emit both forms
and re-reading the engine's own output would trip the conflict throw against
itself). The unrecorded half: `setup_piece_` realized a section's
`scaleOverride` at the **piece** tonic, so a section could change its scale
TYPE but never its TONIC. Accepts `"G"` or `"G Minor"`, composes with LATER
keyContexts, throws by name on a beat-0 conflict and an unknown key. Null test
**38/38 WAVs byte-identical** — measured against a reverted build, not argued
from the code. And run 13's carried caveat is CONFIRMED rather than repeated:
it changes accidentals, it does NOT move the tonic (in D major the C3 entry
is not even a scale tone and snaps to C#). Three-arm A/B, REVIEW 16.
(2) **#18 — grid completion fixed most of it, and measuring found the rest.**
Central `passage_anchors::grid_complete` in `Composer::compose_passage` (the
one point every strategy returns through), NEAREST multiple not ceiling, knob
`endGrid`. That got 21/23 — and the two stragglers were the payload. One was
a **stale render from an 08-01 `--takes 4` run** that every sweep since has
silently counted, run 17's 37/74 included. The other was real:
`wandering_24x` composed a clean 49.0 into a 48-beat section, so realization
truncated it and the last surviving note overhung to 48.042 — quantizing the
final unit could never help, because that unit was never realized. Notes now
clamp to the section end. **5/21 → 21/21.** Blast radius stated, not assumed:
31/38 templates byte-identical, all 7 that changed moved onto the grid.
Beat-vs-barline default is REVIEW 17.
(3) **#19 — half of it does not reproduce, and the flag was not enough.**
`wandering_24x` renders 99 notes at HEAD. On the design question, `score()`
now REFUSES: a composite over 0 notes is not LOW, it is UNDEFINED, so it is
`None` below the 4-note threshold and arithmetic raises instead of quietly
averaging a placeholder. I checked it was safe BEFORE changing it and my
first assumption was wrong — I expected figure-level scoring to break on
2-3-note figures, and `figure_transforms` in fact samples at `kmin=5`, so no
caller is affected. Five aggregation sites in the batch drivers now filter on
`scorable`; `figure_transforms` deliberately does not, because a short figure
there is a bug worth a traceback.

Run-17 highlights (comp): four fronts, all metric/build, no blind taste
iteration. The through-line is **the mechanism was not where the number said
it was**.
(0) **A live collision, not a tree-guard one.** The dsp lane was running in
this working copy AT THE SAME TIME — its commits landed between mine at 08:23
and 08:25, with engine/ and build/ being written throughout. Nothing of mine
touches those files, but it rules out engine work: an engine edit must rebuild
both targets in the same cycle and that is not verifiable against a concurrent
build. **#13 was not attempted and is first up next comp run.**
(1) **#16 — the ending rule's overshoot was the BARLINE, not the draw.** A new
unrendered replay harness (`final_rule_sweep.py`, same rng streams as the real
batch) makes n=200 arms cheap, and decomposing the ending showed the v4 draw
already landing at ratio p50 **2.00**, exactly the corpus median — while grid
completion CEILED every phrase onto the next barline and added a further
uniform [0,1) beats on top, p50 +0.25. Two supporting mechanisms measured:
the v3 anchor multiplies a phrase's own longest note, which is *already* median
2.1x / p95 8x its pulse. v4 `calib` keeps the longest-frequency you asked for
twice and bounds the magnitude — the final EQUALS the phrase max instead of
exceeding it, the histogram branch is capped at corpus p95, and grid completion
rounds to the NEAREST barline. n=200: p50 3.00→2.50, p95 11.00→7.00,
over-corpus-p95 0.170→0.065, final-is-longest 0.785→0.620 (corpus 0.354).
Anti-result recorded: raising the longest-branch probability 0.6→0.7→0.8
SATURATES at 0.685, because nearest-grid rounding trims finals back regardless.
(2) **#17 — the range guard was punishing the wrong transform**, and the new
A/B harness reproduces run 16's exact report: p18, ornament reverted so an
invert three occurrences earlier could stay. Offender-first now; at a tight cap
the transforms surviving the guard go 3 → 15 over 300 phrases.
(3) **#15 — passage mode.** The backlog's own example reproduces exactly:
`pedal_mod_minor3rds_1_1`, 16 notes over 50 beats, 0.719 phrase vs 0.876
passage. One real design correction inside it: tension must be measured to the
PEAK, because a whole-passage slope reads NEGATIVE on exactly the passages that
work (they build and then resolve). Verified by mechanical degradation, not
taste — shuffle_pitch −0.235 vs the phrase composite's −0.066, offbeat_end
−0.062 vs −0.012. Across 74 renders the two screens are **uncorrelated
(Spearman 0.072)**. Two honest negatives kept: retrograde is barely caught (a
build-and-resolve arc is near-symmetric in time), and on the single taste
datapoint available it does NOT reproduce your minor3rds pick — reported, not
tuned to.
(4) **Wiring it in found the real defects.** `pedal_buildup_6lv` is the WORST
row on the phrase composite (0.660, punished for repeating) and 0.808 in
passage mode. And a sweep says **37 of 74 passage renders end off the beat**,
in every family — the phrase path grid-completes onto a barline, the C++
passage strategies have no equivalent, and nothing measured it until now
(#18). Two batch-killing KeyErrors fixed on the way, plus: `wandering_24x` is
an EMPTY render, 0 notes, and the phrase composite scores it **0.407** (#19).

Run-21 highlights (dsp): three fronts, all build/metric, no blind taste
iteration. The through-line is **premises that were wrong, caught by
measuring instead of asserting**.
(0) **Two scheduled runs overlapped in this working copy.**
`corpus/mtd_seg/markov_phrase.py` was clean at my session start and turned up
modified mid-run, written 9 seconds before I looked — a comp run had started
on top of mine, and it went on to interleave two commits (e43e47a, faa7b07)
between my three. My builds were already done and verified by then, so I
committed with explicit paths only and did no further build work (fronts 2
and 3 are render-only and Python-only). Nothing was corrupted. But **the
start-of-session commit-age guard cannot see a run that starts AFTER mine**,
so this can recur; worth a look at the two schedules.
Also: `mforce_ui.exe` was locked by three of your UI processes, so I used run
18's rename-then-link approach — the running exe is now
`mforce_ui_locked_20260805.exe`, your open windows are unaffected, and they
pick up the new build on restart. `--stamp` exit 0, 75-file tlog dep set.
(1) **SlewLimiterSource exists, and the item that asked for it was wrong.**
The backlog said clicks are STEPS that a rate limiter would stretch into
ramps. They are one-sample IMPULSES — 0, ±0.5, 0 — and a sign-keyed limiter
turns that into a one-sample impulse of height `rate*dt`, i.e. it makes the
click quieter and no longer. Rendering the obvious ladder would have measured
a level drop and nothing else. Shown, not argued: in `step_slew_impulse`,
Slew mode holds a POSITIVE impulse for 10.00 ms and a negative one for
0.02 ms — one sample. So the node ships with a third, magnitude-keyed **Peak**
mode that is symmetric, which also makes it a general envelope follower.
All timing gated: Slew rise within 0.5-1.9% of S/rate at 0.002%
nonlinearity (a straight ramp); Lag tau within **0.05%** of 1/rate at 33%
nonlinearity (correctly NOT straight); fallRate asymmetry 10.05x vs 10x.
(2) **The click dial is real and it is not a lowpass.** Attack centroid
7116 (control) → 6404 → 4745 → 3667 → **3237** Hz as fallRate falls
20000 → 50, >8 kHz share 37.9% → 13.6%, monotonic, 6/6 distinct by sha256.
Worth knowing before you listen: the glide is `0.5/fallRate` seconds and a
linear phase glide IS a constant frequency offset, so fallRate trades chirp
LENGTH against a `fallRate/2` Hz deviation. `f20000` is the deliberate
degenerate end (0.025 ms ≈ one sample) and should be indistinguishable from
the control — if it isn't, tell me. (REVIEW 15.)
(3) **The piano smoke's term3 was measuring a noise floor.** `BANDS[0]` is
(160, 700) Hz, so an eval note above 700 Hz left it with no harmonic line and
the ratio became `between_e / 1e-12`. Not hypothetical — it was sitting in
the stored reference: **C6 band0 = 4.63e+11** against O(1) or smaller for
every other note, and C6 (1046.5 Hz) was the only eval note above 700, so
that single cell dominated term3 in the 1.521 → 0.900 smoke. Empty bands now
return NaN and are averaged out as missing. Regenerating the reference
changes C6 band0 to nan and leaves **every other value identical to the
digit**.
**One thing that enlarges the next item**: piano band2 ratios run 120 (C3),
260 (C5), 2056 (G4), and the line mask sits at `k*f0` like the heterodyne
does. Partials stretched by `sqrt(1+B n^2)` fall off the mask and get counted
as inter-harmonic energy — so 3e-NEXT (b) is not just a motion/harm_env fix,
it distorts the broadband term too.
Also corrected: backlog 11 has **shrunk**. `score_candidate.py` and
`iowa_reference.py` were committed by run 20, so HEAD can run the clarinet
pipeline on its own; only the two patch files are still standing dirty.

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

**Superseded by run 18** — comp items 13, 18 and 19 are all DONE. comp next =
**#7 phrase-aware cadence placement** (the AFS impedance finding, and the
oldest untouched item on the list) → #8 voicing open items → #9 the PAC
held-note workaround. #14 (voicing priority ladder) and #20 (passage-mode
semantics) both want your call before anything moves. **Eight comp review
items waiting** (seven listen, one read), two of them new this run and both
default-setting questions: passage endings beat-vs-barline, and whether a
section key should move the tonic.

Old next-up text, kept for the record: comp = **#13 section key first** — it
is an engine edit and needs a
build dir no other lane is using; run 17 could not touch the engine at all
because the dsp lane was live in this working copy → #18 (37 of 74 passage
renders end off the barline) → #19 (the empty `wandering_24x` render) → #7
phrase-aware cadence. Six comp review items waiting (five listen, one read). dsp = **rebuild mforce_ui first and confirm
`--stamp` exits 0**, then item 3c2 (FMSource's `phase` param is dead — apply
phase_ to the carrier for true PM, then re-render the t1_06/t1_07 topologies
as designed), then 2d-1 (vector path, needs an AVX2-availability decision).
All three are engine edits and all three were blocked in run 18 by the locked
exe. Clarinet 600-eval stays gated on your ears. Verdicts fold in whenever
you send them.
**Superseded by run 25** — items 3e-NEXT(b), 14 and 10 are all DONE, and 3g
was reverted back in run 22. dsp next = **3k (audio-thread AV in
`WaveSource::next`, from your 08-06 crash log)** → **3i (envelope hold/loop,
the true-infinite-stream blocker)** → **3l (modulated even/odd partial
weight)**. All three are engine edits and all three were blocked this run by
the comp lane running concurrently in this working copy — not by anything in
the work itself. After those: 3e-NEXT b2 (`derive_motion` takes no B) and b3
(the heterodyne breaks down below ~100 Hz, which contaminates term2 via C2),
then item 13 (piano beat rate, attempt 3). Piano full run and clarinet
600-eval both stay gated on your ears. **Six dsp review items waiting** (four
listen, one try, one read) — the newest is the soprano alto-formant A/B, whose
verdict decides whether the vowel front continues or retires as
sampling-limited.

**Worth your attention at the schedule level:** the dsp and comp scheduled
runs have now overlapped in this shared working copy twice running (run 21,
run 25). The session-start commit-age guard only sees runs that started
*before* it, so it cannot catch this, and the cost each time is that the whole
engine backlog goes untouchable for the session. Staggering the two schedules
would recover it.

Standing tree note (updated run 25, dsp): **the orphan is resolved and the
count is back to two.** `research/ml_ears/refmetrics.py` is no longer dirty —
run 23's commit `4ae968a` swept it in, so the 08-06 orphan's stretch-aware
code was adopted without anyone deciding to. Run 25 verified it, found it
inert, finished the wiring and adopted its test as well; the spec was already
committed in the 08-08 housekeeping pass. Nothing of that orphan is
outstanding. What still needs one word from you is backlog 11:
`patches/clarinet_c2/c2c_quiet.json` and
`patches/fable1_v6/v6_01_res_curve_lo.json`, dirty at every session start for
eleven runs now — commit or revert. Previous note follows.

Standing tree note (updated run 21): **down to two**, and no longer blocking
anything. `iowa_reference.py` and `score_candidate.py` were committed by run
20, so HEAD can run the clarinet CMA-ES pipeline and the piano
onset-alignment prereq on its own. What is still dirty at every session start
is `patches/clarinet_c2/c2c_quiet.json` and
`patches/fable1_v6/v6_01_res_curve_lo.json` — a clarinet variant and a UI
re-save, seven runs running. Backlog 11 wants one word: commit or revert.
(The two comp scorer files from the 08-03 orphan run are a separate decision,
REVIEW 5 — adopt or discard.)

How this works: [WORKFLOW.md](WORKFLOW.md)
