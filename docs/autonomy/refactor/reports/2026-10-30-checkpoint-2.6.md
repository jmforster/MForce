# Architecture report: checkpoint 2.6, the allocation test over every baseline patch

Part 2 of the target architecture, checkpoint 2.6 — commits `6a37849`..`d3df3ea` on
main, 2026-10-29 and 2026-10-30. Plan: `docs/superpowers/plans/2026-10-29-part2-checkpoint-2.6.md`
(its Ledger holds every ruling). Spec: `docs/architecture/part2-valuesource-contract.md`
section 4 ("`prepare` may allocate; `start` may not"; "the allocation test asserts
both … over every baseline patch … with no tolerance list") and section 7's row 2.6
("none; the test is the new gate"). Measurement:
`docs/architecture/measurements/2026-10-29-allocations.md`. The format is the one
agreed at checkpoint 0 (report R1). With this checkpoint Part 2 is complete.

## 1. Summary

`start()` and `next()` allocate nothing, and a gate says so for every patch under
`patches/library` and `patches/baselines`. Until this checkpoint the rule was a
sentence in Part 2 section 4 and a work list in the 2.2 report's debt section;
now it is a running test:

```
alloc_gate [--list] [--verbose] <patch.json | folder>...
```

A static library `mforce_alloc_probe` replaces the global `operator new` and
`delete` family in the executables that link it — `alloc_gate` and
`engine_tests`, never the engine, the CLI or the UI — and counts the calling
thread's allocations while armed, keeping the first one's return addresses.
The gate loads and prepares each patch unarmed, then arms around the live path
the UI's keyboard uses — a pool slot, `prepare_voice_at` (the note's Setup,
then `start` on the voice and its tails), one pull per sample for the note's
life — once per note of the patch's embedded score, or once for a C4; a
mixer-mode graph renders each channel source, whose adapter calls `start` and
`next`. A patch that allocates is reported with its count and the first
allocation's call stack, symbolized; the exit code is 1 if any did.

The first run over the 132 library and baseline patches found 97 allocating,
all in the families the 2.2 report listed — Partials in 75, WavetableSource
and its evolutions in 11, SegmentSource in 8, HybridKSSource, PhasedValueSource
— plus one it had not: a Multiplex push delivery at note-on. Each moved to
`prepare` by one rule: size for what prepare can see, fit within that capacity
at `start`. A count fixed by a setting (partials, stages, the shape's pairs,
the histogram's bins) is sized at prepare, because a graph edited afterwards
is stale until the next prepare (2.2 ruling 6); a length that depends on the
note's frequency (the two tables, the evolutions' buffers, BasicAdditive's
partial count) is reserved for a stated floor, `WaveSource::kMinTableHz` =
20 Hz, the DelayLine's since 2.2, and a note below the floor gets the floor's
length. The last gate run: 132 patches, 0 allocate.

Three allocations the gate found are the offline hosts', not a node's, and the
gate's arming excludes them: `PitchedInstrument::play_note` and
`DrumKit::play_hit` allocate the buffer they pre-render a note into, and
`StereoMixer::render` a scratch vector per call — the CLI's offline model,
listed for Part 5. One was the live path's and is fixed: `prepare_voice_at`
copied the voice's tail list into the `StreamingVoice` it returned, one
allocation at every live note-on on a loop patch; the list is a `std::span`
over the pool voice's now. The live legato Setup (`continue_voice_live`,
which builds a glide envelope) still allocates and is listed (section 7).

The independent review found two more, both fixed in the wave: the
Partials originals were snapshotted at prepare from arrays a note leaves
expanded, so a second prepare after a note (the UI prepares every node again
at every Play) would have expanded twice; and `EKSEvolution`'s pick-position
comb allocated a vector per note and gave the owner's table a new, smaller
buffer — hidden in the gate's set because `hi_hat1`'s pick position is 0.

Renders: byte-identical throughout. The null gate is 78/78 after every task,
and the whole-sample A/B against checkpoint 2.5's engine differs on nothing: 0 of 1,615 patches moved in any of the eight sets.
Render time: within noise against checkpoint 2.5's engine — every row within
seven percent and the second-long rows within four, both signs, on an idle
machine; a reserve at prepare costs nothing per sample.

## 2. Types and files

```
NEW      alloc_probe  (tools/alloc_probe/alloc_probe.h, alloc_probe.cpp; static library mforce_alloc_probe)
  Owns: the program's global operator new/delete (every form: plain, array,
    nothrow, aligned; the matching deletes), forwarding to malloc/free and
    _aligned_malloc/_aligned_free; thread_local state — armed, count, bytes,
    the first armed allocation's return addresses (CaptureStackBackTrace on
    Windows); arm(), disarm(), armed(), count(), bytes(), first_stack(), reset().
  Design choice: a definition of the replaceable allocation functions in any
    translation unit of a program replaces the library's for that program, so
    linking the library is the whole switch, and the engine, the CLI and the
    UI, which do not link it, keep the default allocator untouched. The API
    and the replacements live in one object, so pulling arm() from the
    static library pulls the replacements with it.
  Rejected: _CrtSetAllocHook (Debug CRT only; the gates run Release); an
    allocator parameter on every container in every node (the whole tree,
    for a test); a hook called from ValueSource::start/next (a function-pointer
    test on the per-sample path in production).
  Name: "probe" — it observes and reports; it never refuses an allocation.
  Read: the header, then record() and the replacement set in the .cpp.

NEW      alloc_gate  (tools/alloc_gate/main.cpp)
  Owns: collect (files and folders), gate_one (load unarmed; the instrument
    path or the mixer-mode path), gate_instrument (the live path, per
    embedded-score note or one C4), gate_mixer (each channel source's render),
    report (ALLOC with the symbolized first stack / ok / --list counts),
    the exit code (1: any allocation; 2: a load failure).
  Design choice: the live path, not play_note. play_note pre-renders the
    note into a buffer it allocates — the offline host's model — and the
    loader's embedded-score delivery does control-side work between note-ons
    (JSON reads, the NotePerformer), so an arm around either counts what is
    not the engine's. prepare_voice_at + one pull per sample is the path the
    UI's keyboard runs in real time and the one whose allocations matter.
  Rejected: arming around load_patch_file (prepare may allocate; the
    pre-render happens inside the load); arming around the whole embedded
    delivery (the performer and the JSON reads).
  Read: gate_instrument, then gate_mixer; the header comment.

CHANGED  PitchedInstrument::StreamingVoice  (engine/include/mforce/render/instrument.h)
  advanceList is std::span<const std::shared_ptr<ValueSource>> over the pool
    voice's list, which outlives the handle: a live note-on copies nothing.
    The UI keeps its own owning copies by one assign (its tails die off-thread
    with the voice). prepare_voice_at copied a vector per note-on before.

CHANGED  PitchedInstrument::PushBinding, prepare, apply_note_bindings, deliver_continuation
  cloneSlot: the Multiplex fan-out target's slot, registered at prepare
    (clone_param_slot); the two note-on sites write set_clone_param(slot, v).

CHANGED  MultiplexSource  (engine/include/mforce/source/multiplex_source.h)
  Owns: CloneParam {nodeId, paramName, value, delivered}; clone_param_slot()
    (find or append; gives every clone's pin its constant); set_clone_param(slot,
    value) (write and fan out, no allocation); rebuild_ replays delivered
    values onto fresh clones. The string-keyed unordered_map of pending
    fan-outs, inserted into at every voice's first note-on, is gone, and so is
    the string overload of set_clone_param (its only callers were the two
    note-on sites).
  Read: clone_param_slot, set_clone_param, ensure_clone_param_sources_.

CHANGED  WaveSource  (engine/include/mforce/core/dsp_wave_source.h)
  kMinTableHz = 20 Hz: the floor a frequency-dependent table is sized for.
    The pin's declared minimum, 0.01 Hz, is no floor — a 19 MB table per
    voice; 20 Hz is DelayLine's since 2.2, and the survey found nothing in
    use below 220 Hz as a constant, the keyboard's A0 at 27.5 Hz being the
    lowest note. A note below the floor gets the floor's length (REVIEW R7
    item 3).

CHANGED  WavetableSource, HybridKSSource  (wavetable_source.h/.cpp, hybrid_ks_source.h/.cpp)
  maxLen_ = sample_rate() / kMinTableHz + 1, reserved at on_prepare;
    fill_table clamps a longer length to it and resizes within capacity.
    HybridKS's size_targets sizes the three arrays the transition's DFT fills
    along with the target amplitudes.

CHANGED  WaveEvolution and seven implementations  (engine/include/mforce/source/wave_evolution.h)
  prepare(const RenderContext&, int maxLen): the owner's longest table. Target
    reserves its resampled target, Bowed its two lines (each at most one
    period), Brass its raw loop, ReactionDiffusion its four fields,
    CellularAutomaton its two states, Bezier its target; HistogramEqualize
    sizes its cdf and keeps its histogram as a member (counts_) sized at
    prepare instead of a std::vector<int> per rebuild — the 1,025 allocations
    of algev_test_he.
  Design choice: one argument on the existing virtual; every implementation
    touched once. Rejected: a second reserve(maxLen) hook beside prepare(ctx)
    — two hooks for one phase.

CHANGED  Partials  (engine/include/mforce/source/additive/partials.h)
  prepare_note_arrays(), called from partials_prepare: update_arrays if a
    setting changed (which also snapshots the originals), the expanded count (the partial count times
    (count·2+1)^(recurse+1) when an expand rule is set, from the originals),
    and every array partials_start fills sized to that count —
    the optional ones (decay, bandwidth, motion, shimmer, trade, onset)
    included, whether or not the feature is on. partials_start restores the
    originals and expands in member scratch (exM1_..exP2_) within that
    capacity; the array rebuild on a changed setting moved from start to
    prepare.

CHANGED  PhasedValueSource, SegmentSource, BasicAdditiveSource
  on_prepare sizes the stage counters and boundaries; reserves the segment
    list for the shape's pairs plus the gap; reserves the five per-partial
    arrays for the count at the floor (12 kHz / kMinTableHz = 601) and a lower
    note gets that count.

CHANGED  patch_loader.cpp
  perform_score_json(PitchedInstrument&, const json&): the embedded score's
    delivery as one function (the NotePerformer, bpm 60, hold/onset,
    articulation/ornament, the open line finished), called from
    load_patch_file's pre-render. Extracted for a second caller the gate
    turned out not to need (section 6, ruling 13); it stays as a shorter
    load_patch_file.

NEW      tools/survey_table_frequencies.py
  Every WavetableSource and HybridKSSource node's frequency per folder, the
    lowest constant per folder: the floor is chosen from use, not guessed.

CHANGED  tools/null_gate_perform_source.py — unchanged this checkpoint; tools/gates.py gains
  the allocation gate in its slow list (the run is 230 s on this machine).

CHANGED  tools/engine_tests/main.cpp
  run_alloc_probe_tests (the probe itself); allocations_in_one_note(json,
    noteNumber) — one note through the live path, armed from note-on to the
    last sample; run_allocation_tests_partials / _tables / _nodes / _pins —
    one or more notes per family at two pitches, the floor, below it, the
    pin's maximum; a repetition boundary, a held note's release, a drum hit.
```

