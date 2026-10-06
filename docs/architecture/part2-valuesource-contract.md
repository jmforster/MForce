# Part 2: the ValueSource contract

Status: **signed off by Matt, 2026-10-05.** Written at maximum effort,
one section at a time, each decided with him before the next was
written. Nothing here is built; the implementation plan comes after
Part 3 and the meter. Parent: `target-architecture.md`, section 7.

Sections:

1. Today's contract (facts, from the code)
2. The per-tick memo (decided 2026-10-05: A first, then B)
3. The bases and the pins: Generator, Processor, five member kinds,
   each declared once (decided 2026-10-05)
4. `prepare`: reset versus note length (decided 2026-10-05)
5. The sample rate and the fate of `RenderContext` (decided 2026-10-05)
6. No throw on a render path; a rejected wire says so (decided 2026-10-05)
7. Byte-identity plan: the checkpoints of Part 2 and what each may change

## 1. Today's contract

- **Two calls per pin per sample.** `float next()` advances a node and
  returns its value; `float current() const` reads the value without
  advancing. A consumer calls both on each of its pins
  ([dsp_wave_source.h:33](../../engine/include/mforce/core/dsp_wave_source.h)).
- **Sharing is wiring-order dependent.** When one node feeds several
  pins, the loader hands the real node to the first consumer in wiring
  order and wraps every later consumer in a `RefSource`, which reads
  `current()` without advancing ([patch_loader.cpp:98](../../engine/src/patch_loader.cpp)).
  A wrapped consumer that happens to be evaluated before the advancing
  consumer reads the previous sample. Whether that happens depends on
  the order nodes appear in the file. The review confirmed this (F013).
- **Three mechanisms compensate.** A usage counter decides who advances;
  "starved-ref promotion" ([patch_loader.cpp:150](../../engine/src/patch_loader.cpp))
  re-assigns the advancing role when the advancer is not reachable from
  the output; and the UI mirrors the auto-wrap rule in its own code.
  About 250 lines, per the review.
- **A tap is a guarded read of the last computed value.** A tap edge is a
  `RefSource` with `guard` set: non-finite becomes 0 and the value is
  clamped to ±8. Its one-sample delay is positional: "a tap consumer
  necessarily evaluates before its source in the tick order", so reading
  the last computed value yields last tick's value
  (`docs/feedback_loop_design.md` §2). The engine test pins it as a
  self-referencing counter: `out = 1 + tap(out)` counts 1, 2, 3
  ([engine_tests/main.cpp:202](../../tools/engine_tests/main.cpp)).
- **A node consumed only by taps is never reached by the pull.** The
  loader collects such nodes into a per-voice advance list, ticked once
  per sample after the root pull, in file order (`feedback_loop_design.md`
  §3.3).
- **A cycle is legal only if one of its edges is a tap.** An all-forward
  cycle is unrepresentable in the file and unguarded in the engine.
- **Per-voice clock.** `PerformSource::tick()` is called once per sample
  by the render driver, before the root pull, so bend and wheel move
  during a note. Its leaf adapters (`PerformOut`) are idempotent reads.
- **A known failure class.** `patches/baselines/double_advance_bug.json`
  reproduces a node advanced twice per sample when the sharing rule goes
  wrong; its `_ok` twin differs by one node.

## 2. The per-tick memo

**Decided 2026-10-05: the memo lands with reading A (byte-identical),
then B as its own checkpoint.** Matt's v1 design, described unprompted on
10-03.

### The rule

Every node remembers the tick it last computed and the value it
produced. `next(tick)` returns the remembered value when asked again for
the same tick, and computes otherwise:

```
float ValueSource::next(Tick t) {
  if (t == lastTick_) return value_;      // asked again this tick: serve the memo
  value_ = compute(t);                    // the node's own work; pulls its pins with next(t)
  lastTick_ = t;
  return value_;
}
```

The base class owns `next`. A node implements `compute(t)`. The tick is
the voice's sample index, the clock `PerformSource` already keeps, passed
down the pull.

### What it buys

- **One call per pin per sample** instead of two: `next(t)` returns the
  value. `current()` remains for taps and diagnostics only.
