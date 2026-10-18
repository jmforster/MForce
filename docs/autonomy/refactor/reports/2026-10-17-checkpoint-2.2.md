# Architecture report: checkpoint 2.2, `prepare(ctx)` and `start(frames)`

Commits `7e20e5f` through `5980873` (10 commits on main, 2026-10-17 and
2026-10-18), the second checkpoint of Part 2 (the ValueSource contract,
`docs/architecture/part2-valuesource-contract.md` sections 4 and 5). Report
R3 in the format R1 established; the independent review is section 8. The
plan it executed is `docs/superpowers/plans/2026-10-17-part2-checkpoint-2.2.md`;
that file's Ledger holds every ruling, dated by task.

## 1. Summary

One call became two. Until this checkpoint a node had one entry point for
"the host is here", `prepare(ctx, frames)`, called before every note, and it
did three jobs at once: learn the sample rate, allocate and size whatever
depends on it, and reset per-note state and lay it out over the note's
length. Now:

```cpp
void prepare(const RenderContext& ctx);   // once per build; the only way a node learns the rate; may allocate
void start(int frames);                   // once per note; reset and lay out; may not allocate
```

Both are template methods on `ValueSource`, the way `next()` is: the base
stores the context, sweeps the Auto pins and calls the node's hook
(`on_prepare(ctx)`, `on_start(frames)`); a node writes only the hook it
needs. The 45 constructors that took a sample rate take none; the 44
per-node copies of the rate are gone, read instead through `sample_rate()`
from the one context the base holds. `RenderContext` is created in one
place, the loader, where a patch's graph is finished and prepared, instead
of at every render call. The registry factory takes the seed alone. The
UI holds one context, set from the rate its audio stream actually opened
with, and prepares then starts its editor graph at every play; its three
48000 constants are one.

Renders did not change. The null gate reads 80/80 identical after every
one of the 10 commits; the whole-tree A/B against a binary built from the
commit before the checkpoint reads 1,458/1,458 identical over library (80), baselines (54), audition (708), pending (40), every third sweep patch (419) and every third archived patch (157); the patches absent from the count fail to render in both binaries. Each node family was
also A/B'd on its own over every stored patch that names one of its types
(1,007, 701, 68, 96, 747, 679 and 1,070 patches; 0 differ).

Two things were found on the way and are recorded, not fixed (section 7):
`DelayLineSource` read one sample past its ring, and a stage-less
`PhasedValueSource` crashes when rendered. The render-time cost carried
from checkpoint 2.1 is unchanged here (neutral: within 3 percent on oboe1, piano_default and viola_default, 1 percent on the forty-node chains).

## 2. Types and files

```
CHANGED  ValueSource  (engine/include/mforce/core/dsp_value_source.h, 228 lines)
  Owns: the host's RenderContext (private; read through sample_rate(),
    render_context(), prepared()), and the two template methods.
  Used by: every node; every host.
  Depends on: Component, RenderContext, the pins.
  Design choice: prepare() and start() are non-virtual; on_prepare() and
    on_start() are the hooks. Assignment keeps the context (as it keeps
    the registry): what the host attached to this object is not node state.
  Alternative: keep one virtual prepare(ctx, frames) and let each node
    decide what to do per call. Rejected: the per-note allocation the
    design forbids is then a per-node promise nobody can check.
  Name: "prepare" and "start" are Part 2 §4's words; "start" because a
    span of frames begins, not a note (a repetition or a stream also starts).
  Read: lines 35-75 (the contract), 118-135 (the getters and assignment).

CHANGED  RenderContext  (engine/include/mforce/core/render_context.h, 14 lines)
  Owns: the sample rate; 0 means "no host has prepared this node yet".
  Used by: prepare(ctx) everywhere; the loader makes one per patch.
  Design choice: an aggregate with a zero default, so a node's own copy
    (ValueSource::ctx_) reads unprepared until the host arrives.

CHANGED  Param, Input  (engine/include/mforce/core/pins.h)
  prepare(ctx) and start(frames) forward to the source; the Setting, Array
  and Slot kinds have nothing to forward. The migration shim that lived on
  Param and Input (not on PinBase: a class that declares prepare(ctx) hides
  every base overload of that name) is gone.

CHANGED  SourceRegistry  (engine/include/mforce/core/source_registry.h)
  SourceFactory = std::function<std::shared_ptr<ValueSource>(std::optional<uint32_t> seed)>;
  create(type, seed). 85 lambdas lost their first parameter.

CHANGED  MonoSource, StereoMixer  (engine/include/mforce/render/mixer.h, engine/src/mixer.cpp)
  prepare(ctx) once over gains, volumes, pans and channel sources;
  render(out, frames) starts them per span. render() no longer takes the
  context it only forwarded. The per-render re-prepare the 2026-10-02
  review's F260 named is gone: a plain sound is prepare once, then blocks.

CHANGED  PerformSource  (engine/include/mforce/render/perform_source.h)
  prepare(ctx) computes the wheel/pressure smoother coefficient;
  set_instrument_state(is) no longer carries the rate. The instrument
  prepares it with the voice.

CHANGED  Instrument, PitchedInstrument, DrumKit  (engine/include/mforce/render/instrument.h)
  Holds: RenderContext ctx in place of int sampleRate.
  prepare(ctx) prepares every voice's graph, its tap-only tails and its
  PerformSource (DrumKit: every drum source); every per-note site calls
  start(durSamples); the envelopes built per note (bend, glide, gate) are
  prepared from the member context and started.

CHANGED  patch_loader  (engine/src/patch_loader.cpp)
  build_graph() and build_subgraph_with_seed_perturbation() lose their rate
  parameter (nothing under them read it any more); make_perform_context()
  likewise. The context is made at the five places a patch's graph is
  finished — the instrument in load_patch_file (before the embedded score
  plays a note), its three mixer assemblies, and load_instrument_patch_json
  — and nowhere else in the engine. The channel wrappers gained prepare().

CHANGED  Envelope  (engine/include/mforce/core/envelope.h)
  Envelope() and a protected Envelope(SustainDefault{level}) — a tag, so the
  old Envelope(48000) is a compile error and not a sustain level of 48000.
  The stage counts are kept in step with the stages (add_stage,
  add_stage_default, remove_stage, replace_stages, on_prepare), so start()
  never resizes. The three static factories, envelope_from_preset_json()
  and compile_pitch_curve() return unprepared nodes.

CHANGED  Vibrato  (engine/include/mforce/filter/vibrato.h)
  Vibrato(uint32_t seed): its seven floats were setting initialisers; the
  loader sets them by name. The LFO chain is built in on_prepare, rebuilt
  on a setting change when prepared, and every note starts it from its
  constructed state: RedNoiseSource::reset() + start(). Section 6, item 4.

CHANGED  DelayLineSource  (engine/include/mforce/source/delay_line_source.h)
  The ring is sized at prepare, by compare; it carries one extra slot that
  stays zero (section 7, first item); capacity() is a diagnostic.

NEW      RedNoiseSource::reset(), BWLPSection::reset(), BWHPSection::reset(),
         HybridKSSource::target_count(), DelayLineSource::capacity(),
         DelayFilter::capacity(), Limiter::capacity()
  The resets restore a constructed state for an owner that used to rebuild
  the part per note (Vibrato's LFO, FM's decimation sections); the
  counts are read-only diagnostics for the tests, like tracks_frequency_live().

Every other node type (the recipe, plan section "The recipe (one node)"):
  the constructor loses its rate; on_prepare(ctx, frames) splits into
  on_prepare(ctx) — rate capture, sizing by compare, sub-node construction,
  the Manual pins' prepare — and on_start(frames) — today's per-note body
  in today's order, the Manual pins' start. 56 two-argument hooks became
  38 on_prepare and 55 on_start. The structural parts' interfaces follow:
  IPartials::partials_prepare(ctx) / partials_start(frames),
  IFormant::fmt_prepare(ctx) / fmt_start(frames), WaveEvolution::prepare(ctx).

tools/mforce_ui/main.cpp: g_renderContext (one, from the opened stream);
  DSP_SAMPLE_RATE gone; prepare_graph prepares then starts; saves write
  the stream's rate; the spectrum's rate follows it.
tools/engine_tests/main.cpp: split_equivalent() and the registry-wide
  sweep (every type: prepared once and started per note must render the
  same bytes as prepared and started before every note); one family of
  tests per task; the test doubles count starts.
```