## 3. Interfaces changed

| Before | After | Callers touched |
|---|---|---|
| — | `alloc_probe::{arm, disarm, armed, count, bytes, first_stack, reset}`; the program's `operator new`/`delete` replaced in `alloc_gate` and `engine_tests` | the gate, the tests |
| `StreamingVoice::advanceList` a `std::vector` copied per note-on | `std::span<const std::shared_ptr<ValueSource>>` over the pool voice's list | the UI's `voice_schedule_unlocked` (a span parameter; its own owning copy by one assign); `explore.cpp` and the UI's preview iterate it unchanged |
| `MultiplexSource::set_clone_param(nodeId, paramName, value)` | `int clone_param_slot(nodeId, paramName)` at prepare; `set_clone_param(int slot, float value)` at note-on | `PitchedInstrument::prepare` registers; the two note-on sites write the slot |
| `WaveEvolution::prepare(const RenderContext&)` | `prepare(const RenderContext&, int maxLen)` | `WavetableSource::on_prepare`; the seven implementations |
| `WavetableSource::fill_table`, `HybridKSSource::fill_table` resize to the note's length | reserve `sample_rate() / kMinTableHz + 1` at prepare; clamp and resize within capacity | — |
| `Partials::partials_start` rebuilds the arrays on a changed setting and sizes every per-note array | `partials_prepare` → `prepare_note_arrays()` does both; `partials_start` fills within capacity | — |
| `PhasedValueSource::on_start`, `SegmentSource::update_segments`, `BasicAdditiveSource::wave` size their arrays | sized or reserved in `on_prepare` | — |
| `PushBinding` | gains `cloneSlot` | the two delivery loops |
| `load_patch_file`'s inline score walk | `perform_score_json` (file-local) | the pre-render |
| `tools/gates.py` slow list | + the allocation gate over `patches/library patches/baselines` | — |

## 4. Duplicates collapsed

- The loader's embedded-score walk is one function (`perform_score_json`),
  which shortens `load_patch_file` by seventy lines; its planned second caller
  was withdrawn (ruling 13), so it is a step, not a shared function.
- `HistogramEqualizeEvolution` no longer builds a counts vector per rebuild;
  `MultiplexSource` no longer keys pending fan-outs by a concatenated string.

Not collapsed, listed: `WavetableSource::fill_table` and `HybridKSSource::fill_table`
were twins before this checkpoint and are twins still, each three lines longer
by the same floor clamp (Part 3's node pass). The three `init_arrays` of the
Partials subclasses are untouched.

## 5. Metrics

`python tools/structure/check.py --delta fb01002` at the seal (`d3df3ea`; the two review passes moved no measure but lines, +74):

| Measure | Before (`fb01002`) | After (the seal) | Change |
|---|---|---|---|
| files | 167 | 170 | +3 |
| lines | 59,561 | 60,370 | +809 |
| files over limit | 20 | 20 | +0 |
| functions over limit | 99 | 99 | +0 |
| file-level mutable vars | 115 | 120 | +5 |
| `const_cast` | 10 | 10 | +0 |
| `dynamic_cast` | 58 | 58 | +0 |
| changelog comments | 250 | 250 | +0 |
| duplicate blocks | 159 | 158 | −1 |
| include violations | 13 | 13 | +0 |
| unmapped files | 0 | 0 | +0 |

The three files are the probe (136 lines) and the gate (235); of the 809
lines, 286 are `engine_tests` (the probe test, two helpers, four family test
functions, the two review passes' three cases), 39 `partials.h`, 26 `wave_evolution.h`, 26 `multiplex_source.h`,
12 `basic_additive_source.h`, the rest a line or a handful each. The five
mutable variables are the probe's `thread_local` state — armed, count, bytes,
the stack, its depth — in the test-only library; the meter counts them, and
a probe that is a global is what a replaced `operator new` has to be. The one
duplicate block gone is the Partials' scratch (the first landing's expand
rule had six identical local vectors; the members are declared once).

The gate's own numbers (`docs/architecture/measurements/2026-10-29-allocations.md`):
the first run over the 132 library and baseline patches, 97 allocate —
`FullPartials::init_arrays` 68, `Partials::partials_start` 7,
`WavetableSource::fill_table` 11 (one of them 1,025 times), `SegmentSource::update_segments`
8, `HybridKSSource::compute_ks` 1, `PhasedValueSource::on_start` 1,
`MultiplexSource::set_clone_param` 1 — and the last, 0 allocate, exit 0. Every
patch not in the first list allocated nothing already (35 of 132), oboe1 and
the whole piano family among them. The run took 230 s on the measurement (97
stacks to symbolize) and 78–139 s inside `gates.py` at the seal; over the 60
seconds the plan set for a ctest either way, so the gate is in `gates.py`'s
slow list (REVIEW R7 item 0).

The A/B over the sample (Appendix A): `old` (checkpoint 2.5's engine, `cli_pre26.exe`)
against `new` (this seal) over the eight sets — DIFFER 0 in every set, the same
119 non-loaders in both arms (the 116 unknown node types under `old/` and
pending's three). The checkpoint changes no byte of any render: what moved
is when a buffer is sized, never what is written into it.

Render time (`python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre26.exe
build/tools/mforce_cli/Release/mforce_cli.exe 7`; "speedup" = base / new):

| patch | base wall (ms) | new wall (ms) | speedup | base render (ms) | new render (ms) |
|---|---|---|---|---|---|
| oboe1 | 924.8 | 901.5 | 1.03x | 22.0 | 21.4 |
| piano_default | 1038.3 | 1102.6 | 0.94x | 44.5 | 41.9 |
| viola_default | 1267.4 | 1275.4 | 0.99x | 6.4 | 6.1 |
| A sine | 463.8 | 456.1 | 1.02x | 142.8 | 132.6 |
| B sine+env | 489.4 | 476.8 | 1.03x | 161.9 | 164.4 |
| C filter x8 | 825.6 | 797.8 | 1.03x | 493.7 | 461.2 |
| D 8 sines | 1420.6 | 1365.0 | 1.04x | 1104.0 | 1037.9 |
| E shared ref | 519.3 | 542.3 | 0.96x | 199.8 | 208.3 |
| F 40 sines | 3680.0 | 3858.7 | 0.95x | 3457.5 | 3597.7 |
| G 40 own env | 4723.1 | 4781.6 | 0.99x | 4502.4 | 4550.3 |
| H 40 shared | 3631.3 | 3554.6 | 1.02x | 3407.8 | 3334.2 |
| I 40 envs | 1937.2 | 1943.9 | 1.00x | 1709.4 | 1707.0 |

Minimum wall time of seven alternating runs per patch, on an idle machine after
the A/B (a process sample first; the base arm's walls match checkpoint 2.5's
clean run to within three percent, so the machine was as quiet as then). Every
render column is within seven percent, both signs, the second-long rows (F to
I) within four; the D micro patch renders in both arms now (2.5 made its
zero-frequency sample a clamp). Nothing this checkpoint moved sits on the
per-sample path: a `reserve` at prepare, an `assign` or `resize` within
capacity at start, a slot write at note-on. The allocation gate itself ran in
78 s inside `gates.py` at the seal, against 191–230 s on the measurement runs,
when 97 patches had a stack to symbolize; still over the minute the plan set
for a ctest, so the slow list (REVIEW R7 item 0).

## 6. Decisions and rejected alternatives

Each is a ruling in the plan's Ledger, in the order made.

1. **The probe is a replaced global `operator new`, linked only into the test
   executables.** Rejected: `_CrtSetAllocHook` (Debug CRT only); an allocator
   parameter on every container (the whole tree for a test); a hook on
   `ValueSource::start`/`next` (a per-sample cost in production). The CLI and
   the UI, which do not link the probe, keep the default allocator (Matt,
   2026-10-29).
2. **The gate arms around note-on and render, not around load** — `prepare`
   may allocate, and `load_patch_file` pre-renders a scored patch inside the
   load, which would hide the first-sample class. Superseded in execution by
   ruling 12: the plan's armed region (the extracted delivery plus the
   render) was itself wrong.
3. **The score delivery is extracted, not duplicated.** Executed (Task 1);
   its second caller was withdrawn (ruling 13).
4. **The sizing rule: reserve for the floor at `prepare`, fit within capacity
   at `start`.** A frequency-dependent length is bounded by `kMinTableHz` =
   20 Hz, not the pin's 0.01 Hz minimum (a 19 MB table per voice); a
   setting-fixed count is sized at prepare because a graph edited afterwards
   is stale until the next prepare; `assign(n, v)` with `n == size()` and
   `resize(len)` with `len <= capacity()` allocate nothing, and each such call
   on a render path says so in a comment or sits in a function whose header
   does.
5. **`WaveEvolution::prepare` takes the owner's maximum length.** One
   argument on the existing virtual. Rejected: a second hook.
6. **No tolerance list** (R3 verdict). Nothing stopped: every family moved.
7. **The measurement is the gate's own `--list`**, a committed tool, not a
   counter deleted in the next task; the queue folders are measured for
   information and are not the gate's set.
8. **Where the gate runs is decided by its measured time**: 230 s, so the
   slow list; REVIEW R7 item 0 lets Matt move it.
9. **Byte-identity is expected total**; a moved render would be a finding.
   None moved (the null gate after every task; the A/B, Appendix A).
10. **Order of tasks**: the probe, the extraction, the gate and its
    measurement, the families largest first, the newcomer, the pins, the
    close. The gate joined `gates.py` at the close.
11. **(execution, Task 0) The new tool directories are mapped in
    `tools/structure/modules.json` when they are created**, not at the close —
    the meter counts an unmapped file the moment it exists.
12. **(execution, Task 2) The gate drives the live path.** The plan's armed
    region was wrong twice: `play_note` pre-renders the note into a buffer it
    allocates (`instrument.h:678`, three allocations per note — the offline
    host's model), and the loader's delivery walk does control-side work
    between note-ons (JSON reads, the NotePerformer). The gate runs
    `acquire_voice`, `prepare_voice_at` (the note's Setup, then `start` on the
    voice and its tails), one pull per sample for the note's life (`tick`,
    `next`, the tails), `release_voice` — the UI keyboard's path and the one
    whose allocations matter in real time; each embedded-score event is
    played as a plain note (hold has no streaming form; onsets are not passed
    by `prepare_voice_at` — a gap, section 7); a scoreless patch gets one C4.
    Cost if wrong: a node whose allocation shows only under the offline
    pre-render or a held line would be missed; `engine_tests` pins the held
    release and the drum kit (Task 8).
13. **(execution, Task 2) The public `perform_embedded_score` overload is
    withdrawn** (no caller); the extracted walk stays as `load_patch_file`'s
    private step.
14. **(execution, Task 2) `StereoMixer::render`'s per-render scratch vector is
    the offline CLI host's** — the UI streams its own mixer in the callback
    and never calls it — so the mixer-mode arming is around each channel
    source's `render` (the adapters' `start` and `next`), not the mix; the
    scratch is listed for Part 5.
15. **(execution, Task 2) `StreamingVoice::advanceList` is a `std::span`** over
    the pool voice's list: it copied a vector at every live note-on on a patch
    with tap-only tails (`double_advance_bug`, `double_advance_ok`,
    `feedback/loop_tap_starved`). The UI keeps its owning copy by one assign.
16. **(execution, Task 1) The plan's fidelity test was replaced.** It compared
    the instrument's mono against `load_patch_file`'s left channel bit for
    bit; the mixer scales the channel by the equal-power centre gain
    (√2·cos π/4 in float) and soft-clips, so the two are never bit-equal. The
    moved code's fidelity is the null gate (78/78 over the loader's own path).
    With ruling 13 the function's own test went too; `root["score"]` became
    `root.at("score")` inside the walk (the key is checked first).
