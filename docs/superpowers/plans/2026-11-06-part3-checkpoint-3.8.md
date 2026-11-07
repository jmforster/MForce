# Part 3, checkpoint 3.8: `Partials` split by responsibility — Implementation Plan

**Goal:** `partials.h` (1,148 lines, eight types) becomes one type per file; `Partials` keeps only the render body, the array protocol and its surface, and composes six value-type layers (the coherent walk for motion and shimmer, the pair trade, the bandwidth noise, the onset gate, the decay law) and one layout object (the six partial arrays, their originals and the expansion); the pure `IPartials` becomes the abstract `Component` base `PartialSet` that owns its members' pin lifecycle, the shape 3.4 gave `WaveEvolution`; `BasicAdditiveSource` and `AdditiveSource2` are deleted and `FullAdditiveSource` is named `AdditiveSource`; no render in `patches/library` + `patches/baselines` moves except the one `AdditiveSource2` baseline, which retires to `patches/old/` and is replaced by a hand-authored `AdditiveSource` baseline whose render is new; `partials.h` leaves the meter's `files_over_limit`.

**Architecture:** Part 3 section 7 as written — "`AdditiveSource` is a Generator with `Slot<IPartials>` and `Slot<IFormant>`"; "`Partials` … is split into files first (base, Full, Sequence, Explicit, Composite, ExpandRule), which is mechanical. Splitting the class itself by responsibility (layout, the motion layer, the shimmer layer, expansion) is its own checkpoint, designed when the files are apart and the duplication between the motion and shimmer layers is visible" — and section 10's row 3.8 ("designed then; byte-identical target"). The design, now the files can be seen apart: the audit's F169 named seven responsibilities in `Partials` (the descriptor surface; the subclass array protocol; expand-rule application; per-note rng streams; six stochastic layers each with a start block, a per-sample advance and a per-partial gain; two caches; the render body). The layers are the extractable part and the duplication is the motion/shimmer pair: identical start, advance and mixing code over two sets of settings, differing only in what the body does with the value (cents on the frequency; a floored gain on the amplitude). Each layer becomes a value type with no pins — `reserve(n)` at prepare, `start(n, noteBase, …)` per note, `advance()` per sample, a per-partial accessor — owned by value in `Partials`, the way `dsp::ButterworthSection` is owned by a filter: a layer is not a graph citizen, has no descriptor, and is never wired, so it is not a `Component` (ruling 4). The smoothed random walk with meta-randomised segments (`MotionWalk` + `walk_seg_len`/`walk_init`/`walk_advance`), used by three layers and knowing nothing additive, goes to `core/dsp/segment_walk.h` as `dsp::SegmentWalk` (ruling 5). The six partial arrays, their originals and the expansion scratch become `PartialLayout`, a value type with `count`, `snapshot`, `restore`, `reserve_expanded` and `expand(rule, rng)` (`apply_expand_rule` verbatim); the subclasses' `init_arrays` write its arrays (ruling 6). Every moved line keeps its operations and their order: the per-partial body keeps every `if`, every assignment-versus-multiply (the onset's `pampl = 0.0f` is an assignment, not a multiply by zero: the sign of the zero differs), every expression's operand order; each layer's rng streams are already its own (`stream_seed(noteBase, index, layerId)`, the `PartialsRngLayer` map), so moving a layer's draws into its object changes no draw as long as the draw order within the layer is kept. Byte-identity is proved three ways per task — the null gate (five in-scope patches carry `Partials` nodes; three of them run motion, shimmer, bandwidth and onset, two wire envelope pins), a twin test per moved primitive (3.3's method: the deleted block retyped as a lambda, `same_bits` over a deterministic walk, both sides on identically seeded `Randomizer`s), and a synthetic A/B set that turns every layer on, one at a time and all at once, rendered by the pre-3.8 CLI and the task build.

**Execution (Matt 2026-11-04: 3.8 is Fable throughout — a Fable coordinator, no Opus 4.8 hand-off; Matt 2026-11-06: the "never Opus 5/5.5" rule rescinded, "just delegate to Opus 5.5 agents"):** Fable wrote this plan and executes the design-bearing tasks — the part base (Task 2), the layers and the layout (Tasks 4–6), the hand-authored replacement baseline (Task 1's one patch), and the refuter; Opus 5.5 executors (the Agent tool's `model: "opus"`) take the mechanical ones — the deletions with their tests, strings, tool rule and loader branch (Task 1), the file split (Task 3), the synthetic-set script and every A/B and timing run (Tasks 0 and 7), the docs splicing — each verified by the coordinator (null gate rerun, body-diff where text moved, a mutation of each twin) before its commit. One Fable refuter at the close, read-only, its brief narrowed to byte-identity (section "Review Focus") plus the ten-item brief and this Ledger; a fix wave; the same refuter's re-review by SendMessage. Written 2026-11-06 at xhigh (Matt: "stay at xhigh") with the Fable weekly window at 84 percent; execution starts after its reset (2026-11-07 02:00 HST).

**Tech Stack:** C++20 (MSVC, CMake 3.20+, `/fp:precise`), the `engine_tests` harness (`twin_signal`, `same_bits`, `same_value`, `CHECK`, `PinCounter`, `render_one_live_note`, `allocations_in_one_note`), `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/alloc_gate.py`, `python tools/structure/check.py` (`--delta`, `--snapshot`), `tools/migrate_deleted_types.py`, `git mv`.

**Spec:** `docs/architecture/part3-nodes.md` section 7 (quoted above), section 8 ("`partials.h` as in section 7"), section 10 row 3.8; `docs/architecture/target-architecture.md` section 5 ("`BasicAdditiveSource`: fully covered by `AdditiveSource` with `FullPartials`"; "`AdditiveSource2`: a half-finished port with a pitch bug; one baseline patch uses it and would be migrated"); the 2026-10-02 review's F169; CLEANUP 17 (the three part interfaces' two conventions, "decided when 3.8 opens `Partials`"); 3.4 ruling 1 (`WaveEvolution` an abstract `Component` base owning the pin lifecycle; the sibling pattern rejected for the forgotten-override class); the 3.1 report's "the three lifecycle trios (3.4/3.8)". Surveyed 2026-11-06 at `2709ebc` (no engine or tools file changed since 3.6's `5c4c5db`): `partials.h` holds `ExpandRule`, `ExpandRuleNode`, `IPartials`, `PartialsRngLayer`, `Partials` (lines 143–894), `FullPartials`, `SequencePartials`, `ExplicitPartials`, `CompositePartials`; the meter's longest function is `Partials::partial_value_impl` at 149 lines, one `const_cast` (`get_array`). In scope: 75 `AdditiveSource`, 70 `FullPartials`, 4 `ExplicitPartials` (`evolve` true in all three files that set it), 1 `SequencePartials`; `CompositePartials` and `expandRule` in no in-scope patch; `BasicAdditiveSource` in no patch anywhere under `patches/`; `AdditiveSource2` in `patches/baselines/as2_bright_attack_test.json` (in the null-gate manifest and the A/B list) and four `patches/old/baselines_culled_2026-09-14/as2_*` patches; `FullAdditiveSource` as a type name in no patch. The layers in scope: `strings/cello_full_range`, `strings/viola_default`, `winds/clarinet1` run motion (depth 8–16.8 cents, evolve 0.11–0.49, coherence 0, scale 0), shimmer (depth 0.9–1.0, coherence 0.5–0.59, evolve 0.3–0.38, floor 0 or 0.8), bandwidth (0.003–0.85) and onset (0.011–0.15); trade, decay, inharmonicity and motionScale are 0 in all three; `viola_default`, `fadd_explicit_test` and `fadd_sequence_test` wire `multEnv`/`amplEnv`/`roEnv`/`bwEnv` to envelopes (the per-partial cache rebuilds per sample there). The editor casts `dynamic_cast<Partials*>` for the strip (`main.cpp:11859`) and keeps three hand lists of the part types (`:866`, `:874/:883`, `:3448`); `tools/lint_patches.py` scrapes the loader and the registrations by path; `kBranchKeys` (`patch_loader.cpp:268`) carries the `AdditiveSource2` branch's keys. Matt, 2026-11-02: easily-tweaked decisions are the session's (CLAUDE.md "Refactor verdicts").

