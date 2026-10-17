# Architecture report: checkpoint 2.1, pins declared once

Commits `1b9b69f` through `9f2b4a8` (23 commits on main, 2026-10-16 and
2026-10-17), the first checkpoint of Part 2 (the ValueSource contract,
`docs/architecture/part2-valuesource-contract.md` section 3) and the
first that changes the engine. Report R2 in the format R1 established;
the independent review is section 8.

## 1. Summary

Every pin on every DSP node is now declared once, as a member that
registers itself with its node:

```cpp
struct SVFSource final : Processor {               // `input` ("source") inherited
  Param cutoffFreq{this, "cutoffFreq", 1000.0f, 20.0f, 20000.0f, "hz"};
  Param resonance {this, "resonance",  1.0f,    0.5f, 40.0f};
  Setting<int>  mode{this, "mode", 0, 0, 4, kModeLabels};
  Setting<bool> normalize{this, "normalize", false, false, true};
  float compute() override { ... input.current() ... cutoffFreq.current() ... }
};
```

The five hand-written lists a node used to carry for each pin (the
descriptor row, the `set_param` chain, the `get_param` chain, the
`prepare` forwarding and the advance list in `next()`) are gone from all
71 node types, and the 109 typed `set_x` / `get_x` accessors nothing
called went with them. `Component` is the root (Part 3 section 2): it
owns the registry and serves the four descriptor spans and the by-name
access from it. `ValueSource` is one branch under it, with `Generator`
and `Processor` beneath; `next()` is the same for every node (advance the
wired pins' sources in declaration order, then `cur_ = compute()`), and
`compute()` is the one thing a node writes.

Renders did not change. The null gate reads 80/80 identical after every
one of the 19 commits; a whole-tree A/B against a binary built from the
commit before the checkpoint reads 885/885 identical over library,
baselines, audition and pending, and 724/728 over a sample of every
third patch in the sweep and old archives — the four that differ are
generator output in patches/old that omits the RedNoise keys whose
defaults this checkpoint corrects, so they are the same case as the
next sentence, left alone because old/ is an archive. Eight stored patches
(two baselines, six audition) had silently relied on constructor
defaults that disagreed with their descriptors; the descriptor now wins,
and those eight patches state the value they meant, so what they render
is unchanged (section 6).

Two classes of wiring that no stored patch uses could change by one
sample of modulation lag (section 6, "the two tolerated classes"); the
per-tick memo of checkpoint 2.3 removes the mechanism behind both. One
type, `ShaperSource`, was found by A/B to matter to 28 stored patches,
the whole `feedback_valve2` audition queue, and keeps its old read timing
exactly.

**The per-sample cost is the one thing this checkpoint hands you
unresolved.** The generic `next()` costs a few nanoseconds per node per
sample that the hand-written bodies did not pay, and it compounds over
cheap nodes (constants, combiners, curves, envelopes): oboe1 renders
about 30-40% slower than before the checkpoint, piano_default 25-45%,
viola (additive-dominated) 2-9%; a chain of forty trivial nodes, 50-75%.
Section 5 has the measurements and the experiments that ruled out the
easy explanations; section 6 (item 15) says why it is left as it is and
where it is recovered. It is the first item of your R2 verdict.

## 2. Types and files

```
NEW  engine/include/mforce/core/descriptors.h  (66 lines)
  Does: the self-description types — SourceCategory, ParamDescriptor,
        InputDescriptor, SettingType/SettingDescriptor, ArrayDescriptor.
        Moved verbatim out of dsp_value_source.h.
  Used by: pins.h (a pin builds its own descriptor), the UI, the codec.
  Depends on: nothing.
  Design choice: its own header so pins.h can build descriptors without
    including the ValueSource header that includes pins.h (no cycle).
  Name: "descriptors" — what the UI and the codec read.
  Read: all of it; it is the vocabulary the rest speaks.

NEW  engine/include/mforce/core/pins.h  (150 lines)
     — PinBase, Param, Input, SettingBase/Setting<T>, Array, SlotBase/Slot<T>, Pull
  Does: the five member kinds a node declares its pins with. Each knows
        its name, default and range, how to read its value, and how to
        register with its owner.
  Used by: component.h (the registry), every node header.
  Depends on: descriptors.h; forward declarations of Component,
    ValueSource and RenderContext.
  Design choice: a member that registers itself in its own constructor
    body, after its fields are initialised (section 10, lesson 1). Kind
    is a tag (PinKind) plus a static kKind per type, so the registry can
    find a pin by name and kind with a static_cast, not RTTI.
  Alternative: a base-constructor registration (the plan's text) —
    rejected because the base constructor runs before the derived
    fields exist and cannot call the derived descriptor(). A macro that
    expands one declaration into the five lists — rejected by the spec.
  Name: "pins" — the UI's word for the thing a wire plugs into; Param /
    Input / Setting / Array are the spec's four kinds, Slot is Part 3's.
  Read: lines 13-37 (the two enums and PinBase: what every pin shares,
    and why a pin is neither copyable nor movable), 40-56 (Param: the
    whole idea in sixteen lines), 88-114 (Setting<T>: the conversion and
    the clamp), 129-148 (Slot<T>, declared for checkpoint 3.1).

NEW  engine/include/mforce/core/component.h  (116 lines)  — class Component
  Does: the root of every node. Owns the pin registry its members fill
        in during construction; serves the four descriptor spans and the
        by-name set/get from it; caches the list of sources the per-sample
        sweep advances.
  Used by: dsp_value_source.h (ValueSource derives from it); the UI, the
    loader and the codec through the by-name surface.
  Depends on: pins.h.
  Design choice: one vector of PinBase* in declaration order, four
    descriptor vectors built at registration, one vector of pointers to
    the Auto pins' source members for the sweep. Copy construction
    deleted; assignment leaves the registry alone (section 6).
  Alternative: per-type static tables (CRTP) — the spec's own rejected
    alternative; one small vector per node, built when the patch is
    built, costs nothing on the audio thread.
  Name: "Component" — Matt's MComponent, the one C# name that returns.
  Read: lines 15-19 (what a Component is and is not allowed to do),
    69-89 (register_pin: the one place a declaration becomes a table
    row), 91-95 (find: by kind, by name, no RTTI).

CHANGED  engine/include/mforce/core/dsp_value_source.h  (178 -> 171 lines)
     — ValueSource, NEW Generator, NEW Processor, ConstantSource, RefSource
  Does: the signal contract. prepare() forwards to every pin then calls
        on_prepare(); next() advances the wired Auto pins' sources in
        declaration order then stores compute(); current() stays virtual
        for the three nodes whose value is a live read (RefSource,
        NameGate, PerformOut).
  Used by: everything.
  Depends on: component.h, render_context.h.
  Design choice: next() non-virtual and compute() pure (the template
    method; section 10, lesson 2); Processor takes the pull mode of its
    primary input in its constructor so a node that pulls its own source
    (between gaps, once per clone, at the old read moment) stays a
    Processor.
  Alternative: keep next() virtual for good — rejected, because then a
    node could run its own advance loop again and nothing would notice.
  Name: unchanged; "Generator" and "Processor" are Matt's shape from the
    universal-parameter discussion.
  Read: lines 35-62 (advance, prepare, next, compute: the whole
    contract), 94-111 (the two bases), 160-171 (the pin bodies: what an
    unwired pin reads, what a Manual pin's advance() does).

CHANGED  engine/include/mforce/core/dsp_wave_source.h  (112 -> 67 lines)  — WaveSource
  Does: the phase-accumulating oscillator base: three pins, the
        accumulator, and wave() for the subclass.
  Read: the whole file; it is the worked example every family followed.
```

The families, in the order they were cut. Each entry names what moved
and what stayed; the per-task reports under the plan's workspace carry
the pin tables, and the commit messages carry the Manual pins and why.

- **Oscillators** (`1b9b69f`): WaveSource's trio declared in the old
  advance order (amplitude, frequency, phase — the descriptor table had
  frequency first, so the Properties panel now lists amplitude first);
  `compute_wave_value()` is `wave()` in every subclass.
- **Core** (`4aa668e`, `7174290`): Envelope and the six presets, CurveNode,
  RangeSource, VarSource, NameGate, MultiSource. Envelope's `trigger` is
  the first Manual pin (a trigger chain is evaluated at note setup, never
  per sample). The six presets collapse about 150 lines of by-name
  chains into `Setting<T>` members with the rebuild in
  `on_setting_changed`; a protected `Envelope(int, float)` lets a preset
  declare the base's `sustainLevel` with its own default. MultiSource's
  vector-held input list stays hand-written until Part 3 gives it a pin
  kind, so it derives from ValueSource for now.
- **Noise** (`c71a39b`, `8f640e6`, `8966f61`): thirteen types. Three
  constructor-vs-descriptor disagreements corrected (RedNoise density,
  smoothness, rampVariation; WanderNoise2 minSpeed, maxSpeed); two
  baselines that relied on them now say so. WanderNoise and WanderNoise2
  prepared an `amplitude` they never read — kept faithful as Manual, and
  listed as debt.
- **Wavetable** (`dd09a57`): WavetableSource, HybridKSSource and the twelve
  evolution holders (the docs said thirteen; TargetEvolution has none).
  The holders are structural types (Part 3 moves them under Component):
  only their pins moved; their evolutions now read the holder's pins by
  reference, so each default is declared once. BitRotate found the
  wiring-order rule (section 6).
- **Additive** (`46f9626`): the three additive sources and the eleven
  structural types (Partials and its three subclasses, Formant, the four
  spectra, CompositePartials, ExpandRuleNode). The `MFORCE_PARTIALS_
  MOTION_CONFIG_DESCS` macro and ~250 lines of per-class setting chains
  are one declaration of 27 shared settings; partials.h fell from 1486
  to 1137 lines. Every envelope pin on a part is Manual, advanced by the
  additive source through `partials_next()` / `fmt_next()` as before.
- **Physical** (`c850d83`): the first all-Processor family (AllpassResonator,
  BowTable, DelayLine, KSString, Mesh2D, Pierce). KSString's 21 settings
  keep their clamps in `on_setting_changed`; detune's default corrected
  (every stored KSString states it).
- **Filters** (`4d8f228`): nine Processors and Vibrato. Biquad's frequency
  and radius are Manual (pulled only in resonance mode).
- **Combiners and modulators** (`28e1b04`, `79fac28`): CombinedSource and
  CrossfadeSource have two inputs neither named `source` and derive from
  ValueSource until Part 3; MultiplexSource and RepeatingSource own their
  `source` through `Processor(Pull::Manual)`; ShaperSource keeps its old
  read timing with every pin Manual (section 6).
- **Render** (`6f95c22`): PerformOut, a pinless Generator whose `current()`
  stays a live read.
- **The seal** (`5f9aeea`): next() non-virtual, compute() pure; the twenty
  structural fakes express themselves as `compute()` returning 0.

Base-level additions made during the migration, each test-first:
`59d2807` (the sweep walks the wired Auto pins' sources, not every pin),
`41d53c4` (`Param::advance()` / `Input::advance()` for Manual pins),
`70c2899` (`Setting<T>::clamp()`), `77b424a` (`Processor(Pull)`).

## 3. Interfaces changed

- `ValueSource::next()` is non-virtual; `compute()` is the pure virtual a
  node implements; `prepare()` forwards to the pins and calls the new
  `on_prepare()`; `current()` stays virtual. The by-name surface
  (`set_param`, `get_param`, `add_param`, `clear_param`, `set_setting`,
  `get_setting`, `set_array`, `get_array`) and the four descriptor spans
  live on `Component`, virtual for now (section 7).
- An unwired `Param` has no source: `get_param(name)` returns null where
  it used to return the node's default `ConstantSource`. Every caller
  checked (the loader's starved-ref walk and paramMap binding, the UI's
  envelope collector and Formant strip, `--dump-descriptors`); every
  stored paramMap names a pin its patch wires.
- Nodes are neither copyable nor movable. `Envelope::make_ar`,
  `make_adsr`, `make_adsr_abs` and `envelope_from_preset_json` return
  `std::shared_ptr<Envelope>`.
- The 109 typed accessors are gone; the few callers (`Vibrato::build_lfo`,
  `fire_triggers`, the trigger bindings, the BandSpectrum configurator,
  two Formant sites in the UI) use the pins.
- The loader's typed-setter calls after `wire_params_generic` are gone
  (the generic pass applied the same keys with the same defaults).
- Descriptor order visible in the UI: WaveSource's trio; the six presets
  list Envelope's four settings first (AR and ADR gain a no-op
  `sustainLevel` row, all six gain `timeScale`); the Partials subclasses
  list the 27 shared settings first; `NameGate.in` moved from the param
  list to the input list. No stored patch carries the affected keys.
- `tools/mforce_ui/main.cpp`: its own `PinKind` is `UiPinKind`.
- `Setting<int>` rounds where `int(v)` truncated; no stored patch or UI
  path sends a non-integral int.

## 4. Duplicates collapsed

- Five lists per pin into one declaration, across 71 types.
- 109 typed accessors deleted (the count in the spec's section 7 row,
  confirmed by the diff).
- The Partials settings macro and its per-class `set_setting` /
  `get_setting` chains (~250 lines), the six Envelope presets' chains
  (~150 lines), the evolution holders' mirrored defaults.
- The meter's `duplicate_blocks` reads 208 before and 147 after.

## 5. Metrics

`python tools/structure/check.py --delta 1b4fe1b` at the close (head
`e29b29c`):

| Measure | Before | After | Change |
|---|---|---|---|
| files | 164 | 167 | +3 |
| lines | 57,873 | 57,505 | -368 |
| files_over_limit | 21 | 20 | -1 |
| functions_over_limit | 100 | 99 | -1 |
| file_level_mutable_vars | 112 | 112 | +0 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 51 | 54 | +3 |
| changelog_comments | 270 | 255 | -15 |
| duplicate_blocks | 208 | 147 | -61 |
| include_violations | 13 | 13 | +0 |
| unmapped_files | 0 | 0 | +0 |

The three new files are `descriptors.h`, `pins.h` and `component.h`
(332 lines together). The engine alone lost 3,064 lines (61 files,
+2,621 / -5,685); the tests gained 2,545 in one file (section 7). The
three new `dynamic_cast`s are `Slot<T>::set` (the one the design keeps,
at wire time), WavetableSource's re-derived typed evolution view, and
the loader's default-constant binding check — the first two retire with
`Slot<T>` in 3.1. The `changelog_comments` count is the meter's dated-
comment regex: fifteen fewer, the ones the refuter listed included.

Render time, a binary built from `1b4fe1b` (the commit before Task 1)
against the checkpoint's final binary, the same patch rendered by both
alternately, minimum of 7-9 runs. The machine was never quiet (Chrome
and a Windows broker process held about half of it), so each pair is
given as the range seen across runs:

| patch | nodes | before | after | change |
|---|---|---|---|---|
| library/winds/oboe1 | 21 | 0.68-1.14 s | 0.93-1.45 s | +27% to +40% |
| library/keys/acoustic_piano/piano_default | 45 | 1.00-1.69 s | 1.44-2.08 s | +23% to +44% |
| library/strings/viola_default | 10 | 1.27-2.24 s | 1.34-2.29 s | +2% to +9% |

Synthetic patches that isolate one mechanism each (renders/scratch/
ab_tree/micro, 20-30 s of audio, same method): one unwired sine +0-7%;
a sine with an envelope on its amplitude +1-5%; a chain of eight
lowpass filters +5-21%; a shared envelope read through a RefSource
+5-9%; forty unwired sines through thirty-nine CombinedSource adds
+50-66%; forty sines each with its own envelope +51-81%; forty sines
sharing one envelope +35-52%; forty envelopes added +57-67%. The cost
is per node per sample, roughly 10-25 ns, and shows where a patch is
many cheap nodes.

What was tried, each a rebuild and a re-measure against the same old
binary: `next()` out of line (no change — not an inlining effect);
`next()` and `advance()` force-inlined (no change); the sweep list moved
from a heap vector into the node (`9f2b4a8`, about 10-15 points on the
chains — kept); CombinedSource pulling its own inputs the old way (about
10-20 points on the chains — not kept, the sweep is the design). The
generated code for `advance()` is lean: a count load, a two-entry loop
(entry, pointer, null test, call), the virtual `compute()`, the `cur_`
store, the trace check. The remainder is the structure itself: one more
call layer, one more indirection per pin, a loop and a store per node,
where the old `next()` bodies loaded their pins at fixed offsets and
nothing else. The per-tick memo (checkpoint 2.3) replaces this
per-sample path with pulls inside `compute()`, which is also where the
Task 0 trace check leaves; that is where the cost is recovered or the
design is revisited — before 2.3, not after.

Timing before the whole-tree A/B had been built, after the first family
(`4aa668e`), read +24% / +15% on oboe1 / piano_default and prompted
`59d2807`; the +3% / +8% measured after it was against the implementer's
reported number rather than an interleaved A/B, and before most families
were cut. That earlier figure is withdrawn.

Tests: engine_tests 3,581 checks before, 5,823 after (the core, noise,
wavetable, additive, physical, filter and combiner families each added
behaviour tests: prepare and advance counts per pin, Manual-pin pulls,
default and clamp checks, the ShaperSource read-timing check, a
paramMap-on-an-unwired-pin load, a wired pin surviving a preset rebuild);
a per-sample `isfinite` loop in the vibrato test briefly put the count at
53,810 and was folded into one check in the fix wave.

Gates: `python tools/gates.py` (all) at the seal: CTest (engine_tests,
test_figures, the meter and its unit tests) pass; the UI stamp passes;
the null gate reads 80/80; the wiring null gate reads 80/80 after
conversion (28 patches carried a paramMap). The two roundtrip gates fail
on exactly the backlog-79 set recorded before the checkpoint — the six
loop and double-advance repro baselines and the two perform/wiring
baselines, 8 render diffs, 0 id changes, same names, same count.

## 6. Decisions and rejected alternatives

Each of these is a `Ruling:` in the plan's ledger, in the order made.

1. **Work on main, no worktree.** Phase 1 lands checkpoints on main
   (checkpoint 0 did); the in-tree build would need a full rebuild in a
   worktree. Gates before every commit instead.
2. **A pin registers in its own constructor body, after its fields are
   initialised.** The plan had `PinBase`'s constructor register, which
   would read `Param::constant` uninitialised and call a pure virtual
   `SettingBase::descriptor()` from a base constructor. Lesson 1.
3. **Nodes are neither copyable nor movable.** A copy's pins would not
   re-register. Assignment is allowed and leaves the registry alone,
   which is what `Envelope::replace_stages` needs. The factories that
   returned an Envelope by value return a shared pointer.
4. **`next()` stayed virtual, with the sweep as its default body, until
   the last family; the close sealed it.** The plan's non-virtual
   `next()` would have made the tree unbuildable until all 71 types were
   cut at once, which defeats one family per commit under the gate. The
   by-name surface stays virtual until Part 3 replaces the structural
   inputs with `Slot<T>`.
5. **`Pull::Manual`.** A pin the base must not touch — a trigger chain,
   a pin pulled only in one mode or only between gaps, a pin pulled
   once per table entry at note start — is declared Manual; the node
   prepares and pulls it exactly where the old code did. Declared in the
   OLD DESCRIPTOR order (next item) and pulled in the old advance order.
6. **Declaration order is also the loader's wiring order**, and the first
   consumer of a shared source resolved becomes its advancer while later
   ones get a RefSource. Found by the wavetable family's A/B (BitRotate).
   So an Auto pin set whose old descriptor order differs from its old
   advance order keeps the advance order (the brief's rule) and is
   listed: WaveSource's trio, AllpassResonator, KSString, DelayFilter,
   HammerBank, DistortedSource, SegmentSource. A patch that wires one
   source to two such pins of one node changes by a sample of lag.
   Scanned: 54 nodes in 3,137 patch files share a source across two of
   their own pins; none of them is in those types; the two in the
   manifest (fadd_explicit_test, fadd_sequence_test, both Partials) are
   identical under the gate.
7. **Read timing.** A node that read a pin at advance time (`float d =
   x_->advance()`) now reads after the full sweep. The two differ only
   when an earlier pin is a RefSource and a later pin of the same node
   drives the real source. Rule: a family's A/B over the stored patches
   that use the type decides; identical means the type joins the
   tolerated list (Velvet, Perlin, Murmuration, BowTable, DelayLine,
   Mesh2D, PierceFilter, KSString, the BW filters, SVF, Limiter, Reverb,
   HammerBank, DelayFilter), a difference means the type keeps its old
   timing through Manual pins. ShaperSource differed in 28 stored
   patches (the whole feedback_valve2 audition queue) and keeps its
   timing exactly. The memo (2.3) makes every read same-tick and retires
   the distinction.
8. **The sweep walks pointers to the wired Auto pins' source members**,
   cached at registration, not every pin through a virtual call —
   measured, section 5.
9. **The descriptor default wins over the constructor default**, as the
   plan says; a stored patch that relied on the constructor's value gets
   the value written into its JSON (only the omitted keys) rather than a
   manifest refreeze — a regression baseline's frozen render is the
   verified sound, and the patch then says what it meant. Two baselines
   (`8f640e6`) and six gitignored audition patches (fm_matrix2
   t2_11_all_noise_wander_*). Refreezing is the controller's act only.
10. **A Processor may own its primary input** (`Processor(Pull::Manual)`)
    rather than leaving for ValueSource; a node with two inputs neither
    named `source` (CombinedSource, CrossfadeSource) or a list-only input
    (MultiSource) derives from ValueSource until Part 3 settles the
    multi-input shape — a Processor's inherited `input` would be a
    phantom registry entry beside them.
11. **`NameGate.in` is an Input, not the Param the table said.** An
    unwired gate must never match, which an Input expresses and a
    Param's constant cannot.
12. **The presets' and Partials subclasses' changed UI surface is
    accepted**: self-registering base pins cannot be dropped by a
    subclass; the preset collapse (Part 3 section 6) and the additive
    reshaping (Part 3 section 7) remove the classes.
13. **`Component::find` uses the pin's kind tag and a static_cast**; the
    one `dynamic_cast` the design keeps is `Slot<T>::set` at wire time.
14. **The helpers** `Param::advance()` / `Input::advance()` and
    `Setting<T>::clamp()` were added when the third copy of each idiom
    appeared.
15. **The per-node render cost is left as measured, not patched.** The
    two design-neutral mitigations that measured (the sweep walks the
    wired Auto pins' source members, `59d2807`; the list lives in the node,
    `9f2b4a8`) are in; the compiler-specific one (`__forceinline`) did
    nothing and is not; giving nodes back their hand-written advance
    lines would undo the checkpoint. Checkpoint 2.3 rewrites the
    per-sample path (pulls inside `compute()`, the trace check gone), so
    that is where this is recovered — or, if the memo's per-pin pull
    costs the same, where the design is revisited with the numbers in
    hand. Matt's call, section 1.
16. **`Setting<int>` truncates and a paramMap target the patch leaves
    unwired binds to the pin's constant** — the two behaviour changes the
    independent review caught (section 8), both restored to the old
    behaviour in the fix wave.
17. **Pins stay public members** (plan-mandated; the refuter's item 5):
    `source`, `constant`, `value`, `values` are the node's declared
    interface, read in `compute()`. What the base needs private is
    (`register_pin`, the sweep list, assignment).

## 7. Debt knowingly left

Found while reading, not fixed, because this checkpoint changes
structure only:

- The by-name surface on Component is still virtual, and six types keep
  a minimal `set_param` / `get_param` override for a structural or list
  input (MultiSource, AdditiveSource2, Formant/FormantSequence,
  FullAdditiveSource, Partials/CompositePartials, WavetableSource);
  WavetableSource and FullAdditiveSource keep a typed pointer beside the
  registry Input, synced only through `set_param`. All of it goes with
  `Slot<T>` in checkpoint 3.1.
- The structural types (the twelve evolution holders, Partials and its
  family, Formant and the spectra, ExpandRuleNode) still derive from
  ValueSource with `compute()` returning 0; Part 3 moves them under
  Component.
- `SharingTrace` and `ValueSource::advance()` are the Task 0
  instrumentation; they go with the memo (2.3).
- `Envelope::get_setting` is hand-written for `sustainLevel` (the UI
  reads it back from the stage slots); `sustainLevel` has two meanings
  (stored for the sustain presets, a live view for a plain Envelope).
  Goes with the preset collapse.
- The registry factory still sets `RangeSource.normalized = true` while
  the descriptor default is now false (patches loaded through the
  registry are unaffected; the UI's new-node default differs). Part 4.
- A paramMap aimed at a pin its patch does not wire now fails the load
  ("neither param nor setting") instead of binding to the default
  constant; no stored patch does this. Part 4 retires paramMap.
- Pre-existing bugs found by the implementers, preserved for
  byte-identity: `PluckEvolutionSource` ignores its constructor seed on
  rebuild; WanderNoise and WanderNoise2 prepare an `amplitude` nothing
  reads; `AdditiveSource2`'s "explicit" mode cannot load (its `partials`
  key collides with the input pin); `ExplicitPartials::init_arrays`
  reads out of range when mult1 is set before mult2; `apply_partials`
  copies a Partials' arrays without forcing its rebuild; a stage-less
  `PhasedValueSource` indexes an empty vector in `compute()`.
- The pre-checkpoint binary rendered
  `patches/sweep/stk_bandedwg_port/stk_bandedwg_bar_struck.json`
  differently in 2 of ~55 runs; the migrated binary was 40/40 stable.
  Possibly an uninitialised read in the old tree.
- Generator scripts that emit RedNoise nodes without density /
  smoothness / rampVariation (`tools/gen_formantseq_sweep.py`,
  `tools/gen_fm_matrix.py`) regenerate differently now.
- `tools/engine_tests/main.cpp` is 4,448 lines in one file with
  mid-file includes; `tools/structure/metrics.json` shows as modified
  after a gates run although nothing in the tools writes it (cause not
  found).
- Docs: `part2-valuesource-contract.md` section 3's "today's equivalent"
  column names `evolutionSrc_` (the pre-checkpoint member; the column
  describes the old code and is left). `part3-nodes.md`'s holder count
  is corrected to twelve in the close commit.
- **The per-node render cost** (section 5): the checkpoint's one open
  behaviour-neutral regression, carried to 2.3 by section 6 item 15.
- `ensure_constant` goes through `Component::param` and so reaches Param
  pins only: a paramMap target that is an Input pin wired to a constant
  in a Multiplex template would load but its per-note push would no
  longer reach the clones (the old `get_param` covered Inputs). No tree
  patch has one (eight Multiplex-plus-paramMap patches, every target a
  Param). Part 4 decides whether Inputs are valid paramMap targets.
- Dated or "used to" comments outside the diff the refuter listed remain
  in `envelope.h`, `envelope_presets.h`, `wave_evolution.h`, `ks_string.h`,
  `partials.h` and the test file; thirteen curve-index `std::clamp` calls
  in `envelope_presets.h` duplicate their declared 0..3 bounds; two
  `advance()` comment lines in `pins.h` exceed 100 columns. Polish for
  the preset collapse and the test split.

## 8. Independent review

A reviewer with a fresh context (Claude Fable 5.1, the most capable model
available), given the 19-commit diff, the plan, the spec sections, the
module map, the ledger and `REVIEW_BRIEF.md`, told to refute. Its
refutation table and findings follow verbatim; what was done about each
FAIL and each Important finding comes after.

### The refutation, verbatim

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Separation of concerns | PASS | Each new file does one job: `core/descriptors.h` (the self-description structs only), `core/pins.h` (five pin kinds), `core/component.h` (registry + by-name access), `core/dsp_value_source.h` (the signal contract + the pin bodies). No file in the diff mixes drawing/audio/persistence/model; the UI diff (`tools/mforce_ui/main.cpp`) is the `PinKind`→`UiPinKind` rename, one `get_frequency()`→`frequency.current()` strip and the `shared_ptr<Envelope>` adaptations. Could have failed on `tools/engine_tests/main.cpp`: 4448 lines with family includes mid-file — one job (tests), but see Minor M11. |
| 2 | Single responsibility | PASS | `Component` = "a node's self-description: the pin registry and what is served from it" (`component.h:14-18`); `ValueSource` = "the signal contract" (`dsp_value_source.h:35-91`); `Generator` adds `range()` (`:94-99`); `Processor` adds the primary input (`:105-110`). Could have failed on `Processor`, which also supplies a default `category()` (Filter) that 9 of its subclasses override — plan-mandated; recorded as M8. |
| 3 | Dependency direction | PASS | New chain `descriptors.h` → `pins.h` → `component.h` → `dsp_value_source.h`, all inside `core` (MODULES.md: core may include nothing outside itself); `pins.h` forward-declares `Component`, `ValueSource`, `RenderContext` and defers `Param/Input::current/advance/prepare` to `dsp_value_source.h` and `PinBase::attach`/`Slot<T>::set` to `component.h`. No include cycle. Could have failed: `pins.h`+`component.h` are not usable without `dsp_value_source.h` (declared-not-defined inline bodies); the only such TU today is the test, which already includes the engine. Recorded as M9. No layer below `core` reaches up. |
| 4 | Duplication | FAIL | (a) `Param::advance()` / `Input::advance()` exist but the idiom `x.source ? x.source->advance() : x.constant` is hand-written at `wave_evolution.h:387,390,477,478,590,593,1243,1244`, `hybrid_ks_source.cpp:39`, `wavetable_source.cpp:30,118` (11 sites). (b) `Setting<T>::clamp()` exists but three identical `fit` lambdas re-implement it (`allpass_resonator.h:61`, `ks_string.h:118`, `mesh2d_source.h:124`), `envelope.h:212-217` inlines `std::clamp(x.value, x.min, x.max)` three times, and `fm_source.h:34` clamps by hand. (c) The Manual-pin prepare idiom `if (x.source) x.source->prepare(ctx, frames)` is written ~40 times because `Param::prepare` refuses Manual pins instead of the base's loop skipping them. (d) Six near-identical preset `on_setting_changed` bodies — pre-existing shape, Part 3 §6 deletes the classes. |
| 5 | Encapsulation | FAIL | Plan-mandated public mutable state: `Param::source/constant`, `Setting::value`, `Array::values`. Callers reach through it where the pin's own API exists: the 11 sites under item 4(a) call `x.source->advance()` on pins held as `const Param&`/`const Input&` — "const" references that drive a source each sample. `Component::register_pin` is public although only `PinBase::attach` should call it; `pulled()` publishes raw pointers to the pins' `shared_ptr` members; `PinBase::operator=` and `Component::operator=` are public, so any same-type node assignment compiles and silently copies pin `name`/`kind` while the registry and the descriptor tables are left untouched — the only legitimate callers are `Envelope::replace_stages` and the UI's `*env = std::move(*envelope_from_preset_json(...))`. |
| 6 | Ownership | PASS | Every pointer member has one clear owner and no cycle is introduced: `Component::pins_` and `pulled_` are non-owning pointers into the owning object's own members (stable for the object's lifetime, assignment-safe because `operator=` leaves them alone); pins own their sources (`shared_ptr`); evolution objects hold `const` references to pins/settings of the holder that owns the evolution (holder members are declared before `evo_`, so initialisation order is right); `WavetableSource::evolution_` is a raw view into the holder kept alive by `evolution.source` or into `ownedEvolution_`. Could have failed: `FullAdditiveSource` keeps `partials_`/`formant_` beside the registry inputs — two owning handles to one part, synced only in `set_param`, and neither can be cleared (pre-existing) — recorded as M7; `Slot<T>` (3.1) is the fix. |
| 7 | Abstraction fit | FAIL | One unruled deviation that changes behaviour: `Setting<int>::from_float` rounds (`pins.h:109`) where the plan wrote `value = T(v)` and 33 of the 34 old `set_setting` sites truncated (the one rounding site was `fm_source.h:96`). Everything else that departs from spec §3 is a recorded ruling with a Part 3 owner and reads as such in code: `Pull::Manual`, `Processor(Pull)`, `CombinedSource`/`CrossfadeSource`/`MultiSource` under `ValueSource` against spec §3's "the combiners are Processors", the protected `Envelope(int, float)` + virtual `sustain_level()` + kept `get_setting` override, `RefSource` still a `ValueSource` (deleted in 2.3). The inheritance/template choices the spec gave reasons for are the ones built: self-registering members, `Slot<T>` declared with the one `dynamic_cast`, Generator/Processor as the two bases, `WaveSource` under `Generator` with `wave()`. |
| 8 | Real-time rule | PASS | `ValueSource::next()` is a loop over a prebuilt vector, one indirect `advance()` per wired Auto pin, one virtual `compute()`; `Param::current()` is a branch and a virtual read; settings and arrays cost nothing per sample; `Component::find` runs only on the wiring path. Every `compute()`/`wave()` in the diff checked for `new`/`make_shared`/`resize`/`push_back`/locks/throws: none new. Could have failed: `Envelope::make_ar/make_adsr/make_adsr_abs` now heap-allocate and the preset `rebuild()`s call them, reachable per note through the `isSetting` push — same class as the pre-existing stage-vector allocation in `replace_stages`, one allocation more (M10). Pre-existing throws/allocations on render paths are spec §4/§6 items for 2.2/2.5 — declined. |
| 9 | Names and comments | FAIL | Dated/changelog comments added or moved by the diff: `envelope.h:41` ("spec 2026-09-20-note-onsets-v2 §6"), `envelope_presets.h:215-217` ("Default 0.1 (was 0 — Matt 2026-08-29)… (verified 2026-08-29)"). History-style narrative added: `envelope.h:185-189`, `dsp_value_source.h:103-104` ("at the moment the old code read it"), `perform_source.h:111-117`, `shaper_source.h:23` ("byte-identical legacy path"), `pins.h:16-18` (the `Pull` comment says Manual is for trigger chains; it now also covers conditional pulls, read timing and wiring order). Names: `hybrid_ks_source.cpp:71` renames a phase-in-cycles local to `partial` — the name now lies; `Component::pulled()` returns the Auto pins' source slots, not what was pulled; `ks_string.h:201` still shadows `d`; `phased_value_source.h:91` dead `olAdj`. Test comments that narrate the migration: `engine_tests/main.cpp:3796-3803` ("the last three pull it themselves" — Wormhole is Auto, Shaper is Manual: wrong), plus "used to advance" wording in the filter/combine test headers. |
| 10 | Tests | PASS | The behaviour the diff introduces is covered and the tests observe it rather than restate it: registry/by-name, sweep + compute, Manual advance, `clamp()`, `Processor(Pull::Manual)`, Envelope Auto vs Manual prepare/advance counts, preset rebuild keeps wired params/levels, per-family declaration order == advance order through `OrderProbe` (physical, filter and combine families), the ShaperSource read-timing contract, the default corrections. Gaps I could have failed on: no test covers the two unintended behaviour changes (I1, I2); no test advances a wired pin after `replace_stages`; `run_vibrato_tests` emits 48,000 `CHECK`s in a loop, inflating the count the ledger reports; the Layered rebuild test only proves non-silence. |

### Findings, verbatim

Critical: none.

Important:

- **I1 — `bind_one` throws on a paramMap target the patch left unwired.**
  `engine/src/patch_loader.cpp:1239-1289`: `get_param(paramName)` now
  returns null for an unwired pin (old nodes held a `ConstantSource(default)`
  member, so it never did), the code falls through to the settings scan and
  throws "neither param nor setting". The generic pass wires present keys
  only, so `paramMap: {"frequency": "osc"}` with `osc.params` lacking
  `frequency` — a legitimate authoring shape — loaded and bound to the 440
  constant before this checkpoint and fails to load after it. Tracked trees
  pass only because all 246 targets happen to name a stated pin.
- **I2 — `Setting<int>` rounds where the plan and the old code truncated.**
  `pins.h:109`; plan Task 1: `value = T(v)`; 33 old sites truncated, one
  rounded (`fm_source.h:96`). A non-integral float reaching an int setting
  now lands on a different integer: the per-note push and any JSON float
  on an int key. Unmeasured, untested, and a behaviour change inside a
  structure-only checkpoint.

Minor (M1–M12): the eleven hand-written advance idioms and the clamp
copies; public assignment on PinBase/Component; `register_pin` and
`pulled()` public; the dated and history-style comments listed under
item 9; the names under item 9; RangeSource's default restated in the
registry factory; two handles to one structural part on the additive
sources (`Slot<T>` retires it); `Processor::category()` = Filter
overridden by 9 subclasses; `pins.h`/`component.h` complete only through
`dsp_value_source.h`; the preset rebuild's one extra allocation; the test
file's size and the 48,000-check loop; a dozen small items (a public
`kMaxCombs`, two redundant `category()` overrides, an unnamed Biquad mode,
two long lines, two stale partials.h comments, comment placement on the
Manual-input constructors, "once per evolve()" where the advance is
conditional, a restated 0.7).

Assessment, verbatim: "With fixes. The design matches what was signed,
the byte-identity discipline is the most careful part of the checkpoint,
and nothing in the base is wrong; but two unmeasured behaviour changes
rode in under a 'none' expectation — the loader's `bind_one` now refuses
a paramMap target on an unwired pin (I1) and `Setting<int>` quietly
changed from truncation to rounding (I2) — and the FAILs on items 4, 5
and 9 are the mechanical fix wave the ledger already queued."

### What was done

- **I2 and the base side of items 4, 5 and 9** — commit `97c5da1`:
  `Setting<int>` truncates (the plan's `T(v)`; FMSource's `oversample` is
  the one site whose fractional handling changes, from rounding to
  truncation; no stored patch or UI path sends a non-integral int).
  `Pull` moved to `PinBase`; `Param::prepare` and `Input::prepare` act on
  the source if there is one while the base's prepare skips Manual pins,
  so a node writes `x.prepare(ctx, frames)` where it wrote the null-checked
  line (the reviewer's stronger form — the base preparing Manual pins in
  its own loop — would change prepare order and is left for 2.2, where
  prepare is being split anyway). `register_pin` private with `PinBase` a
  friend; the sweep list protected and named `auto_sources()`; the
  assignment operators protected. The `Pull` and `Processor` comments say
  what the code does. Tests: truncation; a Manual pin prepared only by its
  node; the Manual input observed through behaviour, not the sweep list.
- **I1 and the node and loader side of items 4, 5, 9, plus the FIX NOW
  minors** — commit `e29b29c`: `bind_one` materialises the
  descriptor-default constant for an unwired Param target and binds to it,
  with a load test; the eleven advance idioms become `x.advance()`, the
  evolution references become non-const, the clamp copies become
  `s.clamp()`, the ~40 Manual-pin prepare lines become `x.prepare(ctx,
  frames)`; the dated and history-style comments rewritten in the present
  tense; the names fixed and the dead local removed; the small items; the
  48,000-check loop folded into one check; a test that a wired pin keeps
  advancing after `Envelope::replace_stages`. Null gate 80/80 and the
  whole-tree A/B 885/885 after the wave. The scoped re-review found
  the loader fix covered the plain wire but not the Multiplex clone
  fan-out (a voice whose top node is a MultiplexSource played at the
  default pitch where it used to throw); round 2 (`091e924`) made it one
  mechanism — `Component::param(name)` and `ensure_constant()` — used by
  the loader and the clone fan-out, with a pitch test that fails on the
  round-1 tree. Re-review round 2: all addressed, no new breakage. Null
  gate 80/80 and the whole-tree A/B 885/885 after each round.
- **Deferred to the backlog** (the reviewer's LATER list, each with its
  owner): the registry factory's `RangeSource.normalized = true` (Part 4);
  the two handles to one structural part and the un-wire that cannot
  happen (`Slot<T>`, 3.1); `Processor::category()` as the family default
  (Part 3 §3 moves classification to the registration site); the preset
  rebuild's allocation (Part 3 §6 deletes `rebuild()`); the test file's
  size and mid-file includes (split per family before Part 3); the
  vacuous Layered rebuild test and the missing noise-family order test
  (with the split); Vibrato's constructor defaults (2.2 removes the rate
  arguments); the AdditiveSource2 / ExplicitPartials tests that route
  around the two recorded bugs (land with the fixes in Part 3); the
  engine_tests C4244 warning (compare warning sets at the next close).