17. **(execution, Task 3) The Partials array rebuild on a changed setting moved
    from `partials_start` to `partials_prepare`**; `start()` never ran on an
    unprepared node since 2.5's guard, and a setting changed between prepare
    and start is stale by ruling 4 — a reach too far for one case: a per-note
    push binding into a Partials shaping setting, which the old start
    honoured, is dropped silently (REVIEW R7 item 4, the refuter's I3). The
    expand rule's six per-note scratch vectors are members reserved at prepare
    to the expanded count; every note restores the originals by `assign`
    within capacity. The first landing took the originals' snapshot at
    prepare from the live arrays — wrong after a note (ruling 23). The
    optional arrays are sized whether or not their feature is on — a few
    small vectors per node.
18. **(execution, Task 4) `kMinTableHz` lives on `WaveSource`**, the base both
    table sources derive from — not a free constant. HistogramEqualize's
    per-rebuild `std::vector<int> counts` became the member `counts_` sized at
    prepare: this, not `cdf_`, was `algev_test_he`'s 1,025. The Bowed lines
    reserve `maxLen + 2` each (each line is twice its one-way length and the
    two one-way lengths sum to half the period, so neither exceeds one
    period). A sub-floor note (the test's note 0, 8 Hz) gets the floor's
    length — a pitch change for that note only, which no gated patch has
    (REVIEW R7 item 3).
19. **(execution, Task 6) BasicAdditiveSource's partial count** (12 kHz over the
    frequency) is bounded by the same floor, so the arrays reserve 601 and a
    sub-floor note gets 601 partials — the same class of change as the table
    clamp. SegmentSource reserves `(values.size()/2 + 1) · 2`: one pair per
    shape pair plus the trailing gap pair, the most `update_segments` pushes.
20. **(execution, Task 7) The Multiplex fan-out target is a slot registered at
    prepare.** `set_clone_param`'s string overload is gone; a clone pin that
    lacked a constant gets one at registration rather than at the first note
    — nothing renders through an instrument voice before its first note-on,
    and mixer-mode graphs have no push bindings, so no byte moves (null gate).
21. **(execution, Task 8) The Review Focus pins drive the live path.** 8(b)
    gates a streaming voice's envelopes before `prepare_voice_at` and
    releases them mid-life with the note's length as the reference (the
    release stage re-laid in place, as a line's end does) — the live path has
    no held line, and `play_note`'s hold is the offline host's; the first
    landing released with no reference and so pinned less than it said. 8(c)
    builds a `DrumKit` by hand from a loaded voice's source and arms the drum
    source's `start` and `next` on the kit's clock — it pins nothing a drum
    adds beyond that, since `play_hit`'s own buffer is the offline host's,
    outside the arm. 8(d) "`play_note` alone armed" is withdrawn: the live
    note-on is `prepare_voice_at`, which every one-note test arms.
22. **(execution, Task 9) The allocation tests are four functions**, one per
    family group, each under the function limit the plan sets for new code;
    the first landing's one function had grown past it.
23. **(fix wave) The Partials originals are taken where the arrays are made.**
    The first landing snapshotted them in `prepare_note_arrays` from the live
    arrays, which `partials_start` leaves expanded; a second prepare after a
    note — the UI prepares every node again at every Play — would have made
    the expanded set the originals and the next note expand it again, against
    the base contract that a second prepare changes nothing a render can see
    (the refuter's I1). `update_arrays` snapshots right after `init_arrays`,
    unconditionally (six small vectors per node; `set_expand_rule` may follow
    the first update, which the const getter can trigger during load), and
    the count comes from the originals. Pinned by prepare-play-prepare-play
    against play-play. No stored patch outside `old/` carries an expand rule,
    and the A/B prepares once, so nothing saw it.
24. **(fix wave) `EKSEvolution` filters in reserved scratch.** Its pick-position
    comb built a `std::vector<float>` per note and move-assigned it into the
    owner's table, which both allocated and gave `values_` a buffer of the
    note's length — so a later lower note reallocated too (the refuter's I2).
    `hi_hat1`, the one gated patch with an EKS, has pick position 0 and skips
    the branch; the 2.2 list had not named it. Now `filtered_` is reserved in
    `prepare(ctx, maxLen)`, filled in place and copied back.
25. **(fix wave) Names and comments.** `kMinTableHz` sits below `phase`, out
    of the pins' declaration-order block, and its comment carries no
    checkpoint number; the survey's docstring says "the engine", not a
    checkpoint; `size_note_arrays` is `prepare_note_arrays` (it rebuilds on a
    changed setting and sizes); the test comment says 601, the cap.
26. **(fix wave) The Multiplex slot is registered for value bindings only**,
    and the slot write asserts its range; `clone_param_slot`'s registration
    gives every clone its constant, which its comment states.
27. **(second pass) Every rebuild ends by keeping the originals.** The fix
    wave took the snapshot in `update_arrays` alone; ExplicitPartials rebuilds
    its working arrays directly in three handlers (a `maxPartials` change, `evolve`
    turned off, a row edit), so with an expand rule a row edited after the first
    prepare — the UI's path — never sounded (the re-review's N1, a regression
    against the sealed tree). `snapshot_originals()` is the helper all four
    rebuild sites end with; the handlers' own semantics are unchanged (routing
    them through `update_arrays` would have made a `maxPartials` edit keep the
    user's rows, which it overwrites today). Pinned by a row edit after a note
    and a prepare against the same rows loaded fresh.

## 7. Debt knowingly left

- **The offline hosts allocate their pre-render buffers**:
  `PitchedInstrument::play_note` (three allocations per note),
  `DrumKit::play_hit` (one per hit), `StereoMixer::render` (a scratch vector
  per call). The CLI's offline model; outside the gate's arm by ruling. Part 5
  (render, the host contract) decides whether an offline host may allocate per
  note or pre-sizes for the piece.
- **The Setup path allocates, on the thread the host gives it**: the live
  legato Setup `continue_voice_live → deliver_continuation → make_glide`
  builds an Envelope and its stages per legato note (the UI calls it under
  its audio mutex, from the UI thread); an Envelope preset `rebuild()` via
  `set_setting`, `Vibrato::build_lfo`, `FMSource::build_decimation` and
  `ExplicitPartials::init_arrays_defaults` rebuild on a setting push. The 2.2
  review asked that this group be decided once; Part 5 (the host contract)
  decides which Setup work a host may run on its audio thread (the refuter's
  I4).