## 3. Interfaces changed

| Before | After | Callers touched |
|---|---|---|
| `ValueSource::prepare(const RenderContext&, int frames)` (virtual) | `prepare(const RenderContext&)` + `start(int)` (non-virtual) | every host: instrument (7 sites), mixer, loader wrappers, UI (2), tests (118 calls) |
| `on_prepare(const RenderContext&, int)` (56 overrides) | `on_prepare(const RenderContext&)` (38) + `on_start(int)` (55) | every node with a hook |
| 45 constructors `X(int sampleRate, …)` | `X(…)` | registry (85 lambdas), loader (9 explicit sites), UI (4), tests |
| `SourceFactory(int sampleRate, std::optional<uint32_t>)`; `create(type, rate, seed)` | `SourceFactory(std::optional<uint32_t>)`; `create(type, seed)` | loader, UI (3), CLI (1), tests (3) |
| `MonoSource::render(const RenderContext&, float*, int)` | `prepare(const RenderContext&)` + `render(float*, int)` | CLI (8), explore (1), test_figures (1), UI (3), tests (5) |
| `StereoMixer::render(ctx, outLR, frames)` | `prepare(ctx)` + `render(outLR, frames)` | loader, CLI, explore |
| `PerformSource::set_instrument_state(is, rate)` | `set_instrument_state(is)` + `prepare(ctx)` | loader, instrument |
| `Instrument::sampleRate` (int) | `Instrument::ctx` (RenderContext) | loader, UI (DrumKit) |
| `build_graph(nodeMap, nodeOrder, rate, perf, onsets)` | `build_graph(nodeMap, nodeOrder, perf, onsets)` | loader (5 calls) |
| `Envelope(int)`, `make_ar(rate, …)`, `make_adsr(rate, …)`, `make_adsr_abs(rate, …)`, `envelope_from_preset_json(p, rate)`, `compile_pitch_curve(curve, rate)` | the same without the rate | presets, registry, loader, instrument, UI, tests |
| `Vibrato(rate, speed, depth, attack, threshold, speedVar, depthVar, zct, seed)` | `Vibrato(seed)` + `set_setting` by name | loader, tests |
| `IPartials::partials_prepare(ctx, frames)`, `IFormant::fmt_prepare(ctx, frames)`, `WaveEvolution::on_prepare(ctx)` | `partials_prepare(ctx)` + `partials_start(frames)`; `fmt_prepare(ctx)` + `fmt_start(frames)`; `WaveEvolution::prepare(ctx)` | the additive sources, WavetableSource |
| `ensure_constant(Component&, name)` | `ensure_constant(ValueSource&, name)`; prepares the constant it makes for a prepared node | Multiplex, loader |

## 4. Duplicates collapsed

- The per-render `prepare` of the mixer's gains, volumes and pans and of
  every channel source (mixer.cpp) and the per-note `prepare` of a voice's
  graph and tail list in three places of instrument.h are one `prepare`
  at build and `start` calls at the old sites.
