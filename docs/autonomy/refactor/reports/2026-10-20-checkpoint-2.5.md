# Architecture report: checkpoint 2.5, no throw on a render path; fault flags; a rejected wire says so

Part 2 of the target architecture, checkpoint 2.5 — commits `cb1b4ab`..`1481d15` on
main, 2026-10-20 and 2026-10-26 (the session was cut by the quota at Task 4
and resumed on the 26th). Plan: `docs/superpowers/plans/2026-10-20-part2-checkpoint-2.5.md`
(its Ledger holds every ruling). Spec: `docs/architecture/part2-valuesource-contract.md`
section 6. Measurement: `docs/architecture/measurements/2026-10-20-no-throw-and-wires.md`.
The format is the one agreed at checkpoint 0 (report R1).

## 1. Summary

`start()` and `next()` never throw and never print. Until this checkpoint two
throws were reachable from the audio thread — a WaveSource asked for a
non-positive frequency, a WavetableSource asked to fill a table shorter than
one sample — and a cycle with no tap reported itself with `fprintf` from
`next()`. Now:

```cpp
enum class Fault : unsigned char { FrequencyClamped, TableClamped, NeverPrepared, Clockless, CycleWithoutTap };
unsigned char ValueSource::faults() const;         // what this node reported since the host last read it
void ValueSource::clear_faults();
virtual void MonoSource::collect_faults(std::vector<FaultReport>&);   // the host reads, by node id, and clears
bool Component::set_param(std::string_view, std::shared_ptr<ValueSource>);   // false: no such pin, or the wrong kind
```

An invalid runtime value is clamped to the narrowest value that makes the old
throw's condition false and the node sets a bit in a per-node atomic byte; the
host reads the bits after a render, by node id, and reports them once — the
CLI as a `[faults]` block after its stats, the UI on the transport status
line after Generate and from the live pool once a frame. A cycle with no tap
is a bit, and the same re-entry guard now covers `prepare()` and `start()`,
which overflowed the stack on a hand-wired cycle. A node nobody prepared
reports instead of dividing by zero in `on_start`. `prepare()` throws on a
zero rate, where throwing is allowed, and Mesh2D's silent 48000 fallback is
gone. `set_param` answers `false` for a pin the node does not have or a part
of the wrong kind, and the loader turns that — and an unknown key, on every
node whichever path built it — into a load failure that names the node, the
pin and the reason. The DelayLine reads its ring: the one-past-the-end read
that the extra zero slot stood in for since checkpoint 2.2 wraps to slot 0.

Three things the fault bits found on the way in, each a latent host bug that
rendered correctly by luck (section 6): the push-binding chains (the dynamicPins
sources) were never prepared by the voice; the mixer path's voices did not
know their node ids; and the first form of the never-prepared guard retired
the bare-node idiom that thirteen pre-existing tests and `ensure_constant`
rely on, so it was narrowed to `start()`.