- **A per-note push binding into a Partials shaping setting** (`evenWeight*`,
  `minMult`, `incr*`, `unitPO*`) is no longer honoured at `start`: the rebuild
  moved to prepare (ruling 17). The old rebuild was within capacity for a
  value-only change; HybridKS honours a pushed `numPartials` immediately and
  ExplicitPartials rebuilds in `on_setting_changed`. No patch outside `old/`
  carries such a push. REVIEW R7 item (4): honour it within capacity, or
  refuse the binding at load (the refuter's I3).
- **The streaming pull step is written four times** (the gate, the test
  helper, the held test, the UI's callback) beside `render_chunk`'s; a
  `StreamingVoice::advance()` belongs with Part 5's host work. Also:
  `SegmentSource::update_segments` keeps a `reserve` that `on_prepare` now
  does; Bowed's own floor is a literal 20 two files from `kMinTableHz`; the
  `__perf_frequency` fixture is pasted into ten test patches (the refuter's I7).
- **`StreamingVoice` mixes ownership**: `source` and `clock` are shared owners,
  `advanceList` a view; the pool clock's comment promises the handle survives
  a pool reallocation, which the span does not. A view of the whole voice, or
  the comment corrected — Part 5's (the refuter's I9).
- **The live path carries no onset and no held line**: `prepare_voice_at`
  passes no onset id, so a named onset's envelope variant is never exercised
  by the gate; `hold` has no streaming form. The gate plays each written note
  plainly; Task 8's test pins the release re-layout. Part 5's performer owns
  both.
- **A note below 20 Hz on a WavetableSource, HybridKSSource or
  BasicAdditiveSource** gets the floor's table length or partial count — a
  pitch change (tables) or fewer partials for that note only. No gated patch
  has one; the survey found 220 Hz the lowest constant in use and A0 the
  lowest key. REVIEW R7 item 3 keeps, lowers or parameterizes the floor.
- **The queue folders**: 398 of 749 loading audition and pending patches
  allocated on the first arming; not re-measured after the fixes (the families
  are the same by the first frames sampled). A reader runs `alloc_gate <patch>`
  on a queue patch for its count and first stack.
- **The UI's own live note-on** allocates (`voice_schedule_unlocked`'s
  envelope list and owning copies, Part 7's file); the engine's note-on no
  longer does.
- **`WaveEvolution` is still a part, not a Component** (2.2 report section 7);
  `prepare(ctx, maxLen)` is the minimal hook until Part 3 moves it.
- **The two `fill_table`s are twins** by three more lines (Part 3).
- **A `gap` key on a RepeatingSource** (`repeating_segment_test`) is accepted
  by the global allowlist and ignored — the setting is `gapDuration`; the 2.5
  report's I5 class, Part 4's per-type list. Found while writing Task 8's
  repetition test; the baseline is unchanged by this checkpoint.
- **The gate takes 230 s** over the set, single-threaded; a `--jobs` split
  across processes would put it under ctest's minute. Not done here.
- **The probe's five `thread_local`s** count as file-level mutable variables
  in the meter; they are the replaced allocator's state and have no other home.
- **No library or baselines patch carries a `BasicAdditiveSource` or a
  `DrumKit`**: their fixes are pinned by `engine_tests` alone.
- **The roundtrip gates** fail on the backlog-79 set (the same six);
  pre-existing, Part 4's codec.
- Carried unchanged from 2.5's section 7: the global allowlist's common-name
  blind spot; mixer-mode graphs report no faults; the fault vocabulary's two
  homes; bind-time nodes outside `nodesById`; the test gaps of 2.5's I9;
  `prepared()` true after a throwing `on_prepare`; `previous()` and
  `claimed_tick()` public; `step()`'s limits.

## 8. Independent review

# Refutation R7: checkpoint 2.6, the allocation test over every baseline patch (6a37849..94b63c7, base fb01002)

I read the brief, MODULES.md, Part 2 section 4 and section 7 row 2.6, the plan's Review Focus and Ledger, the report's sections 1–7, the measurement, and the code: both probe files, the gate, `dsp_wave_source.h`, `instrument.h` (prepare, apply_note_bindings, prepare_voice_at, deliver_continuation, play_note, finish_line, DrumKit), `multiplex_source.h`, `partials.h` (prepare, start, apply_expand_rule, size_note_arrays, the three subclasses), `wavetable_source.{h,cpp}`, `wave_evolution.h` (the base and every implementation that owns a buffer, EKS included), `hybrid_ks_source.{h,cpp}`, `phased_value_source.h`, `segment_source.h`, `basic_additive_source.h`, `patch_loader.cpp` (perform_score_json, the two mixer adapters, build_bindings, dynamicPins), `envelope.h` (gate_release, set_gated), `repeating_source.h`, the UI's `voice_schedule_unlocked`, `prepare_graph`, `play_note_held` and key-up, the four test functions, `gates.py`, the survey script, the 2.2 report's ruling 8 and its start-allocation list. On disk I recounted the eight `hashes.txt` pairs and compared the gate's `--list` run against its stacks run with a script of my own that only reads; I also scanned every `patches/` folder for `expandRule`, `EKSEvolution` and for paramMap/dynamicPins entries that push a Partials setting. I built, rendered and ran nothing. Line numbers are HEAD (code identical to 94b63c7) unless a commit is named.

### The brief, item by item

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Separation of concerns | PASS with a flag | The probe is one job (`alloc_probe.cpp:22-117`: state, API, the twenty replacement forms); the gate is one file that collects, gates, symbolizes and reports (`tools/alloc_gate/main.cpp:68-235`), acceptable for a 235-line tool. Flag: `Partials::size_note_arrays` (`partials.h:775-800`) rebuilds the arrays, snapshots the originals and sizes the note arrays — three jobs under a name that says one (see I1, I5). |
| 2 | Single responsibility | PASS with a flag | `CloneParam` (`multiplex_source.h:183-189`) and `Result`/`Event` (`main.cpp:45-59`) each say one thing. `clone_param_slot` (`multiplex_source.h:77-84`) finds-or-registers AND seeds every clone's pin with a constant — a lookup with a side effect the name does not announce (I8). |
| 3 | Dependency direction | PASS | `alloc_probe.cpp` includes only `<cstdlib>`, `<new>`, `<windows.h>` (`:10-16`); `alloc_gate/main.cpp:15-17` includes the probe, `render/patch_loader.h` and nlohmann (tests may include render; `json` is allowed under `tools/`); both directories mapped under `tests` (`tools/structure/modules.json:40`). `instrument.h:4` adds `<span>` only. No new engine include; no cycle. |
| 4 | Duplication | FAIL (Minor) | The StreamingVoice pull step `tick(); source->next(); for tails next()` is now written four times: `alloc_gate/main.cpp:120-124`, `engine_tests/main.cpp:6097-6101` and `:6250-6252`, the UI (`main.cpp:3881` region), beside `render_chunk`'s VoiceGraph form (`instrument.h:506-510`). `update_segments` keeps `currVals_.reserve((pairCount + 1) * 2)` (`segment_source.h:138`) that `on_prepare` (`:69-71`) now does. Bowed's own floor is the literal `20.0f` (`wave_evolution.h:454`) two files from `kMinTableHz`. The `__perf_frequency` PerformNode fixture is pasted into ten test JSONs. The two `fill_table` clamps are acknowledged twins (`wavetable_source.cpp:37-38`, `hybrid_ks_source.cpp:57-58`). |
| 5 | Encapsulation | PASS with a flag | `set_clone_param(int slot, float)` indexes `cloneParams_[size_t(slot)]` unchecked (`multiplex_source.h:85-90`); the only guard is the caller's `if (b.cloneSlot >= 0)` (`instrument.h:408, 556`). Everything else moved stays protected/private (`partials.h:887-888`, `wavetable_source.h:60`, `hybrid_ks_source.h:66`, `basic_additive_source.h:129`). The tests reach `inst.voicePool[slot].allEnvelopes` (`engine_tests/main.cpp:6245-6247`), already public and what the UI does. |
| 6 | Ownership | PASS with a flag | `StreamingVoice::advanceList` is a view over `VoiceGraph::advanceList` (`instrument.h:318-320`, built `:434-435`) while its `source` and `clock` are shared owners — and the `clock` comment says the heap ownership exists "so a StreamingVoice can hold it after the pool reallocates" (`instrument.h:167`), which the span cannot survive. Today no holder outlives the call: the UI copies (`tools/mforce_ui/main.cpp:3782`), `explore.cpp:355` and the UI preview (`main.cpp:13299-13304`) iterate synchronously. The invariant is now mixed inside one struct (I9). `ownedEvolution_`/`evolution_` raw pointer pre-existing. |
| 7 | Abstraction fit | PASS with flags | Matches section 4: `prepare` reserves (`wavetable_source.cpp:18-20`, `hybrid_ks_source.cpp:30-31`, `partials.h:248`, `phased_value_source.h:49-50`, `segment_source.h:70`, `basic_additive_source.h:36-41`, `wave_evolution.h:181, 445-451, 558-561, 859-861, 1025-1028, 1117-1120, 1204`), `start`/first-compute fill within it; the gate is the test over library and baselines with no tolerance list (`gates.py:33-34`). Flags: `kMinTableHz` on `WaveSource` (`dsp_wave_source.h:22`) is a constant of two subclasses placed on the base of every oscillator, and `BasicAdditiveSource` uses it for a partial count, not a table (`basic_additive_source.h:37`); `WaveEvolution::prepare(ctx, maxLen)` is the minimal hook the ruling gave a reason for. The gate's arm covers Setup (`apply_note_bindings`) which section 4 never names and excludes the hosts' buffers — stated honestly (`main.cpp:1-14`). |
| 8 | Real-time rule | FAIL (Major) | `EKSEvolution::shape_excitation` allocates a `std::vector<float> filtered(len)` per note and move-assigns it into the owner's table (`wave_evolution.h:290, 296`), which also discards the capacity `WavetableSource::on_prepare` reserved; the family is registered (`source_registrations.cpp:536`) and in the gate's set — `library/percussion/hi_hat1.json` carries it — passing only because its `pickPosition` is 0.0 (the branch at `:288` is skipped; the default is 0.0, `:704`). Any slider value in (0.001, 0.999) allocates (I2). Also unarmed and unlisted: the live legato Setup `continue_voice_live → deliver_continuation → make_glide` allocates an Envelope and its stages (`instrument.h:486-495, 541-542, 565-573`; UI callers `main.cpp:4763, 4846` under the audio mutex) (I4). Nothing in the diff adds an allocation, lock or throw to a `next()` path. |
| 9 | Names and comments | FAIL (Minor) | `dsp_wave_source.h:20` "20 Hz is the DelayLine's since checkpoint 2.2" — a checkpoint number in an engine comment, against the plan's Global Constraints and CLAUDE.md; the constant sits between `frequency` and `phase` under a comment that explains the pins' declaration order (`:11-23`). `survey_table_frequencies.py:5` "Checkpoint 2.6 reserves". `size_note_arrays` undersells (I5); the test comment "the floor: 600 partials" (`engine_tests/main.cpp:6206`) against a cap of 601 (`basic_additive_source.h:37`). No dates or REVIEW numbers added. |
| 10 | Tests | PASS with flags | Every moved family has an armed one-note test (`engine_tests/main.cpp:6110-6286`), the probe has its own (`:6062-6087`), and the harness's own zero is shown by the SineSource+Envelope graphs of the held and drum cases, so a RED in a family test came from the node under test. Gaps: no test prepares twice before a note (I1 invisible); no EKS case in the evolutions list (`:6146-6155`); the held test calls `gate_release()` with the default argument (`:6253`), so the section-5 re-layout branch (`envelope.h:128-137`, reached only from `finish_line`, `instrument.h:728-729`) is not exercised; the drum test (`:6267-6279`) arms a sine+envelope `start`/`next` with nothing drum-specific; no sub-floor HybridKS note (only the floor, `:6182`). |

### Review Focus, item by item

1. **A table whose length depends on the note's frequency.** Pinned at `wavetable_source.cpp:18-20` (`maxLen_ = ceil(rate / 20) + 1`, `values_.reserve`), `:37-38` (clamp, resize), `hybrid_ks_source.cpp:30-31, 57-58`, and the tests at `engine_tests/main.cpp:6136-6139` (notes 60, 15.49, 0, 135.08) and `:6181-6183`. Traced by hand at 48 kHz: `maxLen_` = 2401; note 15.49 is 20.001 Hz → `lround(2399.9)` = 2400 ≤ 2401; note 0 is 8.18 Hz → 5871 → clamped to 2401, and with `rateScale` = 1 the loop then reads a 2401-sample period, i.e. the note sounds at 19.99 Hz, not 8 Hz — the pitch change the report names, honestly; note 135.08 is 19998 Hz → ratio 2.4 → a 2-sample table. A frequency ≤ 0 is clamped to 0.01 Hz by `WaveSource::compute` (`dsp_wave_source.h:35`) and lands on the same clamp. Evolutions: Bowed `2·Lb + 2·Ln = 2·halfLen ≤ totalLen = lround(rate / max(20, f)) ≤ 2400 < 2403` reserved (`wave_evolution.h:453-462`); Target, Brass, RD, CA, Bezier size to `values.size()` ≤ `maxLen` (`:196, 590, 866-869, 1036-1047, 1222-1223`); HE sizes to `bins` (`:1117-1120, 1148-1149`), and `rebuild_cdf`'s `cdf_.assign(bins)` only runs when `bins` changed after prepare. NOT pinned: EKS (`:290, 296`, see I2); a sub-floor HybridKS note; `BasicAdditiveSource`'s cap is 601 while the floor's own count is 600 (`basic_additive_source.h:37, 65-67`), a one-partial inconsistency between the comment and the code.

2. **A note-on on a pooled voice.** Pinned by the gate's arm (`alloc_gate/main.cpp:113-127`: slot table sized before arming, then `acquire_voice`, `prepare_voice_at`, the pull loop, `release_voice`) and the helper (`engine_tests/main.cpp:6089-6107`). `apply_note_bindings` is inside the arm (`instrument.h:398-409`): `chain->step()`, `set_setting`, `cs->set`, `set_param(name, cs)`, `set_clone_param(slot, v)` — I traced the Multiplex write to `apply_clone_param_to_all_` (`multiplex_source.h:161-172`), which now only finds (the constants exist since `ensure_clone_param_sources_` at registration, `:153-160`). The push chains' `step()` and the table fills' `inputSource.step()` (`wavetable_source.cpp:41`) are both exercised. NOT pinned: `deliver_continuation` (the legato Setup, I4), a `PitchCurve` at note-on (`compile_pitch_curve`, `instrument.h:387-390`; the gate passes none), a steal (`acquire_voice` returning −1 is dead in the gate since every note is released, `main.cpp:118`), and the UI's own copies at schedule (`tools/mforce_ui/main.cpp:3782`, listed debt).

3. **A `RepeatingSource` boundary inside `next()`.** Pinned at `engine_tests/main.cpp:6226-6231`: duration 0.05 s, gap 0.01 s, over a 0.1 s note plus the 0.4 s tail (`instrument.h:432-435`) is about eight `start_repetition → input.start` restarts (`repeating_source.h:36-52, 62`) of a FullPartials-bearing AdditiveSource, each a `partials_start` within capacity. Traced. NOT pinned: a non-Partials child (a Wavetable child would refill its table at each `ptr_ == 0`, the same mechanism, untested); `gapDuration` is the right key (the report's section 7 note on `gap` is correct).

4. **A phrased score.** The gate plays every event plainly (`main.cpp:52-59, 82-93`; documented). The held test (`:6236-6258`) gates the envelopes before `prepare_voice_at` and calls `gate_release()` mid-life — exactly the UI's path (`tools/mforce_ui/main.cpp:4803-4804, 4855`), and `gate_release` with `releaseRefFrames = -1` writes scalars only (`envelope.h:114-154`), so 0 is right. But the re-layout branch (`:128-137`), the thing section 5 and ruling 21 call "re-lays the release", runs only when a reference is passed, which only `finish_line` does (`instrument.h:728-729`): the test and the comment claim a re-layout it does not perform. By inspection that branch assigns `stageCounts_[i]` in place and cannot allocate; the test should pass a reference or say what it pins.

5. **A graph edited after `prepare`.** Not pinned by design; the counts read at prepare are `partials.h:777` (`mult1_.size()` after `update_arrays`), `segment_source.h:70` (`values.values.size()`), `phased_value_source.h:49-50` (`stages_.size()`), `wave_evolution.h:1118` (`bins.value`), `hybrid_ks_source.cpp:17-22` (`numPartials.value`), `basic_additive_source.h:37` (a constant). Two things the Focus did not anticipate fall into this bucket: (a) a **re-prepare with no edit at all**, after a note, on a Partials with an expand rule, re-snapshots the expanded arrays (I1) — the UI's `prepare_graph` re-prepares every node at every Play (`tools/mforce_ui/main.cpp:4189-4197`); (b) an **engine-delivered per-note setting push** (`apply_note_bindings`' `set_setting`, `instrument.h:401`, from `build_bindings` case 1 at `patch_loader.cpp:1152-1165` or `dynamicPins` at `:1290-1322`) into a FullPartials/SequencePartials shaping setting sets `arrayUpdateReq_` (`partials.h:918-925, 975-981`) that `partials_start` no longer consumes (I3) — not a UI edit, and not what "stale until the next prepare" was written for.

### Rulings weighed

1. Stands. All eight `new` forms and all twelve `delete` forms are present (`alloc_probe.cpp:89-117`), aligned released by `_aligned_free` only; the probe's own path touches thread-locals, `CaptureStackBackTrace` and `malloc` (`:28-46`) and allocates nothing; `mforce_engine` is a static library (`engine/CMakeLists.txt:1`), and the stacks file's engine frames prove the replacement reaches it.
2. Superseded by 12, as the report says.
3. Overstated. The walk was extracted (`patch_loader.cpp:1363-1431`) but has one caller (`:1536`); it stands as a decomposition of an over-long function, not as "one function, two callers" (the commit message of 94e9a90 is now inaccurate; git history, nothing to do).
4. Stands, with a reach too far: "a graph edited afterwards is stale until the next prepare" is right for UI edits, but ruling 17 applies it to a setting the engine itself pushes at Setup (I3).
5. Stands.
6. Stands for the measurement's set; "every family moved" is true of what the first run listed. EKS (I2) was never listed because its one gated patch has the parameter that skips the allocating branch.
7. Stands. The `--list` run and the stacks run agree: 132 patches, 97 nonzero in both, 0 per-patch mismatches; the first-engine-frame histogram in the stacks file reproduces the measurement's table exactly (68/11/8/7/1/1/1).
8. Stands on the record (`renders/scratch/alloc26_timing.txt`: "230 s"); the 78 s figure is unverifiable by reading.
9. Stands for what the A/B can see: my recount of the eight `hashes.txt` pairs gives DIFFER 0 in every set (80/52/40/708/418/159/47/2, no one-arm-only entries). The A/B prepares once per load; I1 moves a render only on a second prepare, which no CLI render performs.
10. Stands.
11. Stands.
12. Stands; the exclusion of `play_note`'s buffers (`instrument.h:653, 682-684`) and `play_hit`'s (`:824`) is honestly stated and the arm is the UI keyboard's path.
13. Stands.
14. Stands, and the arm is exactly `start` + `next`: both adapters' `render` call `src->start(frames)` then `next` per sample with no prepare (`patch_loader.cpp:69-72, 84-87`).
15. Stands; see item 6's flag (I9).
16. Stands as stated (the mixer's `kRoot2 * cos(...)` pan law and `soft_clip`, `mixer.cpp:53, 63`); I did not reproduce the bit comparison.
17. Wrong in one part, overreaching in another. The snapshot moved to prepare without guarding against a second prepare after a note: `size_note_arrays` snapshots from the live `mult1_` (`partials.h:779-781`), which `partials_start` leaves expanded (`:764-767`), so the next prepare makes the expanded set the originals and the next note expands it again — against the base contract "Calling it twice with the same context changes nothing a render can see" (`dsp_value_source.h:52`) (I1). And "a setting changed between prepare and start is stale by ruling 4" silently drops a per-note push binding into a Partials shaping setting that the old `partials_start` honoured (I3).
18. Stands. Bowed's bound verified; the HE attribution to `counts_` is by reasoning (the stacks' first frame is `fill_table` for all eleven wavetable patches), consistent with the 1,025 in the list.
19. Stands; the one-partial comment/cap inconsistency noted under Focus 1.
20. Stands. `ensure_constant` gives an unwired pin a constant at the pin's own value (`dsp_value_source.h:279-284`), so a clone pulling it before its first note reads what it read before; mixer-mode graphs build no push bindings. One harmless over-registration: the slot is registered for `isSetting` bindings too (`instrument.h:216-218` has no `!b.isSetting`), where `node.param(name)` is null and nothing happens (I8).
21. Overstated. 8(b) does not exercise the release re-layout (Focus 4); 8(c) pins nothing a DrumKit adds (its armed region is a sine and an envelope, `engine_tests/main.cpp:6275-6276`); 8(d)'s withdrawal is right.
22. Stands.

### Issues

**I1 (Major) — A second `prepare` after a note double-expands a Partials with an expand rule.** `size_note_arrays` (`partials.h:775-786`) takes `origMult1_ = mult1_` from the live arrays and computes `n` from `mult1_.size()`; `partials_start` leaves `mult1_` expanded (`:271-280, 764-767`). First prepare: originals = N; note: N·k. Second prepare (`arrayUpdateReq_` false): originals = N·k, reserve N·k²; next note: N·k². Every further prepare multiplies again. The old code snapshotted once and restored (`git show fb01002:engine/include/mforce/source/additive/partials.h`, the `if (origMult1_.empty())` branch). Exposure: the UI's editor Play re-prepares every node at every Play (`tools/mforce_ui/main.cpp:4189-4197`, "every node is prepared again (idempotent)"), and a Partials node in the editor graph is prepared both as a node and through its AdditiveSource (`full_additive_source.cpp:12`). No library/baselines/audition/pending/sweep patch carries `expandRule` (0; 67 under `patches/old/`), the engine_tests case prepares once (`engine_tests/main.cpp:6120-6125`), and the A/B prepares once, so nothing saw it. Contract broken: `dsp_value_source.h:52`. Fix: snapshot the originals where they are made — in `update_arrays` after `init_arrays` — and compute `n` from the originals when `hasExpand_`; a test that prepares, plays, prepares and plays again and checks `mult1_.size()` (or the render) is unchanged.

**I2 (Major) — `EKSEvolution::shape_excitation` allocates per note and discards the owner's reserved table.** `wave_evolution.h:290` `std::vector<float> filtered(len)`, `:296` `values = std::move(filtered)` — the move-assign replaces `WavetableSource::values_`'s buffer with one of capacity `len`, so `fill_table`'s next longer `resize` reallocates on every later note of a lower pitch as well. Reachable: registered as `EKSEvolution` (`source_registrations.cpp:536`), wired like every evolution in the tests; `library/percussion/hi_hat1.json` carries it in the gate's set and passes because `pickPosition` is 0.0 (the guard at `:288`; default 0.0 at `:704`). Not in the evolutions test list (`engine_tests/main.cpp:6146-6155`), not in the 2.2 work list, not in the report. Fix: a member scratch reserved in `prepare(ctx, maxLen)`, filled in place and copied back (never move-assigned into `values`); an EKS case with `pickPosition` 0.5 in `run_allocation_tests_tables`.

**I3 (Minor) — A per-note push into a Partials shaping setting is now silently ignored.** `apply_note_bindings` delivers `set_setting` at Setup (`instrument.h:401`; bindings from `patch_loader.cpp:1152-1165` and `:1290-1322`); FullPartials/SequencePartials answer with `arrayUpdateReq_ = true` (`partials.h:918-925, 975-981`); `partials_start` no longer calls `update_arrays` (the removed lines at fb01002 `:260-262`), so the note plays the old arrays. For value-only settings (`evenWeight*`, `oddWeight*`, `unitPO*`, `minMult`, `incr*`) the old rebuild was within capacity (`init_arrays` resizes to an unchanged `n`, `:928-933`) and the push took effect without allocating; dropping it is a behaviour change the allocation rule did not require. My scan found no such push outside `patches/old/` (four hits there, all on base settings `decayRate`/`inharmonicity`/`rolloff1` that `partials_start` still reads directly), so no render moves today. Inconsistent with HybridKS, which honours a pushed `numPartials` immediately (`hybrid_ks_source.cpp:34-36`), and ExplicitPartials, which rebuilds in `on_setting_changed` (`partials.h:1023-1030`). Decide one rule: honour the rebuild within capacity at `start` (size for the setting's declared maximum, or reject a growth), or make the loader refuse a push binding into a shaping setting (2.5's "a rejected wire says so").

**I4 (Minor) — The live legato Setup allocates and is neither armed nor listed.** `continue_voice_live → deliver_continuation → make_glide` builds an Envelope with `make_shared` and two `add_stage` (`instrument.h:486-495, 541-542, 565-573`), called by the UI's key-overlap and key-up under the audio mutex (`tools/mforce_ui/main.cpp:4763, 4846`). Section 1's "the engine's note-on no longer does" is overstated for this note-on. The 2.2 review's item 14 asked that 2.6's list carry the Setup-path group (Envelope preset `rebuild()` via `set_setting`, `Vibrato::build_lfo`, `FMSource::build_decimation`, `ExplicitPartials::init_arrays_defaults`, this glide) "so the thread question is decided once"; section 7 does not carry it. List it, with the thread it runs on, for Part 5.

**I5 (Minor) — Comments and names.** `dsp_wave_source.h:20` "since checkpoint 2.2" (a checkpoint number in engine code; the Global Constraints forbid it); the constant placed between two pins under the declaration-order comment (`:11-23`) — move it above `amplitude` or below `phase` and drop the history; `survey_table_frequencies.py:5` "Checkpoint 2.6"; `size_note_arrays` rebuilds and snapshots as well as sizes (`partials.h:775-800`) — `prepare_note_arrays` or split it; `engine_tests/main.cpp:6206` "600 partials" against a 601 cap.

**I6 (Minor) — The held test does not pin the release re-layout it names.** `engine_tests/main.cpp:6253` calls `gate_release()`; the re-layout branch needs a reference (`envelope.h:128-137`) and only `finish_line` passes one (`instrument.h:728-729`). Pass `sv.durSamples` (or add a `finish_line`-style case) and reword the comment; also reword ruling 21.

**I7 (Minor) — Duplication.** The pull step in four places (item 4) — a `StreamingVoice::advance()` (tick, root, tails) would make the gate, the helper, the held lambda and the UI's loop one line each; `segment_source.h:138`'s redundant `reserve`; Bowed's literal `20.0f` (`wave_evolution.h:454`); the repeated PerformNode fixture in the tests.

**I8 (Minor) — Multiplex slot API.** Registration also runs for `isSetting` bindings (`instrument.h:216-218`), a no-op by luck of `param()` returning null; `set_clone_param(int, float)` indexes unchecked (`multiplex_source.h:86`); `clone_param_slot` seeds every clone's pin as a side effect of a lookup. Add `!b.isSetting` to the registration, assert the slot range, and name the function for what it does (`register_clone_param`).

**I9 (Minor) — StreamingVoice ownership is now mixed.** `source` and `clock` are owners, `advanceList` a view (`instrument.h:304-328`), while `VoiceGraph::clock`'s comment (`:167`) promises the handle survives a pool reallocation. Either make the comment say the span does not, or give the handle a view of the whole voice (a `const VoiceGraph*`) and no owners.

**I10 (Minor) — The drum test is vacuous beyond the shared harness.** `engine_tests/main.cpp:6261-6279` arms a sine and an envelope through `start`/`next`; `DrumKit` adds only `play_hit`'s buffer (`instrument.h:824`), which the test skips by ruling. Keep it as the DrumKit clock's smoke or drop it; do not count it as a drum pin.

No Major beyond I1 and I2.

### What I would fix in one wave

- **I1 and I2 now.** Both are wrong behaviour in registered nodes that the checkpoint's own rule covers, both have a one-screen fix (snapshot in `update_arrays`; a reserved EKS scratch copied back), and each needs one new test the current set lacks (a prepare–play–prepare–play case; an EKS case with `pickPosition` 0.5). Re-run the gate and the null gate after; the A/B cannot see I1 and need not be rerun for it.
- **I5 and I6 now.** Comment and name fixes and a test argument; minutes, and the report's rulings 17 and 21 should be reworded with them.
- **I8 now** (the `!b.isSetting` guard and the range assert) — two lines in the same files.
- **I3 as a decision, not a fix:** Matt chooses between honouring shaping-setting pushes within capacity and rejecting them at load; record the ruling in the Ledger and in Part 2 section 4's decided line, then implement in Part 4 (the loader) or Part 5 (the host contract) accordingly.
- **I4 as listed debt for Part 5** (section 7 gains the Setup-path group with the thread each runs on), and section 1's sentence corrected.
- **I7, I9, I10 as debt for Part 3/5**: the pull-step method belongs with the StreamingVoice work Part 5 owns; the Bowed literal and the segment `reserve` go with Part 3's node pass; the drum test can stay as a smoke.

### What was done about each

- **I1 (Major) — fixed, ruling 23.** The originals' snapshot is taken in
  `update_arrays`, where the arrays are made, and `prepare_note_arrays` reads
  the count from the originals when an expand rule is set; a second prepare
  after a note changes nothing. Pinned by a prepare-play-prepare-play case
  against play-play on one voice (`same_bits`), which FAILED on the first
  landing and passes now.
- **I2 (Major) — fixed, ruling 24.** `EKSEvolution` filters its pick-position
  comb in member scratch reserved in `prepare(ctx, maxLen)` and copies back;
  never a move-assign into the owner's table. Pinned by an EKS case with pick
  position 0.5 at two pitches (1 allocation per note before; the lower note
  reallocated the table too).
- **I3 — Matt's, REVIEW R7 item (4):** a per-note push binding into a
  Partials shaping setting is no longer honoured at `start`; honour it within
  capacity, or refuse the binding at load.
- **I4 — listed** (section 7): the live legato Setup (`make_glide`) and the
  Setup-path group the 2.2 review named, with the thread each runs on, for
  Part 5; section 1's sentence corrected.
- **I5 — fixed, ruling 25:** the constant below `phase`, its comment without
  the checkpoint number; the survey's docstring; the test comment's 601;
  `size_note_arrays` is `prepare_note_arrays`.
- **I6 — fixed:** the held test releases with the note's length as the
  reference, so the release stage is re-laid; its comment and ruling 21 say
  what it pins.
- **I7 — listed** (section 7): the pull step in four places (a
  `StreamingVoice::advance()` with Part 5's host work), the redundant segment
  `reserve`, Bowed's literal floor, the repeated test fixture.
- **I8 — fixed, ruling 26:** no slot for a setting binding; the slot write
  asserts its range. The registration's side effect keeps its name
  (`clone_param_slot` finds or registers, and registering means giving every
  clone its constant); the comment says so.
- **I9 — listed** (section 7): the handle's mixed ownership.
- **I10 — reworded:** the drum test pins the drum source's `start` and `next`
  on the kit's clock and says the kit's `play_hit` buffer is outside the arm.

### Re-review

# Re-review R7: the checkpoint 2.6 fix wave (94b63c7..131d48f)

I read `git diff 94b63c7 131d48f -- engine tools` in full (seven files, 72+/28−) and, around it at HEAD, `Partials::update_arrays`, `prepare_note_arrays`, `partials_start`'s head and the three subclasses' rebuild paths; `EKSEvolution` end to end; the `adsr` preset's construction (`envelope_presets.h:231-233`, `envelope.h:452-465`) against `gate_release`'s re-layout branch; `acquire_voice`'s rotation for the new test's slot choice; `same_bits`; the UI's array-edit path (`tools/mforce_ui/main.cpp:503-520`) and the loader's `expandRule` step (`patch_loader.cpp:410-440`); and the working-tree `REVIEW.md` and report for the doc follow-ups. I scanned every `patches/` folder for `expandRule` by Partials type with a read-only script. I built, rendered and ran nothing; the RED/GREEN claims are the Ledger's, which I reason about below but did not reproduce. Line numbers are HEAD (code identical to 131d48f).

### Issues, closed or not

- **I1 — CLOSED for FullPartials and SequencePartials; one path open (N1).** The snapshot is in `update_arrays` after `init_arrays` (`partials.h:699-704`); `prepare_note_arrays` reads `n` from `origMult1_.size()` when `hasExpand_` (`:783`) and never from the live arrays. Traced by hand: load (`arrayUpdateReq_` true) → prepare → `update_arrays` → originals N=4, n=12, reserve; note (restore 4, expand 12); second prepare → `arrayUpdateReq_` false → n = 4·3 = 12, `reserve` a no-op; note → restore 4 → 12. Nothing a render sees changes. Every order the loader can produce resolves: `set_expand_rule` before or after the const getter's lazy `update_arrays` (`:686`) both end at a prepare whose originals are `init_arrays`' output, because every setting re-dirties the flag (`:918-925, 975-981`) and the ctor's direct `init_arrays()` (`:916, 973`) leaves the flag true. The test (`engine_tests/main.cpp:6142-6158`) distinguishes: with 94b63c7's prepare-snapshot, instance B's second prepare makes the expanded 12 the originals and its second note sums 36 partials against A's 12 — different bytes regardless of allocation; polyphony 1 gives slot 0 both times (with a larger pool the `release(acquire())`-then-`acquire` pattern lands on the same slot every call, `instrument.h:256-272`), and both second notes share `noteSeq_` = 1, hence the same `expandRng_`. Not vacuous. Refresh-on-every-rebuild is the right semantics: fb01002's snapshot-once meant a FullPartials setting rebuild after the first note was overwritten by the stale originals on any expand-rule node (`git show fb01002:…partials.h`, the `else` branch at `:276-280`) — a latent staleness the old code hid and the wave removes.
- **I2 — CLOSED.** `filtered_` reserved in `prepare(ctx, maxLen)` (`wave_evolution.h:264`), `assign(len)` within it (`:291`), `std::copy` back into `values` of the same length (`:297`), so `values.size()` and its capacity are untouched. The lowpass (`:300-310`), the decay-stretch average, the drum-blend flip (`rng_.value()`, a Randomizer) and the stiffness allpass (`:314-339`) are scalar loops over `values` in place; no other container. `WavetableSource::on_prepare` reaches the holder's evolution through `evolution_` (`wavetable_source.cpp:20`), so the reserve runs for the `{"ref"}` form the test and `hi_hat1` use. The test case (`engine_tests/main.cpp:6187`) exercises the comb (delay = ⌊0.5·len+0.5⌋, in range for every len ≥ 2), the lowpass, the allpass and the blend at two pitches.
- **I3 — NOT CLOSED, by design (Matt's).** Code unchanged; the working-tree `REVIEW.md` does not yet carry an R7 item (4) (its "(4)" hits at `:68, 114, 143` are other reviews'), so it is half-done in the docs at the time of reading.
- **I4 — NOT CLOSED, by design (debt).** `make_glide`/`deliver_continuation` unchanged (`instrument.h:486-495, 541-542`); the working-tree report's section 7 has no line for it yet (no "glide" or "deliver_continuation" outside section 2's heading).
- **I5 — CLOSED.** `kMinTableHz` below `phase` (`dsp_wave_source.h:16-24`), comment without the checkpoint number and naming BasicAdditive's use; `survey_table_frequencies.py:5` "The engine reserves"; `:6239` "601 partials, the cap"; `prepare_note_arrays` (`partials.h:781`). One comment missed: `partials_start` still says "the originals prepare kept" (`:272`) — `update_arrays` keeps them now (N2).
- **I6 — CLOSED.** `gate_release(int(0.5f * 48000.0f))` (`engine_tests/main.cpp:6287`). The patch's `"preset":"adsr"` with no `timeMode` builds `Envelope::make_adsr` (`envelope_presets.h:232`), percent-based, `absolute_time` false, so the branch at `envelope.h:128-137` runs: at the half-life pull (21,600 of 43,200 samples) the gated envelope sits on the expand stage, `relStage` = 3, and `stageCounts_[3]` is re-resolved to 0.5 s · 0.2 = 4,800 frames in place. Traced; non-vacuous.
- **I7 — NOT CLOSED, by design (debt).** The wave adds a fifth copy of the pull step in `render_one_live_note` (`engine_tests/main.cpp:6111-6123`); the working-tree report's section 7 has no line for it yet.
- **I8 — CLOSED.** `!b.isSetting` at registration (`instrument.h:217`); `assert` on the slot (`multiplex_source.h:87`, `<cassert>` at `:8`) — Debug-only by `NDEBUG`, which is the file family's stated Debug-assert contract (`dsp_value_source.h:71-73`), acceptable.
- **I9 — NOT CLOSED, by design (debt).** `StreamingVoice` unchanged; not yet in the working-tree section 7.
- **I10 — CLOSED as a comment** (`engine_tests/main.cpp:6294-6297` says what the test pins and that `play_hit`'s buffer is outside the arm); the test's thinness stands, acknowledged.

### New findings

**N1 (Minor) — ExplicitPartials' three direct rebuilds bypass the snapshot, so with an expand rule an array or `maxPartials`/`evolve` edit after the first `update_arrays` is invisible to every note.** The originals are refreshed only in `update_arrays` (`partials.h:699-704`), but `ExplicitPartials::on_setting_changed` calls `init_arrays_defaults()` (`:1030`) and `init_arrays()` (`:1035`) directly, and `on_array_changed` calls `init_arrays()` (`:1045`) directly, none setting `arrayUpdateReq_`. Trace: UI load → Play → prepare (`arrayUpdateReq_` true) → `update_arrays` → originals = the user's rows, correct; UI edits a row → `set_array` (`tools/mforce_ui/main.cpp:517`) → `on_array_changed` → `init_arrays` → live arrays new, originals old, flag still false → Play → `prepare_note_arrays` skips the rebuild, n from the old originals → `partials_start` restores the old rows (`:274-276`): the edit never sounds. The CLI is unaffected (its only `update_arrays` is at prepare, after every `set_array` and `set_expand_rule`). Exposure: ExplicitPartials + `expandRule` exists — 13 patches, all under `patches/old/` (60 FullPartials there; none elsewhere) — and the UI's live edit. History: fb01002 had the same staleness (snapshot once), 94b63c7 was accidentally right for it (snapshot at prepare, and the UI re-prepares), the wave re-introduces it while fixing I1 — so it is a regression against the sealed tree, not against the pre-checkpoint one. Fix, one line each: the three calls become `update_arrays()` (or `init_arrays*` set `arrayUpdateReq_ = true` and let prepare rebuild), making "the originals are `init_arrays`' output" true for every subclass; add an ExplicitPartials row-edit-then-prepare case beside the I1 test. Part 7 owns the live-edit case if Matt prefers to defer, but the wave's own rule is what is incomplete.

**N2 (Minor) — A comment the wave made wrong.** `partials.h:272` "Back to the originals prepare kept" — `update_arrays` keeps them (`:699-704`); say "the originals update_arrays kept".

**N3 (Minor) — Doc follow-ups not yet in the working tree.** The report still names `size_note_arrays` at `2026-10-30-checkpoint-2.6.md:149, 197, 459`; REVIEW R7 has no item (4) for I3; section 7 has no lines for I4, I7, I9; rulings 17 and 21 are unchanged at the time of reading. All announced; none landed yet.

### Standing

With the wave, every allocation the gate's set and the tests exercise is in `prepare`, a second prepare after a note changes nothing a render can see for FullPartials and SequencePartials, EKS keeps the owner's reserved table, and the held test now exercises the release re-layout it claims: Part 2 section 4 as signed — `prepare` may allocate, `start` and `next` may not, asserted over every baseline patch with no tolerance list — is met by the code, subject to the Ledger's GREEN claims I did not run. Matt's call: I3 (honour a per-note push into a shaping setting within capacity, or refuse the binding at load) and whether N1 is fixed in this wave (three one-line changes plus a test) or booked to Part 7 with the UI's live edit. Debt: the Setup-path group including the legato glide (I4), the fifth pull-step copy (I7), the mixed-ownership `StreamingVoice` (I9), the drum test's thinness (I10), and the doc lines of N3 for the seal.

### After the re-review

A second pass (commit `e3edef1`, re-checked below): **N1** — the snapshot is a
helper, `snapshot_originals`, that `update_arrays` and ExplicitPartials' three
direct rebuilds all end with (their semantics unchanged: routing them through
`update_arrays` would have made a `maxPartials` edit keep user rows it
overwrites today); pinned by a row edited after a note and a prepare against
the same rows loaded fresh, which FAILED before and passes after (ruling 27).
**N2** — the restore's comment names the last rebuild. **N3** — the doc lines
landed: `prepare_note_arrays` throughout, REVIEW R7 item (4), section 7's
lines for I4, I7 and I9, rulings 17 and 21 reworded.

### Re-check

### Re-check: the second pass (131d48f..e3edef1)

I read `git diff 131d48f e3edef1 -- engine tools` (two files, 35+/5−) and, at HEAD (code identical to e3edef1; `git diff e3edef1 HEAD --stat -- engine tools` is empty), every write to `mult1_`..`po2_` in `partials.h` by grep (`:274-276, 699-700, 707-711, 775-777, 938-940, 994-996, 1035-1036, 1041-1042, 1052-1053, 1069, 1072-1079, 1087-1091`), `Component::set_array`'s hook (`component.h:58, 66`), the new test (`engine_tests/main.cpp:6159-6180`), and the working-tree report and REVIEW.md for the N3 follow-ups. Nothing built, rendered or run; the GREEN claims are the Ledger's.

- **N1 — CLOSED.** Every path that rewrites the six working arrays now ends in `snapshot_originals()` (`partials.h:707-711`) or is followed by a prepare that does: `update_arrays` (`:699-700`); ExplicitPartials' `maxPartials` (`:1035-1036`), `evolve` off (`:1041-1042`) and `on_array_changed` (`:1052-1053`); `ExplicitPartials::init_arrays`' fallback to `init_arrays_defaults` (`:1069`) is inside `init_arrays`, whose three callers all snapshot; the constructors' direct `init_arrays()`/`init_arrays_defaults()` (`:921, 978, 1027`) leave `arrayUpdateReq_` true, so the first prepare rebuilds and snapshots. The only other writers are `apply_expand_rule`'s expansion (`:775-777`) and the restore (`:274-276`), both per note by design. The test distinguishes: with `:1052-1053` reverted, `edited`'s second note restores the loaded `ampl1` [1, .5, .25, .125] while `loaded`'s second note carries [1, .1, .1, .1] — different sums, same `noteSeq_` (both second notes on slot 0 of a one-voice pool), so `same_bits` fails only on the rows; `set_array` reaches `on_array_changed` through `Component::set_array` (`component.h:58`), and `evolve:false` mirrors `ampl1` into `ampl2` on both arms (`:1047-1050` and the loader's `on_array_changed`). Keeping ExplicitPartials' three handlers' own semantics rather than routing through `update_arrays` is right (the `maxPartials` handler regenerates defaults today; a route through `init_arrays` would keep the rows). The snapshot itself runs only on control-side rebuilds (prepare, a setting or array edit), never in `start`/`next`; its one Setup-path reach — a per-note push into `ExplicitPartials.maxPartials` — is the already-listed I3/I4 class (six copies added to an allocation that path already made).
- **N2 — CLOSED.** `partials.h:272` "the originals the last rebuild kept"; the helper's comment (`:704-706`) states the rule every rebuild now follows and is true.
- **N3 — PARTLY.** In the working tree: REVIEW R7 item (4) for I3 (`REVIEW.md:37`), section 7 lines for the legato glide (`report:456`), the pull step (`:471-473`) and `StreamingVoice`'s ownership (`:477`), ruling 17's rewording (`:441` names `prepare_note_arrays`), ruling 21 reworded (`:405`). One stale name remains: the interfaces table still says `size_note_arrays()` (`report:205`).

New: N4 (Minor, wording) — the test's comment "sounds on the next note, expand rule or not" (`engine_tests/main.cpp:6159-6161`) claims the no-rule case, which the test does not cover (without a rule the originals are never read: `prepare_note_arrays` uses `mult1_.size()` and the restore is skipped, so the statement is true but unpinned); say "with an expand rule" or add the rule-less row-edit case. Otherwise none: no new allocation on a render path, no other comment made wrong.

Standing: with e3edef1 the originals the expand rule starts from follow every rebuild in every Partials subclass, a second prepare after a note changes nothing a render can see, and Part 2 section 4 as signed is met by the code subject to the Ledger's GREEN; what remains is Matt's REVIEW R7 item (4), the listed Part 5/7 debt, and the one stale name at `report:205` for the seal.

After the re-check: the interfaces table says `prepare_note_arrays`; the test's comment says "with an expand rule" (N4); both in the seal.

## 9. Reading guide

Ten minutes: `tools/alloc_probe/alloc_probe.h`, then the `.cpp`'s `record()`
and the replacement set; `tools/alloc_gate/main.cpp` — `gate_instrument`
and `gate_mixer`; `core/dsp_wave_source.h` for `kMinTableHz`;
`partials.h` — `size_note_arrays` and the head of `partials_start`;
`wavetable_source.cpp` — `on_prepare` and `fill_table`; `wave_evolution.h` —
the base's `prepare` and `HistogramEqualizeEvolution`; `multiplex_source.h` —
`clone_param_slot` and `set_clone_param`; `instrument.h` — `StreamingVoice`
and `prepare`; then `engine_tests`' `allocations_in_one_note` and the four
family test functions, and the measurement document's two tables.

## 10. C++ lessons

### Lesson 1: replacing `operator new`

In C# or Java there is no equivalent: the runtime owns allocation, and the
nearest cousin is a read-only counter (`GC.GetAllocatedBytesForCurrentThread`).
In C++ the allocation functions `new` and `delete` call are ordinary functions
with fixed names, and the standard makes them *replaceable*: define them in
any translation unit of your program and the linker uses yours for the whole
program instead of the library's. That is the whole mechanism of the probe —
no flag, no hook registration, no build switch; linking the library that holds
the definitions is the switch, and a program that does not link it is
untouched. Two rules make it safe. Every form must come together: the plain
and array forms, the `nothrow` forms, and since C++17 the over-aligned forms
(`std::align_val_t`), each with the `delete` that pairs with it — an aligned
allocation released by the plain `free` is undefined behaviour, so the probe
has two release paths and never mixes them. And nothing inside the
replacement may allocate: the probe's state is plain integers and a fixed
array, and the stack capture (`CaptureStackBackTrace`) writes into that array.

### Lesson 2: `reserve`, `resize`, `assign`, and a contract you cannot see

A `std::vector` has a size and a capacity. `reserve(n)` raises the capacity
without changing the size; `resize(n)` and `assign(n, v)` change the size and
allocate only when `n` exceeds the capacity. So "sized at prepare, filled
within capacity at start" is a correct real-time pattern — and an invisible
one: a reader of `partials_start` sees twenty `assign` calls and cannot tell
from the code whether any allocates. The standard does not even promise that
`assign` reuses storage when it fits; every implementation does, and the gate
is what makes the promise checkable here. That is why each such call carries
the fact in a comment or sits under a function header that states it (ruling
4), and why the gate, not the reader, is the proof. The C# analogue is
`List<T>.Capacity`: the same two numbers, the same invisibility, and the GC
to hide the cost — which is exactly what an audio thread cannot afford.

### Lesson 3 (short): `thread_local`

The probe's counters are `thread_local`: every thread gets its own copy, so
the audio thread's allocations and the UI thread's never meet in one number,
and no lock is needed to read a count — the reader reads its own. C# has the
same idea as `[ThreadStatic]`; C++ makes it a storage duration, like `static`,
and a `thread_local` of a trivial type costs one indexed load per access,
which an `operator new` on the fast path can afford. The gate is
single-threaded, so the distinction is latent there; it matters the day the
probe is armed inside a host that renders on its own thread.

### Lesson 4 (short): `std::span`

`StreamingVoice` used to carry a copy of the voice's tail list; now it carries
a `std::span` — a pointer and a length over someone else's array, with the
iteration interface of a container and no ownership. The owner (the pool's
`VoiceGraph`) must outlive the span, which the UI's contract already
guaranteed. Where C# would pass `ReadOnlySpan<T>` or `IReadOnlyList<T>`, C++
before 20 passed a `const std::vector&` (fine) or copied (what happened here);
`std::span` says "a view, not a copy" in the type, and the gate caught the
copy that the type now forbids.

## Appendix A: the A/B over the sample

| set | in the set | rendered by both | differ (new vs old) |
|---|---|---|---|
| patches/library | 80 | 80 | 0 |
| patches/baselines | 52 | 52 | 0 |
| patches/pending | 43 | 40 | 0 |
| patches/audition | 708 | 708 | 0 |
| patches/sweep, every third | 457 | 418 | 0 |
| patches/old, every third, plus the retired pair | 275 | 159 | 0 |
| `taps` (2.4's 47, in full) | 47 | 47 | 0 |
| `retired` (the `loop_root_sensitivity` pair) | 2 | 2 | 0 |

`old` is checkpoint 2.5's sealed `t5` arm (the engine at `1481d15`, kept as
`renders/scratch/ab_tree/cli_pre26.exe`), `new` the build at this seal; the set
files are `renders/scratch/ab26_tree/*.txt`, the hashes under
`renders/scratch/ab26_tree/<set>/{old,new}/hashes.txt`. Every patch that rendered
in one arm rendered in the other and to the same bytes.