- **Sharing needs no machinery.** Every consumer calls `next(t)`; the
  first call computes, later calls get the memo. A shared node's value no
  longer depends on which consumer the file lists first. `RefSource`
  wrapping, the usage counter, starved-ref promotion and the UI's mirror
  of the wrap rule are deleted.
- **Double advance is impossible by construction.**
- **Cost:** one integer compare per node per sample, in place of a
  virtual call.

### Taps

Taps stay explicit. The tap pin, the tap wire and the cycle-legality rule
are unchanged, and the guard (non-finite to 0, clamp to ±8) stays on the
tap edge. A tap never calls `compute`; it reads the source's memo.

Two readings of "the source's memo", and they differ:

- **A, positional (today's semantics exactly).** A tap reads the value
  as it stands when the tap is evaluated. Inside a cycle entered through
  its forward path that is last tick's value, as today. A tap whose
  source has already computed this tick reads this tick's value, as
  today, and is then no delay at all.
- **B, exact.** Each node also keeps the value of the tick before, and a
  tap reads that. A tap is then always a one-sample delay, which is what
  the design document says it is ("reads what its source computed last
  tick"), regardless of where the output is taken from.

A and B agree everywhere a tap closes a cycle through its forward path.
They differ only where a tap today reads a value its source computed
earlier in the same tick, which happens when the tap is positioned where
a plain wire would have been legal. Whether any library or baseline
patch does that is measurable at load and is measured before the choice
is final.

Order of work: **the memo lands with A**, so the checkpoint is
byte-identical by construction; **B follows as its own checkpoint** with
the affected renders listed, if Matt wants the exact definition.

### The advance list, generalised

After the root pull, the voice calls `next(t)` on every node whose
`lastTick_` is not `t`, in file order. That covers nodes consumed only
by taps, which is what today's advance list holds, and costs nothing for
nodes the pull reached. Today's list is derived by enumerating JSON
edges; this needs no enumeration.

### Cycles without a tap

Still illegal; the builder rejects them. The base defends anyway: if
`compute(t)` is re-entered for the same tick, the node returns its last
value and reports the cycle once, instead of recursing until the stack
overflows.

### What changes in renders

The memo is a structural change for every patch and a behavioural change
for two kinds of patch:

1. A patch in which a wrapped consumer is evaluated before its advancer
   today, and so reads the previous sample. Under the memo it reads the
   current one. This is the wiring-order lag the review called the
   graph's dominant architectural problem; removing it is the point.
2. `double_advance_bug.json`, which exists to reproduce the failure the
   memo makes impossible.

Both are expected differences and are listed in the checkpoint. The
first set is measured before the cut: the current loader is instrumented
to report, per patch, every shared source with a wrapped consumer that
precedes its advancer in evaluation order. The null gate confirms that
only the listed patches change.

### Lessons for a Java / C# reader

- `next(t)` in the base calling `compute(t)` in the subclass is the
  Template Method pattern again, the same shape `WaveSource` uses today.
  The base owns the rule, the subclass owns the work.
- A memo keyed on a tick is how a pull-model graph gets "evaluate each
  node once per step" without a scheduler. The alternative, a scheduler
  that sorts the graph and runs it in order, is what a push-model engine
  does; the memo keeps the pull model and its simplicity.
- Keeping two values per node (this tick, last tick) is the cheapest
  possible unit delay. It is what makes reading B free.

## 3. The bases and the pins

**Decided 2026-10-05:** the self-registering member design, the five
kinds, and the classification rule.

### What a node writes today

Every pin on every node is written in five places: the descriptor table,
the `set_param` chain, the `get_param` chain, the `prepare` forwarding
list and the advance list in `next()`. `WaveSource` does this for three
pins ([dsp_wave_source.h:75](../../engine/include/mforce/core/dsp_wave_source.h));
each oscillator then adds its own. On top of that, 21 node headers carry
109 typed `set_x` / `get_x` accessors that nothing calls. The measured
cost of the five lists is drift: `RangeSource` has three defaults for
one setting and three noise types sound different from the UI than from
the command line because their constructor and their descriptor disagree.

The cause is that C++ has no reflection: nothing can read "this class has
a member called frequency with default 440" off the class. Java and C#
would hang an attribute on the member and let a framework build the
table. The C++ answer is to make the member build the table itself.

### Five member kinds, declared once

A node declares each pin as a member of one of five types, with its name,
default and range in the declaration. The member registers itself with
the owning node when the node is constructed, and the base class does
everything else from that registry.

```cpp
struct SineSource final : WaveSource { ... };        // nothing to add: three pins inherited

struct SVFSource final : Processor {
  Param cutoff   {this, "cutoff",    1000.f, 20.f, 20000.f, "hz"};
  Param resonance{this, "resonance", 0.5f,   0.f,  1.f};
  Setting<int> mode{this, "mode", 0, kModeLabels};
  float compute(Tick t) override {
    const float x  = input.value(t);        // the signal input, owned by Processor
    const float fc = cutoff.value(t);       // a pin: its source, or its constant
    ...
  }
};
```

| Kind | Holds | `value(t)` | Today's equivalent |
|---|---|---|---|
| `Param` | a `ValueSource*` or a constant, plus default and range | the source's `next(t)`, or the constant with no virtual call | a `shared_ptr<ValueSource>` member defaulted to a `ConstantSource` |
| `Input` | a signal input: a `ValueSource*`, required or optional | the source's `next(t)`, or 0 | `source_` on 21 types |
| `Slot<T>` | a structural input: a part that implements interface `T` (`IPartials`, `IFormant`, an evolution holder) | not a value; the node calls `T`'s methods | `partials_`, `formant_`, `evolutionSrc_` with a `dynamic_cast` at wire time |
| `Setting<T>` | a scalar configuration value (float, int, bool), not connectable | the value | `set_setting` chains |
| `Array` | a user-editable float vector | the vector | `set_array` chains |

`Slot` may be a list (`Slots<T>`) for pins that accept several
connections, which is today's `add_param` on `MultiSource`, `Partials`
and the formant spectra.

What the base class provides from the registry, with no per-node code:

- the four descriptor spans (`param_descriptors` and friends), served
  from the registered members;
- `set_pin(name, source)`, `get_pin(name)`, `set_setting`, `set_array`
  by name;
- forwarding `prepare` and `reset` to every pin's source;
- pulling: nothing to forward, since a node reads its pins inside
  `compute(t)` and each pin pulls its own source with `next(t)`.

A `Slot<T>` is type-checked when wired. Wiring a `WaveSource` into a
`Slot<IPartials>` fails with the reason ("partials expects a Partials
node"); today the same wire is silently accepted and the pin stays empty
(the review's F164, and `Parked.txt` item 5).

### The two bases

```
                 ValueSource          next(t) memo; compute(t); the pin registry;
                      │               descriptors; reseed; reset (section 4)
          ┌───────────┴───────────┐
      Generator               Processor
      range(): Bipolar,       Input input;            the primary signal input
      Unipolar or None        phase_delay_at(hz);     default 0
                              extra Inputs declared
          │                   by the subclass
      WaveSource
      Param frequency, amplitude, phase;
      the phase accumulator; wave(pos) hook
```

- **`Generator`** adds one declaration, its unit range, and reserves the
  place where the queued universals (min, max, density, gain) would act
  on the memoised value. Nothing of those is built.
- **`Processor`** owns the primary input and its plumbing. Combiners and
  Multi declare extra `Input`s or an input list; the base pulls none of
  them, since a processor reads its inputs inside `compute(t)` like any
  pin.
- **`WaveSource`** keeps what it has, under `Generator`; its
  `compute_wave_value()` becomes `wave(pos)` and its `next()` becomes
  `compute(t)`.

### Which base each node gets

The rule: a node with a signal input is a Processor; a node that produces
a signal from time and parameters alone is a Generator.

- Generators: every oscillator (via `WaveSource`), every noise type, the
  wavetable, the additive sources (their `partials`, `formant` and
  `spectra` are structural inputs, not signals), `Envelope`,
  `PhasedValueSource`, `SegmentSource`, `Vibrato` (an LFO), `CurveNode`
  when it reads a param, `PerformOut`, `NameGate`, the constants and
  `VarSource`.
- Processors: the filters, `Reverb`, `Limiter`, `HammerBank`,
  `ShaperSource`, `DistortedSource`, `BowTableSource`, `DelayFilter`,
  the combiners (`CombinedSource`, `CrossfadeSource`, `MultiSource`,
  `MultiplexSource`), `RepeatingSource` and `WormholeSource` (they wrap a
  source), and the resonators (`KSString`, `Mesh2DSource`,
  `DelayLineSource`, `AllpassResonator`, `PierceFilterSource`): they
  process an excitation and carry a frequency param, like a filter
  carries a cutoff.
- `RefSource` has no base: it is deleted (section 2).

### What this does not change

Numerics. `pin.value(t)` on a wired pin calls the same `next` the node
called before; on a constant it returns the same number the
`ConstantSource` returned. The checkpoint that introduces the pin types
is byte-identical and the null gate proves it.

### Alternatives considered

- **Per-type static tables with pointers to members** (zero cost per
  instance, built at compile time). Rejected for now: it needs the base
  to be a template over the derived type (CRTP) or type-erased accessors,
  which is more C++ machinery than the problem needs; the per-instance
  registry costs one small vector per node, built when the patch is
  built, never on the audio thread.
- **A macro that expands one pin declaration into the five lists.**
  Rejected: it hides the five lists instead of removing them, and macros
  are what nobody wants to read in a public repo.

### Lessons for a Java / C# reader

- A member that registers itself with its owner in its constructor is
  the C++ stand-in for an attribute plus reflection. The order of member
  declaration is the order of registration, which is why the descriptor
  tables come out in declaration order for free.
- `Slot<T>` is a template: `Slot<IPartials>` and `Slot<IFormant>` are two
  distinct types the compiler checks, where today one untyped pointer and
  a `dynamic_cast` do the job at run time and fail silently.
- "The base pulls nothing; the node reads its pins in `compute`" is the
  same discipline as today's `WaveSource::next` reading its three pins,
  made the rule for every node.

## 4. `prepare`: reset versus note length

**Decided 2026-10-05:** `prepare(ctx)` may allocate; `start(frames)` may
not.

### What `prepare(ctx, frames)` does today

One call, three jobs:

1. **Set up for the sample rate:** allocate buffers, compute coefficients.
   Some nodes do this in the constructor instead, a few in `prepare`
   (Vibrato rebuilds its whole LFO sub-graph with about ten allocations
   on every note; the review's F048).
2. **Reset state for a new note:** phase accumulators to zero, delay lines
   and filter state cleared, random streams re-anchored.
3. **Lay out timing over the note's length.** Eight node types use
   `frames` for more than forwarding it: `Envelope` (stages as a fraction
   of the note), `FormantSequence`, `CrossfadeSource` and
   `PhasedValueSource` (their stages over the note), `Partials` (motion
   timing), `MultiSource`, `Vibrato` and `RepeatingSource`.

Because the three are one call, a host that renders in blocks would have
to call it per block and restart every envelope; today's mixer path does
exactly that, re-preparing the whole graph on every `render()` (F260).
And `RepeatingSource` calls it from inside `next()` at every repetition
boundary (F219), which drags job 1 onto the audio thread.

Live notes already live with a note of unknown length: the UI passes a
nominal length (two seconds) and gates the envelopes so they hold at
sustain until key-up, when `gate_release` re-lays the release.

### The split

```cpp
void prepare(const RenderContext& ctx);   // job 1: once per build, and again if the host changes the rate
void start(int frames);                   // jobs 2 and 3: a span of `frames` samples begins now
```

- **`prepare(ctx)`** is where the sample rate arrives (section 5 decides
  whether it also arrives anywhere else). It may allocate. It is called
  when the instrument is built, never per note and never on the audio
  thread.
- **`start(frames)`** resets the node and lays out anything that depends
  on the length. It may not allocate. The base forwards both calls to
  every pin's source from the registry (section 3), so a node overrides
  only the one it needs.
- **Continuation stays at the voice level.** A held line's next note
  reaches nodes exactly as it does today: `set_note` on the voice's
  `PerformSource`, triggers fired, `gate_release` on envelopes at line
  end. No node-level "continue" is needed.

### What follows

- **Block streaming becomes possible** without touching any node: a
  plain sound is `prepare` once, `start` once, then blocks of `next(t)`.
  That is the shape a plugin host wants (JUCE's `prepareToPlay` and
  `processBlock`) and it retires the per-render re-prepare.
- **`RepeatingSource` calls `start(repFrames)` on its child at each
  boundary,** which is now legal on the audio thread by contract.
- **Vibrato's rebuild moves to `prepare(ctx)`**, once.
- **The allocation test asserts both:** no allocation in `start` or in
  `next`, over every baseline patch. That is campaign 1's test, with a
  second entry point to cover.

### Byte-identity

`start(frames)` does exactly what the per-note part of
`prepare(ctx, frames)` did; the numerics are unchanged, and the null gate
proves it per node family as the split lands. One class of node needs a
look: a node that today rebuilds state in `prepare` with fresh random
draws per note would, after the split, reset that state instead. If the
rebuilt state and the reset state differ, that patch's render changes
and is listed.

### Lessons for a Java / C# reader

- Two-phase set-up is familiar from any audio framework: JUCE calls
  `prepareToPlay(sampleRate, blockSize)` once and `processBlock` many
  times, and a note is an event inside a block. The old single call
  conflated "the format is known" with "a note begins".
- "May allocate" versus "may not allocate" is the whole real-time rule in
  two words, and attaching it to method names makes it checkable: the
  test wraps `start` and `next` and fails on the first `new`.

## 5. The sample rate and the fate of `RenderContext`

**Decided 2026-10-05:** the rate arrives only through `prepare(ctx)`;
constructors and the factory lose it; the file's rate is a render default.

### Where the rate lives today

- **In constructors.** 44 constructors in 29 files take a sample rate,
  and the registry's factory signature passes one: `create(type, rate,
  seed)`. Nineteen node files keep their own copy.
- **In `RenderContext`,** which five nodes read at `prepare`.
- **In the patch file,** as a top-level `sampleRate`: 132 of the 134
  tracked patches say 48000, two say 22050 (the STK validation cells were
  made at their models' native rate).
- **Pinned in the UI.** Every node the UI builds is made at 48000, every
  save writes 48000, and the device is opened at 48000.

So the same number is written in four places and they agree only because
every path passes 48000. Backlog 69 records the consequence for the day
that stops being true: a handful of per-sample constants are 48k-baked
(`KSString`'s damping, an envelope follower, the lip's one-pole, the
noise family's step odds) and would change character at any other rate.

### The rule

**The sample rate is a property of where the sound is rendered, not of
the patch or the node.** It arrives through one door:

```cpp
void prepare(const RenderContext& ctx);   // ctx.sampleRate: the only way a node learns the rate
```

- **Constructors and the factory lose their rate argument.** A node is
  built without knowing the rate; `prepare(ctx)` is where it computes
  rate-dependent coefficients and sizes rate-dependent buffers, which is
  the phase that may allocate (section 4).
- **`RenderContext` stays**, as the vehicle: one field today, and the
  place a host's other facts (block size, say) would go later without
  touching 85 signatures. It is created in one place, where the
  instrument or the plain sound is prepared, instead of forty.
- **The patch file's `sampleRate` becomes a render default:** the rate
  the command-line tool uses for that file when none is given. The UI
  renders and plays at the device's rate, which it already does, except
  that it now tells the engine so rather than assuming 48000 in three
  constants.
- **Backlog 69 stays a list of constants to sort,** physical versus
  normalised, before any non-48k work. This section moves where the rate
  arrives; it does not touch those constants.

### Byte-identity

Nothing changes numerically: the same rate reaches the same code before
anything is computed, because `prepare` precedes `start` and `next`. A
node that today computes a coefficient in its constructor computes the
same coefficient in `prepare` from the same number.

### Lessons for a Java / C# reader

- "Where does this value come from?" with four answers is a smell in any
  language. The fix is not a global (that was `MConfig.VALUES_PER_SECOND`
  in the C#), it is one parameter on the one call that needs it.
- A context object with one field is still worth keeping when it is the
  door new host facts will come through. The alternative, adding a
  parameter to `prepare` later, means editing every node.

## 6. No throw on a render path; a rejected wire says so

**Decided 2026-10-05**, with the sign-off of the whole part.

### Throws today

Exactly two are reachable from a render path:

- `WaveSource::next` throws on a non-positive frequency
  ([dsp_wave_source.h:38](../../engine/include/mforce/core/dsp_wave_source.h)).
- `WavetableSource::fill_table` throws on an invalid frequency, from
  inside `next` ([wavetable_source.cpp:23](../../engine/src/wavetable_source.cpp)).

Nothing between them and the audio thread catches. A modulator that
dips the frequency to zero, or a bend that overshoots, ends the process
from the audio callback. Four reviewers found this independently.

Every other throw in the engine is at load, build or performer time
(JSON readers, `PitchCurve::validate`, the loader), where throwing is the
right tool.

### The rule

- **`start` and `next` never throw.** An invalid runtime value is clamped
  to the node's declared range (a frequency below the minimum becomes
  the minimum) and the node sets a fault flag. Rendering continues.
- **No I/O on a render path.** The flag is a per-node bit, set on the
  audio thread without locking (single writer), and the control side
  collects faults after a render or a buffer and reports them once: the
  UI's status line, the command-line tool's summary. Today `onset_id`
  prints from the UI thread at note set-up, which is fine and stays.
- **`prepare` may throw**, since it runs where the instrument is built.

### A rejected wire says so

Today `set_param` returns `void` and ignores an unknown name or a wrong
type (F011, F012). A typo in a patch, or an oscillator wired into a
`partials` slot, renders silently wrong, and three loader paths swallow
the result.

With the pin registry (section 3), the base knows every pin by name and
kind, so:

- `set_pin(name, source)` fails on an unknown name or a kind mismatch,
  with the reason and the node's id: "node `osc1` has no pin `freq`",
  "`partials` on `add1` expects a Partials node, got SineSource".
- The builder turns that into a load failure, not a warning. A patch
  either builds completely or not at all. (Whether a file with unknown
  keys elsewhere is rejected or preserved is Part 4's decision; this
  section covers wires.)
- The lint tool already in `tools/` can pre-check a folder of patches,
  so the sweep and audition folders are not a surprise at load.

### Byte-identity

Unchanged for every valid patch. Two things differ, both improvements
and both listed: a patch that today terminates the process on a zero
frequency now renders with the frequency clamped and reports a fault;
and a patch that today renders silently wrong because of a bad wire now
fails to load with the reason. The null gate's own run finds the second
set.

### Lessons for a Java / C# reader

- In Java or C# an uncaught exception on a worker thread is logged and
  the thread dies; in C++ an exception with no handler calls
  `std::terminate`, which ends the process. That is why the rule is
  absolute on the audio thread and why a flag replaces the throw.
- "Fail at build, never at run" is the same instinct as validating a
  configuration at start-up: the expensive, loud check belongs where it
  is cheap and someone is watching.

## 7. Byte-identity plan

Part 2 lands as checkpoints in this order. Each ends with the gates green
(null gate, engine tests, the UI's headless checks), a commit, the
independent review and an architecture report with the meter's numbers.

| Checkpoint | What lands | Expected render changes |
|---|---|---|
| 2.1 | The pin registry: five member kinds, descriptors and set/get from the registry, the 109 dead accessors deleted; `Generator`, `Processor`, `WaveSource` under `Generator` | none |
| 2.2 | `prepare(ctx)` / `start(frames)` split; the rate arrives through `prepare` only; constructors and the factory lose it; `RenderContext` created in one place | none, unless a node that re-draws randomness in `prepare` per note turns out to differ from a reset; any such patch is listed |
| 2.3 | The per-tick memo, taps reading A; `RefSource`, the usage counter, starved-ref promotion and the UI's wrap mirror deleted; the generalised advance sweep | the wiring-order lag patches, measured beforehand with the instrumented loader, and `double_advance_bug` |
| 2.4 | Taps reading B | only patches where a tap today reads a same-tick value, measured beforehand; may be none |
| 2.5 | No throw on a render path; fault flags; `set_pin` failures become load failures | none for valid patches; a zero-frequency patch renders instead of terminating; bad-wire patches fail to load, listed |
| 2.6 | The allocation test over every baseline patch, wrapping `start` and `next` | none; the test is the new gate |

The order is chosen so that each behavioural change is isolated in its
own checkpoint with its own measured list. 2.1, 2.2 and 2.6 are pure
structure. The two measurements (the lag list for 2.3, the same-tick tap
list for 2.4) are taken with today's loader before any of this is cut.