- **Declined to judge, accepted as stated**: the pre-existing throws and
  prepare-time allocations on render paths (2.2, 2.5, 2.6 own them); the
  structural inputs that cannot be un-wired and the three additive bugs
  (Part 3); the stage-less `PhasedValueSource`, the Pluck seed and the
  dead debug locals (pre-existing); WanderNoise's inert `amplitude` (a
  behaviour checkpoint decides); KSString's once-per-note frequency read
  (backlog 26a); `RefSource` cycles at teardown (2.3 deletes RefSource);
  the UI-surface changes ruled in section 6; the Task 0 instrumentation
  (2.3).

## 9. Reading guide

Ten minutes: `pins.h` lines 13-56 (a pin), `component.h` lines 15-19
and 69-95 (the registry), `dsp_value_source.h` lines 35-62 and 94-111
(the contract and the two bases), then `dsp_wave_source.h` whole. Then
one migrated node of each shape: `source/triangle_source.h` (Params and
a bool Setting under WaveSource), `filter/biquad_source.h` (a Manual pin
pulled in one mode), `source/shaper_source.h` (the one type that keeps
its old read timing, and why), `core/envelope.h` lines 40-60 (the
Manual trigger and the four settings). Twenty minutes more: section 6
of this report against the ledger's rulings, and
`docs/architecture/measurements/2026-10-16-sharing-lag.md`, the
measurement checkpoint 2.3 will be judged against.

