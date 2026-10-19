# Architecture report: checkpoint 2.3, the per-tick memo

Commits `22bf3be` through `fdf7536` (7 commits on main, 2026-10-18), the
third checkpoint of Part 2 (the ValueSource contract,
`docs/architecture/part2-valuesource-contract.md` section 2). Report R4 in
the format R1 established; the independent review is section 8. The plan it
executed is `docs/superpowers/plans/2026-10-18-part2-checkpoint-2.3.md`;
that file's Ledger holds every ruling, dated by task.

## 1. Summary

A node computes once per tick and serves that value to every later pull in
the same tick. Until this checkpoint, sharing a source between two pins
needed machinery: the loader counted how many pins referenced a node, gave
the first one the node and every later one a `RefSource` that read the
node's last value without advancing it; a second pass promoted "starved"
references whose advancer fell outside the render cone; the UI mirrored the
rule in its own wiring pass; and which consumer the file listed first decided
what the others read. Now:

```cpp
float ValueSource::next();   // this tick's value: computed once, then served to every later pull
float ValueSource::step();   // one sample now, whatever the tick, through every Auto pin
```

The tick is the host's clock, reached through the context: `RenderContext`
carries a `SampleClock*` next to the rate, and every pull loop — a voice, a
drum source, a mixer, a loader adapter, the UI's editor stream — owns one
and ticks it once per sample before its pulls. `RefSource` wrapping, the
usage counter, both starved-ref promotion walks, the UI's mirror and the
sharing trace are gone (170 lines from the loader alone). The tap edge is
`TapSource`, the guarded read of its source's memo, and reads A (positional)
exactly as before. ShaperSource's pins are Auto again: the read-timing
distinction checkpoint 2.1 kept them Manual for no longer exists.

Renders changed in two measured classes and nowhere else (section 5 and
Appendix A): a wrapped consumer of a shared STATEFUL node that read it a
sample late reads this sample now (`double_advance_bug`, `double_advance_ok`,
and 106 gitignored audition and sweep patches); and one patch whose
output sat mid-loop, so its tail pull advanced the root cone a second time
per sample (`loop_root_sensitivity_fires`, which no longer fires and now
matches its `_dead` sibling). No library patch changed; no pending patch
changed. The null gate was refrozen for the three baselines.

Render time: the checkpoint is neutral against the tree before it (piano
1.00x, viola 0.99x; oboe1 0.88x; the chain that shares one envelope 1.12x),
and the sweep experiment of Task 5 bought 8 to 16 points wherever pins are
wired. The cost carried from checkpoint 2.1 is not recovered — oboe1 at
0.73x and piano_default at 0.88x of the pre-2.1 binary — and section 5
hands the design question to REVIEW R4 item 0.

## 2. Types and files

```
NEW      SampleClock  (engine/include/mforce/core/render_context.h)
  Owns: one Tick, `now`, and `tick()`.
  Used by: every driver loop (VoiceGraph, StreamingVoice, DrumSource,
    StereoMixer, the loader's two adapters, the UI's editor stream, a test
    host); read by every node through its context.
  Depends on: nothing.
  Design choice: a value type the driver owns and the nodes observe through
    a pointer in RenderContext. The driver's time is a host fact, like the
    rate, and reaches a node the same way.
  Alternative: next(Tick t) threaded down the pull (the spec's sketch).
    Rejected: every pull signature changes, a pin would need an owner
    pointer to find the tick, and compute() would gain a parameter no node
    uses. Alternative: a thread-local tick bumped by the root pull.
    Rejected: a tail pulled after the root would start a new tick, and a
    test's top-level pull is indistinguishable from a driver's.
  Name: "clock" because it only counts; "tick" is the verb the audio
    callback already used for the perform clock.
  Read: render_context.h whole.

CHANGED  RenderContext  (engine/include/mforce/core/render_context.h)
  Owns: the rate and the clock pointer; a null clock means "no host clock,
    every pull computes".
  Design choice: the host's facts stay one struct; the loader makes it with
    the rate alone and the driver completes it with its clock before it
    prepares its graph.

CHANGED  ValueSource  (engine/include/mforce/core/dsp_value_source.h)
  Owns: the memo (`lastTick_`, `cur_`), the re-entry guard (`computing_`,
    `cycleReported_`), next() and step().
  Design choice: next() checks the guard, then the clock, then sweeps and
    computes; a node re-entered during its own compute serves the previous
    value and reports once (a tap-free cycle, which no file can express).
    step() is the unconditional advance for a node that drives a source at
    its own rate: the two table fills (WavetableSource, HybridKSSource) draw
    N samples in one tick.
  Alternative: no step(); the table fills tick a clock of their own.
    Rejected: a node cannot own time; it would be a second clock inside a
    voice. Alternative: a plain memo with no escape. Rejected: every
    wavetable patch's table would be flat.
  Name: "next" stays (it was the pull verb); "step" because it moves one
    sample whatever the clock says.
  Read: next() and step() near the top of the file, the pins' bodies at its end.

NEW      TapSource  (engine/include/mforce/core/dsp_value_source.h), was RefSource
  Owns: a shared_ptr to its source; read_() guards (non-finite to 0, ±8).
  Used by: the loader's tap branch (TapBind), the UI's tap wire, DelayLine's
    loop walk, the UI's envelope walk.
  Design choice: a ValueSource with no pins, so a consumer's pin can hold
    it; compute() and current() both read the source's memo live and never
    pull; the positional one-sample delay (reading A) falls out of the pull
    order, as feedback_loop_design.md says.
  Alternative: reading B (each node keeps last tick's value; a tap always
    reads it). Deferred to checkpoint 2.4 by the spec, with 47 patches
    measured as changing.
  Name: it is the tap edge and nothing else; "Ref" named a thing that no
    longer exists.

CHANGED  VoiceGraph, StreamingVoice, DrumKit::DrumSource  (engine/include/mforce/render/instrument.h)
  Owns: a SampleClock each (the voice's heap-owned so a StreamingVoice can
    hold it after the pool reallocates; a drum's inline) and the completed
    RenderContext; tick() = the clock, then the perform clock.
  Design choice: PitchedInstrument::prepare completes each voice's context
    before preparing the voice; the per-note envelopes (bend, glide) are
    prepared with the voice's context so they share its clock.

CHANGED  StereoMixer  (engine/include/mforce/render/mixer.h, engine/src/mixer.cpp)
  Owns: a clock for its gain, volume and pan pulls, ticked once per sample
    per channel in the mix loop (today's advance counts preserved); a
    channel's source completes the host's facts with a clock of its own.

CHANGED  WaveSourceMono, ValueSourceMono  (engine/src/patch_loader.cpp)
  Owns: a clock each: an adapter is its own pull loop.

CHANGED  resolve_param, resolve_param_or, wire_params_generic  (engine/src/patch_loader.cpp)
  Owns: resolution only. The usage counter, the wrapping branch and both
    promote_starved_refs walks are gone; collect_advance_ids stays, decided
    from the file as before, with its comment rewritten: a tail's forward
    inputs, if the root pull computed them this tick, are served from the
    memo.

CHANGED  ShaperSource  (engine/include/mforce/source/shaper_source.h)
  Pins Auto; compute() reads after the base's sweep.

CHANGED  update_all_dsp, the voice struct, the editor stream  (tools/mforce_ui/main.cpp)
  One wiring pass; one g_editorClock in g_renderContext, ticked once per
  sample by the callback's stream section and the headless harness; the
  live voices tick their own.

DELETED  SharingTrace, ValueSource::advance(), tools/measure_sharing.py
  The measurement instrumentation went with the memo; its two documents
  stay (docs/architecture/measurements/).

NEW      tools/predict_double_pull.py
  Finds, from a patch's file alone, a tap-only tail whose forward inputs
  walk back into the root cone. It models the pre-2.3 loader on purpose:
  it is the record of how the one such patch was found (section 5).
```

## 3. Interfaces changed

| Before | After | Callers touched |
|---|---|---|
| `ValueSource::advance()` (the traced pull) + `next()` | `next()` (the memo) + `step()` | every driver (instrument 3 loops, mixer, 2 adapters, UI 5 loops, explore), the pins |
| `Param::advance()`, `Input::advance()` | `Param::next()/step()`, `Input::next()/step()` | 53 Manual pulls in 16 node files; 2 become `step()` (the table fills) |
| `RenderContext{sampleRate}` | `RenderContext{sampleRate, clock}` | every driver completes it; the loader's three creations unchanged |
| `RefSource(src, guard)` | `TapSource(src)` | loader (TapBind), UI (tap wire, envelope walk), DelayLine walk, tests |
| `resolve_param(val, valueNodes, usage*)`, `resolve_param_or(…, usage*)`, `wire_params_generic(…, usage*)` | the same without the counter | 21 call sites |
| `promote_starved_refs` (two overloads) | gone | 6 call sites |
| `StreamingVoice` | gains `clock` and `tick()` | UI (3 sites), explore |
| `make_glide(from, to, dur)` | `make_glide(const VoiceGraph&, from, to, dur)` | deliver_continuation |
| ShaperSource: `Processor(Pull::Manual)`, six Manual Params | Auto | none (the base sweeps) |