- The three Butterworth filters built their sections in three constructor
  loops; they build them in three `on_prepare` bodies now, and the two
  section types gained an identical `reset()` (the refuter counted the three bodies and the two resets as new duplication; the fix wave gave the two section kinds one base, `BWSection` (members, the zeta formula, `process`, `reset`), and one builder, `build_bw_sections<Section>`, so each filter's `on_prepare` is the guard, the call and its cutoff reset).
- Vibrato built a fresh four-node chain on every note; it builds it once.
- The UI's three 48000 constants are one requested rate and one context.

## 5. Metrics

`python tools/structure/check.py --delta 1145a52` at the close
(head `5980873`):

| Measure | Before | After | Change |
|---|---|---|---|
| files | 167 | 167 | +0 |
| lines | 57,539 | 59,012 | +1,473 |
| files_over_limit | 20 | 20 | +0 |
| functions_over_limit | 99 | 99 | +0 |
| file_level_mutable_vars | 112 | 113 | +1 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 54 | 56 | +2 |
| changelog_comments | 255 | 254 | -1 |
| duplicate_blocks | 147 | 154 | +7 |
| include_violations | 13 | 13 | +0 |
| unmapped_files | 0 | 0 | +0 |

The one new file-level mutable is the UI's `g_renderContext` (section 7).
The two new `dynamic_cast`s are in the test file (a `PitchedInstrument*`
from an `InstrumentPatch`, twice); none in the engine. Of the new
duplicate blocks, lizard attributes +4 to the test file (the family test blocks) and -1 to `filters.h` (the Butterworth collapse); the two overlapping entries that list `limiter.h`, `reverb.h`, `basic_additive_source.h` and `delay_line_source.h` are new and are those headers' include preambles, token-identical since each gained `<algorithm>` for the `std::fill` in its start; the exact split of the whole-tree figure was not reproduced on the old tree (the meter does not run on an extracted archive).

Render time, the pre-checkpoint binary (`renders/scratch/ab_tree/cli_pre22.exe`,
the tree at `1145a52`) against the final one, the same patch rendered by
both alternately, minimum of 7 runs:

| patch | nodes | before (min of 7) | after (min of 7) | change |
|---|---|---|---|---|
| library/winds/oboe1 | 21 | 0.77 s | 0.79 s | +2% |
| library/keys/acoustic_piano/piano_default | 45 | 1.03 s | 1.06 s | +3% |
| library/strings/viola_default | 10 | 1.20 s | 1.20 s | 0% |
| micro F, forty unwired sines (R2's chain) | 79 | 3.08 s | 3.10 s | +1% |
| micro G, forty sines each with an envelope | 119 | 4.17 s | 4.20 s | +1% |

Within the run-to-run noise of this machine; the per-sample path is
untouched by this checkpoint (prepare and start run outside it), so no
change was expected. The 2.1 regression (R2 section 5) is still there,
carried to 2.3 as ruled.

Tests: engine_tests 5,823 checks before, 6,370 after. The registry
sweep (84 types; a stage-less PhasedValueSource is excluded by name, section 7)
and, per family, `split_equivalent` cases with wired pins, rate-dependence
checks (a node prepared at 24000 against 48000), `PinCounter` checks that a
Manual pin learned the rate (`rate`) and was started per note (`starts`),
and the behaviour the task changed in shape but not in effect (a reused
Vibrato's second note equals a fresh one's first; a reused FM's likewise;
an envelope keeps its context through a preset rebuild; DelayLine, Limiter
and DelayFilter follow a rate change; the Butterworth histories survive a
note boundary; a Repeating boundary starts its child and never prepares it).
Several of the family reviewers mutation-tested the new checks by removing
one line of the implementation at a time; where a check could not fail,
it was replaced (section 8).

Gates: `python tools/gates.py` (all) at the seal: CTest (engine_tests 6,370 checks, test_figures, the meter and its unit tests) pass; the UI stamp passes; the null gate reads 80/80 and the wiring null gate 80/80 (28 patches carried a paramMap); the two roundtrip gates fail on exactly the eight backlog-79 patches recorded before the checkpoint (`double_advance_bug`, `double_advance_ok`, the four `feedback/loop_*` repros, `perform/wiring_setting`, `perform/wiring_smoke`): 8 render diffs, 0 id changes, same names, same count.

## 6. Decisions and rejected alternatives

Each is a ruling in the plan's Ledger, in the order made.

1. **The base holds the context; nodes drop their copies.** Nineteen node
   files kept an `int` copy of the rate (Part 2 §5). One protected
   `RenderContext` in `ValueSource`, read through `sample_rate()`,
   replaces all 44 copies; a node may keep a *derived* float set in
   `on_prepare` (Partials' `rate_`, Mesh2D's `sr_`). Alternative: no
   storage, every rate-dependent constant computed at prepare. Rejected:
   a dozen nodes divide by the rate per sample.
2. **The context survives assignment.** `Envelope::replace_stages` assigns
   a fresh, unprepared envelope over a prepared one; without this rule
   every preset rebuild would zero the rate. It is checkpoint 2.1's
   registry rule extended: what the host attached to this object is not
   node state. Lesson 2.
3. **`prepare(ctx)` is idempotent: "build if absent or if the rate
   changed".** Required by the migration shim (per note until Task 9) and
   by the UI (per play); it also makes a rate change at the host a plain
   re-prepare. Sizing is by compare; structures are rebuilt only when
   absent or stale; the registry sweep fails any type that breaks it.
4. **`start` reproduces today's per-note effect, not a fresh node.** A
   RedNoise's rng, a Butterworth filter's history and a Segment's stream
   run on across the notes a voice slot plays today; the split keeps
   that. Vibrato alone needs fresh semantics (it rebuilt its chain per
   note) and gets them by resetting the chain it owns — RedNoise's
   `reset()` is the constructed state, an Envelope's `on_start` reseeds
   and re-lays out, Range and Constant hold no per-note state — never by
   changing RedNoise's own `on_start`. The same for FM's decimation
   sections (`BWLPSection::reset()`).
5. **`MonoSource::render` loses the context** it only forwarded;
   `MonoSource::prepare(ctx)` is the new door. Part 2 §5's "created in one
   place instead of forty" is not met while every render call makes one.
6. **The UI prepares at every play** (stream start, Generate) from one
   `g_renderContext` set from the opened stream. A play is not a note,
   prepare is idempotent, and the editor graph is re-wired between plays.
   The live keyboard's voices are prepared once by the loader and only
   started per note.
7. **The factory lost the rate at the close, not per family.** Migrated
   lambdas ignored the parameter until Task 9; one signature change, one
   commit.
8. **Allocation at Setup is tolerated through 2.6; allocation inside
   `start` is not removed here.** A per-note push binding may rebuild a
   preset envelope or Vibrato's chain, as today's per-note prepare did
   earlier in the same note. The per-note `assign`s inside Partials,
   HybridKS's `fill_table` and the others in section 7 stay where they
   are, for checkpoint 2.6's test.
9. **Two constructors take a tag or a seed alone against a silent
   conversion.** `Envelope(float)` would have accepted the old
   `Envelope(48000)`; so would `Vibrato(float speedHz, …)`. Every
   `(int sampleRate, uint32_t seed = …)` constructor became
   `(uint32_t seed = …)`, where a leftover `X(48000)` compiles with 48000
   as the seed; each task ran a grep over every construction of its types,
   and the close repeated it tree-wide (empty).
10. **`PerformSource` gets `prepare(ctx)`** rather than keeping the rate
    on `set_instrument_state`: it is a per-voice object that outlives
    notes, prepared by the instrument with the voice.
11. **Task 6: DelayLineSource keeps the old bytes through a zero slot**
    rather than wrapping the stray read (section 7, first item): a
    behaviour change is out of scope for a structure checkpoint
    (target-architecture principle 6).
12. **Task 7: SVF drops its rate-derived caches on a rate change**
    (the table did not list an `on_prepare` for it); **Vibrato prepares
    its chain on every `on_prepare`**, not only when it builds, so a rate
    change reaches the chain.
13. **Task 9: `render_context()` and `prepared()` are public getters.**
    `ensure_constant` can make a constant for an unwired pin after the
    node was prepared (a Multiplex `set_clone_param` at note Setup); it
    now prepares that constant with the owner's context. The host's facts
    are not secret; only the field is.
14. **The test runners stay under 80 lines and `main()` at 80**: each
    family's tests hang off one `run_prepare_start_tests()`.

## 7. Debt knowingly left

- **`DelayLineSource` reads one sample past its ring** when `writeIdx_ -
  len` is a hair negative and `rp += ring` rounds to exactly the ring
  length in float (`w=15, len=15.000002` → `i0 == ring`). Pre-existing;
  the old binary read whatever lay past the vector (zero in practice,
  18/18 runs identical); moving the allocation into `on_prepare` changed
  the heap layout and `patches/baselines/double_advance_ok.json` differed
  run to run until the ring was given one extra slot that stays zero
  (`ring_` is the logical length). The right fix, wrapping `i0` to slot 0,
  changes the bytes of any patch that hits the case and belongs to
  checkpoint 2.5 or Part 3's node pass, with that baseline refrozen then.
  Possibly the same class as the `stk_bandedwg_bar_struck` instability
  report R2 noted.
- **A stage-less `PhasedValueSource` indexes an empty `boundaries_` in
  `compute()`** (report R2 section 7); the registry sweep excludes it by
  name and its wired case is in Task 8's tests. Part 3.
- **`start()` bodies that still allocate**, for checkpoint 2.6's test
  (none added by this checkpoint; all pre-existing, now visible):
  `partials.h` — `update_arrays` via `init_arrays` (~703), `apply_expand_rule`
  (~718) and the orig/mult copies (276, 280), the `assign`s at 290-292,
  297-299, 308-309, 316-319, 336-342, 349-355, 361-363, `onsetDelay_.resize`
  (~370), `dtVals_.resize` in `init_detune_values` (~711);
  `hybrid_ks_source.cpp:46` (`table_.resize` in `fill_table`) and `:63-65`
  (`extract_partials`); `wavetable_source.cpp:32` (`values_.resize` in
  `fill_table`, first sample); `wave_evolution.h` from WavetableSource at
  the first sample — Bowed `adjust` 453-454, Brass 579, ReactionDiffusion
  852-855, CellularAutomaton 1018-1019 / 1028-1029, Target 194,
  HistogramEqualize 1100 / 1126, Bezier 1199; `phased_value_source.h:57, 90`
  (first start only); `segment_source.h:131-132` (`update_segments` from
  `compute()`); `basic_additive_source.h:61-66` (first sample of the first
  note); `additive_source2.cpp:110-126` (within the capacity reserved at
  prepare).
- **`start()` on a node nobody prepared** divides by zero or indexes an
  empty buffer in Release (the assert is Debug-only): DelayFilter, Limiter,
  Reverb, the Butterworth filters, Vibrato with a nonzero threshold. The
  fault flag that makes it loud in Release is checkpoint 2.5's.
- **A graph edited after prepare** (`mark_dirty` on a Multiplex, a Phased
  `add_stage`, an AdditiveSource2 definition setter, a late wire) is stale
  until the next prepare; the UI re-prepares at every play (ruling 6), the
  loader's voices are never edited in place. A Vibrato setting edited
  after `start` and before the next play rebuilds a prepared but not yet
  started chain (the next start resets it); the live-edit-during-play case
  is Part 7's.
- **`g_renderContext` is one more file-level mutable in `mforce_ui/main.cpp`**
  (113 now). It replaces a constexpr that lied about the rate; Part 7
  rebuilds the UI module by module.
- **WaveEvolution's two implementations (Bowed, Brass) keep their own
  `sampleRate_`**: they are parts, not ValueSources, until Part 3 moves
  them under Component.
- **`BWLPSection` and `BWHPSection`** share a base now; the three filters' `on_prepare` guards (`builtRate_`, the cutoff resets) are still three short copies, one per class.
- Comments: a few node headers still carry dated remarks outside this
  diff (`filters.h`'s 2026-08-12 CPU-audit note, `svf_source.h`'s header);
  `tools/engine_tests/main.cpp` is 5,613 lines in one file and gained
  1,025 (the split-equivalence and family tests); its split is Part 8's.
- The render-time cost from checkpoint 2.1 is carried to 2.3 as Matt ruled
  on R2; this checkpoint neither adds to it nor recovers it (neutral: within 3 percent on oboe1, piano_default and viola_default, 1 percent on the forty-node chains).

## 8. Independent review

A fresh reviewer (Fable) was given the commits, the module map, Part 2
sections 4-5, the plan with its Ledger and `docs/architecture/REVIEW_BRIEF.md`,
and told to refute: the ten items of the brief and six more for this
checkpoint (idempotence, "start is today's effect", the DelayLine slot,
allocation, unprepared paths in the hosts, vacuous tests). Each family
task also had its own reviewer before its commit; their required fixes
were applied before each commit and are summarised after the refutation.

### The refutation, verbatim

#### First pass (the checkpoint as committed at `85598ed`)

Build: `cmake --build build --config Release` clean (no warnings in the engine targets). `ctest --test-dir build -C Release`: 4/4 passed. `engine_tests.exe`: ALL PASS (6342 checks). `python tools/structure/check.py --delta 1145a52`: include_violations 13→13, duplicate_blocks 147→154 (+7), changelog_comments 255→254, dynamic_cast 54→56 (+2, both in tests: `engine_tests/main.cpp:5128, 5484`), file_level_mutable_vars 112→113 (`g_renderContext`, `tools/mforce_ui/main.cpp:233`).

**1. Separation of concerns — PASS.** Checked `engine/include/mforce/core/dsp_value_source.h:57-72` (contract only), `engine/src/patch_loader.cpp:1572,1697,1746,1804,1884` (the context is made where a graph is finished; loading is the render module's job per MODULES.md), `engine/src/mixer.cpp:48-71`, `engine/include/mforce/filter/vibrato.h:46-67` (one node, build and per-note reset). The UI change (`tools/mforce_ui/main.cpp:232-233, 4075`) adds one global to an already-monolithic file; it mixes nothing new.

**2. Single responsibility — PASS.** Could have failed at `ensure_constant` (`dsp_value_source.h:169-177`): it now finds-or-makes *and* prepares the constant. The prepare is the invariant of making a node under a prepared owner, not a second job. `RenderContext` (`render_context.h:238-240`), `MonoSource` (`mixer.h:10-14`), `Instrument::prepare` (`instrument.h:64`) each describe in one sentence.

**3. Dependency direction — PASS.** The diff adds two std includes (`dsp_value_source.h:2 <cassert>`, `vibrato.h:8 <string_view>`) and no module edge; `check.py --delta` reports include_violations +0. The 13 standing violations are the pre-existing render→music edges (`instrument.h:6-7`, `patch_loader.cpp`), not this checkpoint's.

**4. Duplication — FAIL.** `engine/include/mforce/filter/filters.h:138-146, 183-191, 226-237`: three `on_prepare` bodies identical but for the section type (the constructors were triplicated before; the diff adds a guard, `builtRate_` and `lastCutoff_` reset to each copy). `filters.h:82-85` and `119-122`: `reset()` written twice. `engine/include/mforce/source/additive/formant.h:42-54, 175-183, 257-265`: each node lists its pins twice (prepare, start). Meter: +7 duplicate blocks against the plan's "0 new". Fix: a function template `rebuild_sections(std::vector<S>&, int count, float rate)` in filters.h called from the three `on_prepare`s, and `reset()` on a shared section base.

**5. Encapsulation — FAIL (minor).** `engine/include/mforce/render/instrument.h:55`: `RenderContext ctx{}` is a public mutable member; writing it bypasses `prepare()` (:189-196) and leaves every voice at rate 0 with no diagnostic in Release. Nothing writes it today (grep over `engine tools`: none). The `int sampleRate` it replaced carried no such invariant. `render_context()`/`prepared()` public read-only (`dsp_value_source.h:79-80`) are fine. Fix: move `ctx` under `protected:` with `const RenderContext& render_context() const`.

**6. Ownership — PASS.** `Vibrato::lfo_` (`vibrato.h:113`) owns the chain; the chain's envelopes, ranges and constants hang off the RedNoise's pins. `RepeatingSource` lost its `ctx_` copy (`repeating_source.h`). `Instrument::ctx` by value. `ensure_constant(ValueSource&, …)` borrows. `PushBinding::cs` shares the pin's own constant (`patch_loader.cpp:1245-1257`, pre-existing). No cycle introduced; `PerformSource::prepare` (`perform_source.h:169-171`) holds nothing new.

**7. Abstraction fit — PASS, with departures recorded.** The two non-virtual template methods with hooks (`dsp_value_source.h:57-75`) are the plan's shape; `SustainDefault` (`envelope.h:92, 492`) is ruling 9; the factory `(std::optional<uint32_t>)` (`source_registry.h:180-181`) is ruling 7. Ledger-versus-signed-design: (i) §4 "`start` may not allocate" is not met (ruling 8 defers the listed bodies to 2.6); (ii) §5 "created in one place" is five loader sites plus the UI global; (iii) `engine/include/mforce/source/mesh2d_source.h:133` keeps `ctx.sampleRate > 0 ? … : 48000` — a second door for the rate and a contradiction of "0 means unprepared"; pre-existing line, but it survives the checkpoint whose rule is one door. Suggested for 2.5: drop the fallback.

**8. Real-time rule — PASS (nothing added).** The one new audio-thread call is `RepeatingSource::compute → start_repetition → input.start()` (`repeating_source.h:42-56`), legal by contract; its allocation depends on the child (an Envelope child no longer resizes in start, `envelope.h:287-374`). No new allocation, lock or throw on a `next()` path; `StereoMixer::render`'s `mono` vector (`mixer.cpp:64`) and `ensure_constant`'s `make_shared` at Setup (`dsp_value_source.h:173`) are pre-existing.

**9. Names and comments — FAIL (minor).** History references added in code: `tools/engine_tests/main.cpp:4519` "(Part 2 §4-5, checkpoint 2.2)", `:4552-4553` "listed in report R2 section 7 and fixed by Part 3", `:5478` "The hosts after checkpoint 2.2", `:5516` "Checkpoint 2.2: the contract's sweep". The plan's constraints forbid REVIEW/report numbers; git holds the history. No dated comment in the engine additions (grep of added lines: none). Names say what they are (`ring_`, `builtRate_`, `lfoDirty_`, `start_repetition`, `size_targets`, `reserve_note_arrays`); `Limiter::capacity()` (`limiter.h:36`) returns the lookahead length — loose, but documented as a diagnostic. Fix: strip the four comments of the checkpoint/report names.

**10. Tests — PASS.** `run_prepare_start_tests` (`engine_tests/main.cpp:5517-5529`) covers the contract sweep, every family and the hosts; all pass. Gaps are item 16.

**11. Idempotence of `prepare(ctx)` — PASS.** Every `on_prepare` checked: Vibrato re-prepares the chain on every call (`vibrato.h:46-52`), which reaches `Envelope::on_prepare`'s same-size `resize` (`envelope.h:283-285`, a no-op); BW filters return early at the same rate (`filters.h:139,184,227`) and reset `lastCutoff_` only on rebuild; FM guarded (`fm_source.h:37-40`); SVF guarded (`svf_source.h:49-54`); Limiter recomputes the same value and guards the assign (`limiter.h:30-33`); Reverb size-compares (`reverb.h:106-109`); DelayFilter (`filters.h:276-278`); DelayLine (`delay_line_source.h:36-41`); HybridKS grows only (`hybrid_ks_source.cpp:14-17`); AdditiveSource2 reserves (`additive_source2.cpp:96-108`); Multiplex and LayeredRed rebuild on a dirty flag only; Partials, Mesh2D, WaveEvolution store the rate; `PerformSource::prepare` recomputes the same coefficient. The tests pin it for Envelope (`:4616-4625`), the Shaper/Biquad/Multiplex pin counts, and the sweep's prepare-per-note arm. No node found whose second prepare changes a render.

**12. `start` is today's per-note effect — PASS.** Vibrato walk (old: `build_lfo()` per note, `vibrato.h@1145a52:59-61`): RedNoise `reset()` (`red_noise_source.cpp:13-21`) restores rampSize_, sampleCount_, lastValue_, lastSign_, nextValue_, zeroRamp_ and re-anchors the stream (`randomizer.h:40` = construction); `WaveSource::on_start` sets `ptr_=-1` (`dsp_wave_source.h:23`), `currPos_`/`lastPhase_` are re-initialised at `ptr_==0` (:31-34) and `currAmpl_/currFreq_/currPhase_/currPhaseIncr_` recomputed before `wave()` (:36-40); `SmoothnessInterpolator` holds only `smoothness`, set every sample (`smoothness_interpolator.h:10,20`); the two `RangeSource`s are stateless (`range_source.h:38-45`); the `ConstantSource`s are stateless; the two `ASEnvelope`s: `Envelope::on_start` resets rng_, lfo state, counts, ptr_, currStage_, stageStart_/End_, expandIdx_, gateActive_ (`envelope.h:287-374`); `raw_` is written before every read, and `gated_/gateStage_/gateFrom_` are never set on the chain (the loader collects `allEnvelopes` from valueNodes, `patch_loader.cpp:1538-1542`; `lfo_` is private). Not reset: `cur_` and `lastAdvancedSample_` — `cur_` is overwritten by `next()` before Vibrato reads it (`vibrato.h:73` uses `advance()`). Order: `start()` keeps Auto-pins-then-hook (`dsp_value_source.h:67-72`); WanderNoise now starts `amplitude` after the Auto pins instead of before (`wander_noise_source.h:33-34`); no node reads another node's state at start, so the order cannot show. FM: fresh sections had zero history; `reset()` zeroes history and the coefficients change only through the setting, which rebuilds (`fm_source.h:28-32`). Test `:5093-5106` pins reused-vs-fresh for Vibrato.

**13. DelayLineSource zero slot — PASS.** `delay_line_source.h:36-41, 72-87`: `i0 == ring_` only when `rp` rounds up to `ring_`, and then `frac == 0`, so the slot's value is multiplied by 1 and `buf_[i1 = 1]` by 0; the slot is never written (`writeIdx_` is reduced mod `ring_`, :87) and `on_start`'s fill keeps it zero; `ring_` is used for the clamp (:73), both moduli (:81, :87) and `capacity()` (:45); `buf_.size()` appears nowhere in `compute()`. The test (`engine_tests/main.cpp:5003-5020`) is not vacuous: a wrapped read would make `out[15]` 1.0. On the design question: preserving the bytes of an out-of-bounds read is what principle 6 asks of a structure-only checkpoint, and the wrap (a listed refreeze of `double_advance_ok.json`) belongs where the Ledger puts it. The comment at :77-79 should say the slot stands in for the read the correct wrap would send to slot 0, so a reader does not take silence for the design.

**14. Allocation — PASS (none added to `start`/`compute`).** The Ledger's list is complete for the per-note bodies. It omits a Setup-path group reachable from a per-note push binding (`instrument.h:354-361`): `Envelope::replace_stages`' `assign` and the preset `rebuild()` allocations (`envelope.h:204`, `envelope_presets.h`), `Vibrato::build_lfo` (`vibrato.h:54-57`), `FMSource::build_decimation` (`fm_source.h:31-35`), `HybridKS::size_targets`, `ensure_constant`'s `make_shared` (`dsp_value_source.h:173` via `multiplex_source.h:144`), `MultiplexSource::set_clone_param`'s map insert. None runs on the audio thread in today's hosts (the UI's `prepare_voice_at` runs on the UI thread under `g_audioMutex`, `main.cpp:4701-4716`), but 2.6's list should carry them so the thread question is decided once.

**15. Hosts — FAIL.** (a) `tools/mforce_ui/main.cpp:8269` prepares the DrumKit at `g_renderContext` (the opened stream's rate, :4075), while `:8285` sizes the render from `AUDIO_SAMPLE_RATE` and `:8559` writes the WAV header with it; the same constant drives `:13108, :13332, :13408-13456` (headless renders) and the fade/release allowances at `:4804, :4878`. When the device opens at a rate other than the requested 48000 (:4063), the kit's hits are laid out at one rate and the host's buffer and file at another — Release-silent. Fix: use `g_renderContext.sampleRate` for every frame count and WAV header, or refuse to start when `getStreamSampleRate() != AUDIO_SAMPLE_RATE`. (b) `vibrato.h:49-52`: `on_setting_changed` rebuilds and prepares the chain but never starts it. The UI pushes setting edits to the editor graph during continuous play (`main.cpp:9434-9435, 9546-9547`, under the mutex); the running note then pulls an unstarted `ASEnvelope` (zero counts → 0) so speed falls to 1 Hz and depth to 0 — vibrato vanishes until the next play, where the old code kept the running chain until the next note. Ledger Task 7 left this for Task 9; Task 9's entry does not close it. Fix: keep the last `frames` from `on_start` and, in `on_setting_changed` when `enabled_`, follow the rebuild with `lfo_->reset(); lfo_->start(lastFrames_)`. (c) Release-silent cases named: an `Instrument` never prepared computes `durSamples = int(d * 0)` (`instrument.h:372, 542, 757`) and renders silence with no assert; push chains (`instrument.h:354`) are `advance()`d without ever being prepared (no rate use, harmless today). Covered correctly: `ensure_constant` (`dsp_value_source.h:172-175`), AdditiveSource2's setters (`additive_source2.cpp:17,32,47,65,78`), Multiplex clones (`multiplex_source.h:83-86`), UI nodes between plays (`main.cpp:4223-4231`), the live keyboard's instrument rebuilt by the loader on every edit (`main.cpp:4663-4668`). Parts added after prepare (`MultiSource::add`, `PhasedValueSource::add_stage`, `FormantSequence::add_param`, `CompositePartials::add_param`, any late `set_param`) stay unprepared until the next prepare, as the Ledger accepts; no engine host does it (the loader wires before :1572/:1884).

**16. Tests — FAIL (gaps).** (a) `split_equivalent` (`engine_tests/main.cpp:4538-4540`) returns true when both arms throw and prints nothing, and the registry sweep (`:4547-4561`) is vacuous for every type that reads 0 at its defaults: the formant family (`formant.h:40,101,141,173,255` return 0), the Partials family, the evolution holders, DistortedSource (no input), NameGate, CurveNode. Wired family cases cover most, but none exists for DistortedSource, SequencePartials, BandSpectrum, or the Averaging/EKS/Brass/ReactionDiffusion/SortErosion/CellularAutomaton/HistogramEqualize/BezierPull evolutions under a WavetableSource (only Pluck, Reed and BitRotate are split-tested; Bowed is rate-tested only) — BrassEvolution's renamed `prepare(ctx)` rate capture (`wave_evolution.h:550`) runs in no test. (b) Changed behaviour with no test: `ensure_constant` preparing a late constant (only the Debug-only assert at `dsp_value_source.h:68` could see it, and CI runs Release); Vibrato's `on_setting_changed` on a started node (item 15b); the UI save writing the stream's rate. The host test (`:5480-5494`) renders one patch (BaselineSIN). Fix: print and CHECK the both-threw case, assert at least one non-zero sample for Generator-category types in the sweep, add wired split cases for Brass, Distorted and SequencePartials, and a Vibrato set-setting-after-start test.

**Overall: ACCEPT WITH FIXES.**

Required fixes, most severe first:
1. (15a) `tools/mforce_ui/main.cpp:8285, 8559, 13108, 13332, 13408-13456, 4804, 4878`: frame arithmetic and WAV headers must use `g_renderContext.sampleRate`, or the UI must refuse a stream that did not open at the requested rate.
2. (15b) `vibrato.h:49-52`: a setting changed on a started Vibrato must leave a started chain (reset + start with the last frames), or the live-edit path must be shown unreachable.
3. (16) `engine_tests/main.cpp:4538-4540, 4547-4561`: report the both-threw case; non-vacuity check for generators; wired split cases for Brass, Distorted, SequencePartials; a Vibrato set-after-start case.
4. (4) `filters.h:138-146, 183-191, 226-237, 82-85, 119-122`: one section-building template and one `reset()`.
5. (5) `instrument.h:55`: `ctx` protected with a const accessor.
6. (9) `engine_tests/main.cpp:4519, 4552-4553, 5478, 5516`: remove the checkpoint and report names from the comments.

Noted, no fix required this checkpoint: `mesh2d_source.h:133` 48000 fallback (item 7, for 2.5); the Setup-path allocation group (item 14, for 2.6's list); the zero-slot comment wording (item 13).

#### Second pass (the fix wave, working tree against `85598ed`)

Rebuilt `engine_tests` and `mforce_cli` in Release: clean. `engine_tests.exe`: ALL PASS (6368 checks); the sweep printed no "silent at its defaults", "both arms threw" or "split differs" line.

**1. (15a) UI rate — PASS with one literal left.** `tools/mforce_ui/main.cpp`: `AUDIO_SAMPLE_RATE` now appears only at :234 (the constant), :235 (`g_renderContext` initialiser), :3615 (the spectrum's static initialiser, overwritten at :11901 by `g_renderContext.sampleRate`) and :4065 (the stream request). The 19 converted sites read `g_renderContext.sampleRate` (:4806, :4880, :4904, :4971, :8180-8195, :8287, :8561, :13110, :13334, :13410-13458). Static initialisation is sound: `g_renderContext` is an aggregate initialised from a `constexpr int`, so it is constant-initialised, and no use of it precedes :235 (grep: none). Remaining rate assumption: `:3829 ringBudget = int(kMaxRingSec * 48000.0f)` — a sample count from a hard-coded rate in the live voice pool; at a non-48k stream the ring-out window is wrong by the rate ratio (benign, but the same class as the fixed sites). `:5315` and `:5420` are WAV-file reader defaults, not the render rate. Fix: `float(g_renderContext.sampleRate)` at :3829.

**2. (15b) Vibrato pending/retired — PASS.** `engine/include/mforce/filter/vibrato.h:46-50, 57-62, 66-75`. Lifecycle checked: `on_setting_changed` before prepare returns (:58) and the first `on_prepare` builds from the settings then (:47); two setting changes between starts: the second `retired_.reset()` is a no-op and the first pending chain is freed by the assignment at :60 on the setting thread; swap invariant: a pending chain exists only after a setting change, which has just nulled `retired_`, so `retired_ = std::move(lfo_)` (:67) never overwrites a live pointer — the audio thread moves two `shared_ptr`s and frees nothing; two starts without a setting change: no pending, no swap; `on_prepare` prepares both chains (:48-49) so a rate change reaches a pending chain; `reseed()` (:117) reaches `lfo_`, the chain the note plays, and the continuation path never creates a pending one (`instrument.h:503-506`: isSetting bindings are skipped on a held line, as before). Audio-thread callers of `on_setting_changed`: none — the engine's `set_setting` on push bindings runs at Setup (`instrument.h:356`), the UI's at :9436/:9549/:6081 under `g_audioMutex` on the UI thread, and `apply_param_map` (:4567) has no caller. A Vibrato under a `RepeatingSource` would have its `start` on the audio thread; that path only swaps. `build_lfo() const` (:92-114) builds the same chain as before. The test `run_vibrato_late_setting_tests` (`engine_tests/main.cpp:5558-5583`) fails under the previous in-place rebuild (the edited note's second 1000 samples would read an unstarted envelope and differ from the control) and fails if the next start does not swap.

**3. (16) Tests — FAIL (minor, the exclusion list).** `split_compare` (`engine_tests/main.cpp:4530-4552`) reports the both-threw case and the sweep prints it (:4567); the non-silence CHECK (:4584-4586) applies to Oscillator/Generator types. The exclusion list (:4577-4581) claims "those have wired cases in their family's tests": true for KSString, AllpassResonator, DelayLine, Mesh2D (:5026-5044), PluckEvolution (:4800-4808) and SegmentSource (:5464-5465); false for **KSPianoString** (appears nowhere in the file but the list) and **AveragingEvolution** (only the holder-shape tests at :2776/:2864, no split or wired case under a WavetableSource). The evolution holders registered as Combiner (`source_registrations.cpp:536-567`) escape the CHECK by category, which the comment's "parts read 0 unwired" covers. New cases: Brass split (:5513-5520) is non-vacuous by the rate pair at :5521-5533 (`!same_bits` cannot hold for two all-zero renders); Distorted (:5535-5542) and SequencePartials (:5544-5552) CHECK `equal && nonzero`; the Vibrato late-setting test can fail as described in 2, though it asserts no minimum swing (depth 0.1 over samples 1000-2000 is past the 22 ms attack, so the compared span moves). Fix: add KSPianoString with a noise `source` to `run_physical_split_tests` and an AveragingEvolutionSource under a WavetableSource to `run_wired_split_tests`, or remove the two names from the list and let the CHECK speak.

**4. (4) BW sections — PASS.** `engine/include/mforce/filter/filters.h:57-78` `BWSection` carries the shared members, the constructor with the zeta formula (identical text to `85598ed:62-64`), `process` and `reset`; `BWLPSection::update` (:93-103) is the old body verbatim, `BWHPSection::update` (:111-121) untouched; `using BWSection::BWSection` (:91, :109) keeps `(k, n, sampleRate)` so `FMSource::build_decimation`'s `emplace_back(float(i+1), float(nSec*2), subRate)` still binds. `build_bw_sections` (:81-85) clears then emplaces `(float(i+1), float(count*2), rate)` in the old order; the three `on_prepare`s (:137-141, :179-183, :220-226) pass `float(sample_rate())`. The bandpass now fills all hp sections before the lp ones instead of interleaving — two independent vectors, no numeric effect. The null gate decides the bytes; nothing here can move them.

**5. (5) Instrument context — PASS.** `engine/include/mforce/render/instrument.h:63-64` `prepare()` is the only writer, `render_context()` the reader, `ctx_` protected at :91; every internal read uses `ctx_`. Build of all tools passes, so no external reader of the old field remained.

**6. (9) Comments — PASS.** `engine_tests/main.cpp:4519, 4562, 5584, 5615` no longer name the checkpoint or the report. The one remaining "checkpoints" at :2163 is a pre-existing sentence about the per-tick memo, not this diff's.

**7. (13) Slot comment — PASS.** `engine/include/mforce/source/delay_line_source.h:77-80` names slot 0 as the correct read and the slot as a stand-in for the old read's result until the render change is taken.

**Overall: ACCEPT WITH FIXES.**

Required fixes:
1. (3) `tools/engine_tests/main.cpp:4578-4579`: give KSPianoString and AveragingEvolution the wired case the list claims, or remove them from the list.
2. (1) `tools/mforce_ui/main.cpp:3829`: the ring budget from `g_renderContext.sampleRate`, not `48000.0f`.

### What was done

Every required fix of both passes is in `5980873` (the fix wave) and the
two of the second pass in the same commit; the null gate reads 80/80 and
the wiring gate 80/80 on it, and the whole-tree A/B in Appendix A is of
that binary.

- **15a, the UI's rate.** Nineteen sites that computed frame counts, WAV
  headers, headless renders, stream-prep lengths and the fade and release
  allowances from the requested `AUDIO_SAMPLE_RATE` now read
  `g_renderContext.sampleRate`, and after the second pass so does the live
  voice pool's ring budget. The constant is used only to ask the device.
- **15b, Vibrato.** Not the suggested restart (which would have made a
  mid-note edit restart the vibrato from phase 0, a behaviour the old code
  never had) but the old behaviour exactly: a setting change builds the
  NEXT chain into a pending slot on the calling thread and the next
  `start` swaps pointers; the playing note keeps its chain; the chain the
  swap retires is freed at the next setting change, so the audio thread
  neither allocates nor frees. The second pass walked the three-pointer
  lifecycle and found no hole. Section 6, Task 10.
- **16, the tests.** `split_compare` reports whether a render had any
  signal and whether both arms threw; the sweep checks non-silence for
  Oscillator and Generator types, with a named list of sources that are
  silent until wired (each now with a wired case: the second pass found
  two of the eight names without one, and they got theirs); wired split
  cases for Brass under a wavetable, DistortedSource, SequencePartials,
  KSPianoString and an averaging evolution; a Vibrato mid-note setting
  test that fails under the in-place rebuild.
- **4, duplication.** `BWSection` is the base of the two section kinds
  (members, the zeta formula, `process`, `reset`) and
  `build_bw_sections<Section>` the one builder; `filters.h` has no
  duplicate block left. The formant nodes' prepare/start pin pairs the
  refuter also named are the contract's shape (a Manual pin is named in
  both hooks) and were left.
- **5, encapsulation.** `Instrument::ctx_` is protected, written by
  `prepare()` only, read through `render_context()`.
- **9, comments.** The four test comments no longer name the checkpoint or
  the report.
- **13, the slot comment** says the correct read is slot 0.
- **Noted for later** (section 7): the Mesh2D 48000 fallback for a zero
  rate (2.5); the Setup-path allocations joining 2.6's list; the
  Release-silent zero-rate arithmetic on a never-prepared instrument
  (2.5's fault flag).

### The per-task reviews, in one paragraph each

- **Task 2 (core):** `main()` had crossed 80 lines (grouped into one
  runner); two stale comments; a reused-FM test that could not catch a
  missing `reset()`; a vacuous `remove_stage` check. Noted: an
  Envelope's stage counts are sized as stages are added, so a never-started
  envelope reads 0 through the stage walk instead of the early return.
- **Task 3 (noise):** the `reset()` test was vacuous at the defaults (the
  first 400 samples are zero; the reviewer mutation-tested it) and now runs
  at density 1 with continuity and sign tendency live, in two rounds; the
  Manual pins' rate is checked; the table's "Brown" is `BlueNoiseSource`.
- **Task 4 (wavetable):** the `numPartials` test relied on an out-of-range
  read to fail and now reads the array size the setting hook produced; a
  bowed evolution rendered at 24000 vs 48000 proves the rate reaches the
  part; HybridKS sizes its targets from `set_target_partials` too.
- **Task 5 (additive):** three comments still said prepare where the work
  happens at start; AdditiveSource2's setters guard with `prepared()`
  (re-reserve; prepare an envelope assigned late); the Formant pins' rate
  is checked. `formant.h` has five `IFormant` implementers, not four.
- **Task 6 (physical):** the DelayLine finding above; member comments on
  the ring; the zero-slot test renders past a full lap. Accepted.
- **Task 7 (filters, Vibrato):** accepted outright; the Vibrato chain was
  walked member by member (no stale member reaches the next note).
- **Task 8 (combiners):** accepted; two nits taken (a comment, the
  repetition helper renamed `start_repetition`); two notes carried into
  Task 9 (the late constant, ruling 13; stale graphs, section 7).

## 9. Reading guide

Ten minutes: `dsp_value_source.h` lines 35-75 (the two calls and the
hooks) and 118-135 (the getters, what assignment copies); `pins.h` lines
39-41 and the Param/Input declarations; `mixer.h` whole; `instrument.h`
`PitchedInstrument::prepare` and `prepare_voice_at`; `patch_loader.cpp`
at `inst->prepare(RenderContext{sampleRate})` (the one place). Then one
node of each shape: `limiter.h` (sizing by compare at prepare, reset at
start), `vibrato.h` whole (a structure built once and reset per note —
ruling 4), `repeating_source.h` (a child restarted on the audio thread),
`multiplex_source.h` lines 82-100 (owned clones), `delay_line_source.h`
lines 36-62 and 73-87 (the zero slot). Twenty minutes more: section 6
against the plan's Ledger, and `tools/engine_tests/main.cpp`
`split_equivalent` and `run_prepare_start_tests` (what "the split is exact"
means mechanically).

## 10. C++ lessons

### Lesson 1: a template method with two phases, and why neither is virtual

Checkpoint 2.1's lesson was `next()`: the base owns the skeleton, the node
fills one step (`compute()`), and because `next()` is not virtual no node
can run its own sweep. This checkpoint applies the same shape twice.
`prepare(ctx)` stores the context, sweeps the Auto pins and calls
`on_prepare(ctx)`; `start(frames)` sweeps the pins and calls
`on_start(frames)`. In C# you would write two non-virtual public methods
and two `protected virtual` hooks with empty default bodies (C# has no
pure-virtual-with-default; `abstract` would force every node to override
both, which most do not need); in Java, two `final` methods and two
`protected` hooks.

Why two phases and not one call with a flag? Because the contract differs:
one may allocate and the other may not, and a rule attached to a method
name is a rule a test can wrap (checkpoint 2.6 wraps `start` and `next`
and fails on the first `new`). JUCE makes the same cut with
`prepareToPlay(sampleRate, blockSize)` and `processBlock`; a note here is
an event inside the second phase, which is what lets a mixer render in
blocks without re-preparing anything (section 2, StereoMixer).

The migration used the two-phase shape against itself: during the cut the
base's `start()` also called the OLD two-argument hook, so an unmigrated
node kept working per note while migrated ones ran their new hooks, and
the registry-wide test compared "prepare before every note" with "prepare
once" for every type after every family. At the close the old hook was
deleted and `override` on it became a compile error anywhere it survived
(Lesson 2 of R2) — the compiler listed zero.

### Lesson 2: what assignment copies, and what it must not

`Envelope::replace_stages` ends with `*this = std::move(fresh)`: a preset
rebuilds its stage list by building a fresh envelope and assigning it over
itself. In C# that line would rebind a reference and the old object would
be gone; in C++ it is a member-wise copy INTO the existing object, which
keeps its address, its registration with its owner — and, after this
checkpoint, the context its host gave it. Two things in a node are not
"its own state" in that sense: the pin registry (2.1: a copy's pins would
not re-register, so `Component::operator=` copies nothing) and now the
`RenderContext` (a fresh envelope has none; copying it would silently
zero the rate of a prepared node on every preset edit). So
`ValueSource::operator=` copies `cur_` and leaves `ctx_` alone, and a test
pins it: an AREnvelope prepared at 22050 and then given a new attack
renders bit-for-bit like one configured before prepare.

The general rule a C++ reader takes from it: when a class holds something
that describes its RELATIONSHIP to the outside (who owns it, who prepared
it, what it is registered with) rather than its value, write the
assignment operator by hand and say which members it skips. The default
member-wise one is right only for value types.

### Lesson 3 (short): a constructor that would silently accept the old call

Removing a parameter is only safe when every old call stops compiling.
`Envelope(int sampleRate)` → `Envelope()` is safe. But a class that also
had `Envelope(int sampleRate, float sustainDefault)` would, after dropping
the rate, offer `Envelope(float)`, and the old `Envelope(48000)` would
compile with a sustain level of 48000. The same trap sits in every
`X(int sampleRate, uint32_t seed = …)` constructor: `X(48000)` becomes a
seed. C++ converts between arithmetic types without a word; C# and Java
would reject the int-to-float or int-to-uint narrowing in most of these
spots. The defence used here was a tag type (`Envelope::SustainDefault{1.0f}`:
an int does not convert to a struct), a seed-only constructor where the
floats were really settings (Vibrato), and a grep over every construction
of every converted type, run per task and again at the close.

## Appendix A: the whole-tree A/B

A binary built from `1145a52` (the plan commit; engine last changed at
`9f2b4a8`) against the checkpoint's head, every patch rendered by both,
WAV bytes compared.

| set | patches in the set | rendered by both | differ |
|---|---|---|---|
| patches/library | 80 | 80 | 0 |
| patches/baselines | 54 | 54 | 0 |
| patches/audition | 708 | 708 | 0 |
| patches/pending | 43 | 40 | 0 |
| patches/sweep, every third | 457 | 419 | 0 |
| patches/old, every third | 273 | 157 | 0 |

A patch missing from "rendered by both" failed to render in both binaries
(the old binary's failures are the same files as the new one's; none is
present in only one arm). The set files are `renders/scratch/ab22_tree/*.txt`,
the hashes under `renders/scratch/ab22_tree/<set>/{old,new}/hashes.txt`.