## 10. C++ lessons

### Lesson 1: a member that files itself with its owner, and what a constructor can see

The problem this checkpoint solves is the one C# solves with an
attribute and reflection: "this class has a member called `frequency`
with default 440; build the table for me." C++ has no reflection, so the
member builds the table itself. `Param frequency{this, "frequency",
440.0f, ...}` passes the owning node's `this` into the member's
constructor, and the member calls `owner->register_pin(this)`.

Where that call goes matters. C++ constructs an object inside out: the
base class first, then the members in declaration order, then the body
of the constructor. The plan had `PinBase`'s constructor do the
registering, but at that moment the derived `Param`'s fields
(`constant`, `min`, `max`) do not exist yet, and a virtual call from a
base constructor dispatches to the BASE's version — in C++ the object
"is" a `PinBase` until the derived constructor begins, so calling the
pure virtual `SettingBase::descriptor()` from there is undefined
behaviour (MSVC aborts with "pure virtual function call"). C# and Java
behave differently here: their virtual calls from a base constructor
dispatch to the derived override, on fields that are still default
initialised. Both languages punish the pattern; C++ does it at the
language level.

So each concrete pin registers in its own constructor body, the last
thing that runs, when every field it describes is set
(`pins.h:48,68,95,122,144`). The same ordering fact is why the pins of a
base class (WaveSource's trio, Processor's `input`) come before the
subclass's in the registry — and why, since declaration order is also
advance order and wiring order, the plan's "declare in the old advance
order" became a rule with two exceptions (section 6, items 5 and 6).