## 4. Duplicates collapsed

- The UI's mirror of the loader's wrap rule (`update_all_dsp`'s second pass,
  52 lines) and the loader's rule itself are gone together; there is no
  rule to mirror.
- The two JSON walks of `promote_starved_refs` (the advancer map and the
  reachability walk, 110 lines) duplicated `collect_advance_ids`'s edge
  enumeration; only the latter remains.
- The per-sample `++SharingTrace::sample` at four driver loops and the
  perform tick at three are one `tick()` per driver type.

## 5. Metrics

`python tools/structure/check.py --delta 587b539` at the seal (head
`fdf7536`):

| Measure | Before | After | Change |
|---|---|---|---|
| files | 167 | 167 | +0 |
| lines | 59,012 | 59,011 | -1 |
| files_over_limit | 20 | 20 | +0 |
| functions_over_limit | 99 | 99 | +0 |
| file_level_mutable_vars | 113 | 114 | +1 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 56 | 57 | +1 |
| changelog_comments | 254 | 252 | -2 |
| duplicate_blocks | 154 | 156 | +2 |
| include_violations | 13 | 13 | +0 |
| unmapped_files | 0 | 0 | +0 |

The loader lost 170 lines and the shaper 27; the test file gained 128 and
the core headers 73 (the clock, the memo, `TapSource`, the sweep cache).
The one new file-level mutable is the UI's `g_editorClock` (section 7). The
`dynamic_cast` count moved by one: the two new casts are in the test file
(a `PitchedInstrument*` from an `InstrumentPatch`, twice), one left with
the promotion walk; the two in the engine (DelayLine's loop walk, the UI's
envelope walk) are retypes of casts that existed. The two new duplicate
blocks are lizard's view of the two loader adapters, which were already
twins and each gained the same clock, context and `prepare`.

The renders that changed. Two classes, both measured before the cut and
confirmed by the whole-tree A/B (Appendix A):

1. **A wrapped consumer of a shared stateful node read it a sample late**
   (`docs/architecture/measurements/2026-10-18-sharing-lag-tree.md`, the
   lag list, 342 patches). The list proved an upper bound (ruling 16): the
   trace counted every wrapper read before the advancer, but since
   checkpoint 2.1 a node reads its pins after its sweep, through a live
   `current()`, so a consumer whose advancer sat later in the same node's
   sweep already used this sample's value. The renders that actually move
   are those where the shared node is stateful and the wrapped consumer's
   whole evaluation preceded the advancer's: on the manifest
   `double_advance_bug` and `double_advance_ok`; across the tree
   106 gitignored audition and sweep patches — the `feedback_valve2`
   queue (a DelayLine and a CombinedSource shared; the ShaperSource
   read-timing set of report R2), `string_harness1`/`2` (an SVFSource),
   `bwg_perc1`/`2` (a Biquad, a VarSource, a CurveNode), and single patches
   under `stk_bowed_ext1`/`2`, `stk_bandedwg_port`, `stk_saxofony_port`
   and `bassoon_ref1` (a BowTable, an Envelope, a Biquad, a CombinedSource,
   a DelayLine). None in library; none in pending.
2. **A tap-only tail whose forward inputs walked back into the root cone**
   (ruling 15; `tools/predict_double_pull.py`): one patch in 1,615,
   `loop_root_sensitivity_fires`. Its output was taken mid-loop at
   `Junction`, so the tail `Delay_line`'s pull of `Damp_lpf` and
   `DC_Block_hpf` reached `Junction`, `Valve`, `Drive_inputs`, `WhiteNoise`,
   `Drive_vib`, `Drive_env`, `Wormhole` and `Wormhole2` a second time each
   sample. Rendered before: peak 0.146, rms 0.078 — the loop fires. After:
   peak 0.00012, rms 0.000026 — it does not, and now matches
   `loop_root_sensitivity_dead` (peak 0.00009, unchanged). The "root
   sensitivity" the pair was written to show was the double advance.

The Task 1 arm (the memo with the wrapping still in place) differed on the
double-pull patch alone over library and baselines; the Task 3, 4 and 5
arms differ from the Task 2 arm on nothing (the `TapSource` rename, the
Shaper pins and the sweep cache are byte-identical over the tree).

Render time (`python tools/ab_render_time.py <base> <new> 7`: the same
patch rendered by both binaries alternately, minimum wall time of 7 runs;
"speedup" = base / new, so 0.80x means the new binary takes 25 percent
longer). The three library patches and the micro set kept from report R2
(`renders/scratch/ab_tree/micro`; D fails to load in every binary and is
omitted).

The checkpoint against the binary before it (`cli_pre23.exe`, the tree at
`587b539`):

| patch | before (min of 7) | after Task 4 (min of 7) | speedup |
|---|---|---|---|
| library/winds/oboe1 | 743 ms | 846 ms | 0.88x |
| library/keys/acoustic_piano/piano_default | 1052 ms | 1053 ms | 1.00x |
| library/strings/viola_default | 1200 ms | 1208 ms | 0.99x |
| A, one unwired sine | 426 ms | 431 ms | 0.99x |
| B, a sine with an envelope | 448 ms | 459 ms | 0.98x |
| C, eight lowpass filters | 737 ms | 753 ms | 0.98x |
| E, a shared envelope read through a wrapper | 509 ms | 490 ms | 1.04x |
| F, forty unwired sines | 3123 ms | 3315 ms | 0.94x |
| G, forty sines each with an envelope | 4251 ms | 4395 ms | 0.97x |
| H, forty sines sharing one envelope | 3549 ms | 3155 ms | 1.12x |
| I, forty envelopes | 1808 ms | 1946 ms | 0.93x |

Neutral to slightly slower on chains of unshared cheap nodes (the memo's
compare and the guard are a few loads per node per sample), faster where a
source is shared (H: the forty wrapper reads are gone). oboe1's 12 percent
is the one outlier and is the shaper's six pins swept by the base in place
of its own pulls plus the memo's per-node cost on a 21-node loop.

The checkpoint against the binary before checkpoint 2.1 (`cli_1b4fe1b.exe`,
the cost report R2 item 0 carried here):

| patch | pre-2.1 (min of 7) | after Task 4 (min of 7) | speedup |
|---|---|---|---|
| library/winds/oboe1 | 579 ms | 839 ms | 0.69x |
| library/keys/acoustic_piano/piano_default | 838 ms | 1061 ms | 0.79x |
| library/strings/viola_default | 1180 ms | 1206 ms | 0.98x |
| A | 407 ms | 438 ms | 0.93x |
| B | 430 ms | 472 ms | 0.91x |
| C | 756 ms | 767 ms | 0.99x |
| E | 475 ms | 497 ms | 0.95x |
| F | 1990 ms | 3211 ms | 0.62x |
| G | 2424 ms | 4305 ms | 0.56x |
| H | 2285 ms | 3097 ms | 0.74x |
| I | 1057 ms | 1896 ms | 0.56x |

Not recovered: the rule in the plan (within 5 percent of pre-2.1 on the
three library patches) is not met, so the one bounded experiment ran:

The experiment: the sweep reads a compacted array of raw pointers to the
wired Auto pins' sources (`Component::autoRaw_`), refreshed by every door
that changes a pin's source — the pin's own setter (a pin now knows its
owner), `prepare()`, `start()` and a node assigning itself — instead of
walking pointers to every Auto pin's `shared_ptr` member and testing each
for null. The Task 4 build against the experiment:

| patch | Task 4 (min of 7) | experiment (min of 7) | speedup |
|---|---|---|---|
| library/winds/oboe1 | 877 ms | 814 ms | 1.08x |
| library/keys/acoustic_piano/piano_default | 1122 ms | 1021 ms | 1.10x |
| library/strings/viola_default | 1245 ms | 1209 ms | 1.03x |
| A | 453 ms | 434 ms | 1.04x |
| B | 464 ms | 467 ms | 0.99x |
| C | 819 ms | 766 ms | 1.07x |
| E | 519 ms | 481 ms | 1.08x |
| F | 3582 ms | 3540 ms | 1.01x |
| G | 4589 ms | 4116 ms | 1.11x |
| H | 3130 ms | 3271 ms | 0.96x |
| I | 1983 ms | 1708 ms | 1.16x |

Byte-identical (null gate 80/80, whole tree at the seal). The gain lands
where pins are wired (one indirection per wired pin per sample, and no
null test anywhere in the loop): piano and the envelope chains 10 to 16
points; nothing on the chains of unwired sines (F, H, within noise). Kept
(ruling 10). Against pre-2.1 the three library patches now stand at:

