# Status — session handoff (Claude reads this first; Matt's queue = */REVIEW.md)

Updated: 2026-11-07 (refactor session X continued, Fable 5.1 xhigh).
NEXT SESSION STARTS HERE: the FORMANT FAMILY is decided with Matt (Part 3
§11, the formant half of CLEANUP 17): `Formant` the abstract base in
`PartialSet`'s shape with one verb `gain(freq)`, `contains()` and `IFormant`
gone; `FormantPeaks` (today's `FormantSpectrum` + its `Formant` nodes, the
peaks as four `Array` rows, the max rule verbatim; the editor's node-to-row
bridge goes); `FormantBands` (today's `BandSpectrum`); `FixedSpectrum`
deleted; `FormantSequence` kept, general; `formant`/`formantWeight`/
`formantFloor` keep their names; `FormantCurve` and the band-grid sweep are
in IDEAS for after the freeze. The word was weighed: a formant is a gain over
absolute frequency imposed on the source, invariant across fundamentals (the
filter half), where the partials are the source's spectrum — so "spectrum"
left the names. The 3.9 PLAN IS WRITTEN at xhigh:
`docs/superpowers/plans/2026-11-07-part3-checkpoint-3.9.md` — eleven rulings;
the one design the survey added is `Part : Component` (`core/part.h`), the
owner-driven part's lifecycle once, with `PartialSet` and `Formant` deriving
from it (ruling 1); `FormantPeaks` caches its band edges; `FormantBands::gain`
folds the old `contains` predicate in (ruling 4: the sub-bin below `startFreq`
is where dropping it would move a render); the consumer's factor is
`floor + weight × gain` with one listed departure, a negative-zero floor, that
no in-scope patch can reach (58 floors at 0.0, 3 at 1.0, none wired). Task
order: Part → FormantPeaks beside the old types → the tool rewrites the tree
(69 + 3 + 4 files; pending/ originals kept) → the base and the deletions →
the editor's bridge (~250 lines) → the close with a Fable refuter. EXECUTE
NEXT SESSION (this one is at three quarters of its context): Fable on Tasks
1, 2, 4 and the refuter, Opus 5.5 executors on 0, 3, 5 and 6. Then Part 4. 3.8 is sealed and
PUSHED (`f612316..22bdc4b`); main level with origin at this update.
GOALS.md stays Matt's uncommitted edit.

---

Updated: 2026-11-06, later the same day (refactor session X, Fable 5.1 xhigh).
NEXT SESSION STARTS HERE: checkpoint 3.8 LANDED on main (`2dfabe2`..`e27f7ad`,
the fix waves `b254b5c` and `268fb0a`, plus the docs commit; report R14 `docs/autonomy/refactor/reports/
2026-11-06-checkpoint-3.8.md`) — the eighth of Part 3's eight: PART 3 IS
COMPLETE. NEXT is Part 4 (patch): its architecture document is NOT written
(target architecture section 8 names it; the BACKLOG row says max). THE
SHAPE OF 3.8: `IPartials` → `PartialSet`, an abstract `Component` base owning
the pin lifecycle (prepare/start over every pin, next over every Param pin,
then the hooks); `partials.h` one type per file (`expand_rule.h`,
`expand_rule_node.h`, `partials.h`, `full_`/`sequence_`/`explicit_`/
`composite_partials.h`); six value-type layers `Partials` composes —
`CoherentWalk` (motion and shimmer, over the new `core/dsp/segment_walk.h`),
`PairTrade`, `BandwidthNoise`, `OnsetGate`, `DecayLaw` — and `PartialLayout`
(the six arrays, their originals, the expansion); `Partials` 1,148 → 561
lines; `BasicAdditiveSource` and `AdditiveSource2` deleted,
`FullAdditiveSource` → `AdditiveSource` (the "Full" label gone, "Additive"
derived); the one AS2 baseline retired to `patches/old/additive_source2/`
and re-authored as `patches/baselines/additive_bright_attack_test.json`
(REVIEW R14 item 1: a render for Matt's ears; the WAV pair under
`renders/scratch/ab38/review/`). PROOFS: null gate 78/78 after every task;
the close A/B 131 of 131 identical, 0 DIFFER; thirteen synthetics (`renders/scratch/ab38/synth/`)
identical after every task; seven twin tests bit for bit, each mutated once
by the coordinator and caught; allocation gate 132 patches 0 allocate;
`gates --fast` as before (the known wiring diff). REVIEW: an Opus 5.5
reviewer, read-only, 0 Critical, 0 Major, 10 Minor, 11 Flag — the Minors fixed in the wave, re-review closeable. EXECUTION: the first checkpoint on the
11-06 combo — Fable coordinator writing the plan, the part base, the
layers, the layout and the report; Opus 5.5 executors on the deletions,
the file split and the close's measurements; the Fable weekly window hit
93 percent at the close, so the reviewer ran on Opus 5.5 too (ruling 13).
THE ONE TIMING FINDING, fixed before the seal: `fadd_explicit_test` ~10%
slower because `PartialSet::next` walked every pin per sample; the Param
pins are now listed at prepare (`268fb0a`), flat after.
HARNESS NOTES: `std::vector<T> v(size_t(n));` is a function declaration
(the most vexing parse) — `static_cast<size_t>(n)`; the committed
`tools/structure/metrics.json` was 3.6's until the close's `--snapshot`, so a
mid-checkpoint `--delta` measures the whole checkpoint, not the task. GIT:
main is ahead of origin and UNPUSHED (ask Matt at close-out per the
standing rule). GOALS.md stays Matt's uncommitted edit.

---

Updated: 2026-11-06 (refactor session X, Fable 5.1 xhigh).
NEXT SESSION STARTS HERE: checkpoint 3.7 is CLOSED WITHOUT A CHECKPOINT
(Matt, 2026-11-06: "skip to 3.8"). The 11-05 handoff restated Part 3 §9's
work list from the spec text; the code shows every item of it landed before
this session — 2.6 (`6a37849`..`d3df3ea`) reserved every frequency-dependent
table at `prepare` for the 20 Hz floor and fits it within capacity per note
(the wavetable's `values_`, the hybrid's `table_` and its three partial
arrays sized with `numPartials`; Segment's list reserved at 3.1), and 3.4
(`efd10c7`..`0d1e826`) gave every evolution `on_prepare(ctx, maxLen)` with
its reserve (Histogram's `cdf_`/`counts_`, Target's `resampledTarget_`,
Bezier's `target_`, CA's `state_`/`snapshot_`, EKS's `filtered_`). PROOFS
already standing: `alloc_gate` 132 patches 0 allocate (in `gates.py`'s slow
list); `engine_tests` allocation tests over the wavetable at four pitches,
nine evolutions at two pitches, Target, Pluck, HybridKS, Phased, Segment,
BasicAdditive, Multiplex, Repeating, a held release and a drum hit. Part 3
§9 and row 3.7 were written before 2.6 and never updated; both now say
landed. WHAT REMAINS under the real-time rule, booked not done: the
live-edit class — a structural setting changed in the editor after `prepare`
and read live on the audio path (HistogramEqualize sizes `cdf_` at `prepare`
and reads `bins.value` per sample) — stays with Part 7's "UI live-edit case"
where 2.6 put it (CLEANUP 33; whether the editor re-prepares a node after a
structural edit is UNVERIFIED); the gate's blind spots (no mid-note edit, no
per-patch held-release case, the mixer's own render unarmed) CLEANUP 34; the
wavetable's two first-sample blocks CLEANUP 35; `AdditiveSource2`'s
`on_slot_changed` override goes with its deletion in 3.8. NEXT is 3.8
(`Partials` split by responsibility, Part 3 §7: "designed then;
byte-identical target"), FABLE THROUGHOUT per the 11-04 combo; its plan is
WRITTEN at xhigh (Matt: "stay at xhigh"), committed:
`docs/superpowers/plans/2026-11-06-part3-checkpoint-3.8.md`. THE SHAPE:
`IPartials` → `PartialSet`, an abstract `Component` base owning the pin
lifecycle (3.4's `WaveEvolution` shape; ruling 1); `partials.h` one type per
file (eight); six value-type layers `Partials` composes — `CoherentWalk`
(motion and shimmer, over a new `core/dsp/segment_walk.h`), `PairTrade`,
`BandwidthNoise`, `OnsetGate`, `DecayLaw` — and `PartialLayout` (the six
arrays, originals, expansion); no layer is a Component (ruling 4);
`BasicAdditiveSource` and `AdditiveSource2` deleted, `FullAdditiveSource`
renamed `AdditiveSource`; the ONE render that moves is the AS2 baseline, by
deletion — retired to `patches/old/additive_source2/`, replaced by a
hand-authored `additive_bright_attack_test.json` (two ExplicitPartials under
a CompositePartials, per-group amplEnv; ruling 8) → REVIEW R14 item 1 is a
render Matt hears. PROOFS: null gate per task, a twin test per moved
primitive (3.3's method), a synthetic set per layer (ab38/synth), the close
A/B over 131, alloc gate 0, timing on the additive set. SPLIT: Fable on
Tasks 2, 4, 5, 6, the replacement patch and the refuter; Opus 5.5 subagents
on 0's set, 1, 3, 7's runs and docs. MODEL RULE CHANGE (Matt, 2026-11-06):
the "never Opus 5/5.5" rule is RESCINDED — "just delegate to Opus 5.5
agents"; executors and reviewers are Opus 5.5 subagents (Agent model
"opus"), Sonnet is not the executor tier, Opus 4.8 is not the required
fallback; CLAUDE.md, the SessionStart hook and the memory carry it; the
plan's ruling 12 amended. EXECUTE AFTER the Fable reset (2026-11-07 02:00
HST); Task 0 first (`cli_pre38.exe`, the ab36 new arm reused as old, the
synthetic set). IFormant is NOT moved (ruling 2 records the convention;
REVIEW R14 item 2 asks the formant base's name). Model check: Fable 5.1 xhigh this session,
the change back from 3.6's Opus 4.8 the 11-05 handoff said to expect.
Budget at session start: Fable weekly 82 percent, reset 2026-11-07 02:00
HST. GIT: main level with origin at session start. GOALS.md stays Matt's
uncommitted edit. `docs/matt/MultiplexQ.txt` was ANSWERED on 11-05: not an
open thread, do not reread it.

---

Updated: 2026-11-05 (refactor session IX, Opus 4.8 high — the 3.6 coordinator
per the model combo; Fable held for 3.7's plan and 3.8).
NEXT SESSION STARTS HERE: checkpoint 3.6 LANDED on main (`ed04dda`..`5c4c5db`,
report R13 `docs/autonomy/refactor/reports/2026-11-05-checkpoint-3.6.md`) — the
sixth of Part 3's eight. NEXT is 3.7 (real-time fixes, Part 3 §9): its plan is
NOT yet written. Per the model combo (Matt 2026-11-04) 3.7 is Fable — Fable
writes the plan and executes the engine hot-path tasks, Sonnet 5.5 the rest, a
Fable refuter with a brief narrowed to the live path, sizing, and the
allocation gate's coverage (Opus 4.8 as refuter only if the week's Fable runs
short). So the session-start model check should expect Fable again (a change
back from this session's Opus 4.8). 3.7's work: the wavetable table sized in
`prepare` not on the first sample, the hybrid-KS DFT scratch in `prepare`, the
per-cycle vectors of `HistogramEqualize`/`TargetEvolution`/`BezierPull`/
`CellularAutomaton` as `prepare`-sized members, EKS's per-note table a member
sized once — none changes a sample; the allocation gate is the proof.

THE SHAPE OF 3.6 (move-only, nothing audible or editor-visible): the six
multi-type node headers each became one type per file in its family folder.
`wave_evolution.h` → the `WaveEvolution` base + 13 evolution files (Task 1
`ed04dda`); `noise_sources.h` → six files, deleted, and the shaped noise draw
white and red share → one `inline` helper `noise/shaped_draw.h`, plus
`wander_noise_source.h` → three (Task 2 `ada05cd`); `filters.h` → four files,
deleted, `combined_source.h` keeps `CombineOp`+`CombinedSource` with
`CrossfadeSource` beside it and `DistortedSource`→`filter/` and the two
statics→`utility/`, `formant.h` keeps `IFormant`+`Formant` with four spectra
beside it (Task 3 `3d2bcf1`); the six family `.cpp` moved under
`engine/src/source/<family>/` with the module map's `source` now two path
prefixes (Task 4 `5c4c5db`). PROOFS: null gate 78/78 after every task
(coordinator rerun before each commit); body-diff byte-identical for all 26
moved/retained types; the shaped-draw twin (4802 rows) proven to catch a
reordered draw and a dropped clamp; a synthetic white-shaped patch (no in-scope
white node leaves the fast path) renders identical old vs new; the close A/B
over all 132 `library`+`baselines` patches 0 DIFFER; ctest 5/5; timing flat
(0.92–1.06×, noise). METER (--delta from pre-3.6): files +32, files_over_limit
20→19, include_violations +0 (14), unmapped 0, dynamic_cast +0,
`duplicate_blocks` 146→163 (+17 — one-type-per-file scaffolding + bw sibling
similarity, no new logic; accepted, CLEANUP 31; the Butterworth base CLEANUP
32). metrics.json regenerated (--snapshot) so 3.7's --delta baseline is current.
DECISIONS (2026-11-02 rule, nothing for Matt's queue): the duplicate_blocks
acceptance; `fm_source.h`/`patch_loader.cpp` dropped transitive-only includes;
a stale `filters.h` comment and `lint_patches.py`'s source path repaired in
place. REVIEW: an independent reviewer in place of the Fable refuter (ruling 8)
— verdict CLEAN, two record-only Flags (the helper's `lastSign==0` guard is a
twin blind spot that can't affect byte-identity; the `lint_patches.py` edit was
not in the plan's Task 4 bullet). No Major, no re-review. EXECUTION: Opus 4.8
coordinator, one Sonnet 5.5 executor per task in sequence, each verified by the
coordinator (null gate rerun, body-diff) before commit. HARNESS NOTES: the
structure meter walks `git ls-files`, so a deleted header must be STAGED before
its tests pass (Tasks 2–4 each failed `structure_meter` until the deletion/
move was staged); `tools/lint_patches.py`'s `SCRAPE_SOURCES` lists source files
by path and must follow a move. GIT: main is ahead of origin and UNPUSHED
(ask Matt at close-out per the standing rule). GOALS.md stays Matt's
uncommitted edit; `docs/matt/MultiplexQ.txt` is Matt's note (both left
untouched).

---

Updated: 2026-11-04 (refactor session VIII continued, Fable 5.1 xhigh).
NEXT SESSION STARTS HERE: the 3.6 plan is written and committed
(docs/superpowers/plans/2026-11-04-part3-checkpoint-3.6.md) — one type per
file over six headers, the family .cpp files under src/source/<family>/,
the white/red shaped draw as one twin-tested helper. Execute it after the
2026-11-07 Fable reset under the three-tier split Matt set on 11-04: Fable
writes plans and refutes where the gates are blind; Sonnet 5.5 executors
take the mechanical tasks; claude-opus-4-8 (never Opus 5/5.5) reviews 3.6
in place of a Fable refuter and coordinates if the Fable window is spent.
At every plan, read get_usage and put the split to Matt in one line.
MODEL COMBO PER CHECKPOINT (Matt, 2026-11-04, "Perfect. Proceed."):
  3.6 — coordinator claude-opus-4-8 (start the session on it: the plan is
  written, Fable is not needed), one Sonnet 5.5 executor per task in
  sequence, reviewer claude-opus-4-8 against the plan's tables, no Fable
  refuter, no re-review unless a Major. The coordinator reruns the null
  gate before every commit.
  3.7 (real-time fixes) — Fable writes the plan and executes the engine
  hot-path tasks; Sonnet the rest; a Fable refuter with a brief narrowed to
  the live path, sizing and the allocation gate's coverage (roughly half of
  3.5's spend); Opus 4.8 as refuter only if the week's Fable runs short.
  3.8 (Partials) — Fable throughout.
  The session-start model check will flag Opus 4.8 as a change from Fable;
  that is intended for 3.6 and this paragraph is the authority for it.
  Matt's new session for 3.6 is planned for 2026-11-05, before the Fable
  reset (2026-11-07 ~02:00 HST): Opus 4.8 draws on the all-models window
  (40 percent used on 11-04), not the Fable one (79 percent).
CHECKPOINT 3.5 LANDED on main: 750f70e..a2ff727 (b1d0222 the review wave, a2ff727 the re-review's two flags) — the fifth of Part 3's
eight. THE SHAPE: Part 3 §6's premise ("Envelope already has a preset
setting") was wrong in the letter — the preset is a load-time FORM
(envelope_from_preset_json expands "preset": "ar"|"adsr" into stages; 1,125
nodes load that way) — so the six preset classes collapsed in that
direction: four more Envelope factories beside make_ar/make_adsr/make_adsr_abs
(make_as, make_asr, make_ads, make_adr — each class's rebuild() body
verbatim); envelope_json.h is six builders over one table (kEnvelopePresets:
name, builder) reading each class's keys with the class's defaults and
shaping curved stages from <stage>Curve/<stage>Power; envelope_preset_keys()
is the one key list both loaders consult (the editor's hand copy gone); the
loader's Envelope branch reads seed in both forms; kBranchKeys 130 -> 134.
DELETED: envelope_presets.h (six classes), their six registrations, the
KSPianoString registration (86 -> 79 registrations, 57 Main / 22
Experimental), SustainDefault + the protected ctor + is_common_knob_ (sustain_level()
a private function), the derived-label "Envelope" suffix, the editor's
preset-only Duration/Shape layout. ADDED: SourceRegistry::retired_type_hint
(seven names -> the tool's name; the loader's unknown-type throw and the
editor's status line both append it); the editor's Preset combo + Apply above
every Envelope's stage table (replace_stages under the audio mutex, then
apply_config); tools/migrate_deleted_types.py (RULES table, --check,
indent-keeping; 14 unit tests) which rewrote 497 nodes in 489 files across
audition/baselines/library/pending/sweep/scratch (11 tracked: the seven
preset files and four pianos in scope); patches/old/ left (59 nodes in 41
files fail by name, the tool named); pending/ originals at
renders/scratch/ab35/pending_before/. DESIGN CALL (plan ruling 1, REVIEW R12
item 1, non-blocking): presets are factories and a load form, not node state;
the alternative (Setting<int> preset + eleven knobs on every Envelope) stays
open as an ADDITIVE step — the on-disk rewrite is identical under both. NO
RENDER MOVED: null gate 78/78 after every task and across the rewrite (all
eleven rewritten in-scope files are in the manifest); A/B 132 identical, 0
DIFFER (old arm from cli_pre35.exe over the UNREWRITTEN files at Task 0, new
arm from 6a825a4 over the rewritten); synthetic set of eight written in the
CLASS form with every knob off its default (as reverse+accuracies+seed, asr,
ads, adr, ar, adsr with every curve, adsr bare for the written release
default, as under a 3-clone multiplex for the injected seed): same-binary
class-vs-form 8/8 under the Task 1 engine, cross-binary old-class vs
new-form 8/8. The six layouts' equivalence was proved while both existed
(Task 1's test at 750f70e: class via set_setting over a knob grid vs the
form, eight stage fields per stage) and the class half deleted at Task 3
with literal stage tables left. Tests 7552 -> 7656 -> 7457 (ALL PASS).
Mutations all caught (asr clamp 14 FAIL; asr release ramp 1 FAIL — the form
shapes the stage from its key's default, so the factory's own ramp is only
reachable by a direct caller, Vibrato; a factory test was added for exactly
that; the tool's adsr default; a resurrected ASEnvelope registration 6 FAIL;
the hint nulled 4 FAIL). Roundtrip whole corpus: the backlog-79 six, 0 id
changes. Timing: run 1 with the refuter working 0.95x-1.07x (base walls 5-8%
above 3.4's clean run; nothing per-sample changed); run 2 on a quiet machine
in the report. METER (--delta 9f0140c): files -1, lines -117, dynamic_cast
+2 (both in the test file's new loader helpers; engine 15 -> 15),
duplicate_blocks -6, changelog_comments -6, include_violations +0 (14),
unmapped 0; thirteen types removed, two added. REFUTER (report §8): no Critical; two Majors on the
editor's Preset Apply (stale cached sustainLevel pushed over a fresh ADSR;
the push outside g_audioMutex), three Minors (tool carried dead keys into
the form; refusal test one knob of eleven; the whole-corpus roundtrip gate
permanently red since 3.2), two Flags; WAVE b1d0222 fixed all but the
standing Flag (apply_envelope_preset re-pulls under the lock, headless
--apply-preset twin proved it; the tool's dead keys; KNOWN_DIFFS = the six,
gate green; per-node combo state); RE-REVIEW closeable, nothing introduced,
four Flags: upper-case preset name and an ASCII note fixed (the commit after
the wave), the three in-scope `as` nodes are DEAD in the render (coverage
corrected: `as` is render-pinned by the synthetics and the tests; Morph_env
CLEANUP 30), a stale scratch label. DOCS: report R12
docs/autonomy/refactor/reports/2026-11-04-checkpoint-3.5.md; REVIEW R12 under
Awaiting (one non-blocking question; six items named); Part 3 §1 bullet and
§6 corrected ("preset form", the landed paragraph), §10 row 3.5; BACKLOG row
2: 3.5 DONE, 3.6 NEXT — plan 3.6 to write (one type per file, Part 3 §8);
CLEANUP rows 22-29; the plan's Ledger carries rulings 1-20 and the execution
notes. HARNESS NOTES (memory project_crlf_edit_scripts): git stash/pop under
autocrlf turns LF working copies CRLF; a span edit must end at the NEAREST
marker (one ran 2,400 lines past and deleted a third of the tests — restored
from the commit); a failed build leaves the old test binary's ALL PASS line
in place — read the compiler errors first; the structure meter walks git
ls-files, so a deleted header must be staged before its tests pass, and its
known-answer test pins the editor's properties-panel length (re-pin on every
edit inside it); the lint's selftest had been passing nothing (its bad key
"frequency" is in the global allowlist) — fixed. DEBT (report §7): CLEANUP
22-29; replace_stages kept for the combo; the Apply button not clicked in a
running editor (no headless path; no desktop control this session — the
roundtrip and the engine tests cover its path); the global allowlist (Part
4); the test file's nineteen PitchedInstrument casts. GOALS.md stays Matt's
uncommitted edit.

Updated: 2026-11-03 (refactor session VIII, Fable 5.1 xhigh). CHECKPOINT 3.4
LANDED on main: efd10c7..0d1e826 — the fourth of Part 3's eight. THE SHAPE:
WaveEvolution is the family's abstract base (struct WaveEvolution : Component)
owning the pin lifecycle once — prepare(ctx, maxLen) prepares every pin then
calls the on_prepare hook, start(frames) starts every pin — with adjust (now
void), shape_excitation and evolve its work verbs; the thirteen evolutions
derive from it with their settings and pins as members read where used, the
draw stream seeded once at construction; the twelve *EvolutionSource wrappers
and IEvolutionHolder are gone; WavetableSource holds Slot<WaveEvolution> and
reads it per sample (the cached pointer, the on_slot_changed override,
set_evolution, ownedEvolution_ gone); TargetEvolution is a registered part
(Wavetable, Experimental, derived label) with targetPartials (Array, default
[1.0]), targetLength, morphRate, holdCycles, decayFactor, its period
synthesised at prepare by the loader's loop moved verbatim (2.0f * dsp::kPi).
THE LOADER: the WavetableSource branch is gone (the generic path builds it);
a string in a Param/Input/Slot position fails the load naming node, pin and
string (refuse_string_pin; "a slot takes {ref}" for a slot) and, for
evolution, tools/string_evolution_to_node.py — the converter (ten unit tests
under ctest tool_tests) moves the keys onto an evolution node, writes seed
(evolutionSeed or 42), writes the deleted branch's two averaging defaults
where the node's differ (sampleCount 2.0, decayFactor 0.996), drops a stray
evolutionSeed from a node-form wavetable, refuses targetWave. kBranchKeys 142
-> 130 (the twelve keys the lint's scrape lost). PATCHES: delay_test converted
(manifest hash unchanged); patches/pending/harsh_pluck.json converted under
the target's 10-05 line naming it (original at
renders/scratch/ab34/harsh_pluck_before.json); six algev_test_* baselines
lost a dead evolutionSeed (the loader stays strict); the eight archived string
forms in patches/old/baselines_culled_2026-09-14/ left, failing the load by
name. NO RENDER MOVED: null gate 78/78 after every task and the wave; A/B 0 of
132 (at c10c386 and after the wave); synthetic set of seven 0 DIFFER at the
seal — pluck_string and target_evolution written in the STRING form, rendered
by the old binary, converted in place, rendered by the new: identical (the
string->node proof); averaging with seed 77 and every setting non-default;
brass; EKS; harsh_pluck scored and unscored. Two live-edit differences, no
render: a Pluck/Averaging setting edited mid-stream no longer restarts the
draw stream (the old Pluck wrapper rebuilt with a constant seed that ignored
the registered one — a latent bug retired); the string form is refused, not
carried. REFUTER (report §8): no Critical, no Major; six Minors (converter
averaging defaults; slot wording; a stale component.h comment; the Part 3 §2
diagram; two test pins — Averaging's autoAdjust, Target's morph/resample) and
ten flags; the wave took all six plus the cheap flags (EKS freq_ a local,
<cstring>, seven redundant = default, the base hook public like ValueSource's,
a converter int test); wave verified: ALL PASS (7552), the five refuter-named mutations all caught, null gate 78/78, synthetic 7 identical, A/B re-rendered 132 identical. Timing: rows 0.90x-1.05x with the
refuter running (base arm within 2% of 3.3's clean run); the seven wavetable
patches in scope 0.97x-1.01x (renders/scratch/ab34/time_wavetable.py) — the
per-sample slot read is noise. METER (--delta 9d4b74a): files +0, lines -202
(wave_evolution.h 1281 -> 979, patch_loader.cpp -45, tests +164), thirteen
types removed, dynamic_cast +0, duplicate_blocks +0, include_violations +0
(14), unmapped 0. DOCS: report R11
docs/autonomy/refactor/reports/2026-11-03-checkpoint-3.4.md; REVIEW R11 under
Resolved (nothing to decide; the pending conversion and the eight archived
patches named); Part 3 §5 landed paragraph, §2 diagram and §8 wording
corrected, §10 row 3.4; the target's §5 and §3.2 landed lines; BACKLOG row 2:
3.4 DONE, 3.5 NEXT — plan 3.5 to write (the six envelope preset classes
collapsed into Envelope presets, 176 nodes migrated on disk; KSPianoString
deleted as a name, 116 nodes migrated; byte-identical by construction, the
null gate the proof); CLEANUP rows 17-21; the plan's Ledger carries every
execution ruling. HARNESS NOTES: tracked files are a mix of CRLF
(source_registrations.cpp) and LF (files earlier sessions wrote) — a
multi-line edit pattern must match either ending or it silently finds nothing
(memory project_crlf_edit_scripts; cost two cycles); a test placed above the
test file's registry include cannot see the registry (the include sits
mid-file). DEBT (report §7): wave_evolution.h one file (3.6); the three part
interfaces' two conventions (CLEANUP 17); the clamp hooks (Part 4); the eight
archived patches and the editor's jsonExtras carrying a string evolution
(Part 4); the wavetable's two first-sample blocks (3.6); the test file 7,8xx
lines (Part 8); carried from 3.3/3.2/3.1 as listed. GOALS.md stays Matt's
uncommitted edit. Main is ahead of origin by every commit since 5a07eeb's
predecessor: ask at close-out whether to push.

Updated: 2026-11-02 (refactor session VII, Fable 5.1 xhigh). CHECKPOINT 3.3
LANDED on main: 0d500fc..b3c6160 — the third of Part 3's eight. THE
PRIMITIVES: engine/include/mforce/core/dsp/ holds nine headers in namespace
mforce::dsp — constants.h (kPi: the nodes' three pi literals are one float),
blep.h (dsp::blep, a function), dc_blocker.h (DcBlocker), tuning_allpass.h
(TuningAllpass), one_pole.h (OnePole::process(x, a) with the coefficient per
call, plus pole_for_samples/seconds/cutoff_hz), bow_friction.h
(dsp::bow_friction, a function; kBowFrictionMax), biquad.h (BiquadState +
BiquadCoefficients with five named recipes and the phase delay; Biquad the
pair — split because the mesh runs 128 sections on one coefficient set per
sample), butterworth_section.h (ButterworthSection with Kind; FIRFilter,
IIRFilter, BWSection and the two subclasses are gone), pierce_allpass.h
(moved in). Twenty-five sites in fifteen files hold or call them. METHOD: each
merge keeps the deleted lines' float operations in the same order; a twin
test per primitive retypes the deleted lines and compares bits over a
deterministic signal with zeros of both signs; mutation-tested once each.
NO RENDER MOVED: null gate 78/78 after every task and the wave; A/B 0 of 132
(twice: at 2f0a755 and after the wave); a synthetic set of ten patches for
the node types the scope lacks (resonator, mesh x3, brass, BW highpass,
KSString bowed, Biquad raw/resonance, FM oversample 2) 0 DIFFER at every
task. ONE DEPARTURE BY CONSTRUCTION, named: EKS's stiffness allpass clears
both memories at a note's start (the old adjust() cleared y1 only); the
refuter measured it — one LSB over the rest of every note after the first,
stiffness > 0 only; the only EKS patch in scope (hi_hat1) has stiffness 0;
kept as a one-entry departure set (1 DIFFER, listed); decided under Matt's
11-02 rule. One bounded class: BiquadSource resonance mode as the general
section (sign of a zero only). Forms with a different difference equation
(KSString's inharmonic HP, nested allpasses, allpass read; Reed/Brass loop
one-poles; SVF 1P; mesh STK edge; the additive bed) were NOT merged — Part 8
or 3.8. REFUTER (report §8): no Critical; two Majors (the departure's extent
misdescribed as "one sample"; phase_delay_at moved with a circular test),
seven Minors, eight flags; the wave (b3c6160) fixed all the cheap right ones
with tests first (the closed form retyped as a reference; reset twins; the
performer's two smoothers merged into OnePole; kBowFrictionMax beside the
curve; int k, n; every comb's dispersion sections set per note; a tautology
dropped). Timing: one clean run, rows 0.96x–1.05x (noise). METER (--delta
5a07eeb): files +8, lines +709 (tests +547), duplicate_blocks -1,
include_violations +0 (14), functions_over_limit +0, unmapped 0. DOCS:
report R10 docs/autonomy/refactor/reports/2026-11-02-checkpoint-3.3.md;
REVIEW R10 under Resolved (nothing to decide; the departure named); Part 3
§4 landed paragraph + §10 row 3.3; the target's 3.1 landed line; BACKLOG
row 2: 3.3 DONE, 3.4 NEXT — plan 3.4 to write (each evolution a Component,
the twelve wrappers and IEvolutionHolder deleted, TargetEvolution
registered, the string-typed form deleted and two patches converted); the
plan's Ledger carries every execution ruling; CLEANUP rows 15–16. SESSION
RULES THAT CHANGED TODAY: (1) superpowers DISABLED for this project
(.claude/settings.json, untracked; Matt: leave installed) — do not invoke
its skills; the plan/Ledger/refuter protocol is the repo's own; (2) Matt's
11-02 rule (CLAUDE.md "Refactor verdicts", REVIEW header): easily-tweaked
items are the session's call, logged in docs/autonomy/refactor/CLEANUP.md
(the master list); REVIEW only for far-reaching calls; the oboe1 A/B at 0
moved is accepted standing; R6, R8, R9 were resolved under it. DEBT (report
§7): the unmerged forms; kPiDesign (the Butterworth's own 15-digit pi); P1
(Biquad writes derived a1/a2 into its settings) and P2 (the 0.0f accumulator
start) on CLEANUP's preservation list; Biquad's per-sample set_raw and the
resonator's per-sample set (no setting hooks); 23 pi literals outside
core/dsp; FM's live-oversample allocation and the limiter's per-sample exp
(pre-existing). HARNESS NOTES: two test-side C++ traps cost a cycle each —
`std::vector<float> v(size_t(n))` is a function declaration; `vector<float>
fir{3, 0.0f}` is two elements (exit 127, no FAIL line); an expectation written
as arithmetic in a test rounds its own way (compare against the retyped
line, never a re-derivation). GOALS.md stays Matt's uncommitted edit. Main
is ahead of origin by every commit since 5a07eeb's predecessor: ask at
close-out whether to push.

Updated: 2026-10-31 (refactor session VI, Fable 5.1 xhigh). CHECKPOINT 3.2
LANDED on main: 67071f8..d5ebbf3 — the second of Part 3's eight. ONE
CLASSIFICATION: each registration carries a TypeTag {family, label, shelf}
(designated initializers); Family is ten values in Part 3 section 3's order
with family_name(); Shelf is Main or Experimental; registered_types() answers
in registration order, which is menu order; types_in(family, shelf) is what a
menu draws; find(name) returns the tag or null and family/label/shelf throw on
an unknown name. SourceCategory, Component::category() and the fifty-nine
per-node overrides are gone. 85 registrations: 63 main, 22 Experimental (today's
Experimental sub-menu plus the eight no menu listed — NameGate, StaticVar,
StaticRange, the KSPianoString alias, Biquad, BowTable, Mesh2D, PierceFilter;
Matt: "just throw them all under experimental"); 27 labels where the menu's
word differs from derived_label(); a test refuses a redundant one. THE MENUS
(create and Replace-with) are one generated loop, shelf_menus(Shelf), over
kFamilyOrder/types_in, one separator before kFirstShapingFamily (Physical),
the four-item quick block kept by hand; THEME (tools/mforce_ui/theme.h): one
colour per family plus output/parameter/note/curve/unknown/dynPinGold/
envelopeStrip, defaults equal to the old literals, so seven types changed
colour (HybridKS teal→tan, PierceFilter lavender→teal, Distortion and Shaper
teal→lavender, Vibrato teal→pale gold, Segment reddish-orange→pale gold,
Wormhole teal→grey); the ten intra-family menu dividers are gone; the Vibrato
and Segment strips are the Envelope family's (yellow, hidden by the Envelopes
toggle). FOLDERS: 42 headers git-mv'd into engine/include/mforce/source/
{oscillator,noise,wavetable,physical,filter,envelope,modulator,combiner,
utility}/; engine/include/mforce/filter/ gone; the envelope, Var, Range, Curve,
NameGate and Multi headers left core/; modules.json folds filter into source.
NO RENDER MOVED: null gate 78/78 after every commit, A/B 0 of 132 over library
+ baselines, allocation gate clean, wiring null gate 78/78. METER: files +1
(theme.h), include_violations 13→14 — music/pitch_bend.h → the envelope
header, a pre-existing edge made visible by the move; map NOT widened (the
target says music depends on core only), function NOT moved (the target's 3.7
moves compile_pitch_curve to render when pitch_bend.h splits) — REVIEW R9
item 3 asks Matt. Refuter (report §8): no Critical, no Major; fourteen Minors
and seven flags; the wave fixed the cheap right ones with tests first (find(),
the registry owning its label copy, Pierce Filter's label, the named
separator, designated initializers on the theme, two naming guards, stale
comments, Part-number comments) and ruled on the alias (stays, 3.5 migrates)
and MODULES.md (regenerated at the close). Timing: see the report's §5. DOCS:
report R9 docs/autonomy/refactor/reports/2026-10-31-checkpoint-3.2.md; REVIEW
R9 AWAITING MATT with seven items (A/B; the seven colours; the menu as
generated and the lost dividers; the meter's +1; the alias on the shelf; the
two strips; the timing); R8 (3.1) and R6 (2.5) still await. BACKLOG row 2:
3.2 DONE, 3.3 NEXT — plan 3.3 to write (core/dsp primitives, merged one at a
time with the copies compared first). Part 3 §3 landed line + Experimental
note + §10 row 3.2; the target's §3.2 landed line; the plan's Ledger carries
every execution ruling. DEBT (report §7): Theme's other literals and JSON
loading, the untestable defaults, theme.h pulling the registry header (Part
7); the six preset envelopes in the menu and the alias on the shelf (3.5);
the .cpps outside family folders (3.6); the pitch_bend edge (Part 5); order
as menu order; kFamilyOrder restating the enum; Shelf in the engine registry;
types_in copying per frame; the UI's remaining name-matching classifiers;
denormals.h in Windows-1252. HARNESS NOTES: the UI stamp gate failed twice on
a stale UI exe (a comment-only edit after a build; the engine lib rebuilt
under it by a mutation cycle) — relink before the gates; the heredoc rule
(no backslash in a heredoc, scripts via the Write tool) held all session.
Measurement scope (library + baselines) held. GOALS.md stays Matt's
uncommitted edit. Main is ahead of origin by every commit since the 2.5 docs
commit 88283c4: ask at close-out whether to push.

Updated: 2026-10-30 (refactor session VI, Fable 5.1 xhigh). CHECKPOINT 3.1
LANDED on main: ba097f6..f1f8244 — the first of Part 3's eight. The twenty-two
parts (four Partials types and ExpandRule, five formant types, twelve
evolution holders) derive from Component and their interface, not
ValueSource; the registry creates Components, the loader's graph, the voice
and the editor's node handle hold them, every consumer holds its part in a
Slot<I>. One wiring door for every pin kind (Component::set_param over a
shared_ptr<Component>: a Param or Input takes a signal and refuses a part, a
Slot takes its interface and refuses the rest; add_param appends on a multi
slot; get_part serves slots), the type-check at wiring in that door, at load
or at an editor change, never per sample. Component::on_slot_changed (third
hook beside settings and arrays) exists because the wavetable's evolution
pointer went stale after a rewire — a test caught what the plan's design
would have shipped. VoiceGraph::signals lists the voice's signal nodes once
at load for the fault, capture and reseed walks (the reseed on the audio
thread, no cast). The interface verbs lost their prefixes (IFormant and
IPartials prepare/start/next; IEvolutionHolder gains prepare/start). NO
RENDER MOVED: null gate 78/78 after every commit, A/B 0 of 132 over library
+ baselines (the new scope), allocation gate clean. ONE BEHAVIOUR CHANGE by
design: a malformed wire (a signal into a slot, a part into a signal pin, a
part as output or channel source, a FormantSequence keyed 'formants') fails
the load naming the node, the pin, what it offered and what the pin takes,
where it rendered silently wrong before; no patch in scope carries one.
TOOLING: tools/alloc_gate.py splits the allocation gate over three processes
(46-50 s) — in gates.py's FAST list, run before every commit; its merge is
unit-tested (ctest tool_tests). The refuter (section 8) graded three Majors,
all fixed in one wave with tests first: the editor's unlink of a holder left
the evolution in place (the rewire pass clears every pin first; REVIEW R8
item 5 asks for the manual try), the FormantSequence 'formants' alias
loaded 34 archived vowel patches without their spectra in silence (a
configurator refuses it by name), the gate wrapper passed a crashed child
(exit 3 now). Timing: three runs, see the report's section 5. Meter: lines
+106 at the first close (tests +152, engine smaller), dynamic_cast +2 (nine
runtime casts gone, the door's and the loader's load-time casts in),
duplicate blocks -5. DOCS: report R8
docs/autonomy/refactor/reports/2026-10-30-checkpoint-3.1.md (§8 the
refutation, the wave, the re-review); REVIEW R8 AWAITING MATT with six items
(the malformed-wire behaviour change; the A/B; the dropped 'formants' alias;
the editor's part strips; on_slot_changed as the third hook; the editor's
unlink try + its hand lists); R6 (2.5) still awaits. BACKLOG row 2: 3.1
DONE, 3.2 NEXT — plan 3.2 to write (families declared at registration;
folders; the create menu generated; colours through Theme). Part 3 §2
landed line + §10 row 3.1 filled; Part 2 §3's Slot row and door paragraph;
the plan's Ledger carries every execution ruling. DEBT (report §7): the
holders as wrappers and the two prepares (3.4), AdditiveSource2 (3.7), the
three lifecycle trios (3.4/3.8), the editor's part strips, signal() casts,
hand lists and silent tap-from-part (Part 7), the expandRule cast and the
allowlist's descriptor keys (Part 4), MultiSource's hand-written list,
Slot's public vectors, nodesById + signals as two structures. HARNESS NOTE:
Bash heredocs halve backslashes even when quoted — three edits broke this
session before the rule 'any backslash goes in a script file' was applied
without exception (memory updated). Measurement scope rule (CLAUDE.md,
Matt 2026-10-30) held: no queue, sweep or old patch measured. GOALS.md
stays Matt's uncommitted edit. Main is ahead of origin by every commit since
the 2.5 docs commit 88283c4: ask at close-out whether to push.

Updated: 2026-10-30 (refactor session VI, Fable 5.1 xhigh). CHECKPOINT 2.6
LANDED on main: 6a37849..d3df3ea — PART 2 COMPLETE. The allocation probe
(tools/alloc_probe: a replaced global operator new, linked by alloc_gate and
engine_tests only) and the gate (tools/alloc_gate: a patch or a folder, armed
around the LIVE path — acquire_voice, prepare_voice_at, one pull per sample —
per embedded-score note or one C4, and each channel source of a mixer-mode
graph; count + symbolized first stack; exit 1 on any allocation) run in
gates.py's SLOW list over library+baselines: 132 patches, 0 allocate (97 on the
first run: Partials 75, Wavetable+evolutions 11, Segment 8, HybridKS, Phased,
a Multiplex push). Every allocation moved to prepare by one rule: a
setting-fixed count sized at prepare; a frequency-dependent length reserved for
WaveSource::kMinTableHz = 20 Hz and fitted within capacity at start (a sub-floor
note gets the floor's length — REVIEW item 3). Also: StreamingVoice::advanceList
is a span (a live note-on copied a vector); the loader's score walk is a
file-local step. The Fable refuter (report §8): two Majors fixed in the wave —
the Partials originals snapshotted from arrays a note leaves expanded (a second
prepare would double-expand) and EKSEvolution allocating a vector per note into
the owner's table — then a second pass for ExplicitPartials' direct rebuilds
(snapshot_originals); re-review and re-check: met as signed. Verified at the
seal: engine_tests ALL PASS (6541); ctest/stamp PASS; null gate 78/78 after
EVERY task (no manifest change); wiring null gate 78/78; allocation gate PASS
(78–139 s); roundtrip gates the known six. A/B over the 1,615-patch sample
(renders/scratch/ab26_tree, old = 2.5's seal cli_pre26.exe): 0 DIFFER in every
set. Timing within noise (every row within 7 percent, F–I within 4). Meter
+809 lines (286 tests, 235 gate, 136 probe), mutable +5 (the probe's
thread_locals), duplicates 159→158. Measurement:
docs/architecture/measurements/2026-10-29-allocations.md; survey:
tools/survey_table_frequencies.py (lowest table frequency in use 220 Hz). DOCS:
report R7 docs/autonomy/refactor/reports/2026-10-30-checkpoint-2.6.md (§8 the
refutation, the fix-wave outcomes, the re-review, the re-check; §10 four
lessons); REVIEW R7 DECIDED 2026-10-30 (all five as read: the gate stays
slow-listed with a --jobs split as a plan 3.1 task; the A/B accepted; the
queues measured clean once — 752 patches, 0 allocate — and never again; the
20 Hz floor kept; a structural setting takes no per-note push, landed
6007b29: SettingRole::Structural on the pin and its descriptor, the loader
refuses the wire, the UI offers no pin). STANDING RULE from the verdict:
library + baselines is the measurement scope for every gate, A/B and
measurement (CLAUDE.md "Measurement scope"). R6 (2.5) still awaits Matt.
BACKLOG row 1: Part 2 complete; row 2 (Part 3 nodes) NEXT — plan 3.1 to write.
Part 2 §4 decided line + §7 row 2.6 filled; the plan's Ledger carries every
execution ruling. Debt (report §7): the offline hosts' per-note buffers
(play_note, play_hit, StereoMixer::render scratch — Part 5), the live legato
Setup's glide allocation and the Setup-path group (Part 5), onsets/hold absent
from the live path, the queues unmeasured after the fixes, the UI's own note-on
copies (Part 7), WaveEvolution still a part (Part 3), the two fill_table twins,
the pull step in five places, the probe's thread_locals in the meter, a `gap`
key on a RepeatingSource baseline accepted and ignored (Part 4). GOALS.md stays
Matt's uncommitted edit. Main is ahead of origin by every commit since the
2.5 docs commit 88283c4 (the 2.6 plan, Tasks 0–9, two review passes, this
docs commit): ask at close-out whether to push.

Updated: 2026-10-26, later (refactor session VI, Fable 5.1 xhigh). CHECKPOINT
2.5 LANDED on main: cb1b4ab..1481d15 — no throw on a render path (the two
throws are clamps with a per-node atomic fault bit), the fault bits reach the
host (CLI `[faults]` block after every render, UI transport status line,
live pool polled), a rejected wire or unknown key fails the load naming
node, pin and reason on every build path (allowlist kBranchKeys = the old
list ∪ the lint's scrape, a meter test guards the drift), the DelayLine wrap
reads slot 0 (the position wraps, not the index). Tasks 0–4 cb1b4ab..fbfffe6
(session V); the close this session: the independent Fable refuter found
the Task 4 wrap WRONG (index wrapped, fraction did not — an extrapolation
once per lap a constant-source test could not see); fix wave 5a618cf
(position wrap, Counter-source test RED 1107/2213 → GREEN; take_faults()
exchange; ComputingScope guard in prepare/start; every CLI render + the UI
chords render report, in id order; configurators' failed wires name their
node; null gate reads stderr for [faults]; kStructural → kBranchKeys;
fixture + CLI blocks collapsed); re-review closed all but I9's declared
test debt and I11 (Matt's); second pass 1481d15 (freeze guard, catch
std::exception, two headers). Verified at the seal: engine_tests ALL PASS
(6486); ctest/stamp PASS; null gate 78/78 refrozen twice for exactly
double_advance_bug/ok; wiring null gate 78/78; roundtrip gates fail on the
known six (backlog 79). A/B over the 1,615-patch sample: old→t3 0 differ;
t3→t4, t4→t5 and old→t5 the same 25 (every one on the wrap list); 5 patches
stop loading (4 sweep bandedwg _struck + old/Squeaker), nothing tracked;
the measurement's ks_morph_saw_test LOADS (a false positive of the old
warning; the Ledger's "21 archive files" was a misread of the lint's total).
Timing: the clean run (pre-wave engine) within noise; the three post-wave
runs on a loaded machine, recorded not chased. Arms under
renders/scratch/ab25_tree/<set>/{old,t3,t4,t5}; binaries
renders/scratch/ab_tree/cli_pre25.exe, cli_25_t0/t3/t4/t5.exe. DOCS: report
R6 docs/autonomy/refactor/reports/2026-10-20-checkpoint-2.5.md (§8 holds the
refutation, the fix-wave outcomes and the re-review verbatim; §10 four
lessons); REVIEW R6 AWAITING MATT with four verdict items (the five patches
that stop loading; the wrap list's renders as frozen truth; the fault
reports' shape; prepare() throwing on a zero rate) plus (4) ruling 2's
narrowing of the signed clamp sentence; R5 (2.4) DECIDED 10-26 (the forty
renders are the truth; previous()/positional paragraph left as they are);
BACKLOG row 1: 2.5 DONE, 2.6 (the allocation test) next, plan to write;
Part 2 §6 decided line + §7 row 2.5 filled; the plan's Ledger carries every
ruling through the second pass. GOALS.md stays Matt's uncommitted edit. Main
is ahead of origin (the two code commits and the docs commit that carries
this line): ask at close-out whether to push. NEXT: checkpoint 2.6 — the
plan is written (docs/superpowers/plans/2026-10-29-part2-checkpoint-2.6.md,
10 tasks: the allocation probe (a replaced operator new in the test
executables only), the loader's score delivery extracted for a second
caller, the alloc_gate tool and its measurement, the families from the
2.2 list (Partials; Wavetable + its evolutions; HybridKS; Phased, Segment,
BasicAdditive), whatever the measurement adds, the Review Focus tests,
the close with report R7). Decided with Matt 10-29: tool first, the gate
is the tool over library and baselines, no probe in the UI or CLI. Open
for his read: the 20 Hz table floor (the pin's 0.01 Hz minimum is no
floor), REVIEW R7 item (3). Awaits Matt's read and his word on the
execution method (2.5's was inline with the Fable refuter at the close).

Updated: 2026-10-26 (refactor session V, resumed after the Fable quota cut it
mid-checkpoint on 10-20; Matt: "assess where things aborted, save state,
restart plan"). CHECKPOINT 2.5 IS FOUR-FIFTHS LANDED on main, Tasks 0-4 of
docs/superpowers/plans/2026-10-20-part2-checkpoint-2.5.md: T0 measure
cb1b4ab; T1 no throw cc38e9f; T2 the bits reach the host 8dfa883; T3 a
rejected wire says so b9b43d5; T4 the DelayLine wrap — COMMITTED fbfffe6 (resumed 2026-10-26 after the quota cut:
the 2.2 zero-slot test restated as run_delay_line_wrap_tests, gates re-run).
Every ruling is in the plan's Ledger (execution rulings appended through
T4) and in the executing-plans ledger
.superpowers/sdd/2026-10-20-part2-checkpoint-2.5/progress.md (gitignored;
read it first on resume). Verified state: engine_tests ALL PASS (6480 at
T3; T4 adds none net), null gate 78/78 on a manifest refrozen for exactly
double_advance_bug and double_advance_ok, fast gates clean but the known
roundtrip smoke; the A/B over the sample: T3 moved nothing (one sweep patch
stopped loading, the bwg stale-key class), T4 moved 25, every one on the
wrap list.
WHAT THE FAULTS FOUND (all in the Ledger): the voice never prepared its
push-binding chains (fixed: PitchedInstrument::prepare prepares them; the
gate went 72/78 → 78/78); the mixer path's voices had no nodesById (filled);
prepare() must not clear faults (a scored patch is pre-rendered at load);
the never-prepared guard on next()/step() retired the bare-node idiom (29
assertions) and was narrowed to start(); the loader was blind to stale keys
on hand-built nodes (14 bwg queue patches repaired byte-identically before
the cut; the allowlist is now the union with the lint's scrape, a meter
test guards the drift); engine_tests needs /bigobj; the UI main() pin is
1713.
REMAINING = TASK 5 ONLY (the close), in the plan's order: step 1 the
leftover-name greps; step 2 `python tools/gates.py` (all) + null_gate_wiring
(expect the roundtrip gates to fail on the backlog-79 set, now with
double_advance refrozen — record the set); step 3 the A/B is done (`old`
vs `t3` vs `t4` under renders/scratch/ab25_tree/, cli_25_t3/t4 under
renders/scratch/ab_tree/) unless the fix wave touches engine code; step 4
timing `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre25.exe
build/tools/mforce_cli/Release/mforce_cli.exe 7` on an idle machine; step 5
meter snapshot + `--delta cb1b4ab`; step 6 the Fable refuter (read-only,
REVIEW_BRIEF, Part 2 §6 as the signed design, the plan's Review Focus and
Ledger, the report draft §1-7), fix wave, re-review; step 7 report R6 —
the DRAFT is at docs/autonomy/refactor/reports/2026-10-20-checkpoint-2.5.md
(untracked; §1-4, §6 rulings 1-22, §7, §9, §10, §5's three classes and
Appendix A are written; placeholders remain for <<METER_TABLE>>,
<<TIMING_*>>, <<ROUNDTRIP_SET>>, <<REFUTATION>>, <<SEALSHA>>) — add the
T3/T4 rulings to §6 from the plan's Ledger; REVIEW R6 awaiting with its four
verdict items (the patches that stop loading; the wrap list's renders as
frozen truth; the fault reports' shape; prepare() throwing on a zero rate);
BACKLOG row 1 2.5 DONE / 2.6 next; Part 2 §6 decision line and §7 row 2.5;
this STATUS; step 8 the docs commit. R5 (checkpoint 2.4) STILL AWAITS
MATT'S VERDICT. GOALS.md stays Matt's uncommitted edit. Main is ahead of
origin by every commit since 30e6b44 (2.4 and 2.5). PUSHED 2026-10-26 on
Matt's word at close-out (a3c7c2e..e383eee, fourteen commits, plus this
line); main level with origin.

Updated: 2026-10-19, later (refactor session V, Fable 5.1 xhigh).
CHECKPOINT 2.4 LANDED on main: a8d5aa9..403269f — taps reading B. Every
node keeps the value it held at the end of the previous tick (prev_, moved
when next()/step() claim the tick) and previous() reads it; TapSource::
compute() reads previous(); TapSource::current() — the read that DELIVERS,
since a node reads its pins after its sweep through current() (2.1's rule)
— is the memo once the tap has claimed the tick and the live source value
before (a Setup-time read between ticks), through a protected
claimed_tick(). Executed inline per the plan
docs/superpowers/plans/2026-10-19-part2-checkpoint-2.4.md (Ledger = every
ruling): T0 re-measured the list with a committed-then-deleted counter
(45; the 10-18 list's 47 at exactly half — the old trace counted the
wrapper's read twice per sample — minus stk_bowed_ext1/2, whose same-tick
position was an artifact of the old advancer placement); T1 the two-value
memo unused, 78/78; T2 the tap's read — the plan's contract for current()
(keep it live) DELIVERED NOTHING: the first A/B arm was byte-identical on
all 47, because consumers read pins through current(); ruling 18 redesigned
current() test-first (a CombinedSource summing c + tap(c) reads 1, 3, 5)
and 2.3's live-read test moved its one post-pull assertion to B. RENDERS
CHANGED: 40 patches tree-wide, every one on the measured list, none off it
(audition/pierce1d_1 6, audition/trombone1 4, pending/brass 1, sweep
trombone1-4 11, onemass_lip1 4, nlbore_probe1 6, stk_blowhole_port 3,
old/nlbore_brass1 5) — in 37 a CONTROL read (NL_curve ← tap(NL_in) → NL_delay.ratio;
pierce's SignCurve → Damp_lpf.cutoffFreq) now lags one sample, loop periods
unchanged, compensate irrelevant; in stk_blowhole's 3 a signal-path tap
(D1In ← tap(D2)) gains one sample — the refuter's C1 corrected my loop-
period story; on disk the class is 116 patches (the A/B sample is 1,615,
sweep/old every third; trombone_tongue in the audition queue has no score,
so the CLI A/B never exercised it); library 0, baselines 0, the retired pair 0; five
listed patches did not move (nonlinear-bore controls whose only same-tick
tap feeds a flat NL_curve, knots all 1.0). Manifest NOT refrozen (78/78
after every task). Timing neutral (oboe1 1.01x, piano 0.99x, viola 0.99x).
Gates at the seal: ctest, stamp, null gate 78/78, wiring null gate 78/78;
the two roundtrip gates fail on exactly the six backlog-79 patches.
REFUTATION: one Critical (the report's loop-period mechanism was wrong for 37 of 40: the same-tick read is a control read, NL_curve/SignCurve; blowhole's is the one signal path), three Important (the sample called whole-tree; the counter's position vs the delivery position; the Setup-time/step() semantics misstated), six Minor (incl. every 10-20 date); assessment: ready to seal with documentation and test fixes only. FIX WAVE: all addressed in one wave (docs, comments, nine pinned limit assertions; no engine behaviour change), re-reviewed clean after three further text edits (the DelayLine sentence, the class count 116 with pierce1d_1 at 6, Lesson 2).
REPORT R5: docs/autonomy/refactor/reports/2026-10-19-checkpoint-2.4.md
(ten sections, the refutation verbatim in §8, three C++ lessons in §10).
REVIEW.md R5 AWAITING MATT, three verdict items (0 the forty changed
renders as frozen truth — his queues and sweeps, re-listen before
accepting; 1 previous()/claimed_tick() public on every node for one reader;
2 feedback_loop_design.md §2's positional paragraph: delta or rewrite).
Debt in §7. CHECKPOINT 2.5 PLAN WRITTEN (2026-10-20, on Matt's "plan it
now; I'll read R5 alongside"): docs/superpowers/plans/2026-10-20-part2-checkpoint-2.5.md
— six tasks (T0 measure: the unknown-key list from the current loader's
warnings, the 157 non-loaders classified by error, a DelayLine-wrap counter
committed then deleted; T1 no throw: a per-node atomic fault byte, the two
throws become clamp-and-fault with the narrowest clamp, prepare() throws on a
zero rate, Mesh2D's 48000 fallback goes, a never-prepared node faults; T2 the
bits reach the host: the cycle fprintf is a bit, prepare/start guard re-entry,
a clockless pull faults, collect_faults on PitchedInstrument/DrumKit, the
CLI's [faults] block and the UI's status line; T3 set_param returns bool and
the loader fails a load with node/pin/reason, the load A/B lists what stops
loading; T4 the DelayLine read wraps to slot 0, the extra slot goes, the
manifest refrozen for the wrap list (double_advance_ok); T5 close with a
Fable refuter and report R6), 13 planning rulings. AWAITING MATT: his R5
verdict and his read of the 2.5 plan; execution approach my rec: inline,
Fable refuter at close. Kept binaries: cli_pre24.exe (=
2.3's cli_final), cli_t0 (the counter), cli_t2 (the seal's engine) under
renders/scratch/ab_tree/; the arms under renders/scratch/ab24_tree/. Not
pushed this session — ask at close-out. GOALS.md stays Matt's uncommitted
edit.

Updated: 2026-10-19 (refactor session V, Fable 5.1 xhigh — the session opened
at high; Matt bumped it before the verdict). R4 DECIDED by Matt: (0) render
time ACCEPTED — the 2.1 cost (oboe1 0.73x, piano 0.88x of pre-2.1) stands as
the price of one next() for every node; Part 3 is not asked to recover it;
(1) the 109 changed renders are the frozen truth; (2) the
loop_root_sensitivity pair RETIRED → patches/old/baselines_retired_2026-10-19/
(git mv, never deleted), its two manifest entries dropped by hand (the null
gate is 78 entries; every other hash untouched), dsp backlog 65 (+66's
residual) closed to that file's ledger — the root sensitivity was the double
advance — and 79's roundtrip set is six; (3) step() KEPT for now and (4)
Shaper's Auto pins STAND for now, both carried as post-refactor revisits in
refactor BACKLOG row 9 (a, b). REVIEW R4 → Resolved with the verdict; the 2.3
report's §7 pair entry carries the verdict line. CHECKPOINT 2.4 PLAN WRITTEN
(writing-plans, xhigh): docs/superpowers/plans/2026-10-19-part2-checkpoint-2.4.md
— four tasks (T0 re-measure the same-tick tap list with the sealed binary
through a counter committed then deleted, the old arms re-used from
ab23_tree/*/final since the engine is unchanged since fdf7536, taps.txt = the
47 in full; T1 the two-value memo prev_/previous(), written and never read,
byte-identical 78/78; T2 TapSource::compute() reads previous() — the one
commit whose renders change, whole-tree A/B ⊆ the list, the docs record the
delta; T3 close: Fable refuter, report R5), 12 planning rulings in its Ledger
(prev_ moves when the tick is claimed so a read before, during or after the
source's compute agrees; previous() non-virtual; no clock = reading A; the
tap's current() stays live; no manifest refreeze — none of the 47 is in
library/baselines; DelayLine's comp=1 becomes true in every rooting).
AWAITING MATT: his read of the plan and the execution approach (my rec:
everything inline — about fifteen engine lines and two test sections — with
one Fable refuter at close; no Sonnet pairs). Verdict commit 8523cb3 and the
plan commit are ahead of origin — ask at close-out. GOALS.md stays Matt's
uncommitted edit.

Updated: 2026-10-18, later (refactor session IV, Fable 5.1 xhigh).
CHECKPOINT 2.3 LANDED on main: 22bf3be..fdf7536 (seven commits + the
docs seal) — the per-tick memo. ValueSource::next() computes once per
tick of the host's clock and serves cur_ to every later pull in that
tick; RenderContext carries a SampleClock* (one clock per pull loop:
voice, drum, mixer, adapter, the UI's editor stream, a test host; a null
clock = every pull computes); step() = one sample now through Auto pins,
for the two table fills; RefSource wrapping, the usage counter, both
starved-ref promotion walks, the UI's wrap mirror and the sharing trace
are GONE (the loader −170 lines); the tap edge is TapSource (always
guarded, reading A); ShaperSource's pins are Auto again; the sweep reads a
compacted array of raw pointers to the wired Auto sources, refreshed by
the pins' setters (a pin knows its owner), prepare, start and a node's
self-assignment. Executed per the plan
docs/superpowers/plans/2026-10-18-part2-checkpoint-2.3.md (Ledger = every
ruling, 24): T0-T2 and T5-T6 inline, T3-T4 Sonnet implementer+reviewer
pairs, one Fable refuter (2 Important: the sweep refresh in the base's
operator= ran before the derived pins were assigned — a preset rebuild
dropped a wired range source from the sweep, live in the editor; step()'s
two limits — fixed/ruled in ONE wave fdf7536, re-reviewed clean).
RENDERS CHANGED, all measured: on the manifest double_advance_bug,
double_advance_ok (the lag class) and loop_root_sensitivity_fires (a
tail pull re-reached the root cone: the output sat mid-loop, so the
whole cone advanced twice per sample; it no longer fires and matches its
_dead sibling — the "root sensitivity" WAS the double advance); across
the tree 109 of 1,458 (library 0, pending 0, old 0; audition 74, sweep
32: feedback_valve2, string_harness1/2, bwg_perc1/2, a few stk ports),
every one sharing a stateful node whose wrapped consumer read it a sample
late. The trace's lag list (342) proved an UPPER BOUND: since 2.1 a node
reads its pins after its sweep through a live current(), so most
"lagging" consumers already used this sample's value. Manifest refrozen
(3 entries). RENDER TIME: the checkpoint is neutral vs pre-2.3 (piano
1.00x, viola 0.99x, oboe1 0.88x, the shared chain 1.12x); the sweep
experiment bought 8-16 points where pins are wired; the 2.1 cost is NOT
recovered (oboe1 0.73x, piano 0.88x of pre-2.1) → REVIEW R4 item 0 (the
generic per-node next() vs compute()-pulls-its-pins per family in Part 3).
Gates at the seal: ctest, stamp, null gate 80/80, wiring null gate 80/80;
the two roundtrip gates fail on exactly the eight backlog-79 patches.
REPORT R4: docs/autonomy/refactor/reports/2026-10-18-checkpoint-2.3.md
(ten sections, the refutation and its re-review verbatim in §8, three
C++ lessons in §10). REVIEW.md R4 AWAITING MATT, five verdict items (0
render time; 1 the changed renders as frozen truth; 2 the
loop_root_sensitivity pair's future; 3 step() as a verb or Part 3's
private excitation; 4 Shaper's breakaway/capture pulled in both modes).
Debt in §7 (a clockless host pull is silent until 2.5's flag; prepare/
start on a hand-wired tap-free cycle overflow; step()'s limits; the
fprintf on the cycle path; the public pin source member; shared Slot
parts advance twice; mixer-mode cross-loop sharing; reading B = 2.4 with
47 patches measured; advanceList keeps the old verb; the meter's live
fixture). NEXT: Matt's R4 verdict, then checkpoint 2.4 (taps reading B;
the 47 same-tick-tap patches in docs/architecture/measurements/
2026-10-18-sharing-lag-tree.md are its expected diffs) — plan written
against the then-current code. Kept binaries for re-timing under
renders/scratch/ab_tree/ (cli_pre23, cli_t1..t4, cli_exp1, cli_final);
the A/B arms under renders/scratch/ab23_tree/ (gitignored). PUSHED
2026-10-19 on Matt's word at close-out (6a32e55..30e6b44, twelve commits,
plus this line); main level with origin.
GOALS.md stays Matt's uncommitted edit.
10-19: Matt asked whether the refactor grows as much compensating code as
it removes (answer: some — the engine is 2,800 lines smaller than before
2.1 but ~90 guard sites exist that did not: byte-identity preservations,
idempotence guards, step(); section-7 lists 14/10/13). DECIDED: re-ask at
the end of the refactor; a workaround-cleanup follow-on is SLATED after
Part 8 (refactor BACKLOG row 8) where renders MAY change for engine
improvement — byte-identity is the refactor's instrument, the patches are
not gold — except oboe1 (target-architecture.md principle 6 note).

Updated: 2026-10-18 (refactor session IV, Fable 5.1 xhigh). R3 DECIDED by
Matt: (0) DelayLine zero slot stays listed for 2.5; (1) UI re-prepare per
play fine; (2) the section-7 allocation list is 2.6's work list, every item
moves into prepare, NO tolerance list; (3) render_context()/prepared() made
PROTECTED, ensure_constant gained a three-argument form taking the caller's
context (Multiplex passes its own; the loader's call runs before any prepare
and keeps the two-argument form) — items 2 and 3 were my recommendations,
taken on Matt's "take your recommendations and proceed". Item 3 LANDED as
6a32e55 on top of 2.2: engine_tests 6370 checks pass, null gate 80/80,
gates --fast green except the known wiring_setting roundtrip diff (backlog
79), UI rebuilt. REVIEW.md R3 moved to Resolved with the full verdict; the
2.2 report's §6 item 13 carries a reversal note; Part 2 §4 records the 2.6
ruling. ALSO THIS SESSION: Matt's "Ode to Joy sounds the same phrased or
not, auto or all slur" diagnosed and reproduced headlessly (scratch script,
15 variants, HEAD vs cli_pre22 byte-identical on all): phrasing is gated by
the patch's instrument-block "sustaining": true (onsets v2 §2); NO library
patch carries it (oboe1/oboe2/piano_default have only polyphony+volume);
only the articulation1/oboe_nasal1 audition copies do. On oboe1 the UI's
stamp_passage drops hold+onset for every note and says so only in the
transport status line (normal colour); the Onsets combo stays enabled. With
sustaining ticked but no onsets vocabulary: phrased ≠ flat, but auto == all
slur == all tongue (degradation contract). Not a refactor issue; the quiet
status + always-live combo are Part 7 usability points; whether the library
oboes get the declaration is Matt's curation call (nothing changed).
CHECKPOINT 2.3 PLAN WRITTEN (writing-plans, xhigh):
docs/superpowers/plans/2026-10-18-part2-checkpoint-2.3.md — seven tasks
(T0 whole-tree lag measurement + cli_pre23 + timing baseline; T1 the clock
in RenderContext + memo next() + step() + every driver ticks + trace gone,
byte-identical; T2 wrapping/promotion/UI-mirror deleted = the measured
renders change, manifest refrozen; T3 TapSource; T4 Shaper pins Auto; T5
render time vs pre-2.1 with one bounded experiment; T6 close + Fable
refuter + report R4), 14 planning rulings in its Ledger (the tick is the
host's clock reached through the context; no clock = every pull computes;
two verbs next()/step() because the two table fills draw N samples in one
tick; the spec's generalised sweep REJECTED — Manual-gated nodes must not
be swept — the JSON tail list stays; landing order memo-first-then-unwrap
keeps T1 byte-identical). AWAITING MATT: his read of the plan and the
execution approach (my rec: inline T0-T2, T5-T6; Sonnet pairs for T3-T4;
Fable refuter at close). GOALS.md stays Matt's uncommitted edit. Nothing
pushed this session (6a32e55 + the docs commit ahead of origin — ask at
close-out).

Updated: 2026-10-18 (refactor session III, Fable 5.1 xhigh). R2 ACCEPTED by
Matt (all four items; render cost carried to 2.3). CHECKPOINT 2.2 LANDED
on main: 7e20e5f..5980873 (ten code commits + the docs commit) — one
call became two: prepare(ctx) once per build (the only way a node learns
the rate; may allocate) and start(frames) per note (may not); 45 rate
constructors gone, 44 per-node rate copies gone (sample_rate() from the
base's one context), RenderContext made only where the loader finishes a
patch, the factory takes the seed alone, Instrument holds the context,
MonoSource::render lost it, the UI has one context from its opened stream.
Executed per the plan docs/superpowers/plans/2026-10-17-part2-checkpoint-2.2.md
(its Ledger = every ruling): T1 inline (contract + shim + registry-wide
split-equivalence sweep), T2-T8 one Sonnet implementer + one Sonnet
reviewer per family (reviewers mutation-tested; four vacuous tests
replaced), T9 hosts inline, T10 close: Fable refutation (16 items, six
fixes: UI rate sites, Vibrato pending-chain swap, BWSection base,
Instrument ctx protected, test gaps, comments) + the same refuter's second
pass (two more small fixes), all in 5980873. Byte-identity: null gate
80/80 after every commit; whole-tree A/B vs cli_pre22.exe (the tree at
1145a52) 1,458/1,458 identical; family A/Bs 1007/701/68/96/747/679/1070,
0 differ. Timing neutral (within 3%). FOUND: DelayLineSource read one past
its ring (kept byte-identical via a zero slot; wrap = listed render change
for 2.5/Part 3); stage-less PhasedValueSource crash (R2 debt, excluded
from the sweep). REPORT R3: docs/autonomy/refactor/reports/2026-10-17-checkpoint-2.2.md
(ten sections, both refutation passes verbatim in §8). REVIEW.md R3
AWAITING MATT, four verdict items (0: the DelayLine slot vs the wrap; 1:
the UI re-preparing at every play; 2: the start() allocation list for
2.6; 3: render_context()/prepared() public). Debt in §7 (the allocating
start bodies with file:line; the Setup-path group; unprepared-node
Release silence until 2.5's fault flag; g_renderContext as the UI's
113th global; the 5,700-line test file). NEXT: Matt's R3 verdict, then
checkpoint 2.3 (the per-tick memo, taps reading A; RefSource, the usage
counter, starved-ref promotion and SharingTrace deleted; expected diffs =
the 13 lag patches in docs/architecture/measurements/2026-10-16-sharing-lag.md
+ double_advance_bug) — plan written against the then-current code; it
is also where the 2.1 render-time cost is recovered or the design
revisited. PUSHED 2026-10-18 on Matt's word (99f5d1d..60060c0 + this
line); main level with origin. GOALS.md stays Matt's uncommitted edit.

Updated: 2026-10-17 (refactor session III, Fable 5.1 xhigh). R2 ACCEPTED by
Matt (all four items; render cost carried to 2.3). CHECKPOINT 2.2 IN
PROGRESS on main: plan docs/superpowers/plans/2026-10-17-part2-checkpoint-2.2.md
(ten tasks, per-type tables, Ledger of rulings; the A/B base is
renders/scratch/ab_tree/cli_pre22.exe = the tree at 1145a52). Landed:
T1 contract + shim + registry-wide split-equivalence sweep (7e20e5f),
T2 core (1a55d88), T3 noise (b03a259), T4 wavetable/evolutions (44b9fba),
T5 additive (0dc8f04), T6 physical (da75aa1), T7 filters+Vibrato
(72fa01e); each family = one Sonnet implementer + one Sonnet reviewer +
controller null-gate rerun, fixes applied inline before the commit. Null
gate 80/80 after every commit; family A/Bs 1007/701/68/96/747/679
patches, 0 differ. FOUND on the way (Ledger in the plan): DelayLineSource
read one past its ring (kept byte-identical via a zero slot; real fix =
listed behaviour change for later); the sweep excludes a stage-less
PhasedValueSource (pre-existing crash). NEXT: T8 combiners (dispatched),
then T9 hosts inline (shim deleted, factory loses the rate, Instrument
holds the RenderContext, UI's one context from the stream; PinCounter
gets a start counter), T10 close (whole-tree A/B vs cli_pre22.exe,
timing, refuter, report R3). Nothing pushed since Matt's push after
session II; GOALS.md stays his edit.

Updated: 2026-10-17 (refactor session II, Fable 5.1 xhigh). CHECKPOINT 2.1
LANDED on main: 1b9b69f..9f2b4a8 (23 commits) — every pin on every node
declared once (Component root; pins.h / component.h / descriptors.h;
Generator / Processor; next() sealed non-virtual, compute() pure; 109
accessors gone; -3,064 engine lines). Executed per the plan: T0-T3 inline
(the measurement doc docs/architecture/measurements/2026-10-16-sharing-lag.md
= 13 lag patches, 0 same-tick taps), T4-T11 one sonnet implementer + one
reviewer per family, fix rounds where needed; final whole-checkpoint
review on Fable with the fixed refuter brief (docs/architecture/REVIEW_BRIEF.md,
written this session — the spec named it, it did not exist); fix wave in
two rounds (97c5da1, e29b29c, 091e924), re-reviewed clean. Byte-identity:
null gate 80/80 after every commit; whole-tree A/B against a binary built
from 1b4fe1b = 885/885 over library+baselines+audition+pending, sweep
455/455 sampled; 8 patches (2 baselines committed in 8f640e6, 6
gitignored audition fm_matrix2 ones) now state the RedNoise defaults
they relied on. Full gates: all pass except the two roundtrip gates on
exactly the backlog-79 set (8, unchanged). REPORT R2:
docs/autonomy/refactor/reports/2026-10-16-checkpoint-2.1.md (ten
sections, refutation verbatim in §8, rulings in §6, debt in §7, three
C++ lessons in §10). REVIEW.md R2 AWAITING MATT with FOUR verdict items —
item 0 is the one that matters: RENDER TIME REGRESSED (oboe1 +27-40%,
piano +23-44%, viola +2-9%; 40-cheap-node chains +50-75%): a few ns per
node per sample from the generic next() that the hand-written bodies
did not pay; bisected (grows with every family), experimented (noinline,
forceinline: nothing; inline sweep list 9f2b4a8: kept, 10-15 points on
chains), asm read (lean). Carried to 2.3 (the memo rewrites the
per-sample path) unless Matt wants it recovered first. Debt list §7
(paramMap-on-Input narrowing, Pluck seed, WanderNoise inert amplitude,
three additive bugs, PhasedValueSource stage-less crash, remaining dated
comments, the 4,448-line test file). KNOWN: tools/structure/metrics.json
snapshot refreshed in the close commit; the pre-checkpoint CLI and the
per-commit CLIs are kept under renders/scratch/ab_tree/cli_<sha>.exe
with the micro patches for re-timing (gitignored). The base21 worktree
is removed. GOALS.md stays Matt's uncommitted edit. NEXT: Matt's R2
verdict (REVIEW.md), then checkpoint 2.2 (prepare/start split; the
plan gets written against the then-current code) — or a performance
pass first if item 0 says so. Nothing pushed; main is 23+ commits ahead
of origin (ask at close-out).

Updated: 2026-10-04 later (refactor session I with Matt; no code changed).
DECIDED: no restart. Engine is refactored under its existing gates; the UI
is rebuilt module by module out of main.cpp. Goal = go public to attract
developer collaborators, code near pristine (CLAUDE.md "Design principles",
recorded at Matt's request). AGREED, NOT BUILT: enforcement goes in BEFORE
campaign 1 — ratcheted structure-budget script (CTest + pre-commit + Claude
Code edit hook), a design section in every spec for Matt's sign-off, a
module map, an independent refuter with a principles checklist (separation
of concerns etc.), and an "architecture delta" report in REVIEW per
campaign (moderately verbose, script-generated metrics, 1-2 C++ lessons).
C++ primer for Matt: docs/matt/cpp_primer_10000ft.md. Measured from git:
mforce_ui/main.cpp = 196 commits, only 3 of them >=500 lines, never a
second source file; Fable 5 53% of added lines, Opus 4.6/4.7 37% — not a
model problem. NEXT: campaign-by-campaign discussion with Matt (my read:
gates first, then campaign 1 = engine RT safety with the allocation test
first and CTest registration pulled forward); spec at xhigh.
SAME DAY, LATER — PLAN CHANGED TO TWO PHASES (Matt: major surgery first,
"extra latitude to slash n burn"; gates only after). Spec:
docs/superpowers/specs/2026-10-04-refactor-gates-design.md, section 0.
**FEATURE FREEZE: no feature work in any lane, no "go" runs, until Matt
lifts it** (CLAUDE.md "Scope right now"). Phase 1 = surgery: latitude on
the route (delete, rewrite, break internal interfaces, big commits, no
hooks/ratchet, areas a goal not a rule); fixed = ONE signed
target-architecture doc for the whole codebase incl. deletion list,
behaviour gates before/after every cut, checkpoints ending in commit +
independent review + architecture report. Checker is built now only as a
meter. Phase 2 (ratchet, hooks, per-change sign-off) switches on when
surgery ends; its five open decisions (spec section 5: limits 600/80,
`lizard` NOT installed, source/filter peers, default-proceed, third REVIEW
file) still need Matt.
PRE-SURGERY STATE OF GIT (10-04): main had no uncommitted code. Docs
checkpoint 93a8633 PUSHED (Matt approved; origin had been 658 commits
behind since 07-03; the repo is public and he is fine with that; push
only when he asks). The two September commits that never reached main
are now on it: 70ae4c9 (UI fix, duplicate same-field PerformNode ids
survive roundtrip) and f684b47 (FormantSequence1 repaired, NATest1
retired). Validation after a full rebuild: engine_tests ALL PASS (3581
checks), test_figures 43/43, UI stamp clean, null gate 77/79 identical
with exactly the two expected differences, roundtrip 134 patches 0 id
changes + 8 render diffs (backlog 79 updated). Manifest refrozen for
those entries plus the two phrase_smoke baselines: 80 entries. April
branches/worktrees (chord-walker, harmony-first, agent-*) are old
experiments, left alone; agent-a57ee260 holds a 27-line uncommitted edit
to music/structure.h from 04-12. GOALS.md stays Matt's uncommitted edit.
TARGET ARCHITECTURE PART 1 DRAFTED: docs/architecture/target-architecture.md
(layers, UI modules vs his Unity project, deletion list, bug list, which
parts get max effort). Five "(ask)" items in its section 5 need Matt.
10-05: MATT ANNOTATED PART 1 (17 `MATT:` notes in the doc); we debate and
decide ONE TOPIC AT A TIME AT MAX EFFORT, and each decision is written
into the doc in place of his note (remaining `MATT:` lines = still open).
DECIDED so far: (1) tiers are joined by DATA, not calls — Performance
returns a list of performed notes, knows an instrument only by its
InstrumentClass; render knows nothing about music; new modules contract /
voicing / play (doc sections 3, 3.7, 3.9-3.11); (2) InstrumentClass is a
real type now in seed form (sustaining + onset vocabulary), named classes
stay backlog 78; (3) converters stay; (4) RenderContext guide written
(doc 3.1) — its fate is a Part 2 decision. Found while reading: performer
logic lives in FOUR places (conductor.h, patch_loader embedded-score
path, UI stamp_passage, UI live keys) and pitch_bend.h is a misnamed
two-tier file, not a type.
ALL 17 NOTES DECIDED 10-05 (every decision is in the doc, dated): Source
vs Filter = one layer, one classification, family list in Part 3; names =
Patch/Graph/Node (description) vs ValueSource/voice/Instrument (live),
patch_json functions not a codec class, build_instrument a function,
OpenPatch in the UI (no "*Document"), Transport not Generator; Theme from
JSON, constants per module, one home for the default sample rate; capture
= per-node strips (not live play); live hand-off = ownership passed by
two queues, agreed in principle, details Part 5; templates + strategy
settings move to compose (form in Part 6; strategies are an open set so
each owns its settings); Composer's writes-to-template = request+record,
Part 6 decides in/out vs separate record; paramMap retired entirely (list
docs/matt/parammap_patches.md; convert library+baselines+audition+the 9
PENDING ones — Matt's one-time exception; wiring gate 80/80 identical);
Mappings dialog reduced to its read-only view; TargetEvolution kept as a
node, string evolution form deleted; IComposer/Genre/ClassicalComposer,
SectionStrategy, 3 Composer members, one mono wrapper deleted;
realization strategies are half-finished NOT dead — finish in Part 6;
node-graph FORM stays (1,048 files), MODE goes, convert = two Patch
operations; pattern library moves to an optional shelved target
(mechanism fits all shelved generators); hiBoost DELETED outright (comp
renders change, listed as expected diffs; curves added by ear later).
ALSO 10-05: standing CLOSE-OUT RULE (CLAUDE.md): when Matt closes a
session, ask whether to push the unpushed commits. Matt's universal-
parameter idea (min/max/density on the base) QUEUED in IDEAS.md §dsp as
a feature for later (freeze); its structure lands in Part 2: a
Generator / Processor level under ValueSource (Matt's shape; Envelope
joins Generator; three pin kinds param / signal input / structural
input; pins declared once).
PART 2 SIGNED OFF 10-05 (docs/architecture/part2-valuesource-contract.md,
7 sections): per-tick memo (taps reading A first, then B as its own
checkpoint), Generator/Processor bases + five self-registering member
kinds (Param/Input/Slot<T>/Setting/Array), prepare(ctx) may allocate vs
start(frames) may not, rate only through prepare(ctx) (file rate = render
default; RenderContext stays), no throw on render paths + fault flags,
rejected wires fail the load; §7 = six checkpoints with expected diffs.
THE PLAN HAS THREE LAYERS (explained to Matt 10-05): destination doc
(signed) -> per-area design specs Parts 2-8 (what/why, decided section
by section) -> per-part implementation plan (writing-plans skill) + cut
at high with gates/review/report per checkpoint. INTERLEAVED, in §8
order: Part 3 spec (xhigh) -> meter + CTest + one gate command (lizard
question returns) -> plan+cut Parts 2 and 3 -> Part 4 spec (max) ->
plan+cut -> Part 5 spec (max) -> plan+cut -> Parts 7, 6, 8 (xhigh).
Effort switches are Matt's (I cannot change my own session's effort).
PART 3 DECIDED 10-05 (docs/architecture/part3-nodes.md): Component is
the ROOT (Matt's MComponent); ValueSource (Generator/Processor) is one
branch, the structural parts (evolutions, partials, formants, spectra,
expand rule) the other — no fake next(); the 13 evolution WRAPPERS go
(each evolution is a Component implementing WaveEvolution; TargetEvolution
registered; string form deleted); family table accepted as a living list;
core/dsp primitives (7, copies compared before merge); 6 envelope preset
classes collapse into Envelope's preset setting (176 nodes migrate);
KSPianoString alias migrates (116 nodes); one type per file; RT fixes per
node; 8 checkpoints all byte-identical by design.
CHECKPOINT 0 LANDED 10-05 (commit 637207a, Matt at HIGH, lizard
installed): tools/structure/ meter (phase 1 = reports, never blocks;
lizard for function lengths + token duplicates; numbers match the audit's
known answers), tools/structure/metrics.json tracked snapshot,
docs/architecture/MODULES.md generated, CTest registers engine_tests /
test_figures / meter (4/4 pass), tools/gates.py one-command gate runner
(--fast green). NEW LANE docs/autonomy/refactor/ (REVIEW.md, BACKLOG.md,
reports/) — report R1 = reports/2026-10-05-checkpoint0-meter.md, the
FIRST architecture report. The independent refuter found 7 FAILs (rule
evaluation spread out, line count +1, wrapped-return-type false positive,
map errors, gates missing, tests missing); all fixed same day in a
fix-up commit and answered in the report section 8. KNOWN: gates.py
--fast now runs tools/rt_smoke.py, which FAILS on
perform/wiring_setting.json — a pre-existing backlog-79 roundtrip render
diff (reproduced on the committed tree), left visible; Part 4's single
codec fixes it. Matt's R1 verdict on the report FORMAT comes before
checkpoint 2.1 produces the next one.
PLAN FOR CHECKPOINT 2.1 WRITTEN + COMMITTED (280713f):
docs/superpowers/plans/2026-10-05-part2-checkpoint-2.1.md — 13 tasks:
T0 the two measurements (lag reads / same-tick taps, instrumented
loader + tools/measure_sharing.py), T1-2 pin types + Component +
Generator/Processor, T3 WaveSource prototype, T4-11 one family per task
with generated pin tables, T12 close (gates, delta, refuter, report R2).
Later checkpoints (2.2-2.6, Part 3) get their own plans against the then
-current code. AWAITING MATT (asked, unanswered at session end 10-05):
(1) R1 verdict on the report FORMAT (docs/autonomy/refactor/REVIEW.md)
— gate for cutting 2.1; (2) execution approach: subagent-driven per task
(my rec for T4-11) vs inline (my rec for T0-3). Session ended at 92%
context; next session starts at HIGH for execution.
(was: NEXT (sequence agreed 10-05): (a) the METER + CTest registration + one
gate command — the gates spec's "built now" set; needs Matt's OK to pip
install `lizard` (function length) or the hand-written fallback; (b) the
implementation plan for Parts 2+3 (writing-plans), executed at HIGH with
checkpoints 2.1-2.6 then 3.1-3.8, each ending in gates green + commit +
independent review + architecture report (first report = 2.1); the two
measurements (wiring-order lag list, same-tick tap list) taken first.
(was: NEXT: Part 3 (nodes) at xhigh — family list from today's menu, shared DSP
primitives (BLEP/Thiran/DC blocker/friction/biquad coefficients), the 13
evolution holders -> one, additive classes, TargetEvolution node, file
splits (wave_evolution.h 1552, partials.h 1486), RT fixes per node.
(Was: PART 2 IN PROGRESS = the ValueSource contract at MAX (per-tick memo,
Generator/Processor bases, pin kinds, prepare split, sample-rate truth/
RenderContext fate, no-throw, rejected wires). Reading list: envelope.h,
patch_loader's RefSource auto-wrap + collect_advance_ids + starved-ref
promotion, feedback_loop_design.md §3.3 (tap z^-1), engine_tests tap/loop
tests, perform_source.h, repeating/multiplex/vibrato prepare paths. The
spec goes in docs/architecture/ as its own part file. Then Parts 3-8 per
doc section 7; then the meter, cutting.

Updated: 2026-10-04 (full architecture/code review DELIVERED 10-03;
no code changed). Report: docs/audits/2026-10-02-architecture-code-review.md
(+ 2026-10-02-raw/). 474 findings, 123 skeptic-verified, 17 confirmed by
hand, rest marked unverified. Headline bugs REPRODUCED: Conductor replays
every part once per section; --dun renders silence; dictionary chord
voicings spin to int overflow. Campaign order in report §8 (RT safety ->
comp spine bugs -> lock-free voice publish -> registry-first loader ->
per-tick memo -> UI split -> build/test hygiene). Effort scheme: xhigh for
specs/reviews, high for executing plans, max for single hard turns,
ultracode keyword per fan-out turn only (5-hour window ~60 agents).
NEXT: priorities discussion with Matt (he is re-reading the report), then
a short xhigh spec for campaign 1. Nothing committed; audits/ untracked.

Updated: 2026-09-22 (comp walk3 BUILT via Opus dispatch; plan
docs/superpowers/plans/2026-09-22-comp-walk3.md T1-T8 done.)
State: NRS v1 profile = styles/nursery_v1.json (tendency rows, NCT
license odds, placement, critic, search sizes); note_map.h +
phrase_critic.h + melody_profile.h; select_anchors scores from the
profile and returns AnchorResult; DefaultPassageStrategy harmonic mode
runs nested best-of-N (10 phrase candidates rerolling first-referenced
derived motifs, 10 passage attempts, top-3 dice). Harmonic mode requires
passage melodyProfile. Batch: renders/comp/audition/walk3/, 10/10
validator, 0 over-budget phrases; strings in
docs/matt/Comp_walk3_for_annotation.txt. Null gate: only
template_mary_walk differs (test_k467_walker fails at HEAD, pre-existing).
Known: ChordLabel::to_string labels G7 "V" (compares ChordDef name, not
shortName); note_map uses its own label function; chord_walker untouched.
comp REVIEW Awaiting: **23** (walk3 listen/annotate); 22 folded into 23.
NEXT: Matt's walk3 annotations → next profile edit or rule round.

Updated: 2026-09-22 (comp walk2 annotated; session ended on QUOTA: 75% of
Matt's weekly Fable limit burned by Tuesday — NEXT SESSION RUNS THE
QUOTA PATTERN: Fable coordinates thin, every heavy build/spec-execution
DISPATCHES TO OPUS. Walk1 was built inline on Fable only because Opus
529'd twice that night; that exception is over.)
**WALK2 ANNOTATED (docs/matt/comp_walk2_annotated.txt, committed) —
DISCUSSION OPEN, NOTHING BUILT (Matt: "don't go off half cocked").**
Verdict: much improved, bar-6 nonsense gone; s108's antecedent came out
as LITERAL Mary; s100 a clean Pass. The distilled finding: walk2's
complaints are ONE missing grammar — NCT approach/departure rules with
leaps as the violation vector: (a) leap-TO-an-NCT (appoggiatura) out of
genre; (b) leap-FROM-a-passing-tone breaks its license; (c) chord
EXTENSIONS are NCT-class (Mary's own E-over-G7 13th is fine BECAUSE it
steps down — the rule was never "no 13ths"); (d) leading tone B wants C,
may detour B-D-C, never leaps down (first scale-degree tendency); (e)
departures COMPOUND ("taken together = computer"). Answered Matt's dice
question: F-G over C was NOT dice — suspension term only fires across
chord changes, 96:4 only over V; fresh-struck NCT left upward inside a
bar was scored zero. AWAITING MATT, the two design forks before any
build: (1) compounding = hard per-phrase departure BUDGET (my lean —
matches his judging) vs super-additive penalties; (2) genre profile
becomes DATA now ("NRS v1" — his "rules are genre-dependent" point;
compound-melody exception = a future profile level) vs constants until
the ruleset stabilizes (my lean: data now). Context-priming (s103 "less
jarring after repeated Gs") deliberately deferred.
Standing order (in batch script): every batch emits passage strings to
docs/matt/Comp_<batch>_for_annotation.txt. False alarm resolved: walk1/
walk2 same-named WAVs — Matt played walk1 s105 by accident; chain
verified byte-exact end-to-end (template==JSON==WAV==strings), spectrum
confirmed. Matt declined filename stamping.
comp REVIEW Awaiting: **22** (walk2 — annotations IN, forks OPEN),
**20** (crawl verdict, formally), 19, 18, 17, 16, 14, 13, 12, 11, 10, 9.
NEXT SESSION: read comp_walk2_annotated.txt + REVIEW 22 fold, get
Matt's fork answers, THEN spec walk3 (NCT grammar + leading-tone
tendency + compounding + possibly profile-as-data) and DISPATCH THE
BUILD TO OPUS. head_a permutation fix still queued behind it.

Updated: 2026-09-21 (comp lane revived — brainstorm → spec → plan with
Matt, then the build; Opus dispatch died twice on 529s, finished inline
on Fable).
**COMP CRAWL SHIPPED — REVIEW 20 (Mary, harmony-first, zero generation).**
Matt's five ground rules are standing law (memory + spec §1): crawl
before walk; harmony fully specified FIRST in the template; generated
figures get FITTED to the underlying chord; model stack extends, never
rebuilds; every generator except Markov Figure is shelved
(comp/GENERATORS.md is the catalog). NEW INSTRUMENT RULE: melody oboe1,
accompaniment piano_default (CLAUDE.md updated, supersedes 09-05
all-piano). NEW CONVENTION: comp-purpose .psg saves use `|` as
STRUCTURAL phrase boundary only.
Spec docs/superpowers/specs/2026-09-21-comp-crawl-mary-design.md; plan
docs/superpowers/plans/2026-09-21-comp-crawl-mary.md.
WHAT LANDED: passage_melody.h (scale-grid math + passage-string →
Locked-figure PhraseTemplates, engine_tests cover the full Mary oracle);
PassageTemplate.melodyPassageFile/melodyOctave (round-trips);
apply_passage_melodies at CLI template load; compose honors per-part
instrumentPatch with one instance per unique path (template null gate
27/27 byte-identical) + multi-instrument-only mix normalization (the
first scoped-to-everything version moved 2 chord baselines — caught by
the gate, rescoped); scores/baselines/template_mary_crawl.json + tracked
Comp_Mary.psg (gitignore anchored: /passages/ not passages/); render
verified event-exact (26 melody events == Matt's transcription, C-E-G /
G-B-D-F root position at the right bars, peak 0.98) →
renders/comp/audition/crawl1/, WAV sent to Matt. Backlog 21 done
(zero-event = failure, rc-0 silent class proven live by positive
control). Baseline template cull 27→19 by feature cover (Matt's
directive; 9 subsets → scores/old/baselines_culled_2026-09-21/); the
k467 family kept SIX not one — period/harmony/motifs/parallel/opening
each carry unique loader features. TRAP for template authors:
ScaleChord.degree is 0-BASED (tonic 0, dominant 4) — 1-based roman
numerals render supertonic chords; first Mary render did exactly that.
Old template_mary.json connectors ({"type":"Step","step":N}) parse as
leadStep 0 — pre-existing, left as-is, loader-era artifact.
SAME DAY, LATER: **STRUCTURAL TEMPLATE + WALK ROUND 1 SHIPPED** (Matt
drove the brainstorms, then "spec/plan/build without me").
(1) template_mary_structural.json — named motifs (family scheme
head/rep3/rep3_a/rep3_b/head_a/close), references, parallel period —
renders BYTE-IDENTICAL to the crawl; first-ever proof of the
reference+connector path. TRAP PINNED at every authoring surface:
template connectors are DENSE per figure ([i] = bridge INTO figure i,
[0] null dummy), NOT an N-1 between-figures list — my plan had it wrong,
the Opus agent silently built the derivation right, and the misauthored
JSON shifted every figure by its connector.
(2) Walk round 1 (spec/plan 2026-09-21-comp-walk1*): Complexify
TransformOp (elaboration reined by target note count); derived-motif
synthesis in realize_motifs (content-less derivedFrom declarations
resolve through figure_transforms::apply, chains, throws by name);
harmonic anchor selector (anchor_selector.h, opt-in
PassageTemplate.anchorMode="harmonic") — Matt's 3 edge rules hard
(phrase-opening downbeat + phrase-final = chord tone; passage-final =
degree 1), chord-tone weights with stacked bar-final/figure-final/long
boosts, cursor proximity, seeded roulette over the ENUMERATED legal
chains, parallel intent pins the consequent opening. Gates: engine_tests
557→754; template null gate 19/19 byte-identical (opt-in); batch
validator 10/10. **REVIEW 21 = ten generated siblings**
(renders/comp/audition/walk1/), first generated output of the reset —
sibling melodies are already tune-shaped (s101 re-anchors head on the
5th, uses F-over-G7 as the seventh, cadences to C).
Known/accepted: exact Mary out of walk support (vary_steps is
interior-only — final-step/cadential freedom is a named future thread);
interior NCT hole open by choice (minor-Mary passes rules).
OPEN ON MATT: **REVIEW 20** (crawl verdict — covers structural template
too, byte-identical) + **REVIEW 21** (walk siblings: which please,
which fall, how) + the standing 18/17/16/13/14/9/10/11/12/19.
OPEN ON DEV: on walk verdicts — weight calibration or next rule
(interior NCTs / re-shape-to-fit); then generated figure slots (Markov
proposes, fit disposes). Voicing-tier question (score vs lead-sheet)
parked in IDEAS.md §comp.

Updated: 2026-09-21 (autonomous dsp run, Fable inline; Matt brainstormed
comp in a parallel session — comp GOALS deliberately untouched here).
WORKING-TREE NOTE: docs/autonomy/GOALS.md is Matt's own uncommitted
refresh — never stage/commit/revert (decomposed into backlog 81–89 this
run, but the file itself stays his).
**STACK RUN — full report reports/2026-09-21-dsp-run.md.** Shipped:
(1) **Oboe trading licks SOLVED as two patches → REVIEW 76**
(oboe_nasal1 queue): two ignition states (fundamental-carried = Matt's
"nasal oboe"; H2-carried, +8 dB = his "flute/clarinet"), line inherits
its breath note's ignition, D6 = the 3/8 coin-flip pitch; junction
middle-slope ×0.85 makes the oboe state unanimous (tone preserved,
−1.6 dB; 1/8 residual flip on 2 s held E6/F#6); −46 dB H2 bias into the
loop makes the flute state unanimous. Tooling tools/oboe_licks.py.
MID-RUN CORRECTION on record: Matt's oboe_compare.wav is the FULL psg
at 120 bpm — first per-note grid of HIS file was mislabeled (2× tempo);
corrected same day via his side-chat note; offline findings were
self-consistent throughout. (2) **Live legato SHIPPED (backlog 53 v1) →
REVIEW 75**: engine deliver_continuation()/continue_voice_live()
extraction (null gate 79/79 byte-identical, engine_tests 508), UI
overlap=slur / release-back / gap=detached / ring-cut on sustaining
patches; tongue needs a latch (wire-ready, Matt's word); FLAG: patches
declare polyphony 1 as offline semantics (even piano_default), so mono
rides `sustaining`, not polyphony. (3) **"All breath" Onsets option**
(mode 3 = pre-articulation) — relaunch picks up both UI items.
(4) **Trombone transitions DIAGNOSED, folded into 74**: glide
no-man's-land collapse → re-ignition overshoot (+2% = the squeak) over
old-partial residue (= the blend); tongue can't fix (pitch still
glides); candidates recorded not built (patch "unfinished" per Matt).
(5) **62 UNBLOCKED**: bwg_perc2 recalibrated into the patches (0–3.3 dB
from library reference, was −20); tomdrum ~47 dB structurally quiet
since round 1, flagged. GREMLIN on record: ONE unreproducible loud
render of identical tomdrum bytes (35 dB), evidence overwritten by my
own stress test — offline-determinism watch, backlog 59 family.
(6) **65 pluck flag ANSWERED**: plucks track; a 27 s ring masks it +
38 c flat; scale demos staged; follow-ups backlog 89. (7) GOALS
decomposed → backlog 81–89; junction periphery measured (−20 dB rel at
±10%, second-order, not zero). dsp REVIEW Awaiting: **76**, **75**,
**74**, **72**, 62 (re-listen), 65 (scales), 38, 39.

Previous update: 2026-09-20 late (interactive with Matt, after the v2 Opus build
below; session continuity save 09-21 morning).
WORKING-TREE NOTE unchanged: docs/autonomy/GOALS.md is Matt's own
uncommitted edit — never stage/commit/revert.
**POST-V2 EVENING ROUND — Matt's first-pass 74 verdict ("goals 100%
achieved. No clicks, clean and smooth articulations") + two unforeseen
effects, both shipped same evening; articulation1 queue REGENERATED
after, so his next listen is one build newer than his first.**
(1) **Per-note draw re-anchoring** (his option (a)): ValueSource::reseed()
no-op virtual; Randomizer stores seed + reanchor(); overrides across the
noise family + Envelope draw rngs + Vibrato/LayeredRed forwards; the
CONTINUATION Setup re-anchors every node (never fresh paths — prepare
consumes layout draws, post-prepare reseed ≠ byte-neutral). Fixes the
phrased-oboe "different oboeist per phrase" (backlog 72 unmasked by
phrasing); pins notes 5/7's overblow back — r5c/72 remains the real
cure. Null gate 79/79; engine_tests 508 incl. an exact-replay test.
(2) **Trombone slur struggles = partial-crossing (diagnosis, Matt
concurs by ear)**; his two experiment knobs shipped: transport "Onsets"
combo (auto/all tongue/all slur — stamp_passage takes the mode; phrase
starts stay breath) and Output-panel "glide ms" drag (write-only-when-
touched; taught-patch + corpus roundtrip verified, 8 known findings, 0
new). If his ears confirm, the likely fix = one emission rule (tongue
across partial boundaries). (3) Onset vocabulary corrected to the
approved [breath|tongue|slur] — breath was undeclared, interning to 0
and false-positiving the vocab warn; now declared-but-unwired (the
fresh-voice attack IS the breath; the glide IS the slur).
OPEN ON MATT: 74 re-listen + his two trombone experiments; 72 formal
verdict (informal: "velocity-linked brightness is there"); 62/65/38/39.
OPEN ON DEV: partial-crossing emission rule (gated on Matt's
experiments); backlog 75 passage-cutoff diagnosis (Matt: minor;
UI-buffer half already fixed 09-20); backlog 79 roundtrip lossiness
(with 65/71); 74a8 breath contour design (its Performer-side home =
wheel/pressure inputs, per v2 spec §5); live legato = backlog 53 (pure
wiring now). Specs: 2026-09-20-note-onsets-v2-design.md (+ plan) is
CANON; the 09-19 v1 spec is history (§9 records the divergence).


Updated: 2026-09-20 (onsets v2: Matt drove the redesign after auditioning
73; brainstorm → spec → plan → Opus dispatch, coordinator re-verified).
WORKING-TREE NOTE unchanged: docs/autonomy/GOALS.md carries Matt's own
uncommitted refresh — never stage, commit or revert it.
**NOTE ONSETS v2 SHIPPED — REVIEW 74; v1 (REVIEW 73) folded to Resolved
the day after it shipped.**
Spec docs/superpowers/specs/2026-09-20-note-onsets-v2-design.md; plan
docs/superpowers/plans/2026-09-20-note-onsets-v2.md; report
reports/2026-09-20-note-onsets-v2.md.
WHY v2 EXISTS: v1's spec described deadline-extension, but the build baked
a phrase's TOTAL at prepare — so a phrase was one long note to every
duration consumer and silently re-scoped every percent envelope (oboe1's
25% release → a 3 s die-off over a line). Matt's audition named the
symptoms: retune ticks, no repeated-note articulation, envelope stretch.
THE SHAPE: every note is atomic and carries two facts — `onset`
(breath|tongue|slur) and `hold` (does the excitation continue past its
end). No phrase object reaches Render-land; continuation is DERIVED from
engine state (is the line's voice open?), the same test live key-overlap
makes. PerformedNote is the Performance→Realization boundary object and
play_note's only argument; play_phrase / PhraseNote / prepare(total) are
deleted. The offline render loop is now RESUMABLE: render_chunk is the
one sample loop, a hold:true note suspends the voice warm, the next note
resumes it at the boundary sample, release+tail run only at line end.
Envelope timebase is the NOTE again, always; its companion is release
re-layout — gate_release(releaseRefFrames) re-resolves the release stage
against the RELEASING note. Pitch changes inside a line GLIDE over
instrument-block glideMs (default 15 ms) via the existing bend machinery
(note: a legacy paramMap patch pushes frequency once at Setup, so only
pulled frequency chains glide — every taught wind patch does).
`sustaining` is DECLARED in the instrument block, never inferred: a
non-sustaining instrument never receives hold, so the piano invariant
holds by construction. UI: Patch Output pane gained sustaining + onsets
widgets (and visible labels — the polyphony spinner's was swallowed by
PushItemWidth(-1) since it was added).
GATES: null gate 79/79 after every engine-touching task, including the
intermediate checkpoint that proved the render_chunk extraction byte-clean
before any semantics changed; engine_tests 494→506; piano invariant
md5-identical at CLI and through gencheck; gencheck parity scale
1.000000 / 1 LSB; roundtrip corpus = the same 8 known findings (backlog
79), 0 new; articulation1 v2 — oboe phrased 0 deep dropouts, dent
−10.9 dB repeated / 0.0 dB slurred, trombone 0 dropouts, −1.4 / 0.0 dB;
both instruments' flat renders byte-identical to a trigger-unwired
control (dormancy).
DEFERRED, on record: live keyboard phrasing is now pure wiring (backlog
53); phrase-scale shape belongs to the Performer, pointer added to
74a8; onset-keyed envelope variants (68); depth-as-expression;
polyphonic phrasing; new backlog 80 = the sustaining/shapeless-envelope
lint the spec defers. Backlog 76 CLOSED (strip drawing was O(all samples
× strips) per frame, commit 030a4d5).
NOT VERIFIED BY THIS RUN: the `|`-passage path in the running UI — there
is no headless entry point for the transport's passage string, so the
emission is covered by construction and by the score-side renders. Matt's
UI pass confirms it.
NOTED, not a regression: a phrased oboe1 line shows gencheck-vs-CLI scale
1.002499 / 313 LSB because a held line renders ~7 dB hotter than four
separate attacks (peak 0.98 vs 0.73) and enters the soft-clip knee, where
the UI's direct-instrument render and the CLI's mixer path have always
differed. Same patch at half gain: 1.000000 / 1 LSB.
dsp REVIEW Awaiting: **74**, **72**, 62, 65, 38, 39.

Previous update: 2026-09-19 evening (articulation day: the note-transitions
build, inline on Fable, same day as the brainstorm).
WORKING-TREE NOTE unchanged: docs/autonomy/GOALS.md carries Matt's own
uncommitted refresh — never stage, commit or revert it.
**NOTE TRANSITIONS v1 SHIPPED — brainstorm (Matt + Fable, this morning) →
spec → plan → build in one day. REVIEW 73.**
Spec docs/superpowers/specs/2026-09-19-note-transitions-design.md; plan
docs/superpowers/plans/2026-09-19-note-transitions.md; report
reports/2026-09-19-note-transitions.md. The shape: `|` phrase marks in
passage strings; a phrase = the unit that acquires a voice and opens the
gate (play_phrase; play_note is now a one-note phrase, byte-identically);
in-phrase notes re-drive the LIVING voice (set_note + non-setting push
bindings, no prepare); transitions arrive as a per-note interned name on
a new Note.transition pin; a new NameGate node (name string visible on
its face) feeds the new Envelope.trigger pin (Setup-sampled,
restart-from-current, click-free by numeric test). Emission rule v1:
first-of-phrase "breath", rest "tongue" — one seam, two callers (score
loader "phrase":"cont" + UI transport). Taught patches:
patches/audition/articulation1/{trombone,oboe}_tongue.json (originals
untouched; the consonant = one TDip envelope, Matt's knob). Ears queue
renders/dsp/audition/articulation1/ — OTJ one-breath-per-line against
today's every-note-a-breath.
GATES: null gate 79/79 after every task incl. the play_note delegation
refactor; two-note phrase == one long note byte-identical (engine test +
CLI smoke pair patches/baselines/phrase_smoke_*.json — NEW baselines,
add to manifest at next deliberate refreeze); mid-voice retune
zero-crossing ratio 1.498; gencheck parity scale 1.000000 / 1 LSB incl.
a taught+phrased score; taught-patch roundtrip intact; engine_tests
474→494.
DEFERRED, on record: live keyboard legato (overlap = phrase; needs
backlog 53's pool work — delivery machinery ready); shape slot
(honk/squeak — own brainstorm); isSetting push bindings hold their
phrase-start value mid-phrase (v1 decision); backlog 75 passage-cutoff
diagnosis queued (Matt: "extremely minor", deferred).
NEW FIND, backlog 79: 7 pre-existing roundtrip render diffs on
loop/bug-repro baselines (+ the known wiring_smoke id loss) — proven
pre-existing against a 46f4564-built CLI on identical roundtripped
JSON; first surfaced because that corpus had never met the harness.
Matt's informal note on 72 (formal verdict pending): "the
velocity-linked brightness is there."
dsp REVIEW Awaiting: **73**, **72**, 62, 65, 38, 39.

Previous update: 2026-09-19 (dsp, trombone attempt 4 — the held-note round;
Opus 5 dispatch).
WORKING-TREE NOTE for the next session: docs/autonomy/GOALS.md carries
Matt's own uncommitted refresh (his steering action item) — never revert
or fold it into a run commit; he commits it himself.
**TROMBONE ATTEMPT 4 SHIPPED — REVIEW 72, backlog 74(a4)(a5)(a7) RESOLVED,
(a6) measurement attempt 1 of 2 spent with no mechanism standing, 71
superseded. Zero engine code.** Artifacts: tools/gen_trombone4.py,
candidate patches/audition/trombone1/trombone_attempt4.json (56 nodes,
gateable=1), 8 ears cells in renders/dsp/audition/trombone4/, report
reports/2026-09-19-trombone4.md.

**74(a5) — THE SUSTAIN WAS NEVER FLAT; A FIXED OUTPUT FILTER WAS EATING
IT.** Matt heard "sustain phases sound identical, just louder". Measured
BEFORE the voicing lowpass (probe Trans4), attempt 3's own settled-window
pp→ff centroid is F2 **+24.9%**, A#3 **+80.8%**, C5 **+53.0%**, F5
**+163.0%**; measured AFTER it, +0.8 / +18.5 / +33.8 / +27.0%. The fixed
1100 Hz corner Matt picked at attempt 2 was picked while the instrument was
stuck at forte (attempt 3's two gain stages), and a FIXED corner sitting
where the energy moves removes most of the movement. FIX: one CurveNode
(Vk) on __perf_v driving both SVF cutoff pins — 550 / **1100** / 1600 Hz at
pp / 0.8 / ff. Both ends SOLVED, not picked: the criterion is that the
instrument's own pre-filter pp→ff ratio must survive to the output, 0.8
pinned to Matt's number. RESULT settled pp→ff: F2 **+23.7%**, A#3
**+99.2%**, C5 **+89.9%**, F5 **+66.9%**; mf→ff (the half he pushes into,
and where attempt 3 was nearly dead) A#3 +0.5%→**+19.8%**, F5
+10.6%→+31.2%. >1 kHz share pp→ff ×3.45→**×102** (A#3), ×8.42→**×233**
(F5). Velocity 0.8 matches attempt 3 to **one 16-bit step** (knot-value
rounding from the steepener domain ±4→±8 at identical slope).

**ELIMINATED FIRST, BY MEASUREMENT.** The loop does NOT self-limit: drive
at the steepener input is linear in breath over 10.8–27.2 dB (A#3 13.9×
drive for 13.8× pressure). The steepener's transfer DOES scale with
amplitude (Δcentroid pp→ff +5→+56 Hz on A#3, +7→+125 on C5). So attempt
3's deferred work order 5 (steepener depth riding the breath) was never
triggered.

**74(a4) CLOSED — IT WAS MY OWN PROBE'S CLAMP.** A probe reading through a
{"tap"} is a GUARDED RefSource clamping every read to ±8
(dsp_value_source.h:164; only {"tap"} sets guard=true,
patch_loader.cpp:105-122). Attempt 3's "the lip rails 53–69% of every
cycle" could not have returned anything else. Raw-{"ref"} probe on the same
render: lip peak **70.0** where the tap said 8.000. In this graph the
engine clamp never touches the lip (Qn reads it by plain {"ref"}) and rails
**0.0%** on all three tap reads in sustain. What does hold is OUR Qn end
knot (37–69% of the positive half-cycle) — and widening it 8→256 at
identical slope moves the settled centroid **1.2%** and breaks F2's tuning.
Not load-bearing. **NO ENGINE ROUND NEEDED.**

**74(a6), MEASUREMENT ATTEMPT 1 OF 2 — THE OSCILLATION IS THE NOTE.**
Envelope modulation rate, two independent detrending methods (polynomial on
the log envelope; moving average), F2 through C5: measured/f0 =
**0.99–1.01** on every note. Bore round trip 0.50–4.01 (matches only where
p=1), lip resonance 1.23–1.41, envelope constants note-independent by
construction. No sub-f0 modulation exists. There is no rate to retune —
changing it means changing the pitch. BELOW the solved range, measured:
C1/F1 produce NO oscillation (rms 0.0000, lock 0.01–0.03), so all that is
audible is the attack thump and the bore ring-down = his "dullish impact
put thru a spring reverb"; the first note that holds is **D2** (lock 0.72),
the note he named by ear; D2/E2 run +27/+12 cents because every map holds
its F2 knot below midi 29.

**74(a7) FIXED WHERE IT WAS REAL.** Slices off the written partial 10 / 32
/ 37 at velocity 0.8 / 0.9 / 1.0 over 14 notes. Not the clamp (0% rails),
not a clean overblow (off-slices ×1.07–1.25 = an attack that overshoots and
settles); C#6 at 1.0 genuinely drops an octave. Fix: per-note ff ceiling
walked down until the ATTACK is stable, not just the settled pitch — **6 of
51 notes moved**, 45 unchanged, 0.8 untouched (his liked squeak intact).
TWO METRIC BUGS OF MINE found on the way: a fixed 60 ms slice is 2.6
periods at F2 and its autocorrelation lag search overruns the slice; and
counting DOWNWARD excursions catches subharmonic readings and pulled 25
notes' ff in on an artifact.

**NEW OPEN, 74(a8): NOTHING CHANGES DURING A HELD NOTE** — the honest
remaining half of Matt's verdict ("missing any swell or brightening *after*
the attack"). Measured at ff, the centroid reaches its value by ~200 ms and
is then identical to three digits from 0.2 s to 2.2 s (A#3 505/505/505/505).
Velocity is fixed for a note's life, so the breath is, so the timbre is.
Needs breath that MOVES while a note sounds — controller (blocked on
hardware, REVIEW 39), per-note breath contour, or PerformSource curve.
Design question, brainstorm before any build. Also new: 74(a9), pp is 4.5 dB
quieter (one constant if Matt says too much); 74(a10), above mf some notes
have almost no breath left (A#3's ceiling is 1.135× its mf).

Gates: 51/51 speak at 0.8 worst **1.9 c** (= attempt 3), no tuning or
speak-time regression, 51/51 ignite from silence at pp AND mf AND ff,
steepener drive inside its domain on every note at ff over the whole note
(worst 6.16 of ±8 — attempt 3 checked 4 notes over the settled window only
and saw 3.19), no clip (worst peak 0.900), level ceiling worst 0.331,
held-pair level match −0.00 dB, queue −0.1 to −7.4 dB vs oboe1 (0.1761).
GATE NOTE: the ignition gate is pitch-and-periodicity, never loudness —
the darker pp corner drops pp 4.5 dB, so attempt 3's 0.0015 rms floor would
have failed 22 notes that lock to the same cent and the same 1.00
periodicity. Floor is now the repo true-silence floor; pp level reported.
dsp REVIEW Awaiting: **72**, 62, 65, 38, 39 (71, 70, 69, 68, 67 resolved).

Previous update: 2026-09-18 (dsp, trombone attempt 3 — the blowing-harder
round; Opus 5 dispatch).
**TROMBONE ATTEMPT 3 SHIPPED — REVIEW 71, backlog 74(a) RESOLVED, 70 folded
to a Resolved stub with Matt's verdict verbatim. Zero engine code.**
Artifacts: tools/gen_trombone3.py, candidate
patches/audition/trombone1/trombone_attempt3.json (55 nodes, gateable=1),
8 ears cells in renders/dsp/audition/trombone3/, report
reports/2026-09-18-trombone3.md.

**74(a) WAS NEVER THE PHYSICS — IT WAS TWO GAIN STAGES OF MINE.** (1)
gen_trombone2.calibrate() peak-normalised INTO the steepener per cell
(in_gain = DRIVE_PEAK/peak) and un-normalised after it, so soft and loud
drove the nonlinearity to the same 0.800 peak BY CONSTRUCTION. Ablated, the
steepener contributes +6 Hz of centroid at S=0.2 and +65 Hz at S=3.0 — it is
a dynamics component the moment it sees dynamics. (2) the instrument was
permanently at forte: one global pressure had to keep G6 (72 kPa) alive
while A#3 plays in tune over 2.7–53 kPa, so there was no softer to go to.
FIXED: DRIVE_GAIN is a constant 1.0 (steepener sees the raw travelling
wave; verified the output trim does not move NL_in by 1e-6), and blowing
pressure is now PER NOTE and VELOCITY-DRIVEN, solved from each note's own
measured floor and ceiling (51 notes × 19 pressures, single note per render
from silence). Velocity 0.8 = attempt 2's exact working point, so the gates
hold by construction.

**THE METRIC, centroid pp→ff:** F2 +0.6%→+1.3%, A#3 −0.3%→**+18.5%**, F5
−0.4%→**+27.0%**. >1 kHz share ×0.98→×3.44 (A#3), ×1.02→×8.42 (F5). Level
pp→ff +4 dB→+23 dB. Attribution measured separately: the per-note span alone
buys +5.3%, removing the normalisation triples it. Both fixes were needed.

**LEVELLING DE-KLUDGED per Matt's directive** ("we want the sound to be
right, can always get bigger speakers"). Attempt 2's trim decomposes into a
15.1 dB smooth register ramp and ±4.9 dB of note-to-note scatter with 5.0 dB
jumps between ADJACENT semitones. Scatter = unambiguous artifact, kept in
full; 65% of the ramp applied. Register spread 0.0 dB → **5.3 dB**, top
louder. Queue WAVs still presentation-calibrated to oboe1 (0.1761).

**THE LIP WAS NOT THE LIMIT** (work order 4, one measurement, negative):
shut ~28% of every cycle even at pp, pressure rise rate up 28× (A#3) and 53×
(F5) pp→ff. No lip parameter touched. Work order 5 (steepener depth riding
the breath) NOT built — it was conditioned on 1–4 leaving brightness flat.

**NEW 74(a4), ENGINE TRAP LOGGED NOT FIXED:** the engine's global ±8 value
clamp (core/dsp_value_source.h:144) is inside the lip's operating range at
ff — displacement rails 53–69% of each cycle, and the POSITIVE rail matters
because flow is proportional to the opening. The top of the dynamic range is
squared off by a safety clamp, not by physics.
**MEASUREMENT NOTE, reusable:** a probe rendering an internal graph node is
multiplied by the voice mix gain (velocity × volume) like the real output —
this round's first pass read a soft note's lip as never closing because of
it. gen_trombone3.probe_signal() divides both out.
**STILL FLAT:** F2, +1.3%. Its breath span is 3.8:1 against A#3's 13.7:1 —
below ~26 kPa it goes sharp and then falls into a pressure hole. 74(a3).
Gates: 51/51 speak, worst 1.9 c, no tuning/speak regression vs attempt 2,
51/51 ignite from silence at pp AND mf AND ff, steepener drive inside its
domain (knots widened ±2→±4 at the identical slope, untested territory
attempt 2's normalisation had hidden), no clip, level ceiling ok.

Previous update: 2026-09-18 (dsp, trombone attempt 2; Opus 5 dispatch).
**TROMBONE ATTEMPT 2 SHIPPED — REVIEW 70, backlog 74 updated. Zero engine
code.** Matt verdicted 69 ("no, not quite yet. But it is way, way better")
with five items; this round answers all five. Artifacts:
tools/gen_trombone2.py (imports gen_trombone1's chassis — verified
BIT-IDENTICAL at matched config, max sample diff 0.0), candidate
patches/audition/trombone1/trombone_attempt2.json (43 nodes, instrument-
style, noteFaces, gateable=1), 8 ears cells in
renders/dsp/audition/trombone2/, report reports/2026-09-18-trombone2.md.

**THE HEADLINE IS A NOTE-NAMING BUG AND IT WAS MINE.** Matt: "low C does
not sound; lowest note is G2 (49Hz)." There is NO live/offline divergence —
mforce_cli and mforce_ui --gencheck agree to **1e-6** on the same patch at
every note midi 24–39, gatecheck gateable=1. mforce_ui names notes by the
HOUSE convention (octave = midi/12, main.cpp `baseNote = octave*12`, cites
comp REVIEW 19); gen_trombone1.py used scientific (midi/12−1). The two ends
of the conversation were an octave apart all round. His "C2" = midi 24 =
32.70 Hz, an octave below anything attempt 1 tuned; his "G2" = midi 31 =
49.00 Hz, which IS the lowest locking note in the render; his G6 = midi 79
= 784 Hz and the "tink" above it = midi 81+, attack transient only. Every
range claim he made reproduces to the Hz. gen_trombone2.py uses HOUSE names
throughout. **There is no shared note-naming helper in the repo** — backlog
74(g), do it on the next tool touch.

**RESULTS.** Solved range went 37 notes (midi 36–72) → **51 notes (midi
29–79, house F2–G6), all speaking, worst tuning 1.9 cents** (was 4.2 over
37). Regression-gated against attempt 1 across the shared range: zero
tuning regressions, zero speak-time regressions. Register level spread
**14.3 dB → 0.0 dB** via a measured per-note output trim in the patch (so
it applies live too). Queue loudness now matched to a measured library
reference — patches/library/winds/oboe1.json, sounding rms 0.1761, the
median of that family — worst cell **−4.8 dB (was −18.9)**, `soft` **+12.1
dB** on last round. Voicing lowpass INSIDE the patch, two corners staged
for his ears (1100 Hz candidate / 2200 Hz alternative, median centroid
737 → 498 / 611 Hz).

**SECOND REAL FINDING: ignition threshold climbs steeply with register.**
Measured as SINGLE NOTES FROM SILENCE (a note rendered after another
inherits the bore's energy and starts on it — which is why attempt 1's top
notes looked fine in a line and would not start alone): everything to B5
ignites at 8 kPa, C6 12, F6 36, F#6 52, G6 72. PRESS_MAP now carries
per-note breath support at 1.6× each note's own threshold, FLOORED AT 1.0
so nothing that already worked moves. This is also the mechanism behind
Matt's "high C in soft struggles to lock in" — attempt 1's flat soft
pressure sat below the top notes' threshold. Cost, stated: the soft end is
limited by the top note, so soft/loud are 31/50 kPa, narrower than attempt
1's claimed 4× (that window had been measured on a line).

**TWO HYPOTHESES KILLED, both my own metric bugs — the corrected metrics
are reusable and live in gen_trombone2.residual / lock_time.** (1) "sustain
is non-harmonic at the bottom": artifact of a FIXED 16-harmonic comb, which
at 49 Hz only covers to 784 Hz. Whole-band comb → residual 0.02–0.05 at
every note. (2) "low notes take longer to become periodic than to become
loud": artifact of a comb tolerance narrower than the analysis window's own
Hann mainlobe (2 bins). Fixed, with a period-scaled window: the bottom
locks in 10–50 ms and the TOP is the slow one (C6 90 ms). Four onset levers
ablated (breath grain, lip kick, steepener, overshoot amount/duration) —
none moves anything; making the onset SLOWER kills the bottom three notes
outright, so "scale the onset in periods" is measurably the wrong
direction. 2-attempt rule invoked on "separate low-register onset defect".
ALSO BUILT, MEASURED, REJECTED: a keytracked voicing corner. It does
flatten brightness-relative-to-pitch (13.4×/1.9× → 4.1×/1.8×) and it takes
the register spread from 13.1 dB to **36.5 dB** by stripping the low notes
of the only band they have energy in. Fixed corner ships.

**KNOWN WEAK SPOT, unchanged and re-measured at real level:** blowing
harder barely changes timbre — centroid 473 → 477 Hz, energy >1 kHz
0.0194 → 0.0210, from 31 to 50 kPa. Backlog 74(a), still the next
measurement, suspects unchanged. New 74(b): G6 needing 72 kPa where
everything below B5 needs 8 is not what a real instrument does.
For the record against the word "bright": 95% of attempt 1's energy is
below 1571 Hz and 22% above 1 kHz, against 67% above 1 kHz for the library
oboe. Whatever Matt hears as brightness, it is not HF content by that
measure.
dsp REVIEW Awaiting: **70**, 62, 65, 38, 39 (69, 68, 67 resolved).

Previous update: 2026-09-17 (interactive, Fable 5 with Matt; ~8% Fable quota — both
heavy dispatches ran on Opus subagents per the quota pattern).
**NONLINEAR BORE FRONT OPENED (Matt: skip survey, go Msallam classic).**
(1) Literature recovered (Opus, fetch-then-convert):
docs/research/nonlinear_bore/MSALLAM_DIGEST.md — ICMC'97 algorithm paper FULL
(all equations; the faithful IRCAM element filters the DELAY signal with
self-referential feedback, integer p>1, Lagrange ≤2) + JASA'96 measurements
FULL; Acta Acustica 2000 unobtainable (metadata-only HAL deposit) so lip
params / bell coefficients / rates are tier-C gaps. Load-bearing physics:
steepening acts on the RADIATED sound, not the lip loop → out-of-loop
placement is sanctioned. (2) Spec (Fable):
docs/superpowers/specs/2026-09-17-nonlinear-bore-design.md — zero-code
probe, DelayLine ratio ← Curve(tap of own input), d = D·(1−depth·x), no
shock handling (C&A: omitting it sounds better). (3) Probe BUILT (Opus,
1a11064, REVIEW 66): mechanism works first try; wiring fact — modulator
must read via TAP not ref (ref = one sample early; null proves lag exactly
400); gates all green (depth-0 bit-identical −231 dB; THD monotonic 6-step
ladder; 9× input → 9.5× distortion = effect appears with level). SPEC
CORRECTION: sine → Bessel ladder (PM, β match to 3 digits), not sawtooth —
expected for the shock-omitted variant, probe is a PM-style brightener.
KEY FINDING: distortion ∝ source frequency (doubles/octave) → bites bright
material only (oboe centroid +633 Hz vs flute +37 Hz at same depth) →
**lip exciter outranks more bore work for round 2**. Ears queue:
renders/dsp/audition/nlbore_probe1/ (8 cells; money A/B =
oboe_soft_d030 vs oboe_loud_d030, centroid 1121 vs 1705 Hz). Round-2
options in spec §Round 2: faithful delay-signal node, in-loop variant,
Smyth&Scott measured bell filters, lip round on the loop family.
**Same day, rounds 2+3 (Opus dispatches 3+4):** Matt verdicted 66 within
the hour ("definitely brightens in a brassy way" — element KEPT, folded to
Resolved). Pairing round (13bb1b5/b0accd0, REVIEW 67): steepener onto the
memoryless-lip carriers, gates green (2.3× >2 kHz loud-vs-soft, pitch
unmoved ±1c, steepener now INSIDE the tube pre-bell), BUT built on the
known-inferior lip → Matt issued the **BEST-POSSIBLE-IN-ROUND-ONE rule**
(verbatim in WORKFLOW.md §Steal-first, bd2666f + ca96c24; also in Claude
memory): known targets get the full state-of-the-art assembly round one,
stealing included; iteration is for true experiments only; probes = prep.
Agent self-relabeled 67 as optional prep. Harness-notes correction: the
09-12 lip is NOT uniformly sharp — 34c flat C3 → 22c sharp C5. Open
oddity: STK carrier goes darker at weakest steepener settings (one cause
eliminated, real one unknown; no queued cell in the dip).
**ONE-MASS LIP ROUND (the real brass attempt; spec
2026-09-17-onemass-lip-design.md, commit 08f00d7, REVIEW 68): the
published dynamic lip IGNITED AND LOCKED, zero engine code.** Berjamin
arXiv:1511.04247 §3.1 Eqs. 43–49 + Table 2 fetched and transcribed
(digest's uncertain y-row CONFIRMED); r=√(mk)/4 ⇒ Q=4 exactly ⇒ constant-Q
keytracking licensed. Bernoulli √ ships real (CurveNode power knot a=1
p=0.5; only approx = one-sample tap lag for the fixed-point solve; flow
subgraph validated ≤0.07% vs Python closed form). Ignition/lock C3–C5
+18…+33c over a 16–46 kPa pressure window; brightness 2.1× >1 kHz with
pressure, steepener adds 2.6× on top independently. DESIGN FIND (bore not
lip): one-period loop ignites but plays wildly sharp — lip reflection
measured 0.38, loop lacks authority; THREE-period bore fixes it and the
lip ratio then SELECTS THE PARTIAL (3.03/3.28/4.13 at ratio 0.8/1.0/1.2)
= the register mechanism in existing nodes. Controls are decisive:
mass-deleted graph is SILENT at every pressure; memoryless reed on the
same bore sounds only the bore fundamental, ignition window 1 scan step
vs the lip's 16. Known + deferred: lip cells consistently sharp, ratio
~0.77 would center (timbre first per spec). Ears queue:
renders/dsp/audition/onemass_lip1/, 7 cells; money A/B =
lip_r080_loud_st1 vs reed_r080_loud_st1 (only difference: the valve has
mass). Opus-meter total for the day ~945k across 4 dispatches; Fable
coordination stayed light. dsp REVIEW Awaiting: **68**, 67 (optional
prep), 62, 65, 38, 39. Plucks/plates paused til next week (Matt); Matt on
catch-up/cleanup + GOALS refresh.

Previous update: 2026-09-16 (interactive morning + "go dsp" run, Fable 5 with Matt).
**Morning (interactive):** clarinet_att2 passage flutter root-caused —
NOT an engine bug: per-note reset proven thorough; ignition at short
prepared durations is marginal and noise-realization-sensitive, masked
everywhere by the fixed virgin RNG seed (backlog 72; gates/sweeps rate
ignition on one lucky seed — needs a notes-2+/varied-seed robustness
probe + an audibility floor). Matt's minSec-floor fix (both attacks
0.1) validated ~9-10/12 across random states; his UI zero-flutter =
Generate determinism (fresh instrument per gen = same seed). Matt's
envelope pass committed (72be940): trailing hold-at-zero stages
REMOVED from winds+viola + releases to 0.1 — they were stealing
duration (percent stages), the staccato culprit; manifest refrozen.
**"go dsp" run (verdicts 58/59 folded, 2768f90):** (1) BandedWG cutoff
click = the fixed 0.4 s tail cutting ringing bars → **adaptive
ring-out shipped (63b, 5dd07d7)**: voices ring to −60 dB or +8 s cap,
offline + live; loader now honors seconds > score end (was silently
overridden — ring headroom was inexpressible); two deliberate
refreezes, quiet-enders byte-identical both times; 4 verdicted
bandedwg pairs re-rendered click-free. (2) **Percussion round 1 →
REVIEW 60**: gen_bwg_perc1.py, F&R mode sets at 48k, 7/8 cells pass
(marimba/xylo/vibes/chime/tomdrum/woodblock/glass; tbar_bright
machine-culled). (3) **Donor papers → REVIEW 61** (backlog 73 done):
DAFx'04 brass NO-GO (linear bore; mouthpiece two-pole steal noted);
Chafe 2D-mesh GO with a zero-code 1D Pierce probe proposed first.
INCIDENT (repaired): worktree removal followed third_party junctions
and wiped the vendored libs; restored (rtmidi re-vendored 6.0.0),
remaining worktree junctions de-linked, builds green. Details:
dsp/reports/2026-09-16-dsp-run.md.
**Same-day round 2 (Matt verdicted 60/61 + BandedWG re-audition within
hours):** ring-out KEPT per his conditional (helps musical notes,
trivial cost); residual hard cutoff was the 8 s cap on near-lossless
resonators → cap now FADES 80 ms (offline+live; piano_seg the one
deliberate DIFF, refrozen). Perc ROUND 2 shipped (gen_bwg_perc2.py →
REVIEW 62): per-mode gains = constant-T60 frequency curves (round 1's
fixed gains made T60 ∝ period = gong-everything), measured calibration
/4, 7/7 pass, woodblock 0.13 s → chime 6.4 s. Bowed "cut before bloom"
explained: gen scripts bake bow-stop at 4.5 s (seconds-mode AdsrEnv),
not a physics threshold — next bowed round uses gated/hold envelopes.
NONLINEAR BORE GREENLIT ("I'm counting on you.. let's do it!"); 1D
Pierce probe approved. Passage strings gained REST tokens (R+duration,
dc4fce3, implemented by an Opus subagent — quota conservation pattern,
works). Session-start rule added to CLAUDE.md: sweep stale renamed UI
exes. dsp REVIEW Awaiting: 62, 38, 39.
NOTE: Matt's running UI predates today's builds — relaunch picks up
ring-out + cap fade + seconds-honor + rests live.
**Evening: MESH2D CAMPAIGN, both stages, via Opus subagents (quota
pattern, 2 dispatches, ~376k on Opus's meter, both verified by
coordinator).** Stage A (74c81c3): Mesh2DSource ported from STK —
positions/decay as pins, 64x64 cap (STK caps 12x12; extended ref class
proven sample-exact vs stock first), validated to exactly one 16-bit
LSB across 6 cases at 48k; null gate 79/79. Stage B (9ecdd82):
edgeMode setting (0 = STK verbatim default, byte-identical proven;
1 = Chafe allpass with edgeFc/edgeR PINS) → pie-pan/Pierce-gong as
pure patch wiring; 8-cell queue = REVIEW 64 (mode count 2-4 → 11-84,
mode-set downshift = Chafe's published detuning; bar_piepan 16.9x
upwelling; drive = safety knob, naive drive runs away — time-varying
allpass non-passive, global edgeR pumps coherently). Speaker incident
earlier folded: stale v1 pierce WAVs in queue → gen-scripts-own-dirs +
level-ceiling rules in WORKFLOW.md (59062f7). Pierce 1D probe =
REVIEW 63 (upwelling 2.35 at s8k). dsp REVIEW Awaiting: 62, 63, 64, 65,
38, 39. Matt verdicted 64 same evening (underwhelming near-identical
chimes; metrics measured -60 dB trivia — feature-audibility gate born
from it) → ROUND 2 same night (faf2cff, REVIEW 65): REAL Pierce/Van
Duyne passive filter recovered from patent 5,703,313 + Faust fi.apnl;
FINDING: the literal published recurrence is NOT passive in discrete
time (2.43x energy creation) — shipped form adds an energy-preserving
state rescale, measured 1.000000000, hard engine_tests gate; new
PierceFilter node + Mesh2D edgeMode 2, full drive, twin-controlled
cells with real audible spread (ΔhB to +95.7 pp), pierce1d_2 pluck
thread included. Homogeneity fact recorded (velocity can't change
timbre through a passive piecewise-linear spring). Next on verdicts: real instrument attempt on the winning
geometry/edge behavior; passive Pierce filter structure (the
principled fix for the pump); perc axis round; nonlinear bore front.

Previous update: 2026-09-14 (fall cleaning, Fable 5 with Matt). **Steering-meeting
execution DONE except GOALS refresh (Matt's) and the audition-dashboard
brainstorm (own session).** What landed (commits aa50c49, dda11f3):
- **Dirty-patch verdict: KEEP** — piano_default (curve renames + added
  Reverb) and viola_default (PerformNode re-save) are Matt's, committed;
  cello/oboe1 render-neutral churn committed with them. chords/ +
  passages/ (UI transport save areas) gitignored; stray hysteresis plan
  doc tracked.
- **BACKLOG nuke** — dsp 864→330 lines: 6 items to ledger, 13
  maybe-somedays to IDEAS.md (ledger keeps ids resolvable), survivors
  compressed to live threads; comp untouched (already tight) but gained
  item 23 = the steering comp-restart campaign. Open Matt questions 3d
  (pan law) and 22 (Vibrato envelopes) still parked — he skipped them.
- **WORKFLOW.md rewritten** for the single-dev run contract: personas
  retired, campaigns carry stop conditions, steal-first model + rate
  discipline, machine self-verdict REJECTS, ears budget ≤ 20/run,
  dashboard Keep→pending/<campaign>/ semantics, IDEAS housekeeping rule.
- **Baselines set-cover cull 116 → 42** (+9 prof_additive ladder): 65
  redundant patches → patches/old/baselines_culled_2026-09-14/; keepers =
  one per node type/loader feature + harness citations + repro patches.
  Additive1 culled (comp uses piano_default). FIND: Biquad/BowTable have
  ZERO tracked gate coverage (only gitignored sweep cells use them) —
  Matt: STK cells go in once they do something good.
- **Null gate refit**: skips library/voice vowels except sing_alto_A +
  speech_c_AE (identical mechanism, formant freqs only — Matt); manifest
  refrozen 196 → 77 entries, verified 77/77 (2 RENDER_FAILs = deliberate
  keepers FormantSequence1/NATest1). Gate wall-clock drops ~60%.
Ears queues UNCHANGED and standing: stk_port1, stk_bowed_ext1/2,
hysteresis_bow*, valve2, string_harness1/2, brass_harness1, r5b axis
tour, feedback_inloop1, + the whole comp REVIEW queue.
**Same-day addendum — Matt verdicted the whole dsp REVIEW queue (1 hr),
verdicts folded + executed:** inloop1 REJECTED (patches→old, renders
deleted; formants stay outside the loop); ext1 hysteresis cells rejected
harsh (→old; next hysteresis round needs ext2's leaky-stick fix);
ext_perf runaway vibrato root-caused (±0.27 summed into neck ratio) and
fixed canonically (±19c C4 measured), "most stringy" keeper; ext1
saxophony + all ext2 + fb5b cells KEPT in place for Matt's hand-play
(his call: heap, no copying to pending/). r5b thread closed, no neighbor
round. Decisions executed: **pan law → unity center** (mixer.cpp,
verified exactly sqrt2 x old, WAV==UI loudness; manifest refrozen 77/77,
UI relinked via rename — Matt was live in it); **Vibrato per-note pins
already work** via dynamicPins, render-proven, backlog 22 closed, no
code. IR pick DROPPED (rotten links) — STK round 3 proceeds from
Larson's measured LTAS curve instead (biquad-stack body fit, A/B vs
bachd.wav) = campaign item 70. **Round 3 DONE same evening:**
fit_larson_body.py (4-pair fine LTAS, 14-biquad fit, err 0.68 dB median
at the data's 2.8 dB spread) + gen_stk_bowed_ext3.py → 4/4 cells to
renders/dsp/audition/stk_bowed_ext3/ (nobody / Maestre / Larson body /
btd+Larson full recipe), all +0c locks, in-graph LTAS validated
(+0.83 dB median vs target) = **REVIEW 58**. AF sax still blocked on
Discord annoyance; offered alternative: green-light building from the
video table.
**09-15: STK round 4 — remaining families ported (Matt: "move on to
remaining families").** Saxofony/BlowHole/BandedWG reference drivers
built (tools/stk_ref/), ground truth at native 22050, ports validated
per rate discipline: saxofony ±1c all locks, noise-free 0.04 dB C4;
blowhole C3-C5 locks (per-note comp anchors; model is register-BISTABLE
at 22050 and the ref flips against itself run-to-run — parity); bandedwg
struck bars/bowls with EXACTLY matching decay rates + bowed bars
blooming ~4 s like the model (bar_bowed not ported — ref silent).
Ears queue = **REVIEW 59** (stk_port2, 7 A/B pairs). En-route engine
finds: **backlog 71** — a shared stateless node in loop context
double-advances the stateful chain behind it (env at exactly 2x; repro
pair patches/baselines/double_advance_{ok,bug}.json; minimal case IS
guarded — needs the advance-list context; 65/66 cluster); 1-sample
seconds-mode envelope stages never fire (2+ do); engine BowTable folds
dv* into its output. Manifest refrozen 79 entries with the repro pair.
dsp REVIEW Awaiting: 59 + 58 + 38 + 39. Campaign next on verdicts:
extension rounds for surviving families, leaky-stick hysteresis round,
48k canonicalization, or the tone-shaping filter round.
**09-15 fronts 2+3 (same run): backlog 67 + 63 SHIPPED.** 67: loader
warns once per (type,key) on unknown param keys — allowlist
corpus-verified (133 patches warning-free; all 16 flagged classes
proved branch-consumed, none dead; positive control fires; gate 79/79
byte-identical). 63: voice tail allowance kVoiceTailSec=0.4 s —
voices render/live past duration so in-voice reverb/filters ring out
(piano_default tail now ends at -66 dB, zero step; containment check
moved to tail end; live path inherits via StreamingVoice.durSamples;
15/79 gate patches ring past duration, manifest refrozen). The
trailing hold-at-zero-stage workaround is obsolete. Run stops here
per contract (3 fronts): everything else dsp is verdict-gated.

Previous update: 2026-09-13 evening (steering meeting + STK port day, Fable 5).
**STEERING MEETING HELD — process decisions, execution deferred by Matt
except the STK port directive.** Decisions (capture only, action items
NOT started): (1) Dipsy/Wolfie personas retired; ONE dev runs both
lanes; lanes survive as backlog namespaces, campaigns = big backlog
items. (2) Run contract: machine gates may self-verdict REJECTS
(logged, patches to old/, renders deleted) + near-duplicate culling by
perceptual distance; ears budget <= 20/run honest count; Keep on the
future dashboard writes to pending/<campaign>/ (Matt's live-play bench;
runs still never write pending/ — the dashboard writes as Matt's
click); library promote from bench stays Matt-only; stop conditions
live in each campaign item. (3) "Audition dashboard" = own brainstorm
session later (queue aware, play/A-B/verdict buttons writing structured
verdict files → future Matt-likelihood ranker). (4) Comp restart: back
to Mary-Had-a-Little-Lamb scale WITH harmony from bar one,
piano_default as the comp instrument (vanilla, non-fatiguing). (5)
DSP model = STEAL-FIRST: vendor proven code, render its WAVs as ground
truth, port into the graph, extend from the proven point; ML-ears and
comp-scorer get cheap discrimination backtests before trust. (6)
Cleanup to come, collaborative and brutal: BACKLOG nuke = dead/done/
moot items die, "maybe someday" moves to IDEAS (NOT keep-only-remembered);
baselines cull 188 -> ~40 by node/feature coverage (set-cover proposal
from me, axe swung by Matt). (7) Null gate measured today: 290 s at
--jobs (parallelized 09-05); batch-mode mforce_cli remains the lever
(95% spawn/load). Gate found the 09-08 mystery edits CHANGE AUDIO:
piano_default + viola_default DIFF vs manifest (cello edit is
render-neutral); manifest also stale (8 NEW). Deferred with the rest.
**STK PORT CAMPAIGN — 4 families ported and validated in one run (Matt's
directive before leaving).** STK checkout at ../stk; minimal reference
drivers tools/stk_ref/ (bowed/clarinet/flute/brass, 48 kHz, no RtAudio);
ground truth in renders/scratch/stk_ref/ (+ JOS Mohonk05 14-WAV bowed
set archived there). Engine: NEW nodes **Biquad** (raw coefficients +
resonance mode with frequency/radius pins) and **BowTable** (Smith
friction curve, slope/offset pins) — registry-only, null gate 186/188
(same 2 pre-existing dirty-file DIFFs as the morning run, engine
byte-neutral). Ports: patches/sweep/stk_{bowed,clarinet,flute,brass}_port/
via tools/gen_stk_*.py, each auto-compared to reference (f0 cents,
envelope corr, gain fit, harmonic profile). RESULTS: **bowed +0.0c on
43/43 locked slots incl. reproducing STK's own C6 multiphonic and
wolves; clarinet +0.0c all 40, noise-free variant 0.09 dB exact; flute
noise-free exact <= 0.11 dB; brass reproduces the reference INCLUDING
its silences (STK brass barely speaks at 48 kHz — corroborates the
native harness ignition-window finding).** Noisy-variant deviations =
noise realization (STK seeds from clock), documented. Roundtrip 33/33
clean after adding noteFaces (UI save consolidates faceless
PerformNodes — same known class as the wiring_smoke find). Full record:
docs/research/stk_port/STK_PORT_NOTES.md; spec
docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md. EARS QUEUE:
renders/dsp/audition/stk_port1/ = 4 port-vs-REF A/B pairs + README
(question: does the port sound like the reference?). NOT COMMITTED —
awaiting Matt (working tree also still carries the deferred dirty-file
question). Next when Matt returns: his ears on stk_port1, then the
extension round (compensate ON, hysteresis-junction swap-in, drive
exploration past STK's ranges) and/or more families (Saxofony,
BlowHole, BandedWG, Mohonk torsion/dispersion features).

**EVENING ADDENDUM — Matt's ear caught a bogus baseline; fixed.** The
Maestre body coefficients (and other fixed numbers in STK) are designed
at STK's NATIVE 22050 Hz; verbatim at 48k the body resonances shift
2.18x up (261/525/1072/1287/1839 -> 569..4004 Hz). Fixes: drivers take
a rate arg (22050 canonical refs rendered); gen_stk_bowed.py default =
**body48** re-realized sections (z -> z^(22050/48000); matches native
body to 0.46 dB max 100-4000 Hz; --verbatim keeps the STK-null form);
audition pair restaged = body48 port vs STK@22050. Measured: STK brass
at 22050 SPEAKS at C4/C5 (48k silence was partly rate artifact). Queued:
brass lip-radius + clarinet OneZero rate corrections. Port lesson:
every fixed constant must be sorted physical-Hz vs normalized-frequency.
**Round 2 of Matt's ears → CANONICAL bowed mode** (gen_stk_bowed default;
--verbatim = old form): his two complaints measured+fixed — (a) "high
harmonic drowns fundamental" = STK@48k itself double-slips (H2 +31..46dB
vs 22050's Helmholtz +18.6; per-sample ADSR rates halve gestures at 48k
+ string filter under-darkens) → canon re-realizes the 22050 model at
48k (pole 0.7600, 22050-second gestures, f-calibrated comp table, 2
iterations); (b) clicks = literal-seconds envelope truncating short
Passage notes + voice cut mid-ring at note end → duration-adaptive
pinned stages + LoopGate 90ms loop-closure on NeckDelay read-gain
(MForce-native; STK truncates too). Canon vs 22050 ref: C3-C5 ±5c,
envCorr ≥.98, C3/C4 harmonics match ~1dB/harmonic, click gone (0.018
residual = attack transient). C6/C7 fragile in ref too. Roundtrip 24/24.
bowed_port.wav in audition = default_canon.
**Round 4 (Matt away): CANONICAL PASS ALL FAMILIES + BOWED EXTENSIONS.**
Bowed verdicted by Matt ("port matches REF"). Clarinet canon VALIDATED
(±2c C3-C6 all variants; 48k biquad refit of the OneZero, zero pinned
11025; parabolic-autocorr f0 added after catching integer-lag
calibration chasing). Flute canon VALIDATED zero-calibration (±1c
C3-C6). Brass canon PARTIAL, root cause PROVEN by experiment: lip DC
gain at 48k = 2.13x native (rate ratio) even peak-preserved; unblocked
mouth DC times the ignition window (C4 speaks with DC-preserving b0);
clean fix = keytracked DC-zero in Biquad resonance mode — parked, brass
superseded by junction roadmap. EXTENSIONS ROUND 1: 13 zero-code cells
on the canonical base retuned to EQUAL TEMPERAMENT (port inherits STK's
own +7/+25/+49c sharpness; extensions don't — control = +0c). 12/13
pass gates+dedup → renders/dsp/audition/stk_bowed_ext1/ (README inside).
HEADLINE: **hysteresis stick/slip junction inside the proven chassis
locks ±7c across C3-C5** — the in-tune multi-octave lock the native
harness never achieved; the chassis was the missing half. Also: torsion
coupling (passes, +5-16c pull), dispersion allpass (re-comped, clean),
bow-bite pressure gesture (live pin, STK can't), body A/B/ablation.
Roundtrip 59/59. EARS QUEUES: stk_port1/ (4 families, canon pairs) +
stk_bowed_ext1/ (12 cells). Nothing committed all day — Matt hasn't
asked; tree carries engine nodes + tools + patches + docs.
**Round 5 (Matt verdicted round 4, approved round-2 plan): PROVENANCE +
BRASS FIX + EXT2.** Matt's verdicts: families match refs; STK skeletons
underwhelm everywhere (flute rubbish both sides, clarinet loses to oboe
harness, gold strike elusive); brass UN-PARKED ("we have no brass at
all"). Mohonk WAVs identified = Peder Larson Music 421 project 2003
(writeup archived in docs/research/stk_port/ + bach/staccato WAVs);
code unpublished → exact WAV reproduction impossible (his cello-body IR
+ bridge coeffs + gestures are lost), mechanism-exact fully possible
(all constants documented). New rate discipline: validate ports AT
native rate; canonicalize separately. Brass canon COMPLETED patch-level:
fixed 12.5 Hz low-shelf (DC 0.461) before the lip — DC excess is
constant 2.17x across notes — ignition map now matches ref (C4/C5
speak); +29c C4 lock offset left for trim. EXT2 (cello register, from
Larson's numbers): 5/7 staged → renders/dsp/audition/stk_bowed_ext2/ —
control2 +0c, width_h2 (his #1 feature), tors_phys (Z/Zt=0.306), disp16
(D-string range by design), larson_btd (b+t+d, +25c retunable). Killed:
2 hyperbolic cells, diagnosed (stick must leak ~2%, plateau must be
continuous) → own round. Body deferred: Matt ear-picks a public cello
IR (links in ext2 README), then biquad-stack fit. Still nothing
committed.
**Late 09-13 addendum:** QWERTY lower zone fixed to ONE octave down
(was two; tools/mforce_ui/main.cpp s_qwertyMap; zones now overlap a
fifth, shared keys show both hints "Q ,"); UI rebuilt, uncommitted with
the rest. Also standing offer accepted in principle: late-night ideas
get dictated and filed to IDEAS.md dated (runs read, never act).
**NEXT SESSION = steering-meeting EXECUTION (Matt, end of 09-13):**
workflow retooling, housekeeping, cleanup — the deferred action items
from the morning meeting (WORKFLOW.md rewrite around the single-dev run
contract, queue bankruptcy w/ BACKLOG→IDEAS rule, baselines coverage
cull to ~40, null-gate refreeze after the piano/viola verdict, GOALS
refresh is Matt's). STK campaign TABLED mid-thread — round-3 entry
point when resumed: fit Larson's body filter (measured curve in
STK_PORT_NOTES; 3-pair LTAS ratio: +20 dB @150 Hz block, −10 @500,
+15/+11 @800-1200, −22..−30 above 2k) as a biquad stack on the chassis,
A/B vs bachd.wav.
**Evening verdicts (Matt), session ends here:** brass canon = "roughly
the same, only C4 actually producing a note" — STK-brass chapter closed
as parity-with-a-poor-reference. Key insight recorded: "brassy" needs
NONLINEAR bore propagation (wave steepening at forte); linear bore +
valve = "saxy not brassy" — which retro-explains the native brass
harness verdict exactly. Lightweight surrogate = amplitude-dependent
waveshaping along the bore (Shaper + drive ← level follower is the
native idiom) — required ingredient for EITHER brass path (donor
survey: OpenWind/MoReeSC/NESS lineage; or native lip on the loop
family). Larger context Matt named: the well-done physical models
(VL1, SWAM, Arturia) are closed; the open ones are skeletons — the
campaign's real yield is the validated chassis + the documented list of
missing ingredients (nonlinear bore, real body IR, finite bow width),
not finished instruments. Ears queues stand: stk_port1, stk_bowed_ext1,
stk_bowed_ext2 (+ hysteresis_bow/valve2/string_harness backlogs).
Nothing committed 09-13; meeting action items still deferred.
**Round 3: legato mechanism verified** — STK slurs = setFrequency on the
sounding string (bow held, no reclear): rise times 0.36/0.03/0/0/0 s vs
detached 0.36/0.14/0.04/0.04/0. Ours re-ignites every note (fresh voice,
state cleared) → staccato 8ths. Graph is already legato-capable (per-
sample frequency pin retunes delays); the gap is note delivery (voice
reuse without reset) = Slide/HammerOn/PullOff articulation family →
backlog 68 brainstorm input, backlog 53 mono-mode twin. NOT built.
bowed_REF_legato.wav staged. Full analysis in STK_PORT_NOTES §LEGATO.

Previous update: 2026-09-13 (render-unification day, Fable 5 with Matt; session
restarted clean after this update — pick up from here; next sesh =
steering committee meeting).
**09-13 digest — tap-loop UI bugs root-caused, then the two-renderer
Generate deleted at Matt's direction.** Morning: Matt's harness live-play
anomalies traced (systematic-debugging): (1) collect_envelopes couldn't
see through RefSource (taps + secondary-consumer wraps expose no
descriptors) → string loop-gain env (Ampl_env on tap-only NutDelay)
stayed ungated → key-up ignored, notes = spinner length (brass gated
fine — Mouth is walkable; Matt confirmed the split). (2) Passage Play
re-rendered via the UI-graph pass (no advanceList → tap tails frozen →
flat waveform + silence + re-render delay). Fixes: walk follows
RefSource::source; Play = dumb replay of last Generate (no staleness —
generate/tweak/Play is Matt's A/B workflow). Then the big one
(spec+plan 2026-09-13-render-capture-unification): generate_unified =
serialize editor graph → load_instrument_patch_json (no temp file) →
play_note with per-node CAPTURE (engine sums each clone's current()
at timeline offsets, legacy addValueListener reborn) → strips + play
buffer from ONE engine render. render_waveforms/passage/both
*_authoritative DELETED (−211 lines); chords/cache/--dump-playback on
the in-memory loader. Proof: UI output ∝ CLI at exactly 1/√2 pan law
(worst 2.5 LSB, 3 families); NutDelay strip live (was silently flat);
null gate 80/80 hash-identical; --gatecheck + --gencheck are the
standing harnesses. Matt's morning verdict: "works great." He deleted
oboe1's orphan Morph_env (found by the truthful strips). STRING VERDICT
(post-fix, 09-13, full log in STRING_HARNESS_NOTES.md §Round 2 ears):
multi-attack GONE except C5/C6 on some cells (possibly BEATING not
retrigger); attack+sustain harsh/saturated "distorted guitar played by
a bow"; ALL cells sound TWO OCTAVES per note (near-equal weight at
4.0x gain); 32/36/05 = reference cell (stability winner + only clean
C6); b48 column confirmed dead (thump-then-sine / breath). Measure
next: f0-vs-2f0 spectrum across gain band, C6 beat-rate vs detune;
drive-onset decoupled from loop gain stays the saturation lever. BRASS
prelim (pre-fix): "very saxy not brassy," squeaky/out-of-tune high,
buzzy vowel low — maps to documented +22-38c sharp, C6+ non-lock,
spectral-balance gap; per-note pressure-window map (probe was C4-only)
is the offered zero-code next step. No promotions; queues in place
pending the steering meeting. Backlogs 65/67/68 unchanged.

Previous update: 2026-09-12 (harness-campaign day, Fable 5 with Matt; session
restarted clean after this update — pick up from here).
**09-12 digest — string + brass harnesses, specced/built/measured in one
day (Matt's directive: state-of-the-art first, sweep, ML ears).** Both
built with ZERO new engine code. STRING (waveguide two-segment, native;
specs 09-11 + addendum; tools/gen_string_harness1.py): IN TUNE ±22c
across C2..C6 all bow positions with capture 0.25; build lessons —
compensate-on-NUT = exact two-delay tuning, cycle NEEDS dcblock (bias
DC → 45 Hz relax "note"), output post-DC. Round 2 drive sweep
(string_harness2): Matt's flutter diagnosis confirmed numerically
(1.2x crit = 98% envelope dips); GAIN is the flutter knob, sweet spot
3.2x crit → flutter 9%, pitch +5.7c iqr 0, ML ears 1.075 = campaign
best (additive-viola lineage started at 1.28). Start cell
str2_g32_b36_h05. BRASS (outward-striking lip = morph-pin N=2 slice;
spec 09-12; tools/gen_brass_harness1.py): pressure must enter POST-cup;
ignition is a WINDOW (linear scan, not bisection); lock probe
pitch-gated; locked register C3-C5 (t100 column), ML 1.65-1.9, flutter
<2% but +22-38c static sharp = intonation trim + spectral-balance
knobs open (blend test moved total but worsened harm — stopped per
no-blind-tweaks). Docs: {STRING,BRASS}_HARNESS_NOTES.md (component
notes, reed-diff tables, hand-tweak guides);
tools/measure_stability.py = new flutter/pitch metrics in the
pipeline; trumpet ML config+reference built (ml_ears/configs/
trumpet_bb.json). Matt's round-1 ears: "far from good but extremely
promising - moments of stringiness, one amazing moment of saxiness";
he's playing string round-1 winner + brass q15_m3 by hand. Queues
AWAITING EARS: string_harness1/2, brass_harness1 (+ hysteresis_bow*
and valve2 verdicts still open). Backlogs: 65 (root sensitivity, +
1-sample ordering residual) OPEN; 67 (loader unknown-key warning)
OPEN; 68 brainstorm pending (weekend meeting postponed). Prior 09-11:
duration perform field + Live checkbox landed (backlog 64 Tier 1);
transport tab rework + passage/chords save-load; bow rounds 1-5
(period-1 capture rule) — see 09-11 commits and hysteresis_bow*
READMEs.

Previous update: 2026-09-08 (marathon interactive session, Fable 5 with Matt; he
may be away ~4 days — Fable usage limit — check powered-by at session
start per the model-switch protocol).
**09-08 session digest — the junction-state day.** Library went NUMBERED
(oboe1/2, flute1, bassoon1 replace _defaults; f330497). Brass probes:
series valve (round 1) and drive-coupled valve (rounds 2-4, valve2) both
FAILED to leave the reed basin — ceiling named: memoryless single-input
junction = reed family, Matt accepted ("we can make any reed you can
imagine"); verdict log docs/research/feedback_sweeps/VALVE_VERDICTS.md.
Two engine bugs found with repro pairs and FIXED: backlog 66 compensation
walk (param-pin descent + member-cap eviction; walk now inputs-only, cap
16; 4c34acf) — 65 (root-sensitivity, ±1-sample ordering ambiguity)
remains OPEN with baselines. Backlog 67 NEW: loader should warn on
unknown keys (would make old-binary/new-patch mismatches self-announcing; NOTE: the 09-08 confusion it was filed under turned out NOT to be a stale binary - Matt was on a fresh launch and the feature is just invisible by design).
**HYSTERESIS JUNCTION SHIPPED same day as spec** (specs 2026-09-08-
hysteresis-junction + junction-2d; plan + tasks f556358/3586751/dc8dd21/
48c70fa): Shaper stick/slip mode, breakaway/capture pins, Stick/Slip
editor labels, null gate 195 clean, open-loop state-machine test. Bow
round 1 = double-slip buzz (no bow); round 2 added BOW-VELOCITY BIAS
(Bow_bias env + Bow_sum; drive PINNED 1.0 — upstream bias gets
drive-scaled into clamps; loop gain via Ampl_env maxValue) → measured
mode-locking chain (sub-octave period-2 cycle → damp x3 → sawtooth
corners → **delay ratio 0.5 lands it ON PITCH: peak/f0 0.994 C3..C7,
1.97 corners/period**, first bowed-mechanism oscillation at pitch;
a3b4ef7). Matt's verdict on those cells: "syn-drum attached to an oboe"
— mechanism right, character wrong; suspects logged in
audition/hysteresis_bow2/README (bias-step percussion, slow bloom,
wind front end). NEXT: bow round 3 from those suspects; 2D junction
gate = zero-code morph-pin probe (spec §2), Matt-gated. Awaiting Matt:
both bow queues + valve2 queue verdicts. Loose end: piano_default /
cello_full_range / viola_default modified in worktree, Matt says not
his (recent) doing — uncommitted, uninvestigated. CLAUDE.md gained the
no-wrap-ups hard rule (ab21d1d).

Previous update: 2026-09-07 (marathon interactive session, Fable 5 with Matt).
**09-07 session digest** (details in the docs cited): UI shipped
copy/paste (OS clipboard, cross-instance; commit 7a1b6ac), plus fixes:
group out-pin killed by Note-face refs, GUI crash from corrupt __output
positions written by save-while-drilled (both 9f15efb), auto-listen on
drill-in REMOVED (cf04155 — the ears now stay on the patch; the tap
surprise cost a day of confusion). oboe_grouped promoted to
library/winds/oboe_default (9d04b64, wormhole install completed).
Reference-match sessions: CLARINET (kSfEDb1cMAw, D maj) and BASSOON
(_t2q0lsUl4k, D maj 8vb) — full records + PLAIN-LANGUAGE LESSONS in
docs/research/feedback_sweeps/{CLARINET,BASSOON}_REF_ANALYSIS.md and
LOOP_PATCH_ANATOMY.md (the proto-doc for Matt's lane MDs). Headline
lessons: WAV peak exactly 0.70 = pinned at the soft-clip (0.999 x pan
0.7071) — volume is the un-distort knob; low-register damp choke
masquerades as slow attack (knee near a=6-7); the fundamental MASKS loop
hash — boost the honk, never strip to it; metrics find addresses, ears
pick furniture (Matt's hand patches beat every optimized cell). Matt's
verdicted keepers: clarinet c8_fast_breath_tweaked +
c8_h130_v25_tweaked, damp res ~0.66-0.67 = HIS overblow fix
(pending/clarinets/); bassoon_attempt still champion, he'll "honk it up"
by hand (honk address: ~500-540 Hz formant hump, ref spectrum peaks at
H3). Backlog 57-64 added (toggle A/B node, Crackle revisit, live-click
incident, save backups, settings audit + housekeeping sub-lane, noiseBed
scoping, voice tail allowance, duration-aware articulation two-tier).
Unreproduced gremlin watch: two knot-save losses + density=2000
reverts — capture drill in backlog 59.
STATUS and both BACKLOGs compacted: done items are now one-line ledgers in
the lane backlogs; run-by-run history lives in the lane `reports/` and in
git history of these files. Stale gates fixed in the same pass (dsp 15's
dead CombineTest ask, 12's folded verdict, 16→3r supersession, 27+3q merge).
**GOALS.md awaits Matt's refresh** — the July goals are largely satisfied,
and the current center of gravity (PerformSource/pin model, AF archaeology,
KS piano family, live MIDI) grew out of interactive sessions, not GOALS.

| Lane | Dev | State | Next up | Awaiting Matt |
|---|---|---|---|---|
| dsp | Dipsy | **09-02: r4 VERDICTED — "nailed it, concept proven."** First feedback-loop patch in library: **flute002 → library/winds/flute_default.json** ("killer, makes Clarinet1 sound fake"-class integrated attacks); reed001a library-grade, awaiting a name (oboe?). Winds > brass > strings (no bow attack yet). Full record: docs/research/feedback_sweeps/R4_VERDICTS.md — incl. code-verified physics answers (voice state IS cleared per note; noise RNG free-runs → real per-hit non-determinism). Cross-cutting gap: noise amp + drive ramp not keytracked (cutoff is) → patches only characteristic in home register **r5a RUN same day**: keytrack round — 6 keepers × 3×3 (noise-amp exp × ramp-time exp), 54/54 rendered to renders/dsp/pending/feedback_curves5a/ (README inside; n00_r00 = control). New render format per Matt: 5 notes C3..C7, 2 s each. Axes verified: pre-bloom hiss 56x C3→C7 flattened by n80; attack 0.38s→0.04s across board at r70. Mechanisms: hiss.amplitude ← power-form CurveNode; driveenv timeScale ← dynamicPins per-note setting **5a verdicted 09-03** (noise kt keeper at -0.80..-0.95, ramp curve dropped) → **r5b RUN 09-03**: reed001a deep-dive 6×4×6 (cutoff mult × drive shape incl. overshoot/tongue × junction jitter ±15%), criticals re-measured per cell, 144/144 rendered **09-05: CURVE-MORPH SHIPPED same day as spec** (docs/superpowers/specs/2026-09-05-curve-morph-design.md + plan): shared Curve evaluator (CurveNode+Shaper delegate, byte-identical), per-segment interp overrides (Envelope vocab + Hold; Stage holdPct REMOVED — scanned unused), Shaper editor gestures (right-drag segment = Expo+power, power dots, presets bow/reed1/reed2/lip/jet/hard/sine/saw/triangle), point-space morph pin + A/B editor workflow. Demo: renders/dsp/audition/curve_morph/oboe_morph_demo.wav (origin-steepened curve B morphed in over each note). Gates parallelized (null gate + roundtrip, --jobs; ~3x) — next gate lever: mforce_cli batch mode (render is 378x realtime; 95% of gate cost is process spawn+load). Pre-existing find: UI roundtrip drops wiring_smoke's __perf_freq (Matt spun off fix task). **09-06: WORMHOLE node pairs replace the net-label tag experiment** (tags fully removed; commits e3780bb..4f4bd82): pure-glass pass-through type, pairing BY WIRING (hidden span; hover/select ghosts it, double-click jumps), right-click a wire = route through a pair, spans exempt from group interface, rename fixes for tap refs. Byte-identical proof: oboe tap through a pair == stock render. OPEN THREADS for next session: (1) face-to-face fallback wire for boundary-invisible edges (proposed, NO go yet — oboe_grouped's tap edge is invisible at every drill level until wormholed); (2) DONE 09-06 later session: small Wormhole mini-faces + backlog 55 tap-pin removal SHIPPED (Matt approved mini for wormholes — the earlier veto was for boundary terminals); drilled-in boundary pin-stubs SUPERSEDED by the new **Replace-with** node-context gesture (swap type keeping wiring/state by name; headless --replace mode; backlog 57 = toggle-node A/B wishlist from same discussion); (3) backlog 56 id migration (Matt-decided, post-lanes, uniqueness check drops); (4) Matt's lane MDs still unwritten — the original plan this week. **09-04 session: junction mechanism ground-truthed → docs/research/feedback_sweeps/JUNCTION_OPERATING_POINT.md** — r5c should use directed axes (origin-asymmetry ratio + drive floor relative to measured oscillation threshold), not breakpoint jitter; ear-validated on patches/pending/oboes/oboe_dv_junc_tweaked.json (Matt's sandbox, awaiting his name — nothing supersedes oboe_default) | Matt's r5b axis-tour verdict, then pull neighbors from the full grid; apply winning shape/mult to other keepers. Backlog 53 (live mono mode) parked. Remaining 37a: in-loop breath (d). 34a; excite 46-48 | **feedback_curves5b axis tour 14 cells = the live queue** (full 144 in sweep/); amp_release/tuned/keytrack demos. **09-05 NEW: renders/dsp/audition/feedback_inloop1/ — 10 cells + control, oboe_default formants moved IN-loop (placement × res, normalized, loop-referenced criticals; README inside, tool: tools/gen_feedback_inloop1.py).** 09-03: **oboe_default promoted** (Matt's build-out of the r5b reed line: formant bandpass bell, Vibrato, Reverb — second feedback-loop patch in library/winds) |
| comp | Wolfie | **09-05: run 27 (interactive) — #22 done.** The three purge-casualty A/Bs (REVIEW 11/13/17) re-rendered at HEAD, same seeds, faithful to the originals → renders/comp/audition/{markov_phrases5,markov_phrases6,passage_end_grid}. Additive1.json rescued from patches/old/ → baselines/; 7 comp harnesses repointed | #21 zero-event check → #8 re-observe | **10 items, ALL verdictable** — the whole comp/REVIEW.md queue awaits ears; nothing blocked |

## dsp

Landed 2026-08-21 (Opus 4.8, MISC.md list): WhiteNoise density/boost/
continuity/zeroCrossTendency restored; Triangle `power` (symmetric, old
shark-fin behind `asymmetric` flag); UI piano dynamic octaves + trailing
top C. All byte-identical at defaults, null gate 196/196.
Before that: **PerformSource P1–P3 all landed** 08-18..20 (paramMap story
is now graph nodes engine+editor; gold pins, Note/Curve nodes, instrument
cache, RtMidi, wheel/pressure smoothers), plus Matt's hands-on day 08-20
and the AF archaeology thread (afp31 v1–v12 + decoded ground truth, afks,
sax recipe).

Review queue: dsp/REVIEW.md REBUILT 2026-09-14 — Awaiting is now (top
first) 53 stk_bowed_ext1, 54 stk_bowed_ext2 + cello-IR pick, 55
feedback_curves5b tour, 56 feedback_inloop1, 38 AF sax [discuss], 39
wheel [parked hardware], + one parked excitation stub. Everything else
folded to Resolved stubs there.

## comp

Run 26 (08-10) landed the phrase-aware cadence (composer/realizer cursor
disagreement fixed, arrivals 1/4→4/4), `cadentialArrival: approach`, and
revived six templates that had been rendering silence. Run 27 (09-05,
interactive) closed #22: the three purge-casualty A/Bs re-rendered.

Review queue (comp/REVIEW.md), now fully verdictable: 18 cadential arrival
[listen], 16 section key [listen], 9 Bruckner v2 [listen], 10 pedal_chords
[listen], 12 voicing_ab [listen], 14 passage-mode [read], 19 octave naming
[decide] — all under renders/comp/pending/ — plus the run-27 re-renders
under renders/comp/audition/: 11 literal repeats [listen], 13 phrase
endings [listen], 17 passage endings [listen].

## Standing notes

- The dsp and comp *scheduled* runs have collided twice in this shared
  working copy (runs 21, 25); staggering the two schedules is still open.
- **patches/library/ curated by Matt 2026-08-22 (bdc390d): 74 files, final
  patches only from here on.** KS piano baseline is now
  keys/acoustic_piano/piano_default.json (Piano_bright retired); clarinet_*,
  fm rhodes, v6 strings, percussion kick/snare are gone. rt_smoke cases
  repointed. Tools/docs that name the old files are historical.
- **patches/audition/ is the listening queue (renamed from pending/ 2026-08-22,
  gitignored); patches/pending/ is now Matt's sandbox — runs never write there.**
- **renders/{dsp,comp}/audition/ follows the same rename (Matt 2026-09-03): future
  round queues go to audition/, renders/*/pending/ = Matt's hand-work. The
  feedback_curves5a/5b queues predate the rename and stay in dsp/pending/.**
- patches/old/ is a PERMANENT archive — never purged (Matt 2026-09-05; the
  former 30-day nightly purge is rescinded, removed from both run prompts).
- Misc task drops: Matt can leave a dated list in a lane's MISC.md
  (pattern established 08-21); see WORKFLOW.md.
- Undecided feature ideas live in IDEAS.md (new 08-22), not the backlogs;
  runs read it, never act on it.

How this works: [WORKFLOW.md](WORKFLOW.md) · reports: dsp/reports/ ·
comp/reports/ · latest: dsp/reports/2026-08-30-dipsy-ui-round-ksbow-shape-editor-spec.md,
comp/reports/2026-08-10-wolfie-run26.md