The corollary is why a node is not copyable (`component.h:18`). A copy
would copy the pins' values but the copied pins never register with the
copy; its registry would be empty or, worse, hold pointers into the
original. `= delete` on the copy constructor turns that silent bug into
a compile error, which is what you want from a constraint. Assignment
is different: both objects already have their own registries, so
`Component::operator=` deliberately copies nothing, and the node's own
members assign normally (`envelope.h`, `replace_stages`).

### Lesson 2: the template method, and `override` as an audit

`next()` is now a plain, non-virtual function in the base: advance the
pins, call `compute()`, store the result (`dsp_value_source.h:56-62`).
`compute()` is pure virtual. This is the "template method" pattern (no
relation to C++ templates): the base fixes the skeleton of the
algorithm and the subclass fills in one step. In C# you would write
`public float Next()` as non-virtual and `protected abstract float
Compute()`; Java the same with `final` and `abstract`. The point is that
nobody can replace the skeleton — a node cannot run its own advance
loop, forget a pin, or advance one twice.

The migration used the compiler to prove that. While the families were
cut one at a time, `next()` stayed virtual with the sweep as its
default body, so an unmigrated node's own `next()` still ran. At the
close `next()` lost `virtual`, and every remaining `float next()
override` in the tree became a compile error: `override` on a method
that is not virtual in the base does not compile. The twenty that
remained were the structural parts' fakes, each converted in the same
commit. That is the technique to remember: when you take a hook away,
`override` makes the compiler list every place that still relied on it.