| patch | pre-2.1 | sealed | speedup |
|---|---|---|---|
| library/winds/oboe1 | 616 ms | 841 ms | 0.73x |
| library/keys/acoustic_piano/piano_default | 888 ms | 1007 ms | 0.88x |
| library/strings/viola_default | 1194 ms | 1235 ms | 0.97x |

The rest of the 2.1 cost is the generic per-node `next()` itself — a call
layer, a loop and a store per node where the hand-written bodies loaded
their pins at fixed offsets — and that is a design question, REVIEW R4
item 0: accept it as the price of one `next()` for every node, or let the
hot leaf types (the oscillators, the filters) pull their own pins inside
`compute()` through Manual pins, decided per family in Part 3's node pass.

Tests: engine_tests 6,370 checks before, 6,403 after. New: the memo (one
compute per tick however many pulls; order independence; step() now and
recursively; no clock computes every pull; a tap-free cycle is finite), the
loader-level guarantee (a source wired to two pins is the same object on
both; a consumer listed before the file's old advancer renders as if the
envelope were duplicated), the square through sharing, the shaper's
same-tick reads.

Gates: `python tools/gates.py` (all) at the seal: CTest (engine_tests 6,403
checks, test_figures, the meter and its unit tests) pass; the UI stamp
passes; the null gate reads 80/80 on the refrozen manifest and the wiring
null gate 80/80 (28 patches carried a paramMap); the two roundtrip gates
fail on exactly the eight backlog-79 patches recorded before the checkpoint
(`double_advance_bug`, `double_advance_ok`, the four `feedback/loop_*`
repros, `perform/wiring_setting`, `perform/wiring_smoke`): 8 render diffs,
0 id changes, same names, same count.

## 6. Decisions and rejected alternatives

Each is a ruling in the plan's Ledger, in the order made.

1. **The tick is the host's clock, reached through the context.**
   `RenderContext` carries `const SampleClock*`; every driver loop owns a
   clock and ticks it once per sample before its pulls. Rejected: the
   spec's `next(Tick t)` sketch (every pull signature changes, a pin needs
   an owner pointer, `compute()` gains a parameter no node uses) and a
   thread-local tick (a tail pulled after the root would start a new tick).
2. **No clock: every pull computes.** A bare node in a test behaves as it
   always has; the hosts complete the clock. The cost — a driver written
   without a clock gets today's double-advance semantics silently — goes to
   2.5's fault flag (section 7).
3. **Two verbs, `next()` and `step()`.** The two table fills (WavetableSource,
   HybridKSSource) draw N samples in one tick; a plain memo would flatten
   every wavetable patch's table. The pins' helpers are named for the node's
   verbs.
4. **The tail list stays as the loader derives it; the spec's "every node
   whose tick is not t" is rejected.** PhasedValueSource's inactive stages,
   RepeatingSource's input between repetitions, Multiplex's solo input and a
   Setup-only trigger chain sit behind Manual pins on purpose; sweeping them
   would change behaviour.
5. **Taps: `TapSource`, always guarded, reading A.** The unguarded wrapper,
   the counter, both promotion walks and the UI's second pass are deleted.
   Reading B is 2.4.
6. **Landing order.** The memo and the clocks landed with the wrapping still
   in place (Task 1); the wrapping went in its own task (Task 2). Corrected
   during execution: Task 1 was byte-identical except for one class the
   trace could not see (ruling 15).
7. **The expected-diff list is measured over the whole tree**, not the
   manifest: 342 patches with lag reads, 47 with same-tick taps, 157 that
   do not load. Corrected during execution: the list is an upper bound
   (ruling 16).
8. **Each driver loop owns its clock; cross-loop sharing in mixer mode is
   unsupported.** Mixer MODE goes in Part 4.
9. **A cycle with no tap is finite.** `computing_` around the sweep and
   compute; a re-entered node serves its previous value and reports once;
   `prepare()` and `start()` clear the flag so a throw cannot poison a node
   past the next note.
10. **Render time is measured against pre-2.1 and pre-2.3 with one bounded
    experiment** (not recovered after Task 4 — oboe1 0.69x, piano_default 0.79x of pre-2.1 — so the experiment ran: the compacted raw-pointer sweep, kept (section 5)).
11. **ShaperSource's pins return to Auto.** 2.1's ruling 7 kept them Manual
    only to preserve a lag read in the feedback_valve2 patches, which Task 2
    changed on purpose; the A/B over 1,298 Shaper patches: 0 differ.
12. **Per-note envelopes (bend, glide) are prepared with the voice's
    completed context**, so they share its clock.
13. **`current()` overrides stay on `NameGate`, `PerformOut` and
    `TapSource`**: a Setup-time read (a trigger chain, a push binding) sits
    between ticks and must see the live value, not the previous tick's memo.
14. **`explore.cpp`'s loop ticks its voice**; it never ticked the perform
    clock before (a bend was frozen there). A behaviour change on a tool
    path, not a render path.
15. **(execution) The double-pull class.** A tap-only tail whose forward
    inputs walk back into the root cone pulled nodes already computed this
    sample a second time, directly, with no wrapper: the output taken
    mid-loop. `tools/predict_double_pull.py` finds it from the file alone:
    exactly one patch in 1,615, `loop_root_sensitivity_fires`, and the null
    gate differed on exactly that entry at Task 1. It no longer fires (peak
    0.146 → 0.00012) and matches `loop_root_sensitivity_dead`; the "root
    sensitivity" the pair demonstrated was the double advance. Refrozen at
    Task 1; the pair's future is REVIEW R4's.
16. **(execution) The lag list is an upper bound.** The null gate differed
    on 2 of the 13 manifest entries the trace listed. The trace counted
    every wrapper read before the advancer, including a wrapper's
    sweep-time read whose owner then read its pins after the sweep through
    `current()` (2.1's rule), which `RefSource::current()` served live: that
    consumer already used this sample's value. The rule for the A/B became
    "every differing patch is on the lag list or the double-pull list, and
    is explained"; 109 of the 1,458 patches rendered by both binaries were, none unexplained.
17. **(execution) Setup-time pulls `step()`.** The two push-binding chain
    pulls at a note's Setup are evaluations between ticks; two Setups with
    no sample between them (two live notes in one callback interval) would
    otherwise hand the second note the first's chain value.
18. **(execution) The plan's cycle test wired its cycle before `prepare`**,
    which forwards through pins with no guard (a stack overflow that lost the
    whole test run). The test wires it after the host calls; the base's
    defence stays on `next()` as the spec says; the host calls' exposure is
    section 7's.
19. **(execution) `step()` through two pins of one parent reads 9 + 9, not
    8 + 9**: both pins read the node after the sweep. The test documents
    2.1's rule as well as the memo's.
20. **(execution) The 2.1 ShaperSource read-timing test** was rewritten at
    Task 3 to the memo's same-tick values (2, 8, 18) so it compiles and
    tells the truth; Task 4 then only flips the pins.
21. **(execution) The meter's known-answer test** pins `main()`'s length
    against the live UI file; the headless harness's per-sample tick lives
    inside `main()`, so the number is 1712. A frozen fixture is Part 8's.
22. **(execution, the fix wave) `step()`'s limits are stated, not fixed.**
    A Manual pin or an owned sub-source inside a stepped subgraph serves
    the memo for the rest of a fill; a node on two pins steps twice. No
    stored patch does either (62 ref-excited fills scanned). No third verb
    and no pull-mode flag now: REVIEW R4 item 3 decides whether `step()`
    stays; Part 3's node pass owns the structural fix (a pull mode the pins
    honour, or `compute()`-pulls-its-pins per family where a fill becomes a
    generator call).
23. **(execution, the fix wave) The once-per-node `fprintf` on a tap-free
    cycle stays until 2.5's fault flag**: the spec asks for the cycle to be
    reported once, and a flag with nobody reading it reports nothing; the
    path is reachable through no buildable graph.
24. **(execution, the fix wave) `Param::source` and `Input::source` stay
    public** beside the sweep cache until Part 3's pins; every writer in
    the tree goes through the setter today, verified by grep.

## 7. Debt knowingly left

- **A clockless pull inside a host is silent.** A driver that prepares with
  `RenderContext{rate}` alone gets every-pull-computes semantics, which is
  what a bare node in a test wants and what a shared node in a host must
  not get. Checkpoint 2.5's fault flag reports it.
- **`prepare()` and `start()` on a hand-wired tap-free cycle overflow the
  stack.** No file can express one (refs point backward, taps are the only
  forward edge, the UI refuses); the base defends `next()` only, as the
  spec says. 2.5's fault flags, or a guard in the two host calls.
- **`computing_` stays set after a throw inside `compute()`** until the next
  prepare or start; 2.5's no-throw rule retires the case.