## Global Constraints

- **Byte-identity is total in scope, with one named exception**: `python tools/null_gate_perform_source.py --jobs 3` → every manifest entry identical after EVERY task (78 of 78 until Task 1 retires the `as2_bright_attack_test` entry, 78 of 78 again once its replacement is in the manifest — Task 1 adds the new baseline's hash in the same commit); the A/B at the close over `renders/scratch/ab38/library_baselines.txt` (the 131 that remain, plus the synthetics) against `renders/scratch/ab_tree/cli_pre38.exe` → 0 DIFFER; the new baseline is listed as NEW (no old render) and its WAV pair (the old `as2` render, the new `additive` render) is kept for Matt (REVIEW).
- **Every moved expression keeps its operations and their order**: no `x *= 0` for `x = 0`, no reassociation, no cached reciprocal, no `fast_` substitution, no draw reordered within a layer's stream; a twin test per moved primitive proves it bit for bit, and the reviewer mutates each twin.
- **The real-time rule**: every layer reserves at `prepare` (`reserve(n)` with the count `prepare_note_arrays` computes) and fills within capacity at `start`; `python tools/alloc_gate.py --jobs 3 patches/library patches/baselines` → 0 allocate after every task that touches a layer; the "everything on" allocation test (`run_allocation_tests_partials`) stays and passes.
- **No layer is a `Component`**: no pins, no descriptors, no registration; a layer is a value member.
- **One type per file, the file named after the type**; `partials.h` under 600 lines at the close (`files_over_limit` 19 → 18); no new file over 600, no new function over 80 (`Partials::start` and `partial_value_impl` shrink; their new lengths reported); `include_violations` +0; `unmapped_files` 0 (`core/dsp/` and `source/additive/` are mapped prefixes); `dynamic_cast` +0 in the engine; `duplicate_blocks` informational for the node folders (CLEANUP 31).
- **No dated changelog, backlog id, REVIEW, Part or checkpoint number in code comments, file names or folder names.**
- **Commit after every task; `python tools/gates.py --fast` before every commit**; the UI relinked before a stamp run (every task changes headers the editor compiles).
- **Work on main, no worktree.** Edits through script files; tracked files mix CRLF and LF (memory `project_crlf_edit_scripts`); a failed build's stale test binary is not a pass.
- **Dates:** `date` first; this plan is dated 2026-11-06.

## Review Focus

The refuter's oracle, narrowed to byte-identity and the record. Each row names the task that pins it.

1. **Every moved primitive is bit-identical** (Tasks 4–6): for each twin — `dsp::SegmentWalk` against the three deleted walk functions; `CoherentWalk::start`+`advance` against the motion start block and the motion advance; `PairTrade`; `BandwidthNoise::factor` against the body's bandwidth block; `OnsetGate::apply`; `DecayLaw::set_gain`+`advance`; `PartialLayout::expand` against `apply_expand_rule` — the reviewer names a mutation each twin would miss (a reordered draw, a dropped clamp, a multiply for an assignment, a swapped operand) and then performs one per twin and watches `CHECK(bad == 0)` fail.
2. **The rng streams**: each layer's `start` seeds the same streams (`stream_seed(noteBase, index, layerId)`) in the same order as the deleted block (shared before per-partial; `cur`, `target`, `len`, `pos` per walk); no layer reads another's stream; `noteBase_`/`noteSeq_` stay in `Partials` and advance at `start`, never at `prepare` (the split-equivalence tests still pass) (Tasks 4–5).
3. **The render body's order of operations is unchanged** (Task 5): motion (frequency) → cutoff → phase advance → rolloff → formant → fade → decay → bandwidth → shimmer → trade → onset → `fast_sin_turns`; the onset's `pampl = 0.0f` is an assignment; a partial past cutoff still returns before advancing its bandwidth walk. Reviewer: diff the body against `git show 2709ebc:…/partials.h` block by block.
4. **`PartialSet` advances exactly what `Partials` advanced** (Task 2): the eight Manual envelope pins, in declaration order, once per `next()`, prepared and started once each (a `PinCounter` test per verb); `CompositePartials` forwards `prepare`/`start`/`next` to its sets in slot order and advances no pin of its own; the slot's type-check refuses a signal with the new hint.
5. **The layout protocol** (Task 6): `arrayUpdateReq_` → `update_arrays` → `init_arrays` + `layout_.snapshot()` for every subclass rebuild (the 2.6 N1 class: an `ExplicitPartials` row edited after the first prepare sounds on the next note — the existing test still bites); the expansion starts every note from the originals; a second prepare after a note changes nothing (the existing idempotence test); the editor's `get_array` reads the layout.
6. **The deletions' record** (Task 1): no `AdditiveSource2`/`BasicAdditiveSource` text remains in engine, loader, registrations, tests, editor or tools; `retired_type_hint` names both; the tool refuses both by name; the loader's branch keys left `kBranchKeys` only where no scrape produces them (`structure_meter_tests` green); the retired baseline is in `patches/old/`, its replacement's form is the table's, the manifest carries the replacement's hash; the four `old/` AS2 patches fail by name with the hint.
7. **The meter and the clock** (Task 7): `partials.h` < 600; `files_over_limit` 18; `include_violations` +0; the timing over the additive set within noise of the pre-3.8 binary (three rounds; the layers' accessors inline; a slowdown beyond the round-to-round spread is a finding to fix before the close).
8. **The ten-item brief** (`docs/architecture/REVIEW_BRIEF.md`) over the whole diff, with line citations.

---

## File structure

| Old | New (one type each) |
|---|---|
| `source/additive/partials.h` (`ExpandRule`, `ExpandRuleNode`, `IPartials`, `PartialsRngLayer`, `Partials`, Full, Sequence, Explicit, Composite) | `additive/expand_rule.h` (`ExpandRule`, the plain struct); `additive/expand_rule_node.h` (`ExpandRuleNode`); `additive/partial_set.h` (`PartialSet`, the abstract base — ruling 1); `additive/partials.h` (`Partials` and the `PartialsRngLayer` map, the stream ids in one place); `additive/full_partials.h`, `sequence_partials.h`, `explicit_partials.h`, `composite_partials.h` |
| the motion/shimmer/trade walk (`MotionWalk`, `walk_seg_len`, `walk_init`, `walk_advance`) | `core/dsp/segment_walk.h`: `struct dsp::SegmentWalk { float cur{0}, target{0}; int pos{0}, len{1}; static int seg_len(int baseLen, float evolve, Randomizer&); void init(int baseLen, float evolve, Randomizer&); float advance(int baseLen, float evolve, Randomizer&); }` — the three bodies verbatim |
| the motion start/advance block and the shimmer start/advance block (`moShared_`/`shShared_`, `moSharedRng_`/`shSharedRng_`, `moWalks_`/`shWalks_`, `moRng_`/`shRng_`, `moVals_`/`shVals_`, `moLen_`/`shLen_`) | `additive/coherent_walk.h`: `struct CoherentWalk { void reserve(int n); void start(int n, uint32_t noteBase, uint32_t sharedLayer, uint32_t indLayer, float hz, float evolve, float rate); void advance(float evolve, float coherence); float value(int i) const; }` holding `dsp::SegmentWalk shared_`, `Randomizer sharedRng_`, `std::vector<dsp::SegmentWalk> walks_`, `std::vector<Randomizer> rngs_`, `std::vector<float> vals_`, `int len_`; two members in `Partials`: `motion_`, `shimmer_`. The flags `motionActive_`/`shimmerActive_` and the depth scalars `sMd_`/`sSd_` stay in `Partials`, where the body reads them |
| the trade block (`trWalks_`, `trRng_`, `trVals_`, `trLen_`) | `additive/pair_trade.h`: `struct PairTrade { void reserve(int n); void start(int n, uint32_t noteBase, uint32_t layer, float hz, float rate); void advance(); float value(int pair) const; }` |
| the bandwidth block (`bwCur_`, `bwTarget_`, `bwPos_`, `bwRng_`, `bwLen_`) | `additive/bandwidth_noise.h`: `struct BandwidthNoise { void reserve(int n); void start(int n, uint32_t noteBase, uint32_t layer, float hz, float rate); float factor(int i, float dry, float wet); }` — `factor` is the body's block verbatim: the segment step, the smoothstep, `++pos`, `return dry + wet * noise` |
| the onset block (`onsetDelay_`, `onsetFadeSamples_`, `sampleIdx_`) | `additive/onset_gate.h`: `struct OnsetGate { void reserve(int n); void start(int n, uint32_t noteBase, uint32_t layer, float spread, float tilt, float fade, float rate); void advance(); float apply(int i, float pampl) const; }` — `apply` keeps the assignment for `t <= 0`; `sampleIdx_` moves here (its only reader), reset at every `start`, advanced at every `next()` as today |
| the decay block (`decayGain_`, `decayState_`) | `additive/decay_law.h`: `struct DecayLaw { void reserve(int n); void start(int n); void set_gain(int i, float pmult, float decayRate, float decayExp, float rate); void advance(); float gain(int i) const; }` — `set_gain` is the cache loop's double-precision law verbatim, called from `ensure_partial_cache` |
| the six arrays, their originals, the expansion scratch, `apply_expand_rule`, the expansion's part of `prepare_note_arrays` | `additive/partial_layout.h`: `struct PartialLayout { std::vector<float> mult1, mult2, ampl1, ampl2, po1, po2; int count() const; void snapshot(); void restore(); void reserve_expanded(const ExpandRule& rule); void expand(const ExpandRule& rule, Randomizer& rng); private: the six originals, the six scratch }` — `expand` is `apply_expand_rule` verbatim over the members; one member `layout_` in `Partials`; the subclasses' `init_arrays` write `layout_.mult1` … (the text `mult1_` → `layout_.mult1`, mechanical) |
| `IPartials` (pure, `partials.h:78`) | `PartialSet : Component` (`partial_set.h`): non-virtual `prepare(ctx)` (every pin's `prepare`, then `on_prepare(ctx)`), `start(frames)` (every pin's `start`, then `on_start(frames)`), `next()` (every `Param` pin's `next()` in declaration order, then `on_next()`); the hooks `on_prepare`, `on_start`, `on_next` (default empty); pure `partial_count`, `get_partial_value`; `sum_partials` with the default body as today. `Partials : PartialSet` moves its pin forwards into the base and keeps its work in the hooks; `CompositePartials : PartialSet` forwards to `partials.views` in the hooks. `Slot<PartialSet>` in `AdditiveSource` and `CompositePartials`, hint "a PartialSet part"; the load-failure message follows (`run_review_gap_tests`'s expected text) |
| `source/additive/full_additive_source.h` + `src/source/additive/full_additive_source.cpp` (`FullAdditiveSource`, type name `AdditiveSource`); `additive_source.h` (the alias header) | `git mv` to `additive/additive_source.h` + `src/source/additive/additive_source.cpp`; the class is `AdditiveSource`; the alias header deleted (it is the moved file's name); the registry's `.label = "Full"` dropped (the derived label stands — ruling 9); CMake and the include sites follow |
| `additive/basic_additive_source.h`; `additive/additive_source2.h` + `src/source/additive/additive_source2.cpp` | deleted, with their registrations, the loader's `AdditiveSource2` branch (`patch_loader.cpp:762–804`), the branch keys only it read, the editor's two type strings in `pin_type_compat_error`, the tests on the two types (`run_additive_source_pin_tests`' two halves, `run_additive_auto_pin_tests`, the two smoke lines, the two split cases and `make_split_additive2`), `Partials`' four `get_mult1()`-style accessors ("for AdditiveSource2 compat"), `Component::on_slot_changed`'s comment naming AdditiveSource2; `retired_type_hint` gains `"AdditiveSource2"` and `"BasicAdditiveSource"` → "deleted; author an AdditiveSource with a FullPartials (or ExplicitPartials under a CompositePartials for per-group envelopes)"; `tools/migrate_deleted_types.py` gains a `Refuse` rule class and two rows naming the hint (a `Refuse` reports the node and exits 1: there is no mechanical form) |
| `patches/baselines/as2_bright_attack_test.json` | `git mv` to `patches/old/additive_source2/as2_bright_attack_test.json` (never deleted; the four 09-14 culls stay where they are); its manifest entry retired. Replacement, hand-authored (Fable): `patches/baselines/additive_bright_attack_test.json` — the same three envelopes; an `AdditiveSource` (frequency 165, amplitude `ampEnv`) whose `partials` is a `CompositePartials` of two `ExplicitPartials`, `lo` (mult 1..5, `ampl1` 0, `ampl2` 1/m, `evolve` true, `amplEnv` = `loEnv`) and `hi` (mult 6..60, likewise, `amplEnv` = `hiEnv`), each with `shimmerDepth1`/`2` 0.03, `shimmerHz` 0.2, `shimmerCoherence` 0 (the nearest form of the old node's per-partial amplitude wander); a two-note embedded score as the old file's. Its render is NEW; the old render (from `cli_pre38.exe`) and the new are kept side by side under `renders/scratch/ab38/review/` for Matt (REVIEW R14 item 1) |
| Include sites | `additive_source.h` (`partial_set.h`, `formant.h`, `band_spectrum.h`); `composite_partials.h` (`partial_set.h`); `patch_loader.cpp` (`partials.h` for the `expandRule` host cast, `expand_rule_node.h`, `expand_rule.h`); `source_registrations.cpp` (one include per registered type); `tools/engine_tests/main.cpp` (every type it constructs); `tools/mforce_ui/main.cpp` (`partials.h` for the strip cast) |

Untouched by rule: `formant.h` and the four spectra (the convention is recorded in ruling 2; the formant half of CLEANUP 17 is a later move), `ks_string.h`.

---

### Task 0: Date, base, the old arm, the synthetic set

- [ ] `date`; sweep stale `mforce_ui_*.exe`; build; `gates --fast` confirms the stamp. Copy `build/tools/mforce_cli/Release/mforce_cli.exe` → `renders/scratch/ab_tree/cli_pre38.exe`. The old arm is `renders/scratch/ab36/new/hashes.txt` (no engine or tools file changed after `5c4c5db`), copied to `renders/scratch/ab38/old/hashes.txt` and noted in the Ledger; no re-render (memory `project_ab_arm_reuse`). The A/B list `renders/scratch/ab38/library_baselines.txt` is ab31's 132 minus `as2_bright_attack_test` (Task 1 moves it) — 131; record the sha.
- [ ] **Opus 5.5:** `renders/scratch/ab38/synth/make_synth.py` writes the synthetic set, two notes each (C4, then C2), in the shape of `fadd_explicit_test`: `motion` (depth1 10 / depth2 20, Hz 4, coherence 0.5, evolve 0.3, scale 0.5), `shimmer` (depth 0.5 → 1.0, Hz 3, coherence 0.4, evolve 0.3, floor 0.3), `trade` (0.3, 2 Hz), `onset` (spread 0.2, tilt −0.5, fade 0.05), `decay` (rate 40, exp 0.6), `inharmonicity` (0.01), `bandwidth` (0.5 → 0.9 with `bwEnv` wired to an AR envelope, Hz 30), `expand` (count 2, recurse 1, spacing 0.3/0.4, dt 0.02, loPct 0.2, power 1.5, po 0.25/0.5), `composite` (a Full + an Explicit under a `CompositePartials`), `sequence` (`SequencePartials`, minMult 1 → 1.2, incr 1 → 1.5), `explicit_evolving` (`evolve` true, `multEnv`/`amplEnv`/`poEnv`/`roEnv`/`dtEnv` wired to envelopes so the per-partial cache rebuilds per sample), `everything` (all of the above on one `FullPartials`), `formant` (a `FormantSpectrum` on the source, `formantWeight` 0.8, `formantFloor` 0.2). Render all with `cli_pre38.exe` → `renders/scratch/ab38/synth/old/hashes.txt`. These are the second set file at every task's A/B.
- [ ] Record in the Ledger the Task 0 scan: the in-scope `Partials` nodes by type and the layer values (the Spec's survey, re-read at the base sha).

### Task 1: The deletions (Opus 5.5, with Fable's patch)

- [ ] Delete `basic_additive_source.h`, `additive_source2.h`, `additive_source2.cpp` (CMake's list); their two registrations; the loader's `AdditiveSource2` branch; the `kBranchKeys` entries no scrape produces afterwards (run `python tools/structure/check.py` and ctest `structure_meter_tests`: they say which); `Partials`' four `get_*()` accessors; the editor's two type strings; the tests named in the table; `retired_type_hint`'s two names; the tool's `Refuse` class and two rows with a unit test each (`tools/tests/test_migrate_deleted_types.py`: a refused node reports and exits 1; `--check` likewise).
- [ ] `git mv` the AS2 baseline to `patches/old/additive_source2/`; retire its manifest entry; add Fable's `patches/baselines/additive_bright_attack_test.json` and its manifest hash (rendered by this task's build); render the old file once with `cli_pre38.exe` → `renders/scratch/ab38/review/as2_bright_attack_test.wav` and the new file → `review/additive_bright_attack_test.wav`.
- [ ] Build; `engine_tests` ALL PASS; null gate 78/78 (the manifest's 77 old entries identical, the new one as rendered); `alloc_gate` 0; `gates --fast`; the registry count tests follow (79 → 77 registrations). Commit: "additive: BasicAdditiveSource and AdditiveSource2 deleted; the one AdditiveSource2 baseline retired to old/ and re-authored on AdditiveSource; the tool refuses both names".

### Task 2: `PartialSet`, the part base (Fable)

- [ ] **RED first:** `run_partial_set_lifecycle_tests` — a `FullPartials` with `PinCounter`s on its eight envelope pins: `prepare` once each, `start` once each, `next()` × 7 → `nexts == 7`, in declaration order (the counter's order list); a `CompositePartials` of two sets forwards each verb once per set and advances nothing itself; a `SineSource` into `AdditiveSource::partials` is refused with "takes a PartialSet part".
- [ ] `partial_set.h` as the table gives; `Partials` and `CompositePartials` rebased on it (the three hand-written eight-pin forwards deleted; the work moved into `on_prepare`/`on_start`/`on_next` unchanged in order); `AdditiveSource`'s slot retyped; the hint and the loader's message; the editor's list comments; `IPartials` gone (`grep -rn IPartials` empty).
- [ ] Build; ALL PASS; null gate 78/78; synthetic A/B 0 DIFFER; `gates --fast`. Commit: "additive: PartialSet, the abstract base of a partial set, owns its members' pin lifecycle; IPartials gone".

### Task 3: One type per file (Opus 5.5)

- [ ] A script writes the eight files from `partials.h`'s text (each type's span, verbatim, under a banner and the includes it uses: `expand_rule.h` ← `<vector>`; `expand_rule_node.h` ← `component.h`, `expand_rule.h`; `partial_set.h` ← `component.h`, `formant.h` (for `IFormant*` in the verbs), `<cmath>`; `partials.h` ← `partial_set.h`, `expand_rule.h`, `fast_math.h`, `randomizer.h`; the three subclasses ← `partials.h`; `composite_partials.h` ← `partial_set.h`); the include sites per the table; `git mv full_additive_source.{h,cpp}` → `additive_source.{h,cpp}`, the class renamed, the alias header gone, CMake's list, the registry's label.
- [ ] Build; ALL PASS; null gate 78/78; synthetic A/B 0 DIFFER; `gates --fast`; the meter: `files` +7, `files_over_limit` still 19 (`partials.h` is still over until Task 6), `include_violations` +0, `unmapped_files` 0. Body-diff by the coordinator: every moved type's span byte-identical to the Task 2 text. Commit: "additive: one type per file; FullAdditiveSource is AdditiveSource, the alias header gone".

### Task 4: `dsp::SegmentWalk`, `CoherentWalk`, `PairTrade` (Fable)

- [ ] **RED first:** `run_segment_walk_twin_tests` — the three deleted functions retyped as lambdas over a `dsp::SegmentWalk`-shaped local struct; a grid of (baseLen 1, 7, 1600, 48000) × (evolve 0, 0.3, 1.0) × 8 seeds, 500 advances each, both sides on identically seeded `Randomizer`s, `same_bits` on every advance output and `CHECK(bad == 0)`; `run_coherent_walk_twin_tests` — the motion start block and the motion advance retyped verbatim (shared then per-partial; `coh * shared + (1 - coh) * ind`) against `CoherentWalk::start`+`advance` over n ∈ {1, 8, 61}, coherence ∈ {0, 0.5, 1}, evolve ∈ {0, 0.3}, 300 samples, `same_bits` per partial per sample; `run_pair_trade_twin_tests` likewise over the trade block (n odd and even).
- [ ] `core/dsp/segment_walk.h`, `additive/coherent_walk.h`, `additive/pair_trade.h` as the table gives; `Partials` holds `motion_`, `shimmer_`, `trade_`; `start` calls each layer's `start` where its block stood (inside the same `if (…Active_)`), `on_next` calls `advance` where its loop stood, the body reads `motion_.value(index)`, `shimmer_.value(index)`, `trade_.value(index >> 1)`; `prepare_note_arrays` calls `reserve(n)`; the deleted members gone.
- [ ] Build; ALL PASS; mutate each of the three twins once (a draw reordered in `init`; `(1 - coh)` written `1 - coh * ind`; the pair index) and watch each fail; restore. Null gate 78/78 (cello, viola, clarinet pin motion and shimmer on real patches); synthetic A/B 0 DIFFER (`motion`, `shimmer`, `trade`, `everything`); `alloc_gate` 0; `gates --fast`. Commit: "additive: the segment walk is a core/dsp primitive; motion, shimmer and trade are layers Partials composes, twin-tested".

### Task 5: `BandwidthNoise`, `OnsetGate`, `DecayLaw` (Fable)

- [ ] **RED first:** `run_bandwidth_noise_twin_tests` (the start block and the body's bandwidth block retyped; a `dry`/`wet` pair per row; 400 samples × n ∈ {1, 5, 40}; `same_bits` on the returned factor and on the state after each call), `run_onset_gate_twin_tests` (the start block with tilt ∈ {−1, −0.5, 0, 0.5, 1} and the `apply` block; `same_bits` on `pampl` out, including the rows where `t <= 0` — the sign of the zero is the point), `run_decay_law_twin_tests` (the cache loop's law and the advance over pmult ∈ {1, 2.5, 17}, rates {0, 1, 40, 400}, exps {−2, 0, 0.6, 4}).
- [ ] The three headers as the table gives; `Partials` holds `bandwidth_`, `onset_`, `decay_`; the body's three blocks become `pampl *= bandwidth_.factor(index, sBwDry_, sBwWet_)` (inside the same `if (sBwActive_)`), `pampl *= decay_.gain(index)` (inside `if (decayActive_)`), `pampl = onset_.apply(index, pampl)` (inside `if (onsetActive_)`); `ensure_partial_cache` calls `decay_.set_gain(i, pmult, decayRate.value, decayExp.value, rate_)` inside its `if (decayActive_)`; `on_next` calls `onset_.advance()` unconditionally where `++sampleIdx_` stood and `decay_.advance()` inside its `if`.
- [ ] Build; ALL PASS; mutate each twin (the smoothstep's `3.0f - 2.0f * u` reassociated; `pampl *= 0.0f` for the assignment; `float` for the law's `double`) and watch each fail; restore. Null gate 78/78 (the three patches pin bandwidth and onset); synthetic A/B 0 DIFFER (`bandwidth`, `onset`, `decay`, `everything`); `alloc_gate` 0; `gates --fast`. Commit: "additive: the bandwidth noise, the onset gate and the decay law are layers, twin-tested; the per-partial body reads them".

### Task 6: `PartialLayout` and the expansion (Fable)

- [ ] **RED first:** `run_partial_layout_twin_tests` — `apply_expand_rule` retyped verbatim over six local vectors against `PartialLayout::expand` for rules (count 1..3, recurse 0..2, every field off its default, po ≠ 0), n ∈ {1, 4, 16}, `same_bits` on all six arrays; `restore` after `expand` gives the snapshot back; `reserve_expanded` leaves `capacity() >= count` for the recursed size and `expand` then allocates nothing (an `alloc_probe` arm around it).
- [ ] `partial_layout.h` as the table gives; `Partials` holds `layout_`; `snapshot_originals` → `layout_.snapshot()`, the start's restore → `layout_.restore()`, the loop → `layout_.expand(expandRule_, expandRng_)`, `prepare_note_arrays`' expansion sizing → `layout_.reserve_expanded(expandRule_)`; the body reads `layout_.po1[index]` etc.; the three subclasses' `init_arrays` write `layout_.*` (text substitution, the arithmetic untouched); `get_array` reads the layout; `partial_count()` is `layout_.count()`.
- [ ] Build; ALL PASS; mutate (the left sub-partials' `semis1` from `count - j` to `j`; the centre copy dropped) and watch the twin fail; restore. Null gate 78/78; synthetic A/B 0 DIFFER (`expand`, `composite`, `sequence`, `explicit_evolving`, `everything`); `alloc_gate` 0; the idempotence and row-edit tests pass unchanged; `gates --fast`; the meter: `partials.h` < 600, `files_over_limit` 18. Commit: "additive: PartialLayout holds the six partial arrays, their originals and the expansion; Partials keeps the render body and its surface".

### Task 7: Close

- [ ] **Opus 5.5:** A/B `python tools/ab_render_set.py render renders/scratch/ab38/library_baselines.txt renders/scratch/ab38 new` against the copied `old` → 0 DIFFER over 131, the new baseline NEW; the synthetic set 0 DIFFER; `alloc_gate` 0; `python tools/structure/check.py --delta <Task 0 sha>`; `--snapshot`. Timing: `ab_render_time.py` against `cli_pre38.exe`, three rounds, over `viola_default`, `cello_full_range`, `clarinet1`, `voice/liar_220`, `voice/oo_ee_110` (or the set's nearest names), `fadd_explicit_test`, `fadd_sequence_test`; a slowdown beyond the rounds' spread on any of them is a finding (the layers' accessors inline; check `/O2` emitted no call), fixed before the review.
- [ ] **Fable refuter** (Agent, read-only, xhigh; brief at `renders/scratch/ab38/review/brief_r14.md`: the eight Review Focus items, the table, the Ledger's rulings, the commit range; it reports only what it ran); the fix wave (Majors and cheap Minors, tests first); the same refuter's re-review by SendMessage.
- [ ] Docs: report `docs/autonomy/refactor/reports/<date>-checkpoint-3.8.md` (ten sections; section 10's lessons: composition of value-type layers versus inheritance — why a layer has no pins and is not a `Component`, and the C#/Java analogue (a strategy object, not a node); an abstract base with a template method versus a pure interface, and where `PartialSet` sits between Java's abstract class and interface; `reserve` as a contract between an owner and a layer it holds by value; signed zero — why `x = 0` and `x *= 0` differ bit for bit and what `same_bits` sees that a WAV hash cannot); Part 3 section 7's landed paragraph, row 3.8, and section 1's additive bullets marked landed; REVIEW R14 (item 1: the `AdditiveSource2` deletion and the replacement baseline's render, with the WAV pair — a render Matt should hear and a deletion; item 2, non-blocking: the part-base convention recorded in ruling 2 and the formant base's name when its half of CLEANUP 17 moves); BACKLOG row 2 (3.8 DONE; PART 3 COMPLETE; Part 4 next, its document not yet written); CLEANUP (17 updated for the Partials half; the editor's strip cast and the `is_partials_type` list lacking `CompositePartials`, Part 7; `get_array`'s `const_cast`); STATUS; this plan's Ledger. Commit.

---

## Ledger

Rulings made in planning; execution notes are appended per task.

1. **`IPartials` becomes `PartialSet`, an abstract `Component` base that owns its members' pin lifecycle**, the shape 3.4 ruling 1 gave `WaveEvolution`. `Partials` today hand-forwards its eight Manual envelope pins in `prepare`, `start` and `next` — twenty-four lines that a ninth pin must be added to three times, and a forgotten line prepares nothing and fails no compile (the forgotten-override class). The base walks `pins()` for `prepare` and `start` (every `PinBase` has both) and the `Param` pins for `next()`, in declaration order, which is the order `Partials` wrote by hand; the verbs `partial_count`, `get_partial_value` and `sum_partials` (with today's default loop) stay on the base. `CompositePartials` has no `Param` pin, so the base's loops are empty there and its hooks forward to the sets in slot order, as today. The name: a set of partials is what Full, Sequence, Explicit and Composite each are — `CompositePartials`' own comment calls what it concatenates "sets". Alternative: keep `IPartials` pure and leave the forwards. Rejected: the forgotten-override class and the two conventions CLEANUP 17 names. The rename touches the slot type in two nodes, two hints, one loader message, the editor's comments and the tests' strings; `kPartTypes` is unchanged (`IPartials` was never a registered name).
2. **The convention for the part interfaces, recorded for CLEANUP 17**: a part interface is an abstract `Component` base named for what the part is, without an I-prefix, owning the pin lifecycle its members share (`WaveEvolution`, `PartialSet`); `IFormant` is the one that remains pure and I-prefixed, and its five implementers each hand-forward their pins. 3.8 does not move it: it is opening `partials.h`, not `formant.h`, and the formant base's name is a choice Matt should see (`Formant` is taken by the concrete peak; the candidates are a spectrum-flavoured name or `FormantShape`). REVIEW R14 item 2, non-blocking; the move itself is mechanical and follows the same shape.
3. **The seven responsibilities, where each lands**: the descriptor surface (pins, settings) — `Partials`; the subclass array protocol — `Partials` (`init_arrays`/`update_arrays`/`arrayUpdateReq_`) writing a `PartialLayout`; expand-rule application — `PartialLayout::expand`; per-note rng streams — `Partials` keeps `baseSeed_`/`noteSeq_`/`noteBase_` and hands each layer its stream ids at `start`; the six stochastic layers — five value types (motion and shimmer share `CoherentWalk`); the two caches — `Partials` (they are the render body's); the render body — `Partials::partial_value_impl`, unchanged in order, reading the layers. The per-note detune (`dtVals_`, `init_detune_values`) stays in `Partials`: a per-note draw with no advance is not a layer.
4. **A layer is a value type, not a `Component`.** It has no pins and no descriptor, is never wired, registered or shown, and is owned by value in `Partials` — the relation `dsp::ButterworthSection` has to a filter. Making layers `Component`s would give each a registry entry and an editor face that nothing wants, and would put a `shared_ptr` hop in the per-partial loop. The settings stay on `Partials` (the one descriptor surface the editor and the loader address) and are passed to the layers' `start` as values; a layer reads no `Setting`. Alternative, F169's "each layer's own descriptor list": a second descriptor surface the pin registry does not have, for a gain of nothing the editor can use.
5. **The smoothed random walk is a `core/dsp` primitive, `dsp::SegmentWalk`.** Three layers use it (motion, shimmer, trade) and it knows nothing additive: a segment length, an excursion, a smoothstep. Part 3 section 4 put the shared DSP primitives in `core/dsp`; a primitive one node family uses three times qualifies. Alternative: a static inside `coherent_walk.h` that `pair_trade.h` includes — the trade layer would depend on the motion file for no reason of its own.
6. **`PartialLayout` is the arrays, their originals and the expansion, with public arrays.** The subclasses' `init_arrays` write the arrays directly today (protected members); a layout with accessors for six vectors would add twelve functions to say what `struct` says. The layout's job is the originals-and-expansion protocol (`snapshot`, `restore`, `reserve_expanded`, `expand`), which is where the 2.6 N1 class lives; it keeps the originals private so a subclass can only reach them through `snapshot`. `ExpandRule` stays a plain struct in its own file; `ExpandRuleNode` is the part that snapshots one; the expansion is a method on the layout because every array it reads and writes is the layout's.
7. **Byte-identity by construction, proved anyway**: each moved block is the same text in a new scope; the twins (one per primitive and per layer) compare bits over parameter walks that include the sign-of-zero rows; the null gate pins motion, shimmer, bandwidth and onset on three library patches and the wired-envelope cache path on three more; the synthetic set covers trade, decay, inharmonicity, the expansion, Composite, Sequence and the evolving Explicit, which no in-scope patch runs; the close A/B covers the 131. Gates hash 16-bit WAVs and cannot see sub-LSB classes; the twins can.
8. **The one render that moves is the `AdditiveSource2` baseline, by deletion, not by the refactor.** The node's form — per-partial-group envelopes assigned by filter and range, and a "rolloff mode" whose amplitude is `(1/m^e) * e^2` in the envelope value `e` — exists nowhere else; the target calls it a half-finished port with a pitch bug and lists it for deletion with the baseline "migrated". There is no mechanical rewrite that keeps the render, so: the tool refuses the name (a `Refuse` row, the hint naming the hand form), the file retires to `patches/old/additive_source2/` (nothing in `old/` is deleted), and a replacement baseline is authored by hand on the surviving types — two `ExplicitPartials` under a `CompositePartials`, each with its group's envelope on `amplEnv` (`ampl1` 0 → `ampl2` 1/m, so the amplitude is linear in the envelope, the nearest form), the old node's amplitude wander as shimmer (depth 0.03, 0.2 Hz, coherence 0). Its render is new and is listed as such; the WAV pair goes to REVIEW R14 as a render Matt should hear. The four `old/` AS2 patches stay and fail by name with the hint, as 3.5's archived presets do.
9. **`BasicAdditiveSource` is deleted outright, no tool rule beyond the refusal.** No patch anywhere under `patches/` names it (0 of the whole tree); its law (partials to 12 kHz, triangle-wave wander) is not `FullPartials`' and a rewrite would move a render that does not exist. `FullAdditiveSource` is renamed `AdditiveSource` (its `type_name` already) by `git mv` of its two files; the alias header, which exists only to spell the old name, goes; the registry's `.label = "Full"` goes with its two siblings (the derived label stands; a cosmetic call under the 11-02 rule, CLEANUP as found).
10. **The loader's branch keys are pruned by the scrape, not by hand.** `kBranchKeys` must contain every key `lint_patches.py` scrapes; an entry no scrape produces is allowed but dead. After the `AdditiveSource2` branch goes, the executor removes the entries only it read and lets `structure_meter_tests` say whether one is still scraped elsewhere (`envelope`, `filter`, `from`, `to`, `partials`, `partialCount` are shared with other branches and stay).
11. **Order of tasks: deletions first.** `AdditiveSource2` holds `Slot<Partials>` and reads `Partials`' four array accessors; deleting it first leaves the layout refactor unconstrained. The base (Task 2) precedes the split (Task 3) so the split moves final text; the layers (4–5) precede the layout (6) so the body's reads change once per block.
12. **Execution is Fable on the design-bearing tasks** (2, 4, 5, 6, the replacement patch, the refuter) **and Opus 5.5 subagents on the mechanical ones** (0's set, 1, 3, 7's runs and the docs splicing), under this session's Fable coordinator; the coordinator reruns the null gate before every commit and mutates every twin itself once (3.6's verification shape). The plan first named Sonnet executors because that was the 11-04 pattern's tier, not a weighed choice; Matt asked why Fable/Sonnet rather than Fable/Opus and rescinded the "never Opus 5/5.5" rule the same day (2026-11-06: "just delegate to Opus 5.5 agents"), so the executor tier is Opus 5.5 and Opus 4.8 is not required. The Fable weekly window was at 84 percent when this plan was written; execution starts after its reset.

13. **The reviewer for this close is an Opus 5.5 subagent, read-only** (added
at the close): the Fable weekly window stood at 93 percent when the six
engine tasks were in, and Matt rescinded the "never Opus 5/5.5" rule the same
day; the remaining Fable went to the fix waves and the report. The reviewer
performed no mutations; the coordinator's eight per-task mutations are the
mutation record.

### Execution notes

**Task 0 (2026-11-06, Fable 5.1 xhigh; base `4d32899`).** `date` 2026-11-06;
no stale `mforce_ui_*.exe`; the editor not running. The Release build was
clean and did not relink the CLI (no engine or tools file has changed since
`5c4c5db`), so `cli_pre38.exe` is the 11-05 binary (sha `ed925180…`) and the
ab36 new arm is this checkpoint's old arm: `renders/scratch/ab36/new/hashes.txt`
→ `renders/scratch/ab38/old/hashes.txt` (132 lines, the AS2 entry among them,
no re-render). The A/B list `renders/scratch/ab38/library_baselines.txt` is
ab31's minus `as2_bright_attack_test`: 131. `gates --fast` on the base: ctest
5/5, the stamp current, the allocation gate PASS (62 s), the roundtrip smoke
FAIL on the known wiring diff (5/6 shapes, as at 2.4 through 3.6). The Task 0
scan is the Spec's survey, re-read at the base: unchanged.

The synthetic set (an Opus 5.5 executor, the first under the 11-06 rule):
`renders/scratch/ab38/synth/make_synth.py` wrote the thirteen (instrument
mode, two notes C4 then C2, `maxPartials` 24 unless the table says less)
and thirteen feature-off controls; `cli_pre38.exe` rendered 13/13 and 13/13;
every synthetic's hash differs from its control's (`activity.txt`), and the
thirteen synthetic hashes are distinct. Controls that reduce to the same
FullPartials at defaults share a hash, as they should. Set file
`renders/scratch/ab38/synth/set.txt`; old arm `synth/old/hashes.txt`.

The replacement baseline (Fable, ruling 8): `patches/baselines/
additive_bright_attack_test.json` — the old file's three envelopes and mixer;
an `AdditiveSource` (seed 9, 165 Hz, amplitude `ampEnv`) over a
`CompositePartials` of two `ExplicitPartials`, `lo` (1..5, `amplEnv` = `loEnv`)
and `hi` (6..60, `amplEnv` = `hiEnv`), each `ampl1` 0 → `ampl2` 1 under the
default rolloff (so 1/m at the envelope's top, as the old node's law gave at
e = 1), `evolve` true, shimmer 0.03 at 0.2 Hz with coherence 0 for the old
amplitude wander. Rendered by `cli_pre38.exe`: sha
`7ceff8535abf573e77b4d0b88d372a9b011b2bc0faef4c5e85fdc98ed79357d3`, peak
0.258, rms 0.0476 (the old node: peak 0.285, rms 0.0473, sha `19544bac…`, equal
to its manifest entry — the manifest hashes are WAV hashes). The pair is kept
at `renders/scratch/ab38/review/{additive,as2}_bright_attack_test.wav` for
REVIEW R14 item 1.

**Task 1 (`2dfabe2`, an Opus 5.5 executor; coordinator-verified).** The two
types, their registrations, the loader branch, `Partials`' four accessors,
the editor's two strings, the tests on the two types (`run_additive_auto_pin_tests`
whole; the two halves of `run_additive_source_pin_tests`, which also tests
the additive source and stays; the smoke lines; the split cases and
`make_split_additive2`; the `kFamilyTable`/`kMenuTable` rows — Main 57 → 55,
walked 79 → 77, `kPartTypes` 23), `retired_type_hint`, the tool's `Refuse`
rule with two tests (17 pass), the baseline's move and the manifest's entry.
Beyond the named files, three comments and three tool scripts' `SOURCE_TYPES`
sets named a deleted type and lost the name. **Ruling 10 corrected by the
meter:** all fourteen candidate keys left `kBranchKeys` — `envelope`,
`filter`, `from`, `to`, `partials` and `partialCount` were read by the
deleted branch alone, not shared as the ruling guessed (`structure_meter`
green with none restored). Coordinator reruns: `engine_tests` ALL PASS (7462
→ 7375, 87 fewer), null gate 78/78 (the replacement's hash matched). The
executor's gates: allocation gate 132 patches 0 allocate; `gates --fast` as
before (the known wiring diff); meter `--delta 4d32899` files 211 → 208,
`files_over_limit` 19, `include_violations` 14, `dynamic_cast` 63,
`duplicate_blocks` 163 → 158. `tools/structure/metrics.json` still names the
deleted files until the close's `--snapshot` (as at 3.6).

**Task 2 (`e900c73`, Fable).** RED watched: the lifecycle test alone, naming
`PartialSet`, failed the test build with C2065 at its first use. Then
`partial_set.h` (71 lines: the three non-virtual verbs over `pins()` — every
pin's `prepare` and `start`, every `Param` pin's `next` — then the hook; the
three work verbs with the batched sum's default body moved verbatim from
`IPartials`); `Partials : PartialSet` keeps its work in `on_prepare` (the rate,
`prepare_note_arrays`), `on_start` (from `noteBase_` on) and `on_next` (from
`refresh_sample_scalars` on), the twenty-four forward lines gone;
`CompositePartials : PartialSet` forwards in the hooks; `Slot<PartialSet>` in
the source and the composite, hint "a PartialSet part"; the editor's three
comments; the tests' strings. One naming note against the table: the layers'
sizing method will be `prepare(n)` (it assigns, as `prepare_note_arrays` does
today), not `reserve(n)` — a method that assigns should not be called reserve.
Proofs: `engine_tests` ALL PASS 7375 → 7405 (the new test's thirty checks:
counts 1/1/7 through the base, declaration order `multEnv…shimmerEnv`, a
composite forwarding to a Full and a Sequence and advancing nothing of its
own, 60 partials); null gate 78/78; the thirteen synthetics identical to the
pre-3.8 arm (13/13 rendered, 0 DIFFER); `gates --fast` as before (ctest 5/5,
the stamp current, the allocation gate PASS, the known wiring diff). `grep
IPartials` over engine and tools: 0.

**Task 3 (`745eaaf`, an Opus 5.5 executor; coordinator-verified).** A script
sliced `partials.h` by line spans: `expand_rule.h` (20 lines),
`expand_rule_node.h` (52), `partials.h` (762: the stream map and `Partials`),
`full_partials.h` (67), `sequence_partials.h` (51), `explicit_partials.h`
(105), `composite_partials.h` (69); the coordinator's body-diff found all
eight spans verbatim in `e900c73`'s text, once each. The alias
`additive_source.h` removed, `full_additive_source.{h,cpp}` `git mv`'d to
`additive_source.{h,cpp}` with the class renamed; the only lines beyond the
rename are the includes (it names `PartialSet`, `IFormant`; `band_spectrum.h`
and `partials.h` were transitive weight) and the old-name paragraph, now one
line. The registry's `.label = "Full"` dropped: the derived label is
"Additive" (`kFamilyTable` row updated). Include sites name their types
(`patch_loader.cpp` three additive headers, the registrations six, the tests
nine, the editor `partials.h` alone); removing `composite_partials.h` from
the registrations fails the build (C3861). The executor's additions to the
table: `<cstdint>` in the three subclasses (their constructors name
`uint32_t`), no `<vector>` in `composite_partials.h` (it names no `std::`).
One comment inside `Partials` named the old class and was edited after the
body-diff ("the additive source used to run"). Proofs: ALL PASS 7405, null
gate 78/78, synthetics 13/13 identical, `gates --fast` as before, the meter
measured against an archive of `e900c73` (the committed snapshot is 3.6's):
files 209 → 214 (+5: six new headers, the alias gone, the pair renamed),
`files_over_limit` 19, `include_violations` 14, `dynamic_cast` 63,
`duplicate_blocks` 156 → 160 (preamble scaffolding, CLEANUP 31).

**Task 4 (`5f25207`, Fable).** RED watched: the three twins alone failed the
test build (C1083, `coherent_walk.h` missing). GREEN: `core/dsp/segment_walk.h`
(`dsp::SegmentWalk`: `seg_len`, `init`, `advance`, the three deleted
functions verbatim), `additive/coherent_walk.h` (`CoherentWalk`: `prepare`,
`start`, `advance`, `value`; the motion block verbatim with the layer ids as
arguments), `additive/pair_trade.h` (`PairTrade`); `Partials` holds
`motion_`, `shimmer_`, `trade_`, its `on_start` calls each layer's `start`
inside the same `if`, `on_next` each `advance`, the body reads `value(index)`
and `value(index >> 1)`, `prepare_note_arrays` calls the three `prepare(n)`
(the assigns the original made at prepare, one per layer — hence `prepare`,
not the table's `reserve`); the walk functions, `MotionWalk` and twelve
members gone. One harness lesson: `std::vector<W> w(size_t(n));` is a
function declaration (the most vexing parse); `static_cast<size_t>(n)` in
every twin. Proofs: ALL PASS 7405 → 7411 (three twins, two checks each:
48,000 / 252,000 / 10,800 rows compared bit for bit); null gate 78/78;
synthetics 13/13 identical; allocation gate PASS; `gates --fast` as before.
Mutations, each caught by its own twin and only its own: `(1 - coh) * ind`
reassociated → the coherent twin FAILs (7557); the pair index xor'd → the
pair twin FAILs (7585); then the primitive's `cur`/`target` draws reordered
→ the segment twin FAILs (7517) and the two layers built on it with it;
all restored, ALL PASS.

**Task 5 (`d4218e0`, Fable).** RED watched: the three twins alone failed the
test build (C1083, `bandwidth_noise.h` missing). GREEN: `additive/
bandwidth_noise.h` (`BandwidthNoise`: `prepare`, `start`, `factor` — the
body's block verbatim, returning `dry + wet * noise` so the multiply's right
operand is the same float expression), `additive/onset_gate.h` (`OnsetGate`:
`prepare`, `reset`, `start`, `advance`, `apply` — the assignment kept for
`t <= 0`; `sampleIdx_` lives here, reset at every start and advanced at every
next as before), `additive/decay_law.h` (`DecayLaw`: `prepare`, `start`,
`set_gain` in double as the cache loop had it, `advance`, `gain`); `Partials`
holds `bandwidth_`, `onset_`, `decay_`; the bandwidth start is unconditional
as it was, the decay and onset starts inside their `if`s; `on_next` calls
`onset_.advance()` where `++sampleIdx_` stood and `decay_.advance()` after
`ensure_partial_cache`; the body's three blocks are one call each;
`ensure_partial_cache` calls `decay_.set_gain` inside its `if`; eleven members
gone. Proofs: ALL PASS 7411 → 7418 (three twins: 110,400 bandwidth rows,
144,000 onset rows with the `t <= 0` rows counted and both signs of the input,
128,000 decay rows); null gate 78/78 (cello, viola and clarinet pin bandwidth
and onset on real patches); synthetics 13/13 identical; allocation gate PASS;
`gates --fast` as before. Mutations, each caught by its own twin and only its
own: the smoothstep reassociated → the bandwidth twin FAILs (7637); the
onset's assignment made `pampl *= 0.0f` → the onset twin FAILs (7683: the
sign of the zero); the law in float → the decay twin FAILs (7714); all
restored, ALL PASS.

**Task 6 (`e27f7ad`, Fable).** RED watched: the twin alone failed the test
build (C1083, `partial_layout.h` missing). GREEN: `additive/partial_layout.h`
(113 lines; `PartialLayout`: the six public arrays, `count`,
`original_count`, `snapshot`, `restore`, `reserve_expanded` — the expansion's
part of `prepare_note_arrays`, returning the expanded count — and `expand`,
`apply_expand_rule` verbatim over the members; the originals and the scratch
private); `Partials` holds `layout_`: `on_start` restores and expands on it,
`snapshot_originals` forwards to it, `prepare_note_arrays` asks it for the
count; `apply_expand_rule` and eighteen vector members gone; the array
members renamed `mult1_` → `layout_.mult1` across `partials.h` and the three
subclasses by one regex (85 sites; the `Array` pins `mult1` of
`ExplicitPartials` untouched, having no underscore). `partials.h` is 561
lines. Proofs: ALL PASS 7418 → 7908 (the twin: 54 rule × size × seed rows,
every expanded array compared bit for bit, `restore` giving the snapshot back
per row, and a reserved layout's `restore` + two expansions under the
allocation probe counting nothing); null gate 78/78; synthetics 13/13
identical (`expand`, `composite`, `sequence`, `explicit_evolving`,
`everything` among them); allocation gate PASS; `gates --fast` as before.
Mutations, each caught by the twin: the left sub-partials' spacing index
`count - j` → `j` (FAIL 7806); the centre copy dropped (FAIL 7806); restored,
ALL PASS.

**Task 7 (the close; an Opus 5.5 executor for the measurements, an Opus 5.5
reviewer, Fable).** Close A/B over the 131: identical 131, DIFFER 0, the
retired baseline present in the old arm only; the synthetics 13/13; the new
baseline's close render equals its manifest hash; allocation gate 132
patches 0 allocate. Meter `--delta 4d32899` (the committed snapshot was 3.6's;
only docs changed between, so the delta is the whole checkpoint): files 211
→ 221 (fifteen added, five removed), lines +78, `files_over_limit` 19 → 18,
`functions_over_limit` 98, `dynamic_cast` 63, `const_cast` 10,
`duplicate_blocks` 163 → 161, `include_violations` 14, `unmapped_files` 0;
`partials.h` 561 lines, `partial_value_impl` 149 → 134, `on_start` 131 → 72.
Timing (the executor, three rounds, `cli_pre38.exe` vs `e27f7ad`): the voice
patches, clarinet, viola and `fadd_sequence` within noise; `fadd_explicit_test`
slower in every paired round (render 1.04 / 1.12 / 1.14 over 3-, 5- and
10-round runs, medians 89.8 → 100.6 ms); `cello_full_range` 1.08 wall once on
a tight run, then noise. REVIEW (ruling 13): 0 Critical, 0 Major, 10 Minor,
11 Flag — no render can move, every moved block matches the base; the Minors
were the twins' grids and the record. The first fix wave `b254b5c` took every
Minor and four Flags (the grids, the hint test through the loader, four
comments, `partials_twin`, `is_partial_set`, the orphan); re-review:
closeable. That commit swept in the executor's staged `metrics.json` (the
snapshot at `e27f7ad`) — the snapshot is retaken at the docs commit. The
second fix wave `268fb0a`, for the timing: `PartialSet::next` had walked every
pin of the set with a kind branch per sample (eight Params and some thirty
Settings) where `Partials::next` made eight calls; `prepare` now lists the
Param pins into a `std::array<Param*, 16>` inside the node (an assert on the
cap; `Component`'s own sweep idiom) and `next` walks the list. Ten paired
rounds after it on a noisy machine: `fadd_explicit_test` render 0.99 (at or
below the old arm in nine of ten pairs), `cello_full_range` 0.96,
`viola_default` 1.02; ALL PASS 7588, null gate 78/78, synthetics 13/13,
`gates --fast` as before. Corrections to this record: the table's "a two-note
embedded score as the old file's" for the replacement baseline was wrong —
neither file has a score, both render four seconds in mixer mode; Task 2's
"advancing nothing of its own" for a composite is unobservable (it has no
`Param` pin) — the test proves no double-forward; Task 3's "+5 files" was that
task's own delta (the checkpoint's is +10). Docs: report R14, Part 3 §7's
landed paragraph, row 3.8 and the §1/§2 tokens, REVIEW R14, BACKLOG row 2
(Part 3 complete), CLEANUP 17/36/37/38, STATUS, this Ledger.

## Self-review notes

- `Partials::start` is 149 lines by the meter's count today; with six layer `start` calls in place of six blocks it should land near 60. `partial_value_impl` carries about sixty lines of comment (the anti-results, the truncf note); the extraction removes the bandwidth and onset blocks but the comments stay where their code stays — its new length is reported, not promised.
- The `PartialsRngLayer` enum stays in `partials.h` so the stream map is one list; each layer takes its ids as arguments. A layer that hard-coded its id would be self-contained but the map would be scattered over six files.
- `CoherentWalk::start` takes `hz` and `evolve` and computes `len_` itself (`std::max(1, int(rate / std::max(0.01f, hz)))`) — the motion and shimmer blocks compute it identically; the trade block's `std::max(1, int(rate / std::max(0.01f, hz)))` is the same expression with `evolve` 0.
- `BandwidthNoise::start`'s length guard is the original's `bandwidthHz.value < 1.0f ? 1.0f : bandwidthHz.value`, kept as written even though `on_setting_changed` already clamps `bandwidthHz` to 1 — removing a dead guard is not this checkpoint's business.
- The editor's `dynamic_cast<Partials*>` for the strip keeps working: `Partials` keeps its name and `get_array`. `is_partials_type` lacks `CompositePartials` today and will after; CLEANUP, Part 7.
- `AdditiveSource`'s `Slot<PartialSet> partials` and the editor's pin-compat lists name the same four types as before; nothing in the editor needs the new base's name except comments.