### Lesson 3 (short): `Slot<T>` is a compile-time typed plug

`Slot<IPartials>` and `Slot<IFormant>` are two distinct types. Wiring a
WaveSource into a `Slot<IPartials>` fails at the point of wiring with a
reason, where today one untyped pointer plus a `dynamic_cast` fails
silently and the pin stays empty. The template is instantiated per
interface; the one `dynamic_cast` left in the design is inside
`Slot<T>::set`, at wire time, never on the audio thread. The type is
declared and tested in this checkpoint; the first user is checkpoint
3.1.

## Appendix A: the whole-tree A/B

A binary built from `1b4fe1b` (the commit before Task 1) against the
checkpoint's head, every patch rendered by both, WAV bytes compared.

| set | patches | differ |
|---|---|---|
| patches/library | 80 | 0 |
| patches/baselines | 54 | 0 |
| patches/audition | 708 | 0 (6 before their RedNoise defaults were written in) |
| patches/pending | 43 | 0 |
| patches/sweep, every third | 455 | 0 |
| patches/old, every third | 273 | 4 (all omit the corrected RedNoise keys: fm_matrix t1_05, t1_08, t2_11; formantseq_sweep vowel5_red_walk) |

The six audition patches are `fm_matrix2/{modulated,ramped}/
t2_11_all_noise_wander_*`; each has three RedNoise nodes that omitted
density, smoothness and rampVariation.