- **`step()` reaches Auto pins only and has no memo** (the refutation's
  I2). A Manual pin or an owned sub-source inside a stepped subgraph is
  pulled by its node with `next()` and serves the memo for the rest of the
  fill; a node on two pins of the subgraph steps twice. No stored patch does
  either (62 ref-excited fills, none); a wire in the editor could. Stated in
  `step()`'s comment; REVIEW R4 item 3 decides whether the verb stays, and
  Part 3's node pass owns the structural fix. `step()` does not guard
  re-entry either; its two callers drive private sources.
- **A tap-free cycle is reported with `fprintf` from `next()`** (the
  refutation's M1): a stdio lock on the audio path, reachable only through
  a graph no file can express and the UI refuses. 2.5's fault flag replaces
  it.
- **`Param::source` and `Input::source` are public and writable** beside a
  sweep that only the setter refreshes (M2). Every writer in the tree goes
  through `set()` today (verified by grep: `ensure_constant`, the two
  constructor defaults, `replace_stages`); the member goes private with Part
  3's pins.
- **Shared `Slot` parts advance twice** (the refutation's D10): `partials_next()`,
  `fmt_next()` and `evolve()` are entry points outside `next()`, so a
  Partials or Formant part referenced by two owners still advances its walks
  twice while its pins memoise. "Double advance is impossible by
  construction" holds for `next()`; Part 3's `Slot<T>` design.
- **`advanceList` keeps the retired verb** (M7); it is feedback_loop_design.md's
  "advance list" and Part 5 renames the voice.
- **Mixer-mode cross-loop sharing is unsupported**: a node shared between a
  channel's adapter and the mixer's gain loop computes once per loop (it
  read a block-stale wrapper before). Mixer MODE goes in Part 4.
- **Reading B** (a tap always one sample late) is checkpoint 2.4; the 47
  patches it changes are measured.
- **The `loop_root_sensitivity` baseline pair** no longer demonstrates what
  it was written for (section 5); REVIEW R4 asks what becomes of it.
- **The meter's known-answer test pins `main()`'s length against the live
  file** (1712 now); a frozen fixture is Part 8's.
- **The 2.1 render-time cost** stands at oboe1 0.73x, piano_default 0.88x
  of pre-2.1 after the sweep experiment; REVIEW R4 item 0 (section 5).

## 8. Independent review

A fresh Fable 5.1 context that did not write the code, given the brief
(`docs/architecture/REVIEW_BRIEF.md`), the diff of the six commits
`22bf3be`..`e4ef603`, the module map, Part 2 section 2 and the plan's
Ledger, told to refute. Its report, verbatim:

---

# Refutation R4: checkpoint 2.3, the per-tick memo (22bf3be..e4ef603, base 587b539)

Reviewer: a fresh Fable 5.1 context that did not write the code. Read: `REVIEW_BRIEF.md`, Part 2 §2 and §7, `MODULES.md`, the plan with its Ledger, the review diff (one pass, every hunk). Read outside the diff, each for a named risk: `envelope.h:193-203` and `envelope_presets.h:38-92` (the one direct writer of a pin's `source`, against the new sweep cache); `component.h:20-60` (`set_param`/`clear_param` route through the pin setter); `pins.h:44-90` (what the `friend` buys); `wavetable_source.h:22`, `hybrid_ks_source.h:28` (the stepped pins); `patch_loader.h:12` (the mixer is heap-held, so `ctx_.clock` cannot dangle); `tools/mforce_ui/main.cpp:985-1012, 4188-4197, 4856-4880, 8195-8245` (the editor's edit, stream-start and drum-kit paths). Nothing built, nothing run against the tree; two read-only Python scans over the stored patches and the A/B `hashes.txt` arms (results below). The working tree is at e4ef603 with only `docs/autonomy/GOALS.md` modified.

### The brief, item by item

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Separation of concerns | PASS | Every changed file keeps its one job: `render_context.h` (the host's facts, now two), `dsp_value_source.h` (the contract), `component.h` (the registry and the sweep it serves), `instrument.h` / `mixer.cpp` / `patch_loader.cpp` adapters (drivers own clocks). One seam opened: the core contract header now does I/O (`dsp_value_source.h:3` `<cstdio>`, `:138` `std::fprintf`); a diagnostic channel inside the model — judged under item 8. |
| 2 | Single responsibility | PASS | `SampleClock` (render_context.h:11-14): the driver's sample counter. `RenderContext` (:23-26): the host's facts. `TapSource` (dsp_value_source.h:209-229): a guarded read of another node's memo. `Component` after e4ef603: the pin registry and the compacted sweep it serves (component.h:72-84) — one sentence, the cache is derived from the registry. `VoiceGraph` gained a clock and a context (instrument.h:161-168) on top of source, perform bindings, tails, ids, envelopes — it was already a grab-bag before this checkpoint. |
| 3 | Dependency direction | PASS | The only added `#include` in the diff is `<cstdio>` (diff line 786). `SampleClock`/`Tick` live in core (`render_context.h`), consumed by render (`instrument.h`, `mixer.h`, `patch_loader.cpp`) and ui — downhill per MODULES.md. `git grep RefSource|SharingTrace|promote_starved|advance() -- engine tools` lists only `tools/structure/metrics.json` (deferred minor 8). No cycle: `dsp_value_source.h` includes `component.h` and `render_context.h`, both core. |
| 4 | Duplication | FAIL (Minor) | The clock-completion idiom `ctx = c; ctx.clock = &clock;` appears five times: `instrument.h:202-203` (voice), `:770-771` (drum), `mixer.cpp:13-14`, `patch_loader.cpp:65-66` and `:81-82` (the two Mono adapters, which were already twins and each grew the same ten lines). Issue M3. |
| 5 | Encapsulation | FAIL | `Param::source` and `Input::source` stay public and writable (`pins.h:61`, `:82`) while e4ef603 makes the sweep read a raw-pointer cache that only the setter refreshes (`pins.h:67`, `:88`; `component.h:80-84`). A direct write bypasses the cache. The tree has exactly one such writer, `envelope.h:198-200`, and it is wrong today — Issue I1; the member itself is Issue M2. |
| 6 | Ownership | PASS | `RenderContext::clock` is an observing pointer (render_context.h:25); every owner is named: `VoiceGraph::clock` heap-owned so `StreamingVoice` copies survive pool reallocation (instrument.h:163-164, :290); `DrumSource::clock` by value in a vector with the no-growth contract stated (instrument.h:758-762; the only builder resizes before prepare, ui main.cpp:8212/8235); `StereoMixer::clock_` a member of an object held by `unique_ptr` (mixer.h:39, patch_loader.h:12); the adapters likewise (patch_loader.cpp:60-90, `unique_ptr<MonoSource>`). Inherited, not new: `TapSource::source` is a `shared_ptr` (dsp_value_source.h:210), so every tap-closed loop is a `shared_ptr` cycle that outlives its voice — `RefSource` had the same; Part 3's `ValueSource*` pins are where it ends (Declined, D11). |
| 7 | Abstraction fit | PASS | The signed design is §2: base owns `next()`, node owns `compute()` (dsp_value_source.h:61-83) — matches. Where the plan departs, the Ledger gives a reason I weighed: ruling 1 (the clock through the context, not `next(t)`) is sound and JUCE-shaped; ruling 4 (the tail list stays; no generalised sweep) is sound and better than the spec's paragraph — a Manual pin's node decides when it pulls; ruling 5 (reading A, `TapSource` always guarded) is the spec. Ruling 3, the second verb `step()`, is the weakest fit: it recurses through Auto pins only and has no memo (Issue I2). Ruling 13's live `current()` on three stateless views is the right exception. |
| 8 | Real-time rule | FAIL (Minor) | `dsp_value_source.h:135-140`: `report_cycle_once_()` calls `std::fprintf(stderr, ...)` from `next()` — a stdio lock and I/O on the audio path, once per node, reachable only through a graph no file can express and the UI refuses (`would_close_normal_cycle`, ui main.cpp:1000). Spec §6 says a flag, not I/O; 2.5's fault flag is the fix. Everything else checked and clean: `next()`/`step()` allocate nothing (:61-83); `refresh_auto_sources()` in `start()` is a bounded loop over at most 16 (component.h:80-84, :117); `Param::set()` runs at wire/Setup time only; the `step()` at Setup (instrument.h:369, :521) replaces `advance()` one for one. Pre-existing and out of scope: `fill_table` throws and resizes inside `next()` (wavetable_source.cpp:27-31). |
| 9 | Names and comments | FAIL (Minor) | `dsp_value_source.h:128` "the pins' sources were just assigned" is false (Issue I1). `advanceList` (instrument.h:174, :288; ui main.cpp:3703) keeps the retired verb; so do `run_manual_pin_advance_tests` (engine_tests:2037) and `PinCounter::advances`. `tools/structure/tests/test_structure.py:131-133` names "checkpoint 2.3" in a code comment — the plan's own no-changelog rule. `TapSource` reports `"Tap"` as its type name (dsp_value_source.h:221), the same word the file format uses: good. |
| 10 | Tests | FAIL | Covered: the memo (engine_tests:2168-2213), the sweep following the setter (:2220-2240), a shared envelope bit-identical to a duplicated one through the loader (:2275-2310), the tap guard and the positional counter (:175-212), the shaper's same-sample read (:4333-4349). Not covered: the assignment door of the sweep cache (Issue I1 — a wired `maxValue` after a preset `rebuild()`), and `step()` through a node with a Manual pin or an owned sub-source (Issue I2). Neither existing test could have failed on either. |

### Review Focus, item by item

1. **N pulls in one tick (the two table fills).** Covered: `engine_tests:2185-2198` pins `step()` advancing on every call and recursing through Auto pins; `wavetable_source.cpp:35`, `hybrid_ks_source.cpp:49` use it. Gap: the recursion stops at the first Manual pin or owned sub-source inside the excitation subgraph, where the node's `compute()` pulls with `next()` and gets the memo for the rest of the fill — Issue I2.
2. **A source shared between a stepped subgraph and a per-tick consumer.** Covered: `engine_tests:2197` documents that a node on two pins of a stepped parent steps twice (18, not 17); the Task 1 and Task 2 tree A/Bs. I scanned the six A/B set files (1,615 patches) for every `WavetableSource`/`HybridKSSource` whose `inputSource` is a ref: 62 fills, 0 with a node shared inside the excitation subgraph, 0 with a Manual-pin or owned-sub-source node in it. So no stored patch exercises either limit of `step()`; the limits are a UI wire away.
3. **A graph pulled by two driver loops.** Not covered by a test; ruled unsupported (ruling 8). My reading of the mechanism, for the record: both drivers call `prepare` on the shared node and the last one wins the clock (`mixer.cpp:12-20` prepares the gains with the mixer's clock first, then the channel's adapter prepares the graph with its own, `patch_loader.cpp:64-67`); the loop that does not own the node's clock never advances it, so that loop sees one computed value frozen for its block — the same frozen value the old wrapper served. Consistent with the ruling.
4. **A host that never set a clock.** Covered: `engine_tests:2199-2202` (a bare node computes on every pull). Gap as the plan says: inside a host nothing reports it — 2.5's flag.
5. **A cycle with no tap.** Covered on `next()`: `engine_tests:2203-2213` (finite, 1 then 2). Gaps as the Ledger says: `prepare()`/`start()` still recurse (Task 1 ruling); and the report itself is the `fprintf` of item 8.

### Strengths

- **The setter is the door.** A pin knows its owner (`pins.h:52`, set in `attach`, component.h:133) and `Param::set`/`Input::set` refresh the owner's compacted sweep (`pins.h:67`, `:88`; component.h:80-84). That is the cheapest correct cache for this graph, and `run_sweep_follows_wiring_tests` (engine_tests:2220) pins exactly the behaviour that matters: a source wired after `prepare`/`start` is pulled on the next tick, one cleared is not.
- **`PinBase::operator=` made a no-op** (`pins.h:45-47`) so a node assigning itself never copies another pin's `owner_`. Non-obvious and right.
- **The memo test is the right shape**: computed once, served to a third pull, order independence with the leaf pulled first (engine_tests:2172-2183), `step()` then `next()` serving the last step (:2185-2189).
- **`run_shared_source_tests`** (engine_tests:2275-2310) is the right oracle: a shared envelope renders bit-identical to the same patch with the envelope duplicated, through the loader, with the second consumer proven to hold the node itself. That is the sentence the whole checkpoint exists for.
- **The loader lost about 220 lines** and `resolve_param` (patch_loader.cpp:117-141) is now five lines of intent with no counter, no wrapping branch, no second JSON walk; the comment above `collect_advance_ids` (:154-164) says precisely why the file, not the built graph, decides membership.
- **Ruling 4** (keep the tail list; reject the spec's "every node whose tick is not t") is better than the spec it overrides, and says why in terms the owner can check (RepeatingSource, Multiplex, PhasedValue).
- **The Setup-time `step()`** on push chains (instrument.h:369, :521) catches a real subtlety — two Setups in one callback interval — that `next()` would have got wrong silently.
- **The Ledger is honest where it hurt**: ruling 6 corrected at Task 1 by writing a predictor for a class the trace could not see; the "upper bound" admission at Task 2; the Task 5 experiment kept on a measured rule with the numbers stated.
- **I re-derived the A/B claims from the stored `hashes.txt` arms**, read-only: old→final differs on 77 of 882 over library, baselines, audition, pending (0, 3, 74, 0), every one of the 77 on the measurement document's lag list or double-pull list; t2→final differs on 0 of 882 (Tasks 3, 4 and 5 byte-identical beyond the manifest); the Shaper set differs on 0 of 1,298. The sweep set is the exception — see Issue M9.

### Issues

#### Critical (Must Fix)

None.

#### Important (Should Fix)

**I1. The sweep refresh in `ValueSource::operator=` runs before the pins are assigned; a preset rebuild drops a wired `minValue`/`maxValue` from the sweep.**
`dsp_value_source.h:125-130`: the base assignment calls `refresh_auto_sources()` with the comment "the pins' sources were just assigned". In C++ an implicitly-defined copy or move assignment assigns the base subobjects first and the members after ([class.copy.assign]/12); `Envelope` has no user-written `operator=`, so `*this = std::move(fresh)` at `envelope.h:202` runs the base (and the refresh) while `Envelope`'s pins still hold what `replace_stages` just moved OUT of them (`envelope.h:198-200` move the three wires into `fresh`). The refresh therefore sees `minValue.source` and `maxValue.source` null (both Auto, `envelope.h:38-39`), compacts them out of `autoRaw_`, and only then does the member assignment put the wires back. Result: after any `rebuild()` (`envelope_presets.h:46,56` and the other five presets), a wired range source is held by the pin but never pulled until the next `prepare()` or `start()` refreshes the list. No dangling pointer (the objects are the same ones, moved out and back), so no UB — the source freezes.
Where it shows: the editor. A property edit on a preset envelope while a stream plays goes `update_node_dsp` → `node.apply_config()` (`ui main.cpp:987`) → `set_setting` → `rebuild()` → `replace_stages`, and nothing calls `start()` until the next `play_continuous` (`prepare_graph`, :4188-4197). On the engine's note paths `prepare_voice_at` calls `vg.source->start()` after the push bindings (instrument.h:397), which heals it before the first sample — which is why no gate or A/B saw it.
Fix: delete the refresh from `ValueSource::operator=` (it can never be correct there, by the language rule) and, in `replace_stages`, put the wires back through the setter after the assignment (`minValue.set(...)`, or one `refresh_auto_sources()` at the end). Test: a preset envelope with `maxValue` wired to a `Counter`, prepared and started under a `TestHost`, `set_setting("attack", ...)` after a few ticks, then N more ticks: the counter's `computes` must keep advancing. This is also the one place the public `source` member (M2) bit.

**I2. `step()` is "one sample now" only through Auto pins; inside a stepped subgraph a Manual pin or owned sub-source freezes for the fill, and a shared node is stepped once per pin.**
`dsp_value_source.h:77-83`: `step()` sweeps `s->step()` over the Auto sources and calls `compute()`. A node whose `compute()` pulls anything itself does so with `next()` — the fourteen Manual-pin sites renamed in f67f8bd (`biquad_source.h:52-53`, `combined_source.h:116-121`, `phased_value_source.h:121-133`, `repeating_source.h:39`, `multiplex_source.h:107,110`, `multi_source.h:65`, `layered_red_noise_source.h:78`, `vibrato.h:82`, `additive_source2.cpp:152-153`, `full_additive_source.cpp:57,59`, the formant and partials parts, the evolutions). Under a stepped parent those `next()` calls hit a memo whose `lastTick_` equals the clock, so the second and every later draw of the fill reads the same value: a `LayeredRedNoiseSource` or `Vibrato` as a wavetable excitation fills a flat table; a `Biquad` in resonance mode with a wired frequency fills at one cutoff. Before this checkpoint `advance()` advanced all of them N times. Separately, `step()` has no memo at all, so a node reached on two pins inside the stepped subgraph advances twice per draw (`engine_tests:2197` documents 18 = 9 + 9) — the double advance the memo was built to make impossible, re-admitted inside table fills.
Exposure today: none in stored patches (my scan: 62 ref-excited fills, 0 diamonds, 0 Manual/owned nodes inside them), which is why the Task 1 A/B was clean. It is a silent regression for the next wiring a user makes in the editor, and the contract comment promises more than the code does ("through every Auto pin" is honest; "recursive" in Review Focus 1 is not).
Fix, minimum for the seal: the `step()` comment states both limits; section 7 lists them as debt; a test pins the frozen case (a `Vibrato` or `LayeredRedNoiseSource` on `inputSource`, N steps, the table is not flat — failing today, so it documents rather than asserts, or asserts the limit explicitly). Structural fix with REVIEW R4 item 2 (which already asks whether `step()` is permanent): either a per-node pull mode set by `step()` that the pins' `next()` honours (pins know their owner now, so `Param::next()` can forward to `source->step()` while the owner is stepping; owned sub-sources still need the node to use a base helper), or the `compute()`-pulls-its-pins form per family in Part 3, where a table fill becomes a generator call rather than N pulls in one tick. I would not ship a third verb.

#### Minor (Nice to Have)

**M1. I/O on the `next()` path.** `dsp_value_source.h:135-140`, as item 8: replace with a per-node fault bit when 2.5 lands its flags; until then it is unreachable from any buildable graph. `<cstdio>` leaves the core header with it.

**M2. `Param::source` / `Input::source` public and load-bearing.** `pins.h:61`, `:82`. With the raw-pointer cache, a direct write is a stale sweep at best and a dangling pointer on the audio thread at worst. Make the member private with `get()`/`set()` the API; the reads to touch are few (`env->trigger.source` in the loader and UI, `evolution.source.get()` in `wavetable_source.h:40`, `pin->source` in `ensure_constant`, `envelope.h:198-200`). If not now, a Debug assert in the sweep that each raw pointer still equals its pin's `source.get()` would catch the next writer.

**M3. The clock-completion idiom five times** (item 4). One function — `RenderContext RenderContext::with_clock(const SampleClock*) const`, or `SampleClock::complete(const RenderContext&)` — names the operation the architecture report will have to explain anyway.

**M4. `prepare()` does not reset the memo.** `dsp_value_source.h:30-37` sets a new context, possibly a new clock, and leaves `lastTick_` (:143) from the old one. The memo is keyed on a tick VALUE that is only meaningful relative to one clock. Harmless today (every clock counts up from 0; the one node that changes host — a voice-pool source handed to a `DrumKit`, `ui main.cpp:8226-8235` — was never ticked under its first clock), but `prepare` is the door the clock comes through and `lastTick_ = -1` there is one line that makes the invariant structural.

**M5. `DrumSource` owns its clock by value inside a `std::vector`** and points `ctx.clock` into it (instrument.h:758-762, :771), guarded by a comment; `VoiceGraph` chose the heap for the same hazard (:163-164). Pick one convention; the comment is the only guard today.

**M6. `friend struct Component;` in `pins.h:53` is vestigial.** `Component` touches nothing private in `PinBase`: `attach` and `source_changed` (component.h:133-134) are `PinBase`'s own members and `register_pin` reads the public `kind`.

**M7. Names that kept the dead verb** (item 9): `advanceList`, `run_manual_pin_advance_tests`, `PinCounter::advances`. `tails` says what the list is.

**M8. Test nits.** `engine_tests:2236` `p->param("source1");` is a discarded call — make it `CHECK(p->param("source1") == nullptr)` or delete it. `test_structure.py:131-133` names the checkpoint in a code comment.

**M9. The close's whole-tree A/B is incomplete at review time.** `renders/scratch/ab23_tree/sweep/final/` exists and is empty (no `hashes.txt`); the sweep set's last arm is t2 (32 of 419 differ against old, all on the lag list). The final binary (Task 5's raw-pointer sweep) is proven byte-identical to t2 on 882 patches of the other four sets and on the 80-patch manifest, so the risk is small, but Task 6 step 3 says every set. `patches/old` (273 entries in `old.txt`) was never rendered by any new arm, while the measurement document lists about seventy `patches/old/...` entries on the lag list that no A/B confirmed; say so in the report rather than let the list imply they were.

### Declined to judge

- D1. Mixer-mode cross-loop sharing (a node under two clocks; the last `prepare` wins and the other loop sees a block-frozen memo) — ruling 8, Part 4's mixer MODE; mechanism recorded under Review Focus 3.
- D2. The mixer clock ticks once per channel per sample (mixer.cpp:38-44), so with N channels the gain envelopes advance N times per frame — pre-existing behaviour preserved for byte-identity; mixer.h:33-35 says so; Part 4.
- D3. A hand-wired tap-free cycle still overflows in `prepare()`/`start()` — Task 1 ruling, 2.5.
- D4. `computing_` stays set after a throw inside `compute()` and the next pull falsely reports a cycle — Ledger ruling 9 / section 7, retired by 2.5's no-throw rule.
- D5. A host that never sets a clock double-advances a shared node silently — ruling 2, 2.5's flag.
- D6. A tap pulled twice in one tick serves its first read (positional A, first evaluation wins) — reading B, checkpoint 2.4.
- D7. `WaveSourceMono::render` / `ValueSourceMono::render` call `start(frames)` per block (F260) — pre-existing.
- D8. `fill_table` throws and `resize`s inside `next()` (wavetable_source.cpp:27-31) — pre-existing, 2.5 and 2.6.
- D9. The push-binding loop duplicated between `prepare_voice_at` (instrument.h:368-379) and `deliver_continuation` (:519-527), each edited identically here — pre-existing duplication.
- D10. Shared `Slot` parts: `partials_next()` / `fmt_next()` / `evolve()` are entry points outside `next()`, so the memo does not cover them — a `Partials` or `Formant` part referenced by two owners still advances its walks and counters twice while its pins memoise. The spec's "double advance is impossible by construction" holds for `next()`; section 7 should say so. Part 3's `Slot<T>` design.
- D11. `TapSource::source` as a `shared_ptr` makes every tap-closed loop an ownership cycle that outlives its voice — inherited from `RefSource`, resolved by Part 3's `ValueSource*` pins.
- D12. `explore.cpp` now ticks its voice, a behaviour change on a tool path — ruling 14, accepted.
- D13. `ShaperSource`'s `breakaway`/`capture` pulled in both modes — Task 4 ruling, 0 of 1,298 differ (verified from the arms).
- D14. The Task 2 rule change from "equal, patch for patch" to "every differing patch is on a list and explained" — accepted; I verified 77 of 77 on four sets; the explanation is per class, not per patch (see M9 for the sets it does not yet cover).
- D15. Render time not recovered against pre-2.1 (oboe1 0.73x, piano 0.88x) — Task 5 routes it to REVIEW R4 item 0 with the tables; not this review's call.

### Triage of the deferred minors

1. `tools/predict_double_pull.py` describes the pre-Task-2 loader — can wait; re-describe its docstring at the close (it is the record of how the class was found; keep it).
2. `dsp_value_source.h` TapSource comment on who prepares/starts a tail — before the seal: one line (:216-217 says "the consumers that pull it"; a tail is prepared and started by the voice's list, instrument.h:205/:398).
3. No test for `TapSource::current()` live / a tap pulled twice per tick — before the seal for `current()` live (ruling 13 depends on it: `fire_triggers`, push chains); the twice-pulled tap goes with 2.4.
4. `tools/gen_onemass_lip1.py` docstring — can wait (tool prose).
5. `engine_tests ~4364` vestigial `hysteresis` setter in the advance-order test — with the fix wave while the file is open; not a blocker.
6. `engine_tests ~4325` comment over a four-pin loop — same.
7. `shaper_source.h ~48` "Read only in hysteresis mode" — before the seal: it is the Task 4 ruling's only record in code; say the base pulls them regardless.
8. `tools/structure/metrics.json` naming `RefSource` — before the seal by construction (Task 6 step 4 regenerates it).

### Assessment

**Ready to seal? With fixes.** The memo, the clocks, the deleted machinery and the measured render list are correct and well pinned, and I could confirm the A/B claims from the stored arms. Two things must change first: the mis-ordered sweep refresh in `ValueSource::operator=` (I1, two lines and a test; it is live in the editor), and `step()`'s two silent limits made explicit and pinned (I2), with the structural question handed to R4 item 2; the sweep set's final A/B arm must also complete (M9).

---

### What was done

One fix wave, `fdf7536`, each item test-first where a test could fail:

- **I1** — RED: an `AREnvelope` with `maxValue` wired to a counter, three
  ticks, `set_setting("attack", …)`, three more ticks: the counter stayed
  at 3. The refresh left `ValueSource::operator=` (a base's assignment runs
  before the derived pins are assigned, so it can never be right there);
  `Envelope::replace_stages` keeps its three wires aside over the
  assignment and puts them back through the setter. GREEN: 6.
- **I2** — ruled, not restructured (ruling 22): `step()`'s comment states
  both limits; section 7 carries them; REVIEW R4 item 3 asks whether the
  verb stays; Part 3's node pass owns the structural fix.
- **M3** `RenderContext::with_clock()` at the five drivers. **M4** `prepare()`
  resets the memo tick. **M5** a drum's clock is heap-owned like a voice's.
  **M6** the friend is gone. **M7** the dead verb is out of the test names
  (`advanceList` stays: feedback_loop_design.md's term, Part 5's rename).
  **M8** the discarded call asserts; the checkpoint name is out of the
  meter test's comment. **M1**, **M2** and **D10** go to section 7 with a
  reason each. **M9** the final arm was rendered over all six sets with
  the sealed binary (Appendix A).
- The deferred minors the refuter triaged before the seal: the `TapSource`
  comment names what prepares and starts a tail; `run_tap_reads_live_tests`
  pins `TapSource::current()` reading live; the shaper's threshold-pin
  comment says the base pulls them regardless; `metrics.json` regenerated.

The same reviewer's second pass over the fix wave, verbatim:

---

# Refutation R4, re-review of the fix wave (fdf7536 on e4ef603)

Scope: each finding of `refutation-r4.md` verdicted against the fix diff `review-e4ef603..fdf7536.diff` (read once, every hunk), and the fix diff inspected for new breakage. Read outside the diff only to confirm what the diff claims: the checked-out `dsp_value_source.h`, `envelope.h`, `render_context.h`, `instrument.h`, `mixer.cpp`, `patch_loader.cpp`, `shaper_source.h`, `engine_tests/main.cpp` (line numbers below are post-fix); `tools/mforce_ui/main.cpp:8193-8245` (the only `DrumKit` builder, for the move-only change); a grep for any remaining direct pin `source =` writer (none); the A/B `hashes.txt` arms for M9. Nothing built, nothing run against the tree; the controller's test, gate and null-gate results are taken as stated and are consistent with what the diff shows.

## Finding Verdicts

| Finding | Verdict | Evidence |
|---|---|---|
| **I1** refresh before the pins are assigned | **ADDRESSED** | `dsp_value_source.h:132-139`: the refresh is gone from `ValueSource::operator=` and the comment states the language rule ("a base's assignment runs before the derived members are assigned"). `envelope.h:200-210`: `replace_stages` moves the three wires into locals (`:204-206`), assigns (`:207`), and puts them back through `set()` (`:208-210`), which refreshes the sweep; the raw cache never holds a freed pointer in between because the locals own the sources across the assignment. Test `engine_tests:2244-2255`: an `AREnvelope` with `maxValue` wired to a `Counter`, 3 ticks → 3 computes, `set_setting("attack", 0.3f)` (a `rebuild()` → `replace_stages`), 3 ticks → 6, and the wire is still the same object (`:2255`). That is exactly the failing case I described (RED at e4ef603 by the mechanism; the controller reports RED→GREEN). No direct writer of a pin's `source` remains anywhere in engine/ or tools/ (grep over every pin name: none). |
| **I2** `step()` reaches Auto pins only, has no memo | **RULED-DEFERRED** | `dsp_value_source.h:77-84`: the contract states both limits (Manual pin / owned sub-source serves the memo for the rest of the fill; a node on two pins steps once per pin) and the supported case (the two table fills drive a private excitation). The Ledger line (plan `:383`) records the ruling, routes the verb's fate to REVIEW R4 item 3 and the structural fix to Part 3's node pass. On the question put to me: a pinning test is **not** required for the seal. A test that asserts a limitation would be deleted the day Part 3 removes it, and the supported contract is already pinned (`engine_tests:2185-2198` for `step()`; the wavetable and HybridKS pin tests, `seed->nexts == 100`). The stated limit, the Ledger and R4 item 3 are enough. |
| **M1** `fprintf` on the `next()` path | **RULED-DEFERRED** | Still at `dsp_value_source.h:148`; ruled to 2.5's fault flag (plan `:383`). Reachable through no buildable graph; accepted. |
| **M2** public `Param::source` / `Input::source` | **RULED-DEFERRED** | Members unchanged (`pins.h:60`, `:81`); ruled to Part 3's pins (plan `:383`). The one writer that bit (I1) now goes through the setter; grep finds no other. Accepted. |
| **M3** clock-completion idiom ×5 | **ADDRESSED** | `render_context.h:28-32` `RenderContext::with_clock(const SampleClock*) const`; used at all five sites: `instrument.h:202`, `:769`, `mixer.cpp:13`, `patch_loader.cpp:66`, `:81`. `RenderContext` stays an aggregate (no constructor added), so `RenderContext{rate}` and `{rate, &clock}` initialisers still hold. |
| **M4** `prepare()` does not reset the memo | **ADDRESSED** | `dsp_value_source.h:32` `lastTick_ = -1;` with the reason. Checked the one risk a reset carries — a node re-prepared inside a tick after it computed, which would make its next pull compute again: every `prepare` call in engine/ is at build, on a part the node has just made (`additive_source2.cpp:65,78`, `vibrato.h:61`, `ensure_constant`), or in a host's `prepare`; none is on a pull path. |
| **M5** `DrumSource` clock by value in a vector | **ADDRESSED** | `instrument.h:757-760` `std::unique_ptr<SampleClock>`, completed at `:769`, ticked at `:785`. `DrumSource` is now move-only; the only builder (`ui main.cpp:8211-8235`) default-constructs, `resize`s and assigns a member — all legal for a move-only element. |
| **M6** vestigial `friend struct Component` | **ADDRESSED** | Gone from `pins.h` (the `private:` block now holds `owner_` alone, `:50-52`). |
| **M7** names keeping the dead verb | **ADDRESSED / RULED-DEFERRED** | `run_manual_pin_next_tests` (`engine_tests:2037`, called `:5798`), `PinCounter::nexts` (`:2351`), `LengthProbe::nexts` (`:4114`) and every use. `advanceList` kept by ruling (feedback_loop_design.md's term; Part 5's rename) — accepted. |
| **M8** test nits | **ADDRESSED** | `engine_tests:2238` is a `CHECK(p->param("source1") == nullptr)`; `test_structure.py:131-132` no longer names the checkpoint. |
| **M9** close's A/B incomplete (sweep's final arm empty; `old` never rendered) | **NOT ADDRESSED (in progress; non-code)** | `renders/scratch/ab23_tree/sweep/final/hashes.txt` now exists (20:43): old→final differs on 32 of 419, every one on the measurement document's lag list, and final equals t2 on all 419 — the sweep gap I raised is closed. The fdf7536 run the controller describes has so far rewritten `library/final` (21:02) and `baselines/final` (21:03); `audition`, `pending` and `sweep` carry the 20:39-20:43 arms (the e4ef603 CLI), and `old/final/` is still empty. Nothing in fdf7536 can move a CLI render (a rebuild at load precedes `prepare`; at Setup `start()` follows; the memo reset happens only at build), and the null gate is 80/80, so I expect no change — but the seal should wait for the run to finish over all six sets, or the report should say which binary produced each arm. The `patches/old` caveat (listed on the lag list, never confirmed by an A/B) stands until that arm lands. |

Deferred minors, as triaged in the refutation:

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | `predict_double_pull.py` describes the pre-Task-2 loader | ADDRESSED | `:3-9`: "models that earlier engine on purpose: it is the record of how the one such patch in the tree was found"; "Rules mirror the loader as it was then". |
| 2 | TapSource comment on who prepares/starts a tail | ADDRESSED | `dsp_value_source.h:225-227`: "its consumers, or the voice's tail list when only taps reach it". |
| 3 | No test for `TapSource::current()` live / a tap pulled twice | ADDRESSED / RULED-DEFERRED | `engine_tests:2260-2275` `run_tap_reads_live_tests`: `current()` reads live between ticks without a pull (`:2267`, `:2269`, `:2275`), and the positional reading A is pinned on the way (`:2272`: pulled before its source computed this tick, last tick's value). The twice-pulled tap is 2.4's, as triaged. |
| 4 | `gen_onemass_lip1.py` docstring chain list | NOT ADDRESSED | `:301` still reads "in whatever order the consumers pull". Triaged "can wait"; not a seal blocker. |
| 5 | vestigial `hysteresis` setter in the advance-order test | ADDRESSED | The line is gone from `run_combine_mod_advance_order_tests` (grep: no match); the six-pin order is the same in both modes now. |
| 6 | comment over the four-pin loop | ADDRESSED | `engine_tests:4382-4383`: "every wired pin ... (four of the six are wired here; the auto-pin test wires all six)". |
| 7 | shaper comment "Read only in hysteresis mode" | ADDRESSED | `shaper_source.h:23-25`: "compute() reads them only in hysteresis mode; the base pulls them every sample regardless, so a modulator wired here runs from the note's start". |
| 8 | `metrics.json` names `RefSource` | ADDRESSED (working tree) | `tools/structure/metrics.json` now has 0 occurrences of `RefSource`/`SharingTrace`; the file is not in fdf7536, so it was regenerated for the seal commit as planned. |

## New Breakage in the Fix Diff

**None.** What I checked, each against the way it could fail:

- `envelope.h:200-210`: the wires are owned by locals across `*this = std::move(fresh)`, the fresh layout's pins are null (a preset builds `fresh` new; `Component`'s copy constructor is deleted, so it cannot carry wires), and the three `set()` calls refresh the sweep after the member assignment, which is the one order that is correct. `trigger.set()` on a Manual pin is a harmless refresh. Preset constructors call `rebuild()` → `replace_stages` during construction; `owner_` is set by the pin's own constructor before that, so `source_changed()` has an owner. `refresh_auto_sources()` is a bounded loop with no allocation, so a `rebuild()` reached from a key-up or a Setup push stays real-time safe.
- `dsp_value_source.h:32`: resetting the memo at `prepare` cannot double-compute inside a tick because no `prepare` is reachable from a pull path (the grep above); the UI's `prepare_graph` runs with streams stopped.
- `render_context.h:28-32`: a const member returning a copy; the struct stays an aggregate and trivially copyable; the engine tests' `TestHost` and the UI's `g_renderContext{AUDIO_SAMPLE_RATE, &g_editorClock}` still initialise as before.
- `instrument.h:757-786`: a move-only `DrumSource` is legal in `std::vector` with `resize` and growth (the only builder, ui main.cpp:8211-8235); `clock` is never null (default member initialiser). The UI's compile of this header is not in the controller's stated verification (engine_tests, null gate, fast gates); by reading, nothing in tools/ copies a `DrumKit` or a `DrumSource`.
- `engine_tests`: the new tests' arithmetic checks out by hand (3 → 6 computes; the live-read sequence 1, 2, 2, 2, 3); the duplicate `#include "mforce/core/envelope_presets.h"` (`:2217` and `:2345`) is harmless under `#pragma once`; removing the hysteresis setter from the order test changes nothing now that all six pins are Auto.
- `pins.h`: dropping the friend compiles (nothing in `Component` touched `owner_`), as the test run confirms.

## Out-of-Scope Observations

- The fdf7536 A/B run is mid-way (library and baselines done at 21:02-21:03; audition, pending, sweep still the e4ef603 arms; `old` empty). The report's A/B table should name the binary per arm or wait for the run.
- `step()`'s comment says the two callers "drive a private excitation source"; the loader does not enforce it (an `inputSource` `{"ref"}` can be shared today — 0 of 62 are). A one-line load warning when a table fill's excitation has a second consumer would turn the assumption into a check; material for R4 item 3 / Part 3, not this wave.
- `metrics.json` is regenerated in the working tree but uncommitted; it belongs to the seal commit as the plan says.
- Deferred minor #4 (`gen_onemass_lip1.py:301`) is the one line of prose still describing the old order; one edit whenever that file is next touched.

## Verdict

**All findings addressed or ruled; no new Critical or Important breakage.** I1 is fixed where I said it broke and pinned by the test that fails on e4ef603; I2 is ruled with its limits stated at the contract and routed to R4 item 3, and I do not hold a pinning test to be required; M3-M8 and deferred minors #1-#3, #5-#8 are done; M1, M2 and `advanceList` are ruled with reasons I accept. The one open item is non-code: M9's A/B run over all six sets must finish (sweep is now covered and equals t2; `old` is pending) before the seal states the whole-tree result.

---

## 9. Reading guide

Ten minutes: `render_context.h` whole; `dsp_value_source.h` next()/step()
and `TapSource`; `instrument.h` `VoiceGraph::tick`, `PitchedInstrument::prepare`,
`render_chunk`; `patch_loader.cpp` `resolve_param` and the advance-list
comment; `mforce_ui/main.cpp` `update_all_dsp` and the callback's stream
section. Then the memo tests (`run_memo_tests`, `run_shared_source_tests`)
and `tools/predict_double_pull.py`.

## 10. C++ lessons

### Lesson 1: a memo keyed on a clock, not a scheduler

A pull-model graph has a problem a push-model graph never meets: when two
consumers ask the same node for "the next sample", who advances it? Before
this checkpoint the answer was bookkeeping — the first pin wired got the
node, every later pin got a reader of its last value, and a second pass
repaired the cases where the first pin was not in the render cone. The
push-model answer is a scheduler: sort the graph once, run every node once
per sample in that order, let consumers read results. That is what most
audio frameworks do (a JUCE `AudioProcessorGraph` orders its nodes and runs
them; Max/MSP and Pure Data schedule their patches), and it costs a sort,
a schedule object and a second way of thinking about the graph.

The memo keeps the pull model and its simplicity. Every node remembers the
tick it last computed at; a pull at the same tick serves the remembered
value; the first pull computes. "Evaluate each node once per step" falls
out with one integer compare, in whatever order the pulls arrive, and the
order of pins in a file stops mattering. The C# reader knows the idiom as
`Lazy<T>` or a per-frame cache (`if (cachedFrame == Time.frameCount) return
cached;` in a Unity script): memoisation keyed on a monotonically advancing
clock, invalidated by time rather than by an explicit reset.

The two things the memo needs that a scheduler supplies for free are the
clock itself — somebody must say "a new sample begins" — and an escape for
a node that legitimately wants several samples in one tick (`step()`,
lesson 3). The clock went into the host's facts (lesson 2).

### Lesson 2: an observing pointer in a value-type context

`RenderContext` is a small struct passed by value and copied into every
node: the rate as an `int`, and now the clock as `const SampleClock*`. A
C# or Java reader would write a reference to a `Clock` object and think no
further, because every object there is a reference and the garbage
collector decides when it dies. In C++ the pointer forces the lifetime
question into the open: who owns the clock, and is it alive whenever a
node reads it?

The answer is the ownership convention the code makes explicit. The driver
loop owns the clock — a `VoiceGraph` holds its `SampleClock` by
`shared_ptr` so a `StreamingVoice` copied out of the pool keeps it alive;
a `DrumSource`, a mixer and an adapter hold theirs inline because nothing
copies them out — and the driver prepares its graph with a context that
points at the clock it owns. A node observes; it never owns. The pointer is
`const` because a node must never tick the clock (that would be a node
deciding what time it is), and it is a raw pointer, not a `shared_ptr`,
because a node holding a share of the host's clock would be a node holding
a share of the host.

Why not a reference member (`const SampleClock&`)? A reference member makes
the struct unassignable, and `RenderContext` is assigned at every prepare.
Why not the clock by value inside the context? Every node would then hold
its own copy, which could never advance. The observing pointer is the
smallest thing that says "the host's clock, which the host owns".

### Lesson 3 (short): two entry points on one template method

`next()` is the Template Method (the base owns the algorithm — guard, memo
check, sweep, `compute()` — and the node supplies one step). Checkpoint 2.1
sealed it: `next()` is not virtual, `compute()` is pure. This checkpoint
adds a second entry point to the same skeleton, `step()`, which runs the
same sweep and the same `compute()` without the memo check. Two public
verbs, one algorithm, one overridable step. The alternative — a flag
parameter, `next(bool force)` — says less at the call site and invites a
third mode later; the alternative on the other side — a second virtual —
would let a node override the rule. The pattern's C# analogue is a sealed
base class with two public methods calling one `protected abstract` step;
the reason to prefer it over a flag is the same there.

## Appendix A: the whole-tree A/B

The pre-2.3 binary (`renders/scratch/ab_tree/cli_pre23.exe`, the tree at
`587b539`) against the sealed one (`cli_final.exe`, `fdf7536`), every
patch rendered by both, WAV bytes compared. Every differing patch is on the
measurement document's lag list (108) or is the double-pull patch (1); none
is unexplained.

| set | patches in the set | rendered by both | differ |
|---|---|---|---|
| patches/library | 80 | 80 | 0 |
| patches/baselines | 54 | 54 | 3 |
| patches/audition | 708 | 708 | 74 |
| patches/pending | 43 | 40 | 0 |
| patches/sweep, every third | 457 | 419 | 32 |
| patches/old, every third | 273 | 157 | 0 |

The three baselines are `double_advance_bug`, `double_advance_ok` and
`loop_root_sensitivity_fires`. The 74 audition patches: `feedback_valve2`
(24), `string_harness1` (18), `string_harness2` (18), `bwg_perc1` (7),
`bwg_perc2` (7). The 32 sweep patches: `string_harness1` (10),
`feedback_valve2` (8), `string_harness2` (6), `stk_saxofony_port` (3),
`stk_bandedwg_port` (2), `stk_bowed_ext1`, `stk_bowed_ext2`,
`bassoon_ref1` (1 each). A patch missing from "rendered by both" failed to
render in both binaries (the same files in each arm; none present in only
one). The intermediate arms — Task 1 (`cli_t1.exe`), Task 2 (`cli_t2.exe`)
— were compared too: Task 1 differed from the base on the double-pull patch
alone over library and baselines; the sealed binary differs from the Task 2
arm on nothing. The set files are `renders/scratch/ab23_tree/*.txt`, the
hashes under `renders/scratch/ab23_tree/<set>/{old,t1,t2,final}/hashes.txt`.