Renders: every valid patch byte-identical through Tasks 1 to 3 (78/78 after
each); the wires moved no valid render (Task 3's A/B: 0 differ in every set) and
the patches that stop loading are the stale-key class the lint found —
four sweep patches and one archive file, nothing tracked, queued or pending; the DelayLine wrap moved 25 patches of the
sample, every one on the measured list (the two `double_advance` baselines
refrozen, the fourteen bwg queue patches, eight sweeps), and three listed
patches did not. The independent review found that wrap landed wrong — the
index wrapped, the fraction did not, an extrapolation once per lap that a
constant-source test could not see — and the fix wave re-landed it (ruling
26): the same twenty-five renders moved again, the two baselines were
refrozen a second time, and nothing else in the sample moved. The wave also
closed the review's minor findings: the collectors take their bits in one
atomic exchange, the prepare/start re-entry mark unwinds on a throw, every
CLI render and the UI's chords render report, in id order, and a
configurator's failed wire names its node (rulings 27–33).

Render time: within noise against the pre-2.5 binary — the second-long rows
within four percent and in the new binary's favour, two sub-second rows a few
percent slower, inside the method's scatter; and the one micro patch in the
timing set that used to terminate the process renders.

## 2. Types and files

```
NEW      Fault, fault_bit(), fault_name()  (engine/include/mforce/core/dsp_value_source.h)
  Owns: five named bits and their one wording each.
  Used by: every node that reports (WaveSource, WavetableSource, HybridKSSource,
    the base's guards); the hosts' reports through fault_report_text().
  Design choice: an enum class over unsigned char, one bit per fault, a
    switch for the name. Rejected: a string per fault (I/O-shaped data on a
    render path); an int code without names (the report is for Matt).
  Name: "fault" — not "error" (nothing failed; the render continued) and not
    "warning" (the host decides how loud).
  Read: the enum and fault_name() at the top of the file.

CHANGED  ValueSource  (engine/include/mforce/core/dsp_value_source.h)
  Owns: the fault byte (std::atomic<unsigned char>), faults(), clear_faults(),
    take_faults() (an exchange: read and cleared in one step, the collectors'
    call), the protected fault(); the never-prepared and re-entry guards in
    prepare(), start(), next() and step() — in prepare() and start() the mark
    is a scope (ComputingScope: set in its constructor, cleared in its
    destructor, so a throw out of on_prepare cannot leave the node marked);
    the clockless fault.
  Design choice: a lock-free byte written with a relaxed fetch_or on the
    fault path only and read with a relaxed load by the host after a render.
    prepare() throws on a zero rate (a host error, where throwing is allowed)
    and does NOT clear the faults: a second prepare must change nothing a
    host can see, and the loader's mixer path re-prepares an instrument after
    its notes were pre-rendered. start() on a never-prepared node sets the bit
    and skips on_start (the 2.2 §7 division sites); next() and step() on one
    set the bit and compute as a bare node always has.
  Alternative: a plain byte. Rejected: the UI's live poll reads it from
    another thread while the audio thread writes — a data race, undefined
    behaviour in C++ even for a byte. Alternative: next() on a never-prepared
    node serving cur_ (the plan's ruling 4). Rejected in execution: 29
    assertions in 13 tests pull bare rate-free nodes without prepare, the
    idiom 2.3's ruling 2 kept; a rate-using node pulled unprepared stays a
    host bug, now named.
  Name: faults_ / faults() / fault() — one word, three roles; "report" was
    rejected because the host reports, the node only records.
  Read: prepare(), start(), next(), step(), the three fault methods.

CHANGED  Component  (engine/include/mforce/core/component.h)
  Owns: bool set_param / add_param / clear_param.
  Design choice: the node answers "did that wire land"; the builder, which
    holds the id, decides it is an error. The six overrides (MultiSource,
    AdditiveSource2, Formant, FullAdditiveSource, Partials, WavetableSource)
    return false for a part of the wrong kind, where they returned silently.
  Alternative: a set_pin beside set_param returning a result struct (the
    spec's name). Rejected: two doors for one act until Part 3's pins; the
    overrides are touched either way.
  Read: the three signatures and their comment.

CHANGED  patch_loader.cpp  (engine/src/patch_loader.cpp)
  Owns: reject_wire() and reject_unknown_keys(); wire_params_generic takes
    the node id; the unknown-key check runs once per node after whichever
    path built it; the allowlist (kBranchKeys) is the union of the old
    warning's list and the lint's scrape of every key the hand-written
    branches and configurators read; a configurator's runtime_error is
    rethrown with the node id and type.
  Design choice: the check moved out of wire_params_generic because the
    hand-built nodes (VarSource, PerformNode, Multiplex, …) never passed
    through it — the lint had found fourteen queue patches with a silently
    ignored VarSource key the loader could not see. The allowlist is global,
    not per type, like the lint's ("a spurious entry can only hide a
    rejection, never invent one"); the structure meter's suite fails when the
    scrape gains a key the loader lacks.
  Alternative: a per-type allowlist. Rejected here: Part 4's codec owns the
    file format; a global list never rejects a valid patch.
  Read: the two functions above wire_params_generic; the per-node call at
    the end of build_graph's node loop.

CHANGED  MonoSource, StereoMixer  (engine/include/mforce/render/mixer.h)
  Owns: FaultReport {nodeId, type, faults}; fault_report_text(); the virtual
    collect_faults (default empty); the mixer walks its channels.
  Design choice: the virtual sits on MonoSource, not Instrument, because
    the CLI's main path renders every patch through Patch::mixer, whose
    channels hold MonoSources; the loader's mixer-mode adapters inherit the
    no-op (Part 4 gives them ids).
  Read: the top of the file.

CHANGED  PitchedInstrument, DrumKit  (engine/include/mforce/render/instrument.h)
  Owns: collect_faults over every voice's nodesById (one entry per file id,
    ORed across the pool, taken as read, sorted by id); a drum reports as
    "drum <n>"; the voice prepares its push-binding chains.
  Read: prepare(), collect_faults().

CHANGED  WaveSource::compute, WavetableSource::fill_table, HybridKSSource::fill_table
  A non-positive frequency becomes the pin's declared minimum; a table
    shorter than one sample becomes one sample; each reports. The narrowest
    value that makes the old condition false, so no render that did not
    terminate moves (ruling 2).

CHANGED  Mesh2DSource::on_prepare   the 48000 fallback goes; prepare() has already thrown on a zero rate.

CHANGED  DelayLineSource  (engine/include/mforce/source/delay_line_source.h)
  The read position wraps to slot 0 — the position, so the fraction with it;
    the ring has no extra slot. The first landing wrapped the index alone and
    left a fraction of ring_ behind: an extrapolation once per lap that a
    constant-source test could not see (refutation I1, fixed in the wave,
    ruling 26). The test feeds a Counter and pins slot 0's value on both laps.

CHANGED  the three cutoff-alias configurators  (engine/src/source_registrations.cpp)
  "cutoff" → cutoffFreq on the Butterworth and SVF filters: a set_param that
    answers false throws at load (it cannot today — the pin exists on each
    type) instead of being ignored.

CHANGED  the hosts  (tools/mforce_cli/main.cpp, tools/mforce_ui/main.cpp)
  The CLI prints one "[faults] <id> (<Type>): <names>" line per node, in id
    order, after every render it makes — run_patch, run_chords, run_josie,
    run_play, run_melody's and run_compose's mixes, run_dun's two — through
    one report_faults(host) template that takes the Instrument or the patch's
    mixer; the UI's Generate, its chords render and --gencheck put "faults: …"
    on the transport status line (after the "Generated …" message, in the
    error colour), and the live pool is polled once a frame.

MEASURED, GONE  the DelayLine wrap counter and tools/measure_25.py  (cb1b4ab only)
  Lesson 3 of report R5 applies: a measurement is a commit, not a mode.
```

## 3. Interfaces changed

| Before | After | Callers touched |
|---|---|---|
| `void Component::set_param/add_param/clear_param` | `bool` | six overrides; the loader checks every result; the UI's editor ignores it (its wires come from descriptors) |
| `wire_params_generic(src, params, valueNodes)` | `wire_params_generic(src, id, params, valueNodes)` | twelve call sites |
| the unknown-key warning inside `wire_params_generic` | `reject_unknown_keys(src, id, params)`, once per node in `build_graph` | the node loop |
| `ValueSource::prepare(ctx)` | throws `std::invalid_argument` on `sampleRate <= 0`; guards re-entry | every host (none passes a zero rate) |
| `ValueSource::start(frames)` | faults and returns on a never-prepared node; guards re-entry | — |
| `ValueSource::next()/step()` | the cycle, clockless and never-prepared faults; no I/O | — |
| — | `faults()`, `clear_faults()`, protected `fault(Fault)` | the two clamps, the base, the hosts |
| — | `FaultReport`, `fault_report_text()`, `MonoSource::collect_faults()`, `StereoMixer::collect_faults()` | the CLI, the UI, the tests |
| `PitchedInstrument::prepare` | also prepares every push-binding chain | — |
| `VoiceGraph::nodesById` empty on the mixer path | filled on both loader paths | collect_faults |
| `WaveSource`, `WavetableSource` throw | clamp and fault | — |
| `Mesh2DSource` 48000 fallback | gone | — |
| `engine_tests` | built with `/bigobj` (MSVC) | the one test translation unit crossed the COFF section limit |
| a collector's `faults()` then `clear_faults()` | `take_faults()` — one exchange | the two collectors (the fix wave) |
| `prepare()`/`start()` set and reset `computing_` | `ComputingScope` marks for the call's scope and unwinds on a throw | — (the fix wave) |
| `PitchedInstrument::collect_faults` in hash-map order | sorted by node id over its appended range | every report (the fix wave) |
| the three `cutoff` configurators ignore `set_param`'s answer | throw on `false`; the loader rethrows with node and type | load time (the fix wave) |
| `print_faults(vector)` and four copied collect-and-print blocks in the CLI | `report_faults(host)` after every render | eight call sites (the fix wave) |
| `kStructural` | `kBranchKeys` | the meter's allowlist test (the fix wave) |
| the null gate hashes the WAV | a `[faults]` line on the CLI's stderr is a diff of its own | — (the fix wave) |

## 4. Duplicates collapsed

- The unknown-key warning and the failure are one function, called once per
  node instead of once per generic wiring.
- `WavetableSource` and `HybridKSSource` clamp their table length the same
  way with the same fault; the two bodies are still twins (Part 3's node
  pass).

- The tap-free-cycle fixture — a `CombinedSource` summing a constant and
  itself, wired after the host calls — was built twice in `engine_tests`
  after the first landing (the 2.2-era cycle test and the top of
  `run_fault_tests`); one `tapless_cycle(h)` helper now (the fix wave).
- The CLI's four copied collect-and-print blocks are one `report_faults(host)`
  (the fix wave; refutation item 4c).

## 5. Metrics

`python tools/structure/check.py --delta cb1b4ab` at the seal (`1481d15`; the
second pass moved no measure):

| Measure | Before (`cb1b4ab`) | After (the seal) | Change |
|---|---|---|---|
| files | 167 | 167 | +0 |
| lines | 59,169 | 59,561 | +392 |
| files over limit | 20 | 20 | +0 |
| functions over limit | 99 | 99 | +0 |
| file-level mutable vars | 114 | 115 | +1 |
| `const_cast` | 10 | 10 | +0 |
| `dynamic_cast` | 57 | 58 | +1 |
| changelog comments | 252 | 250 | −2 |
| duplicate blocks | 157 | 159 | +2 |
| include violations | 13 | 13 | +0 |
| unmapped files | 0 | 0 | +0 |

Types added: `Fault` (`dsp_value_source.h`) and `FaultReport` (`mixer.h`); none
removed; no file added or removed. Of the 392 lines, 166 are
`engine_tests/main.cpp` (the four test sections, the fixture helper, the
fix wave's three tests), 63 `dsp_value_source.h` (the enum, the four fault
methods, the guards, the scope), 36 `patch_loader.cpp` (the allowlist's
scrape, `reject_wire`, `reject_unknown_keys`, the configurator's rethrow),
33 `instrument.h` (the chains prepared, two `collect_faults`), 32 `mixer.h`
(`FaultReport`, `fault_report_text`, `collect_faults`), 24 the UI, 19 the
CLI; `dsp_wave_source.h` lost one (the include) and `delay_line_source.h`
is back to its length (the zero slot went, the position wrap came). The
four measures that moved: the mutable variable is `g_genFaults`, the UI's
status text for the last Generate, beside the other transport statics (Part
7's); the `dynamic_cast` is a test's `PitchedInstrument*` from
`ip.instrument` in `run_fault_tests`, the idiom nine other tests use; the
two changelog comments went with the old unknown-key warning block (its
"backlog 67" and "2026-09-15 scan" notes) and none arrived; the duplicate
count, verified by scanning the `cb1b4ab` tree beside the seal: the two
loader voice loops grew from 55 to 62 shared lines (both now fill
`nodesById`, ruling 21; the pair is pre-existing and Part 4's), run_dun's
two render blocks (`mforce_cli/main.cpp:896-903` and `:1007-1014`, a
pre-existing pair that the one added `report_faults` line pushed over the
detector's eight-line threshold), and two blocks the detector reports in
`tools/test_figures/main.cpp`, a file this checkpoint did not touch
(byte-identical in both trees; lizard's token-window boundaries moved with
the rest of the set). The two duplicate pairs the first landing added — the
test fixture and the CLI's four collect-and-print blocks — are collapsed
(section 4), which is why the first landing's +4 is the seal's +2.

The renders. Three classes were measured before the cut
(`docs/architecture/measurements/2026-10-20-no-throw-and-wires.md`) and checked
by the A/B over the 1,615-patch sample at Task 3 and Task 4 (Appendix A):

1. **The two render-path throws** are unreachable from any stored patch: of
   the 157 that do not load, 155 fail on an unknown node type and 2 are
   sweep manifests without a graph. Task 1 moved nothing (78/78).
2. **The wires.** Task 3's A/B (`old` against `t3`): 0 differ in every set —
   nothing a valid patch renders moved. What stops loading is one class, a
   stale key on a hand-built or legacy node, and the loader names each: in
   the sample, `sweep/stk_bandedwg_port/stk_bandedwg_bowl_struck` (`node
   'Input' (VarSource): unknown key 'value'`, the key the fourteen bwg queue
   patches carried and were repaired of before the cut); over the whole
   tree, by the lint's node-key findings and each confirmed against the CLI
   at the close, the four `stk_bandedwg_port/*_struck` sweeps (bar, bowl,
   glass, tbar) and one archive file, `old/Squeaker.json` (`node 'additive1'
   (AdditiveSource): unknown key 'amplVarPct'`, seven legacy AdditiveSource
   keys). Nothing in library, baselines, pending or audition does. The
   measured list (a)'s one entry, `old/baselines_culled_2026-09-14/
   ks_morph_saw_test`, loads and renders: its four keys (`decayFactor`,
   `holdCycles`, `morphRate`, `targetLength`) are read by WavetableSource's
   hand-written branch, which the old warning's list lacked — the warning
   was wrong in that direction too, and the union allowlist (ruling 22)
   retired the false positive; Appendix A's `old` row (0 stopped loading)
   already showed it, and the measurement document's "stops loading after
   the cut" sentence for that file is superseded here (ruling 25). `sweep/`
   is regenerable; `old/` is never deleted, and a file that no longer loads
   is still the record of what was tried (ruling 9).
3. **The DelayLine wrap.** The measured list held 28 patches whose read
   landed on the slot past the ring. The first landing (`t4`) moved 25 of the
   sample, every one on the list — by the wrong amount: the index wrapped and
   the fraction did not, so the wrap sample extrapolated by the ring length
   instead of reading slot 0 (refutation I1, ruling 26; the test could not
   see it, its source a constant). The fix wave (`t5`) moved the same 25
   again, and against the pre-2.5 arm the set is the same 25 and nothing
   else: `double_advance_bug` and `double_advance_ok` (the manifest refrozen
   for those two and no other entry, twice), `pending/brass/Trombone_att1`,
   the fourteen `audition/bwg_perc1` and `bwg_perc2` patches, and eight
   sweeps (`feedback_valve1/fbv_r090_q5`,
   `stk_bandedwg_port/stk_bandedwg_tbar_bowed`, the three
   `stk_blowhole_port`, `trombone1/loud`, `trombone3/ab_attempt2_ff` and
   `line_soft`). Three listed patches did not move under either read:
   `nlbore_sine_d005` and `nlbore_sine_neg040` (one or two reads of a slot 0,
   and a slot 1, that held zero) and `stk_bandedwg_bowl_struck` (it fails to
   load since Task 3). The mechanism: a read that returned the extra slot's
   zero returns slot 0's sample — one sample per lap, where the loop's write
   index and read length align to within a float's rounding.

Render time (`python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre25.exe
build/tools/mforce_cli/Release/mforce_cli.exe 7`; "speedup" = base / new):

| patch | base wall (ms) | new wall (ms) | speedup | base render (ms) | new render (ms) |
|---|---|---|---|---|---|
| oboe1 | 943.3 | 948.5 | 0.99x | 20.6 | 22.2 |
| piano_default | 1133.5 | 1144.1 | 0.99x | 45.3 | 45.8 |
| viola_default | 1273.9 | 1320.3 | 0.96x | 5.8 | 6.1 |
| A sine | 460.3 | 490.9 | 0.94x | 138.0 | 140.2 |
| B sine+env | 515.4 | 526.8 | 0.98x | 166.3 | 162.9 |
| C filter x8 | 870.5 | 838.7 | 1.04x | 505.4 | 502.0 |
| D 8 sines | 24.1 | 1473.9 | — | — (terminated) | 1148.9 |
| E shared ref | 549.4 | 558.8 | 0.98x | 196.8 | 209.9 |
| F 40 sines | 3886.3 | 3724.7 | 1.04x | 3643.3 | 3492.9 |
| G 40 own env | 5032.2 | 4872.2 | 1.03x | 4784.6 | 4640.1 |
| H 40 shared | 3684.1 | 3634.4 | 1.01x | 3446.6 | 3408.7 |
| I 40 envs | 2000.8 | 1941.0 | 1.03x | 1732.3 | 1677.7 |

Minimum wall time of seven alternating runs per patch, on an idle machine
after the gates (a first run, taken while the lint and three CLI loads of mine
ran beside it, was discarded: its base arm came out 1.4x slower than this one
on the same binary). The second-long rows (F to I) are within four percent,
all in the new binary's favour; the sub-second rows scatter both ways, oboe1
(+1.6 ms of 20.6) and E (+13 ms of 197) beyond the two percent the plan
expected, on renders short enough that one scheduler tick is several percent.
Recorded, not chased (ruling 13): the checkpoint trades the throw branch for a
clamp branch in `WaveSource::next` and adds nothing else to the sample path;
the fault store sits on the cold branches. The D row is the checkpoint itself:
`D_8sines_2wired_each` has an envelope-driven frequency that reaches zero, so
the base binary terminated at the first such sample (`WaveSource: non-positive
frequency`; the 2.4 table shows a dash in both arms) and the 2.5 binary renders
the whole patch with that sample clamped — silently, because the micro patch is
mixer-mode and no channel collects the bit (section 7).

The table is the engine at `fbfffe6`, before the fix wave. The fix wave was
timed three times against the same base binary and none of the runs met the
table's standard: the first ran beside VS Code's C++ IntelliSense server
re-indexing after the header edits (its base arm 1.1x the table's), the second
under a transient load (1.7x), the third on a machine a process sample called
quiet (1.1x, viola and F at 1.3x) — and the render columns of that third run
scatter from 11 percent faster (H) to 7 percent slower (C) with no row slower
in all three. The wave's one change on the sample path is a float compare in
place of an int compare in the DelayLine read (ruling 26); the exchange, the
scope guard and the host reports are control side. Recorded, not chased:
checkpoint 2.6 times this seal as its base on a quiet machine.

## 6. Decisions and rejected alternatives

Each is a ruling in the plan's Ledger, in the order made.

1. **A fault is a per-node atomic byte**, set with a relaxed `fetch_or` on the
   fault path only and read and cleared by the host after a render. Rejected:
   a plain byte (a data race with the UI's live poll — undefined behaviour,
   not a benign tear); a per-host counter (loses which node faulted, which is
   the whole report). Cost: one byte per node; nothing on the per-sample path.
2. **The clamp is the narrowest value that makes the old throw's condition
   false**: a non-positive frequency becomes the pin's declared minimum; a
   table shorter than one sample becomes one sample (as `HybridKSSource`
   already did). Not the pin's maximum: a frequency above 20 kHz never threw,
   and clamping it would move renders the spec does not list. Nor the pin's
   minimum for a positive frequency below it: the signed text says "a
   frequency below the minimum becomes the minimum", and the landed clamp is
   narrower — a frequency in (0, min) is neither clamped nor faulted, for the
   same reason. The narrowing is REVIEW R6's item (4): Matt's to accept into
   Part 2 §6, or to schedule as a behaviour checkpoint with its own measured
   list (refutation I11).
3. **`prepare()` throws on a zero rate**, where the spec allows a throw, and
   Mesh2D's 48000 fallback goes: one rule in the base is better than one
   fallback per node. No test prepared with a zero rate.
4. **A never-prepared node: `start()` faults and skips `on_start`.** The
   plan's ruling had `next()` and `step()` serve `cur_` too; withdrawn in
   execution (ruling 16): they fault and compute as a bare node always has.
5. **A clockless pull sets `Fault::Clockless`** on the cold branch; a bare
   node in a test sets it and nobody reads it there.
6. **The tap-free cycle is a bit, and the guard covers the host calls.**
   `report_cycle_once_`, `cycleReported_` and `<cstdio>` are gone from the
   base; `prepare()` and `start()` re-entered through a cycle return with the
   bit instead of overflowing.
7. **`set_param` returns `bool`; the builder throws.** The node answers "did
   that wire land"; the loader, which holds the id, turns `false` and an
   unknown key into `std::runtime_error` with node, pin and reason. Rejected:
   a `set_pin` beside `set_param` (two doors for one act until Part 3's pins).
8. **The unknown-key list was measured with the current loader** and
   cross-checked with the lint; the loader proved blind to hand-built nodes
   (ruling 14), and the union became the list.
9. **Tracked patches on the list are repaired before the cut** (the key was
   ignored, so removing it cannot move the render; the hash proves it);
   `sweep/` and `old/` stop loading and are listed.
10. **The DelayLine wrap lands in its own task with its own measured list**;
    the counter is committed in Task 0 and deleted in Task 1.
11. **Reports, by host**: the CLI's `[faults]` block, the UI's status line
    after Generate and `--gencheck`, the live pool polled once a frame;
    mixer-mode graphs are not collected (Part 4's mixer MODE).
12. **Landing order and the old arms**: Task 1 and 2 byte-identical, Task 3
    moves nothing valid and lists what stops loading, Task 4 is the one task
    whose renders change; 2.4's sealed arms are 2.5's `old`.
13. **Timing is measured at the close against `cli_pre25.exe`**; a cost
    beyond noise is reported, not chased.
14. **(execution, Task 0) The loader's warning covers descriptor-wired nodes
    only.** `VarSource` is built by a hand-written branch that never reaches
    the unknown-key block inside `wire_params_generic`; the lint found
    fourteen queue patches with a silently ignored `VarSource.value` the
    loader could not see. The list is the union of the loader's warnings and
    the lint's node-key findings; the check runs once per node after
    whichever path built it. The fourteen (all `value: 0.0`, the branch's
    default) were repaired byte-surgically and re-rendered: 14 of 14
    identical.
15. **(execution, Task 1) `/bigobj` for `engine_tests`**: the 6,000-line test
    translation unit crossed MSVC's COFF section limit once the fault header
    and a fourth test section landed; the compiler's own answer, no code
    change; Part 8 owns splitting the file.
16. **(execution, Task 1) The first null gate was 72/78** — the piano family,
    viola_default and wiring_setting: `PitchedInstrument::prepare` never
    prepared the push-binding chains (the dynamicPins sources, stepped at a
    note's Setup and never pulled by the root), which computed anyway because
    a CurveNode needs no rate — until the never-prepared guard served the
    memo. The voice now prepares every chain with its context; all six
    sources are rate-free CurveNodes on the perform frequency, so preparing
    them moves nothing: 78/78. The fault was built to find exactly this.
17. **(execution, Task 1) Ruling 4 withdrawn for `next()` and `step()`**: 29
    assertions in 13 pre-existing tests failed under "serve `cur_`", every one
    a bare rate-free node pulled without prepare — a ConstantSource, a
    CurveNode, a PerformOut, a NameGate, a TapSource, a CombinedSource wired
    after the host calls: the idiom 2.3's ruling 2 kept on purpose, and one
    `ensure_constant` leans on at load. A rate-using node pulled unprepared
    can still misbehave in `compute()`, as today; the fault names it when the
    host survives to read it (section 7).
18. **(execution, Task 2) `collect_faults` lives on `MonoSource`**, not
    `Instrument`: the CLI's `run_patch` renders every patch through
    `Patch::mixer`, whose channels hold `MonoSource`s, so the mixer walks its
    channels without a cast and the loader's adapters inherit the no-op.
19. **(execution, Task 2) The meter's known-answer test re-pinned at 1713**:
    the gencheck mode lives inside the UI's `main()` and gained one line
    (2.3's ruling 21 did the same).
20. **(execution, Task 2) `prepare()` does not clear the faults.** A scored
    patch is pre-rendered at load and the mixer's `prepare()` re-prepared the
    instrument's nodes afterwards, wiping the bits before the CLI could read
    them; the plan's contract had the clear and was wrong — a second prepare
    must change nothing a host can see.
21. **(execution, Task 2) Both loader paths fill `VoiceGraph::nodesById`**:
    the mixer path's voice loop left it empty, so `collect_faults` had
    nothing to walk on the CLI's main path while `--gencheck` reported.
22. **(execution, Task 3) The allowlist is the union of the old warning's
    list and the lint's scrape.** The per-node check, now reaching the
    hand-built nodes, rejected keys their branches read (PerformNode's
    `field` first): twenty manifest patches stopped loading and `engine_tests`
    died on the throw. The loader's list is generated from
    `tools/lint_patches.py`'s own scraper (143 keys), global like the lint's,
    and a structure-meter test fails when the scrape gains a key the loader
    lacks. A per-type list is Part 4's codec.

23. **(execution, Task 3) The load A/B found one more of the bwg class.**
    `old` against `t3` over the eight sets: 0 differ everywhere; one patch
    rendered in `old` and fails to load from `t3` on —
    `sweep/stk_bandedwg_port/stk_bandedwg_bowl_struck`, `VarSource.value` on
    a sweep the lint cross-check (library, baselines, audition, pending) had
    not covered. The lint then ran over `sweep/` and `old/` too, so section 5
    lists every file of the class.
24. **(execution, Task 4) The 2.2 fix-wave test pinned the workaround.** RED
    was the new wrap assertion (0 against 1.0); with the fix in,
    `run_delay_line_zero_slot_tests` failed on its two zero-slot assertions —
    it asserted the extra slot's zero, the thing being removed — and was
    restated as `run_delay_line_wrap_tests` (slot 0, the first write, on both
    laps), the duplicate added at RED removed. The null gate moved exactly the
    two listed baselines (`double_advance_bug`, `double_advance_ok`), refrozen
    for those two and no other entry; the A/B moved 25, every one on the wrap
    list; three listed did not move (section 5).
25. **(execution, Task 5) The archive count in the Ledger was wrong and is
    corrected here.** The Task 3 addendum read the lint's total over `old/`
    ("44 in 21 files") as 21 archive files that stop loading; 37 of the 44
    are the lint's paramMap curve-endpoint notes in 20 files, which the
    loader has never rejected (Task 0 said so of the same class in
    audition). By the lint's node-key findings and the CLI, one archive file
    stops loading (`Squeaker.json`), and the measured list (a)'s one entry
    (`ks_morph_saw_test`) loads: the old warning flagged four keys
    WavetableSource's own branch reads, a false positive the union allowlist
    retired. The lesson is the one the plan already carried: the loader's old
    warning was not the authority in either direction.
26. **(fix wave) The wrap wraps the position, not the index.** The refuter's
    I1: with `i0` wrapped alone, `frac = rp − i0` was `ring_` on the wrap
    sample, so the read extrapolated `buf_[0] + ring_·(buf_[1] − buf_[0])`
    once per lap — and the test could not see it, its source a constant
    (`buf_[0] == buf_[1]`). Now `rp` itself wraps before `i0` and `frac` are
    taken; the test's source is the `Counter` (consecutive samples differ)
    and it asserts slot 0's value on both laps. The two refrozen baselines are
    refrozen again and the A/B gains a `t5` arm (section 5).
27. **(fix wave) `take_faults()` collects.** A collector's `faults()` then
    `clear_faults()` left a window in which a bit the audio thread set was
    wiped — the UI's live poll is exactly that reader. `exchange(0)` reads and
    clears in one step; both collectors use it; `faults()` and
    `clear_faults()` stay for the tests.
28. **(fix wave) The re-entry mark is a scope.** `prepare()` set `computing_`
    and cleared it after `on_prepare`, with no unwind: an `on_prepare` that
    throws — `prepare` may, and Multiplex's rebuild calls the builder, which
    now throws on a bad wire — left the node and every ancestor on the stack
    marked forever (every `next()` a cycle fault, every later `prepare()` a
    no-op). `ComputingScope` marks in its constructor and unmarks in its
    destructor; `start()` takes the same scope; `next()` keeps its plain
    stores (`compute()` may not throw, by this checkpoint's own rule). Pinned
    by a node whose first `on_prepare` throws.
29. **(fix wave) Every CLI render reports, through one `report_faults(host)`.**
    Ruling 11 named four paths and missed three (run_melody's mix,
    run_compose's mix, run_dun's two renders) and the UI's chords render;
    each now collects after its render, and the four copied collect-and-print
    blocks are one template function taking the Instrument or the mixer (the
    two share no base — `StereoMixer` is not a `MonoSource`; a template is the
    honest door until Part 5 gives the host one).
30. **(fix wave) The report is in id order.** The collector walks an
    `unordered_map`; its appended range is sorted by node id, so the CLI block
    and the UI line read the same on any STL. Pinned with two faulting nodes
    declared in reverse order.
31. **(fix wave) A configurator's failed wire names its node.** The three
    `cutoff` → `cutoffFreq` alias configurators ignored `set_param`'s answer;
    they throw on `false` with the key and the pin, and the loader's generic
    path rethrows what a configurator throws — any `std::exception`, the JSON
    reader's type errors included (the re-review's N4 widened it from
    `runtime_error`) — with the node id and type, so a configurator's
    refusal, this one or a preset name's, reads like `reject_wire`'s.
32. **(fix wave) The null gate reads the CLI's stderr.** A `[faults]` line on a
    manifest patch is a diff of its own (`FAULTS:` in place of the hash): the
    false-alarm class — a spurious `NeverPrepared` or `Clockless` on a valid
    patch — had been checked by hand on oboe1 and nowhere else.
33. **(fix wave) Names and comments.** `kStructural` → `kBranchKeys` (the list
    is every key a hand-written branch or configurator reads; "structural"
    named a third of it); the two comments naming `Instrument::collect_faults`
    say `MonoSource`; the ruling-20 comment stands above
    `refresh_auto_sources()` on its own lines; `dsp_wave_source.h` drops the
    `<stdexcept>` it no longer uses; the three new test headers lose their
    "Checkpoint 2.5:" prefix (a date in another form); the tap-free-cycle
    fixture is one `tapless_cycle(h)` helper; a prepare-after-render test pins
    ruling 20.

## 7. Debt knowingly left

- **A rate-using node pulled without prepare still misbehaves in `compute()`**
  (a DelayLine's `% ring_` with a zero ring): `start()` is guarded, `next()`
  is not, by ruling (section 6). The fault names the node when the host
  survives to read it; the Debug assert on `start()` stays. Part 3's nodes own
  their own guards, or Part 5 decides the host contract.
- **The unknown-key allowlist is global, and it holds the commonest pin
  names** — `frequency`, `source`, `gain`, `cutoff`, `curve`, `mode`, `delay`,
  `ratio`, `volume`, `a`, `b`, `x` — so a key that spells one of those lands on
  any node: a WavetableSource given `source` (its pin is `inputSource`) loads
  clean and renders without excitation, a WhiteNoiseSource given `frequency`
  loads clean. A slip in a rarer name is rejected. The lint shares the blind
  spot. A per-type list is Part 4's codec (refutation I5).
- **Mixer-mode graphs report no faults**: the loader's adapters inherit the
  no-op until Part 4 gives them ids. The live case sits in our own timing set:
  `renders/scratch/ab_tree/micro/D_8sines_2wired_each.json`, a mixer-mode
  micro patch whose envelope-driven frequency reaches zero, terminated every
  binary before this checkpoint (`WaveSource: non-positive frequency`; the
  2.4 timing table has it as a dash in both arms) and renders under 2.5 with
  the frequency clamped — and prints nothing, because no channel collects.
- **The UI's own JSON path drops an unknown key on load and never writes it
  back**: a patch repaired by the UI is a patch with the key gone. Part 4's
  codec.
- **`Fault::Clockless` is set by every bare test node** and read by nobody
  there; a host is the only reader.
- **Two `fill_table`s still allocate inside `next()`**: `WavetableSource`'s
  (`values_.resize`) and `HybridKSSource`'s, reached from `compute_ks`; 2.6's
  allocation test will find both (refutation I6a).
- **Nodes made at bind time are outside `nodesById`**: the push-binding
  chains' CurveNodes, built in the loader's binding pass, are prepared
  (ruling 16) but collected by nobody; a fault on one is invisible until
  Part 4 gives bind-time nodes ids (refutation I6b).
- **The fault vocabulary has two homes**: `Fault` and `fault_name` in core,
  `FaultReport` and `fault_report_text` in `render/mixer.h`, a mixer header
  that owns the wording every host prints. A `render/fault_report.h` is the
  tidy answer; Part 5, the host contract, decides where the report lives
  (refutation item 1).
- **The meter's known-answer test pins the UI's `main()` at 1713** (the
  gencheck mode lives inside it); a frozen fixture is Part 8's.
- **Test gaps the review named and the wave did not close**:
  `StereoMixer::collect_faults` through `load_patch_file` on a mixer-mode
  patch with an instrument channel (the CLI's main path is proved by hand
  on `zero_freq.json`), `DrumKit::collect_faults`, `HybridKSSource`'s
  `TableClamped`, the array `add_param` rejection (refutation I9); Part 8
  owns the test file.
- **The UI collects in three places** (`generate_unified`, the chords render,
  the live poll), the collect-then-format shape the CLI collapsed; below the
  meter's threshold; Part 7's file (re-review N2).
- **`prepared()` is true after a throwing `on_prepare`**: `ctx_` is assigned
  before `on_prepare` runs, so a node whose `on_prepare` threw reports itself
  prepared; pre-existing since 2.2, covered by the host contract (a thrown
  `prepare` means the graph is not used), and the node recovers on the next
  `prepare()` (re-review N5).
- **The roundtrip gates** fail on the backlog-79 set — `double_advance_bug`,
  `double_advance_ok`, `loop_tap_counter`, `loop_tap_starved`,
  `perform/wiring_setting`, `perform/wiring_smoke` — the same six as at 2.4
  (the wrap changed `double_advance_ok`'s render but not the set);
  pre-existing, Part 4's codec.
- Carried unchanged from 2.4's section 7: `previous()` and `claimed_tick()`
  public on every node for the tap edge; `step()`'s limits; the tap on a view;
  the Setup-time read through a pulled tap.

## 8. Independent review

# Refutation R6: checkpoint 2.5, no throw on a render path; fault flags; a rejected wire says so (cb1b4ab..fbfffe6, base cb1b4ab)

I read the brief, the module map, Part 2 section 6 as signed, the plan with its Ledger, report sections 1–7, the Task 0 measurement, and the full `git diff cb1b4ab fbfffe6 -- engine tools`, then the touched files at HEAD and the untouched code they lean on (`render_context.h`, the mixer adapters in `patch_loader.cpp`, every node that declares a `Pull::Manual` pin, `source_registrations.cpp`'s configurators, `multiplex_source.h`, `ks_string.h`, `tools/lint_patches.py`, `tools/gates.py`, the root `CMakeLists.txt`). I recounted the eight A/B sets from `renders/scratch/ab25_tree/<set>/{old,t3,t4}/hashes.txt` with a script of my own that only reads and prints. Nothing was built, rendered or run; no engine binary or test was executed. Line numbers are HEAD (14af1ff; `git diff fbfffe6 HEAD -- engine tools` is empty) unless a commit is named.

### The brief, item by item

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Separation of concerns | PASS with a flag | No file newly mixes drawing, audio, persistence and model: the CLI and UI additions are host code (`mforce_cli/main.cpp:29-33`, `mforce_ui/main.cpp:4340-4348`), the loader's additions are load-time (`patch_loader.cpp:216-284`). The flag: one concept has two homes — `Fault`/`fault_name` in core (`dsp_value_source.h:18-39`) and `FaultReport`/`fault_report_text` in `render/mixer.h:10-28`, a mixer header that now also owns the report wording every host prints. A `faults.h` in render (or the wording beside `fault_name` in core) would be one home. |
| 2 | Single responsibility | PASS | `reject_wire` (`patch_loader.cpp:216`) and `reject_unknown_keys` (`:229`) each do one thing; `PitchedInstrument::collect_faults` (`instrument.h:222`) reads and clears; `fault_report_text` formats. `ValueSource` now also "records what went wrong on its own render path" — the design puts the bit on the node (section 6, "the flag is a per-node bit"), so that is the contract, not an "and". |
| 3 | Dependency direction | PASS | New includes: `<atomic>`, `<stdexcept>` in core (`dsp_value_source.h:2,6`); `<algorithm>` in render (`instrument.h:2`), `<string>` in `mixer.h:2`; the CLI dropped `delay_line_source.h` (the Task 0 counter). No engine include crosses the map; the meter's include violations are 13→13 (report §5). The only cross-module reference from core to render is a comment, and it is wrong (item 9). |
| 4 | Duplication | FAIL (minor) | (a) The tap-free-cycle fixture: `engine_tests/main.cpp:2206-2216` and `:5958-5972` — nine lines each, confirmed. (b) The two table-length clamps are five identical lines: `wavetable_source.cpp:29-33` and `hybrid_ks_source.cpp:44-48` (listed as Part 3's). (c) New and unlisted: four identical one-line collect-and-print blocks in the CLI (`mforce_cli/main.cpp:142, 216, 753, 819`) — `print_faults` should take the `MonoSource&` and do the collect. (d) The two loader voice loops grew in lockstep (`patch_loader.cpp:1413-1449` and `:1712-1744`, 55→62 shared lines) — pre-existing pair, Part 4's. |
| 5 | Encapsulation | PASS with a flag | `faults_` is private (`dsp_value_source.h:227`), `fault()` protected (`:195`), `faults()`/`clear_faults()` public (`:155-156`); `DelayLineSource::trace`/`wraps` public statics are gone. The flag: the host's read-then-clear is two public calls on a byte another thread writes (`instrument.h:225-227`) where one atomic take belongs (I2). `g_genFaults` is a new file-static mutable in the UI (`mforce_ui/main.cpp:4340`), the pattern the transport already uses. |
| 6 | Ownership | PASS | `FaultReport::type` borrows a string literal: every `type_name()` returns one (grep for a non-literal return found none). `b.chain` is a `shared_ptr` the voice owns and now prepares (`instrument.h:208`). `nodesById` holds `shared_ptr` copies of nodes the voice already owns (`patch_loader.cpp:1446, 1741`), the pre-existing shape; `build_graph` runs per voice, so the `std::move` of `g.valueNodes` is sound. The test's self-cycle is broken at `engine_tests/main.cpp:5972`. No new ownership cycle. |
| 7 | Abstraction fit | PASS with flags | The bit, the atomic byte, the host-side collection and the load-time throw are what section 6 signed. Three deviations the design text does not contain, each recorded as a ruling: (a) `set_param` returning `bool` stands in for the design's `set_pin` result (ruling 7, Part 3's); (b) the clamp is to the throw's condition (`f <= 0`, `dsp_wave_source.h:29`), not to "the node's declared range (a frequency below the minimum becomes the minimum)" — a frequency in (0, 0.01) is neither clamped nor faulted (ruling 2; I11); (c) the engine's allowlist is generated from a Python regex over its own sources (`lint_patches.py:67-73`, `test_structure.py:129-141`) — the design said the lint "can pre-check", not that the loader's truth is the lint's scrape. All three are defensible for a behaviour-preserving checkpoint; (b) needs Matt's assent in section 6, since it narrows the signed sentence. |
| 8 | Real-time rule | PASS with flags | The clocked common path of `next()` is unchanged (`dsp_value_source.h:104-107`); the fault store is a lock-free `fetch_or` on the cold branches only (`:108-111, 195`); `start()` gained two branch tests (`:80-82`). Every production host is clocked, so no per-sample RMW lands on a hot path: the mixer adapters (`patch_loader.cpp:60-87`), the voices (`instrument.h:204`), the drums (`:798`), the UI editor (`mforce_ui/main.cpp:239`). Remaining allocation inside `next()`: `WavetableSource::fill_table` (`wavetable_source.cpp:33`, called from compute at `:107`) AND `HybridKSSource::fill_table` (`hybrid_ks_source.cpp:48`, called from `compute_ks` at `:100-102`) — the report's §7 names only the first. `collect_faults` allocates, control side only. No throw reachable from `next()`/`start()` in core, source or filter (grep: the only throws left are `prepare`'s and `envelope_json.h`'s). |
| 9 | Names and comments | FAIL (minor) | Three new "Checkpoint 2.5:" section headers in `engine_tests/main.cpp:5896, 5954, 5998` — a changelog marker the meter's regex (`measure.py:19`: dates, `backlog N`, `REVIEW N`) cannot see; the file already carried ten, so the count moved 10→13 unmeasured. `dsp_value_source.h:17` and `:153` say `Instrument::collect_faults`; the virtual is `MonoSource::collect_faults` (`mixer.h:40`, ruling 18). `dsp_wave_source.h:4` keeps `<stdexcept>` with no throw left. `kStructural` (`patch_loader.cpp:239`) names a list that is now mostly branch keys, not structure. The functional names (`fault`, `faults`, `clear_faults`, `FaultReport`, `reject_wire`, `reject_unknown_keys`) say what they are. |
| 10 | Tests | FAIL (Major, via I1) | The wrap test (`engine_tests/main.cpp:5272-5290`) feeds a constant 1.0, so `buf_[0] == buf_[1]` and the interpolation `buf_[0]*(1-frac) + buf_[1]*frac` equals 1.0 for ANY `frac`; the test cannot see that `frac` is `ring_` on the wrap sample (I1). Unpinned: ruling 20 (a second `prepare()` keeps the bits — the CLI's main-path report depends on it); `StereoMixer::collect_faults` (`mixer.h:65`, the CLI's main path) and `DrumKit::collect_faults` (`instrument.h:786`); `HybridKSSource`'s `TableClamped`; the multi-input `add_param` rejection (`patch_loader.cpp:308`); the paramMap "has no pin" throw (`:1149`); any corpus-level check that a valid patch reports no faults (the null gate ignores stderr). Pinned through a real consumer: the loader + `PitchedInstrument::render` + `collect_faults` (`:5983-5995`) and the three loader failure texts (`:6001-6026`). |

### Review Focus, item by item

1. **A modulated frequency reaching zero mid-note.** Pinned at `engine_tests/main.cpp:5903-5920` on a bare SineSource with a ticking `TestHost` (f 440 → 0 → 440: no throw, finite, bit set, cleared), and through the real consumer at `:5978-5995` (a loaded instrument with `frequency: 0.0`, `play_note`, `render`, `collect_faults` → one entry `osc` with `FrequencyClamped`, then empty). Traced by hand: `dsp_wave_source.h:28-29` sets `f = frequency.min` (0.01, `:16`), `currPhaseIncr_ = 0.01/48000`, `wave()` finite. Not pinned: a positive frequency below the minimum (0.005) — by ruling 2 it is neither clamped nor faulted; and the `size() == 1` assertion would not notice a second bit (`Clockless`) on the same node, so the instrument path's clock is proved only by `instrument.h:204`, not by this test.
2. **A never-prepared node started and pulled.** Pinned at `:5932-5945`: under `NDEBUG` a bare `DelayLineSource` `start(4800)` sets `NeverPrepared` and does not divide (on_start skipped, `dsp_value_source.h:81`); a bare `Counter::next()` computes 1.0 and sets the bit (`:108-109`). The Release harness runs this (ctest `-C Release`); in Debug the assert at `:80` is the contract and the block is skipped. Not pinned: `step()`'s fault (`:130`); the ruling-17 consequence that a rate-using node's `compute()` after an unprepared start still misbehaves (recorded as debt, correct).
3. **A cycle with no tap, hand-wired after the host calls.** Pinned at `:5958-5972`: `next()` serves 1.0 (1 + previous 0, traced through `:103`), `prepare()` and `start()` re-entered through the cycle return with the bit (`:61, :82`) — no overflow. Not pinned: that the node still computes after the re-entered `prepare()` (it does: `computing_` is reset at `:69` on the outer call), and the exception path of the same guard (I3): a throw out of `on_prepare` leaves `computing_` true with no test to say so.
4. **A wire the registry cannot place.** Pinned at `:6001-6026`: `set_param` false for `freq` on a SineSource and for a SineSource in `partials`; the loader's three texts, traced against `reject_unknown_keys` (`patch_loader.cpp:281-283`) and `reject_wire` (`:219-220`), including the hand-built `VarSource.value` case the lint found. Not pinned: the array `add_param` path (`:308`, a FormantSequence `spectra` list holding a non-formant), the paramMap throw (`:1149-1151`), and the three configurator calls at `source_registrations.cpp:423, 430, 443`, whose `bool` is ignored (I12).
5. **A DelayLine read rounding to the ring length.** Pinned at `:5272-5290` by the mechanism (`nextafter(1470, 0)` at 22050 → `len` 15.0000012, write index 15, `rp = -1.2e-6 + 1106` rounds to `1106.0f`, `i0 = 1106 → 0`), on both laps. Traced by hand: the test passes, and the fix is wrong — `frac = rp - float(i0)` at `delay_line_source.h:83` is `1106.0f - 0.0f = 1106`, so the read is `buf_[0]*(1-1106) + buf_[1]*1106`, which equals 1.0 only because the test's source is a constant. See I1. The A/B's "25 moved, every one on the wrap list" is true; by how much they moved is not what the report believes.

### Rulings weighed

1. **Stands**, with I2: the byte is atomic and relaxed as ruled; the host's "read and cleared" is two operations (`instrument.h:225-227`), not one, so a bit set between them is lost.
2. **Overstated as "the spec's"**: the spec says "a frequency below the minimum becomes the minimum"; the code clamps `f <= 0` only (`dsp_wave_source.h:29`). The byte-identity argument is sound and this is the right refactor-phase choice, but it narrows the signed sentence and should be recorded in section 6 as such (I11).
3. **Stands.** `RenderContext{}` defaults to rate 0 (`render_context.h:23`); no host passes it (the five loader sites use `{sampleRate}`); `mesh2d_source.h:133` is the context's rate.
4. **Superseded** by 17.
5. **Stands.** Every host is clocked (item 8); the bit is set only by bare test nodes and by a host bug the report would name.
6. **Stands for the bit; wrong in one clause.** "`computing_` after a throw is retired with the throws" — `prepare()` throws by design (ruling 3) and `on_prepare` can throw (Multiplex `rebuild_` → the loader's builder, which this checkpoint made throw on a bad wire; `resize` → `bad_alloc`). The guard has no unwind path (I3); the old `computing_ = false` at entry was the recovery.
7. **Stands**, with I12: three configurator `set_param` results are ignored (cannot fail today).
8. **Stands**, superseded in part by 14 (the warning was blind to hand-built nodes and wrong in the other direction too, ruling 25).
9. **Stands.**
10. **Stands as process**; the landed fix is wrong (I1), so the wrap list's renders moved by the wrong amount and the two refrozen manifest entries pin a wrong value.
11. **Overstated.** The CLI collects on four paths (`:142, 216, 753, 819`) and not on `render_and_write` (`:317-331`, run_melody's render), `run_compose` (`:608`) or `run_dun` (`:892, :1002`); "the melody render" in the report is run_josie's `render_piece_to_wav` (`:305`). The UI's chords render (`mforce_ui/main.cpp:8137`) is not collected either. I4.
12. **Stands.** My recount: `old` vs `t3` DIFFER 0 in all eight sets; `t3` vs `t4` 25 distinct; one file in `old` not in `t3` (`sweep/stk_bandedwg_port/stk_bandedwg_bowl_struck`). Appendix A is right to the row.
13. **Stands**; the numbers and the discarded run are claims I cannot check by reading.
14. **Stands**; `:6021-6026` pins the VarSource case.
15. **Stands.**
16. **Stands**; note the chain nodes are not in `nodesById`, so a fault on one is invisible (I6).
17. **Stands**; the consequence is honestly in §7.
18. **Stands**; the CLI's main path needs it (`mixer.h:65-68`).
19. **Stands.**
20. **Stands, unpinned**: no test re-prepares an instrument after a fault and reads it; a reintroduced clear would silently empty the CLI's main-path report. The comment carrying the ruling hangs on `refresh_auto_sources()` (`dsp_value_source.h:64`).
21. **Stands**; the pair grew (item 4d).
22. **Stands as mechanism; the debt is understated.** The list (`patch_loader.cpp:239-271`) holds `frequency`, `source`, `gain`, `cutoff`, `curve`, `mode`, `a`, `b`, `x`; a one-word slip loads clean (I5). The meter test is non-vacuous (scrape minus allowlist must be empty, the scrape read from the real sources) and is a ctest gate (`CMakeLists.txt:62`).
23. **Stands** (recount: sweep `old` 419, `t3` 418, the one file named).
24. **Stands as history**; the restated test inherits the fix's blind spot (I1).
25. **Stands as far as reading can check**: `amplVarPct` occurs in `patches/old/Squeaker.json` only; `ks_morph_saw_test.json` is in `old.txt` and has a hash in all three arms, so it loads; its four keys are in the allowlist. The measurement document still says it stops loading (its "What stops loading after the cut" paragraph) — superseded by the report, not corrected in place.

### Issues

**I1 — Major. The DelayLine wrap wraps the index and not the position: the wrap sample reads `buf_[0] + ring_·(buf_[1] − buf_[0])`.** `delay_line_source.h:80-83`: `int i0 = int(rp); if (i0 >= ring_) i0 -= ring_; … const float frac = rp - float(i0);` — when `rp == float(ring_)` (the only way `i0 >= ring_`), `i0` becomes 0 and `frac` becomes `ring_` (1106 at 22050, 2404 at 48000). The read at `:86` is then an extrapolation thousands of times the sample-to-sample difference, once per lap, in a feedback loop. The old code read the zero slot with `frac = 0`; the correct read is `buf_[0]` with `frac = 0`. The test passes because its source is a constant (`:5273-5274`: `buf_[0] == buf_[1]`). Fix: wrap the position, not the index — `if (rp >= float(ring_)) rp -= float(ring_);` before `i0`/`frac` are taken; test with a source whose consecutive samples differ (the file's `Counter`) and assert `out[15]` equals the first write; refreeze the two baselines again and re-render `t4 → t5` over the 25 (they will all move again); the "three listed did not move" explanation ("slot 0 held zero") must be re-checked, since with the shipped read a zero slot 0 still moves whenever slot 1 is non-zero.

**I2 — Minor. The host's read-then-clear can lose a fault.** `instrument.h:225-227` loads `faults()`, then stores 0; `instrument.h:789-791` reads `faults()` twice and then clears. A `fetch_or` from the audio thread between the load and the store is wiped — exactly the UI live-poll case ruling 1 was written for. Fix: `unsigned char take_faults() { return faults_.exchange(0, std::memory_order_relaxed); }` on `ValueSource`, used by both collectors; keep `clear_faults()` for the tests.

**I3 — Minor. `prepare()`'s re-entry guard is not exception-safe.** `dsp_value_source.h:62-69`: `computing_ = true` … pins … `on_prepare` … `computing_ = false`, no unwind. `on_prepare` can throw: `MultiplexSource::on_prepare` (`multiplex_source.h:83-84`) calls `rebuild_()`, which calls the loader's builder with no `try` (`:114-127`), and the builder now throws on a bad wire; `values_.resize` can throw `bad_alloc`. After such a throw this node and every ancestor on the stack keep `computing_ == true`: `next()` serves `cur_` with `CycleWithoutTap` forever, a later `prepare()` returns at `:61` without preparing — the UI's reload-after-edit path would find the graph wedged with a misleading fault. The pre-2.5 `computing_ = false` at entry was the recovery. Fix: a scope guard (`struct Reentry { bool& f; ~Reentry() { f = false; } }`) in `prepare()` and `start()`.

**I4 — Minor. Three CLI render paths and one UI path collect nothing.** `mforce_cli/main.cpp:329` (`render_and_write`, run_melody), `:608` (run_compose), `:892, :1002` (run_dun); `mforce_ui/main.cpp:8137` (the chords render). Faults set there are never seen. Fix: `print_faults(MonoSource&)` that collects and prints (also collapsing the four duplicate blocks), called after each render; the UI's chords path writes `g_genFaults` as Generate does.

**I5 — Minor. The global allowlist's blind spot is larger than §7 says.** `patch_loader.cpp:239-271` admits, on every node, `frequency`, `source`, `gain`, `cutoff`, `curve`, `mode`, `delay`, `ratio`, `volume`, `a`, `b`, `x`. Concrete: `{"type":"WavetableSource","params":{"source":{"ref":"exc"}}}` (the pin is `inputSource`) loads clean and renders without excitation; `"frequency"` on a WhiteNoiseSource loads clean. The debt line should say that the most common pin names are on the list, not only that "a typo that spells a key some other branch reads passes". Part 4's per-type list; nothing to fix now beyond the sentence.

**I6 — Minor. Two omissions in §7.** (a) `HybridKSSource::fill_table` also allocates inside `next()` (`hybrid_ks_source.cpp:48`, from `compute_ks` `:100-102`); §7 names `WavetableSource` only — 2.6's test will find both, say so now. (b) Nodes made at bind time (the push-binding chains' CurveNodes, `patch_loader.cpp:1073-1092`) are not in `nodesById`, so their faults are collected by nobody.

**I7 — Minor. Duplicates** — item 4: the fixture (listed), the two clamp bodies (listed), the CLI's four blocks (new, unlisted; fixed by I4).

**I8 — Minor. Comments and a stale include.** `dsp_value_source.h:17, 153`: `Instrument::collect_faults` → `MonoSource::collect_faults`. `engine_tests/main.cpp:5896, 5954, 5998`: drop "Checkpoint 2.5:" from the three headers (say what the section tests, not when it landed). `dsp_wave_source.h:4`: `<stdexcept>` is dead. `dsp_value_source.h:64`: the ruling-20 comment belongs on its own line above the method, not trailing `refresh_auto_sources()`.

**I9 — Minor. Test gaps** — item 10: a test that re-prepares a faulted instrument and still collects the bit (ruling 20); a test of `StereoMixer::collect_faults` through `load_patch_file` on a mixer-mode patch with an instrument channel (the CLI's main path); `DrumKit::collect_faults`; `HybridKSSource` `TableClamped`; the array `add_param` rejection. Cheapest and most valuable: the null gate fails on any `[faults]` line in the CLI's stderr over the manifest set — the false-alarm class (a spurious `NeverPrepared`/`Clockless` on a valid patch) is otherwise checked only by hand on `oboe1`.

**I10 — Minor. Report order.** `instrument.h:224` iterates an `unordered_map`; the plan's contract said "entries in id order". The CLI block's order is hash order and not stable across STL implementations. Sort `out` by `nodeId` at the end of `collect_faults`, or state the order is unspecified.

**I11 — Minor. A design deviation needing assent.** Ruling 2 clamps the throw's condition, not the declared range; section 6 as signed says "a frequency below the minimum becomes the minimum". Record the narrowing in section 6 ("decided 2026-10-26: the clamp applies to a non-positive frequency; a positive frequency below the pin's minimum is unchanged, so no non-terminating render moves") or schedule the wider clamp as a behaviour checkpoint with its own measured list.

**I12 — Minor. Three unchecked `set_param` results.** `source_registrations.cpp:423, 430, 443` (`cutoffFreq` on types that have the pin — cannot fail today); report §3 says "the loader checks every result". Check them with `reject_wire`'s text, or amend the sentence.

No further Major issues found. The spec's two throws are gone from every render path; every production host is clocked; the loader rejects on every path a node is built (every branch at `patch_loader.cpp:534-924` assigns `valueNodes[id]`, so `:942` reaches each); the A/B counts are right; the Task 0 leftovers are gone (`tools/measure_25.py` absent, no `MFORCE_TRACE_WRAP` in the tree); `tools/test_figures` is byte-identical between the two trees.

### What I would fix in one wave

Fix now: **I1** (the wrap: position not index; a non-constant test source; refreeze the two baselines; a `t5` arm over the 25 and a fresh "did not move" explanation), **I2** (`take_faults()` via `exchange`), **I3** (a scope guard in `prepare()`/`start()`), **I4** (the four uncollected renders, which also collapses the CLI's duplicate blocks — I7c), **I8** (the four comment/include fixes), **I10** (sort the report), **I12** (check the three configurator results), and from **I9** the null gate's `[faults]` stderr check plus the ruling-20 test — each is small, and I1 is the one wrong behaviour in the checkpoint; the rest are the contract's own edges. **I6** and **I5** are sentences in §7: write them now.

Debt, listed for a named Part: **I5**'s per-type allowlist (Part 4's codec); the two clamp twins (Part 3's node pass); the loader voice-loop pair (Part 4); the bind-time nodes outside `nodesById` (I6b, Part 4 gives them ids); **I11** is Matt's call on the design text, not a code fix.

### What was done about each

- **I1 (Major) — fixed, ruling 26.** The position wraps, not the index. The
  restated test with the `Counter` source went RED on the shipped wrap exactly
  as traced (`out[15]` = 1107 against 1; the second lap 2213 against 1107) and
  GREEN with the fix. The two baselines were refrozen again (exactly two
  manifest entries changed), and a `t5` arm was rendered over the eight sets:
  against `t4` the same 25 differ, against `old` the same 25, nothing else
  moved (Appendix A); the three listed non-movers were re-checked and are
  unchanged under both reads (section 5).
- **I2 — fixed, ruling 27.** `take_faults()` (one `exchange`) in both
  collectors.
- **I3 — fixed, ruling 28.** `ComputingScope` in `prepare()` and `start()`;
  pinned by a node whose first `on_prepare` throws.
- **I4 — fixed, ruling 29.** run_melody's and run_compose's mixes, run_dun's
  two renders and the UI's chords render report; one `report_faults(host)`.
- **I5 — the sentence rewritten** (section 7): the list holds the commonest
  pin names, with two concrete cases.
- **I6 — written** (section 7): both `fill_table`s; the bind-time nodes
  outside `nodesById`.
- **I7 — (a) collapsed** into `tapless_cycle(h)`; **(b) listed**, Part 3's;
  **(c) collapsed** by I4 (section 4).
- **I8 — fixed, ruling 33:** the two `MonoSource` comments, the three test
  headers, the dead include, the ruling-20 comment on its own lines.
- **I9 — partly:** the null gate reads the CLI's stderr (ruling 32; clean on
  every manifest patch), and a prepare-after-render test pins ruling 20. Not
  written, listed as test debt in section 7: `StereoMixer::collect_faults`
  through `load_patch_file`, `DrumKit::collect_faults`, `HybridKSSource`'s
  `TableClamped`, the array `add_param` rejection.
- **I10 — fixed, ruling 30.** Sorted by node id; pinned with two faulting nodes
  declared in reverse order.
- **I11 — Matt's:** REVIEW R6 item (4); ruling 2 records the narrowing.
- **I12 — fixed, ruling 31.** The three alias configurators throw on `false`;
  the loader rethrows a configurator's `runtime_error` with the node id and
  type.
- **Item 1's flag** (two homes for the fault vocabulary) — listed, section 7,
  Part 5's. **Item 9's `kStructural`** — renamed `kBranchKeys` (ruling 33).

### Re-review

# Re-review R6: the checkpoint 2.5 fix wave (fbfffe6..5a618cf)

I read `git diff fbfffe6 5a618cf -- engine tools` in full, the files it touches at HEAD (5a618cf; `git diff 5a618cf HEAD -- engine tools` is empty), report section 8 "What was done about each", rulings 26–33, the Ledger's "Task 5, the fix wave" entry, and the updated Appendix A; I recounted all eight A/B sets including the new `t5` arm with a read-and-print script of my own, and traced the restated DelayLine test's values by hand. Nothing was built, rendered or run; no engine binary, test or meter was executed, so the Ledger's gate results, meter numbers and timing are its claims, not mine.

### Issues, closed or not

- **I1 — CLOSED.** `delay_line_source.h:82-85`: `rp` wraps before `i0` and `frac` are taken, so on the wrap sample `rp = 0`, `i0 = 0`, `frac = 0`, read = `buf_[0]`. Traced by hand at 22050 Hz (`ring_ = 1106`, `len = 15.00000095f`, source `Counter` 1, 2, 3, …; `render_samples` at `:4867-4872` only pulls, no restart): pull 15 writes 16 at slot 15, `rp = -9.5e-7 + 1106 → 1106.0f → 0.0f`, out = `buf_[0]` = 1 ✓; pull 14 reads `1104.99999905f → 1105.0f`, an unwritten slot, 0 ✓; pull 16 reads `rp = 0.99999905`, out = `1·9.5e-7 + 2·0.99999905 ≈ 2.0` ✓; second lap, pull 1121 (write index 15) reads slot 0, written at pull 1106 with 1107 = `ring+1` ✓, pull 1122 ≈ 1108 ✓. Under the shipped (index-only) wrap the same inputs give 1107 and 2213, exactly the RED the Ledger reports. A/B recount: `t4` vs `t5` 25 differ, `old` vs `t5` the same 25, `t3` vs `t4` the same 25, nothing else in any set; `stk_bandedwg_bowl_struck` the one file in `old` absent from `t5`; the two `nlbore` patches identical across `t3`/`t4`/`t5`. `tools/null_gate_manifest.json`: exactly the two `double_advance_*` entries changed since fbfffe6.
- **I2 — CLOSED.** `dsp_value_source.h:159` `take_faults()` is one `exchange(0)`; `instrument.h:228` (PitchedInstrument) and `:794` (DrumKit) use it; the DrumKit's double read is gone (`:792-795`). `faults()`/`clear_faults()` remain for tests.
- **I3 — CLOSED.** `dsp_value_source.h:62` and `:84`: `const ComputingScope scope{computing_}` after the early returns (`:60-61`, `:81-83`), so a re-entered call faults and returns without touching the outer mark; the destructor (`:230-236`) clears on every exit. `next()`/`step()` untouched (plain stores, `:114/:117`), the same clear point as before on the normal path; nothing else in the class reads `computing_`. Pinned non-vacuously: `ThrowingPrepare` (`engine_tests/main.cpp:5985-6002`) — under the old code the second `prepare()` would have faulted and returned with `prepares == 0`.
- **I4 — CLOSED.** `mforce_cli/main.cpp:29-39` `report_faults(Host&)`; eight render sites, eight reports: `:109/147`, `:190/221`, `:329/335` (per instrument), `:608/615` (per instrument), `:728/760`, `:786/826` (the mixer), `:892/900`, `:1002/1011` (`test_ornaments`, called from `main` at `:1243` — the Ledger's "run_dun x2" is run_dun plus this). No path reports the same host twice; `print_faults` is gone. UI: `render_chords_waveforms` collects at `mforce_ui/main.cpp:8138`; with `:4420` and `:8614` every UI instrument render reports; the UI's stream (mixer mode) remains uncollected by ruling.
- **I5 — CLOSED as a sentence** (section 7); the global list is unchanged by design, Part 4's.
- **I6 — CLOSED as sentences** (section 7: both `fill_table`s, the bind-time nodes).
- **I7 — CLOSED.** `tapless_cycle(TestHost&)` at `:2172-2180`, used at `:2217` and `:5966`; the CLI's four blocks are the template; the clamp twins stay listed.
- **I8 — CLOSED**, one cosmetic residue: `:17` and `:153` say `MonoSource::collect_faults`; `dsp_wave_source.h` drops `<stdexcept>`; the ruling-20 comment stands on its own lines (`:65-66`); the three headers lost "Checkpoint 2.5:" but two now begin lowercase (`:5961` "the fault bits…", `:6029` "a rejected wire…").
- **I9 — PARTLY (as declared).** `null_gate_perform_source.py:36-41` returns `FAULTS:<line>` on any `[faults]` stderr line; `engine_tests/main.cpp:6017` re-prepares after the render and `:6019-6021` still collect two faults (ruling 20 pinned). Still unwritten and listed as debt: `StereoMixer::collect_faults` through `load_patch_file`, `DrumKit::collect_faults`, `HybridKSSource` `TableClamped`, the array `add_param` rejection.
- **I10 — CLOSED, weakly pinned.** `instrument.h:225, 235-236` sorts the appended range by `nodeId`; `find_if` from `first` keeps a second host's same-id entry separate (as before). The test (`:6008-6021`, nodes `b` then `a`) asserts `a` before `b`; whether it would fail without the sort depends on the `unordered_map`'s order for those two keys on MSVC — the contract is pinned, the sort's removal is not reliably detected (N3).
- **I11 — NOT CLOSED, by design: Matt's call.** REVIEW R6 item (4); the code still clamps `f <= 0` only (`dsp_wave_source.h:28`).
- **I12 — CLOSED.** `source_registrations.cpp:422-423, 429-430, 442-443` throw on a false `set_param`; `patch_loader.cpp:930-933` rethrows a configurator's `runtime_error` as `node '<id>' (<type>): <what>`. No test or Python tool matches on a configurator's or `resolveFn`'s text (grep over `tools/*.py`, `engine_tests`, `mforce_ui`, `mforce_cli`: only the loader's own `reject_*` texts are matched, at `:6032-6050`, and those are unchanged); `type` here is the registry name, which equals `type_name()` for registry types, so the prefix reads like `reject_wire`'s.
- **Item 9 flag — CLOSED.** `kBranchKeys` at `patch_loader.cpp:239, 267` and `test_structure.py:138`.
- **Items 1 (two homes), 4b (clamp twins), 4d (loader pair) — listed as debt**, Parts 5, 3 and 4 respectively.

### New findings

**N1 — Minor. `--freeze` would pin a `FAULTS:`/`RENDER_FAIL:` string as a manifest value.** `null_gate_perform_source.py:66-73` writes whatever `render_hash` returned; a baseline that faults (or fails) at freeze time is frozen with that string and passes every later run that reproduces it. Change: refuse to freeze when any value is not a 64-hex-character hash, naming the patch. Also the `FAULTS:` return skips `out.unlink()` (`:40-41`); the next run reaps the file at `:29-30`, so hygiene only.

**N2 — Minor. The collect block the CLI shed now has three copies in the UI.** `mforce_ui/main.cpp:4420`, `:8138`, `:8612-8615` — the same collect-then-format shape with three sinks (`g_genFaults`, the status line twice). Below the meter's threshold; a `transport_report_faults(Instrument&)` returning the text would collapse two. Part 7's file either way.

**N3 — Minor. The id-order test is a weak pin.** `engine_tests/main.cpp:6008-6021`: with two keys, the `unordered_map` walk may already yield `a` before `b` on MSVC, in which case removing the sort does not fail the test. No fix available by reading; say in the report that the test pins the contract, not the mechanism, or add a third node and assert `std::is_sorted` over the three (still hash-dependent, less likely to pass by luck).

**N4 — Minor. The configurator rethrow prefixes `std::runtime_error` only** (`patch_loader.cpp:931`). A `nlohmann::json` type error inside a configurator (a `"sections": "two"`) or a `std::invalid_argument` still surfaces without the node id — pre-existing wording, and not a regression; noted so ruling 31's "a configurator's refusal … reads like `reject_wire`'s" is read as "a runtime_error does".

**N5 — Observation, no change asked.** After a throwing `on_prepare`, `ctx_` is already assigned (`dsp_value_source.h:63` precedes `:70`), so `prepared()` is true on a node whose `on_prepare` never finished; pre-existing since 2.2, and the host contract (a thrown `prepare` means the graph is not used) covers it. The `ThrowingPrepare` test shows the node recovers on the next `prepare()`, which is the property I3 asked for.

Unverified by me, stated as the Ledger's claims: engine_tests 6486 pass, ctest/stamp/null gate green with no `[faults]` on any manifest patch, the meter's +392 / 157→159 decomposition, and the post-wave timing (the table is pre-wave; the wave's one sample-path change is the float compare at `delay_line_source.h:82`, whose `float(ring_)` the compiler shares with `:78`).

### Standing

With the wave, the checkpoint meets Part 2 section 6 as signed on every clause I can check by reading: no throw and no I/O on `start()`/`next()`/`step()` (the two throws are clamps with bits; the cycle print is a bit; the only throws left in core/source/filter are `prepare`'s zero-rate and the load-time readers), a per-node lock-free bit written on the fault path and taken by the host in one exchange, every CLI and UI instrument render reporting in id order, a rejected wire or unknown key failing the load with node, pin and reason on every build path, and the DelayLine reading its ring correctly with the two baselines refrozen on the right value. Matt's call: I11 (ruling 2 clamps the throw's condition, not the signed "below the minimum"), and the plan's four verdict items. Debt, listed: the test gaps of I9, mixer-mode graphs uncollected, the two-homes split, the global allowlist's common-name blind spot, both `fill_table` allocations (2.6's gate), the bind-time nodes outside `nodesById`, N1's freeze guard, and the post-wave timing not yet measured to the clean run's standard.

### After the re-review

A second, smaller pass (commit 1481d15), not re-reviewed: **N1** — the
null gate refuses to freeze when any value is not a 64-hex hash, naming the
patch, and reaps the WAV on the faults path; **N4** — the loader's rethrow
catches `std::exception`, so a JSON type error inside a configurator names
its node too (ruling 31 now reads as written); **I8's residue** — the two
headers capitalised. **N2** is listed (section 7, Part 7's). **N3** stands as
stated: the id-order test pins the contract (two ids declared in reverse
order come out sorted), not the sort's presence; on an STL whose walk
happens to yield them in order the test would not catch the sort's removal.
**N5** is noted in section 7. Gates after the pass: ctest, stamp, null gate
78/78 (section 5's Ledger entry has the detail).

## 9. Reading guide

Ten minutes: `dsp_value_source.h` — the `Fault` enum, then `prepare()`,
`start()`, `next()`, `step()`, the four fault methods and `ComputingScope` at
the bottom; `mixer.h`'s top
(`FaultReport`, `fault_report_text`, `MonoSource`); `instrument.h`'s
`PitchedInstrument::prepare` and `collect_faults`; `patch_loader.cpp`'s
`reject_wire`, `reject_unknown_keys` and the per-node call at the end of the
node loop; then the four test sections (`run_no_throw_tests`,
`run_fault_tests`, `run_wire_rejection_tests`, `run_delay_line_wrap_tests`)
and the measurement document's three lists.

## 10. C++ lessons

### Lesson 1: why a bit replaces a throw on the audio thread

In Java or C# an exception that nobody catches on a worker thread is logged,
the thread dies and the process goes on; a `try/catch` around the callback
would have been the reflex. In C++ an exception with no handler calls
`std::terminate`, which ends the process, and the audio callback runs on a
thread the operating system's audio service owns — there is no frame above
it that could catch. So the rule is absolute: nothing on the render path
throws. But the information the throw carried ("this frequency is
impossible") must not vanish, so the node keeps it as a bit and the control
side asks after the render. The shape is the same as `errno` or a hardware
status register: the fast path records, the slow path reads. What makes it
honest is that the fast path does not merely swallow the error: it clamps to
a stated value (ruling 2), continues deterministically, and the host prints
what happened and where.

### Lesson 2: a one-byte mailbox with `memory_order_relaxed`

The fault byte is written by the audio thread and read by the UI thread. A
C# reader knows the "tearing" story — a 64-bit write can be seen half-done —
and might reason that a byte cannot tear and so needs no protection. In
C++ that reasoning is wrong: a write and a read of the same non-atomic
object on two threads without synchronisation is a data race, and a data
race is undefined behaviour regardless of the object's size; the compiler
may assume it does not happen and optimise accordingly. `std::atomic<unsigned
char>` fixes that at the smallest possible cost: with
`memory_order_relaxed` there is no fence and no lock, only the guarantee that
the operation is indivisible and that both threads see a value that was
written. Relaxed is enough because the byte carries no message about other
memory — the host reads "something faulted on osc1", not "and therefore
buffer X is ready". `fetch_or` lets several faults accumulate from one
writer without a read-modify-write race, and the real-time rule is kept: a
lock-free atomic on x86 and ARM is a plain instruction, never a lock.

The reader's side has its own trap, and the review caught it: the first
landing collected with a `load` followed by a `store(0)`. Two atomic
operations are each indivisible, but the pair is not — a `fetch_or` from the
audio thread can land between them and is wiped by the store, a lost update.
`exchange(0)` returns the old value and writes the new one as one atomic
operation, which is what "read and clear" has to mean when another thread is
writing; `take_faults()` is that call. The C# analogue is
`Interlocked.Exchange`, and the lesson is the same there: an atomic read
followed by an atomic write is not an atomic read-modify-write.

### Lesson 3 (short): a `bool` at a boundary, an exception behind it

`set_param` returns `false`; the loader throws. Why not throw from the node?
Because the node does not know its id, and because a node must be usable
from a host that would rather carry on (the UI, wiring from descriptors,
cannot produce `false` at all). The pattern — a predicate at the leaf, a
decision at the layer that has the context — is the same as `TryParse`
beside `Parse` in C#, and the choice of which layer throws is the same
choice: the one that can say something useful in the message. The loader
can say `node 'osc' (SineSource): unknown key 'frequncy'`; the node could
only have said "no".

### Lesson 4 (short): a scope guard — the destructor is the `finally`

`prepare()` marks the node as busy, calls `on_prepare`, and unmarks it. In
C# or Java the unmark would sit in a `finally` so that a throw out of
`on_prepare` cannot skip it. C++ has no `finally`; it has destructors, which
run on every exit from a scope — a return, a fall-through, or an exception
unwinding through it. `ComputingScope` is a two-line struct whose constructor
sets the flag and whose destructor clears it; declaring one as a local
(`const ComputingScope scope{computing_};`) makes the unmark unskippable. The
idiom is RAII — "resource acquisition is initialisation" — and it is how C++
closes files, releases locks (`std::lock_guard`) and, here, clears a mark. The
first landing wrote the two stores by hand and the review found the path
between them that a throw would take (I3).

## Appendix A: the A/B over the sample

| set | in the set | rendered by both | differ (t4 vs t3) | differ (t5 vs t4) | differ (t5 vs old) | stopped loading (t3) |
|---|---|---|---|---|---|---|
| patches/library | 80 | 80 | 0 | 0 | 0 | 0 |
| patches/baselines | 52 | 52 | 2 | 2 | 2 | 0 |
| patches/pending | 43 | 40 | 1 | 1 | 1 | 0 |
| patches/audition | 708 | 708 | 14 | 14 | 14 | 0 |
| patches/sweep, every third | 457 | 418 | 8 | 8 | 8 | 1 |
| patches/old, every third, plus the retired pair | 275 | 159 | 0 | 0 | 0 | 0 |
| `taps` (2.4's 47, in full) | 47 | 47 | 7 | 7 | 7 | 0 |
| `retired` (the `loop_root_sensitivity` pair) | 2 | 2 | 0 | 0 | 0 | 0 |

The pre-2.5 arms are checkpoint 2.4's sealed arms (the engine was unchanged
between the two seals); `t3` is the Task 3 engine (`cli_25_t3.exe`), `t4` the
Task 4 engine (`cli_25_t4.exe`), `t5` the fix-wave engine (`5a618cf`, kept as
`renders/scratch/ab_tree/cli_25_t5.exe` from the seal's build, `1481d15`,
whose engine differs by one catch clause on a load path); the set files are
`renders/scratch/ab25_tree/*.txt`, the hashes under
`renders/scratch/ab25_tree/<set>/{old,t3,t4}/hashes.txt`. Tasks 1 and 2 were
proven on the null gate (78/78 after each) and not re-rendered over the
sample; the `old` against `t3` compare, 0 differ everywhere, carries them. A
patch missing from "rendered by both" failed to render in both binaries
(the 157 that do not load since checkpoint 2.2), except
`stk_bandedwg_bowl_struck`, which rendered in `old` and fails to load from
`t3` on. Twenty-five distinct patches differ between `t3` and `t4` (the
`taps` set overlaps pending and sweep); every one is on the wrap list. The
same twenty-five differ between `t4` and `t5` — the fix wave's corrected read
moved every patch the wrong read had moved — and the same twenty-five between
`old` and `t5`: the checkpoint's whole effect on the sample's renders is the
wrap list, nothing else.
