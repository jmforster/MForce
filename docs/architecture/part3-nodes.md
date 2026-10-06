# Part 3: nodes

Status: **draft for Matt, 2026-10-05.** Written at extra-high effort.
Nothing here is built. Parent: `target-architecture.md`, section 7.
Builds on Part 2 (the contract, the bases, the pins).

Sections:

1. Today, from the code
2. The root: `Component` (proposed)
3. The families (proposed; the table is Matt's to edit)
4. Shared DSP primitives in `core/dsp`
5. The evolution holders disappear
6. The envelope presets become one type; the `KSPianoString` alias goes
7. The additive classes
8. One type per file
9. Real-time fixes, node by node
10. Checkpoints

## 1. Today, from the code

- **85 registered types** in eight categories, and the categories are
  wrong in places: ten of the thirteen evolution holders are registered
  as Combiners and two as Generators; `KSString`, the delay line, the
  allpass resonator and the mesh are Oscillators. The create menu is a
  separate hand-written list that disagrees with the categories
  (section 3.2 of the target architecture).
- **Twenty-four types produce no signal.** The thirteen evolution
  holders, the four `Partials` types and `ExpandRule`, the five formant
  and spectrum types, and `CompositePartials` all return 0 from `next()`
  ("exists for graph wiring only") and are ValueSources only so that the
  graph and the UI can hold them.
- **The thirteen evolution holders** (wrappers that make an evolution
  algorithm wireable as a node) are copies of one forty-line shell:
  type name, category, `get_evolution`, a settings table, `set_setting`
  that rebuilds the evolution object, `get_setting`, and three no-op
  overrides. About 950 of `wave_evolution.h`'s 1,552 lines.
- **DSP primitives written per node:** the band-limited step twice
  (`SawSource`, `PulseSource`); the first-order tuning allpass in the
  wavetable twice, the allpass resonator and the EKS evolution; the DC
  blocker in the allpass resonator and twice in `KSString`; the
  bow-friction table in `BowTableSource`, `KSString` and the
  bowed-string evolution; biquad coefficient recipes in `KSString`, the
  allpass resonator, the brass evolution, `BiquadSource` and the mesh;
  the one-pole time constant in `HammerBank`, `Limiter`, `KSString`;
  and the Butterworth low-pass and high-pass sections as copy-pasted
  pairs in `filters.h`.
- **Six envelope preset classes** (`AR`, `AS`, `ASR`, `ADS`, `ADR`,
  `ADSR`) repeat seventy lines each of stage dispatch, while `Envelope`
  itself already has a `preset` setting. In the active patch folders
  1,126 nodes are plain `Envelope` and 176 use a preset class.
- **`KSPianoString`** is a registered alias of `KSString`; 116 nodes on
  disk use the alias, 3 the real name.
- **Files over 400 lines in the node folders:** `wave_evolution.h`
  1,552; `partials.h` 1,486 (`Partials` alone is 800 lines with about 85
  data members); `ks_string.h` 638; `noise_sources.h` 536; `filters.h`
  457.
- **Real-time violations:** the wavetable allocates its table on the
  first sample of a note; the hybrid KS runs an allocating DFT in the
  render path; `HistogramEqualize`, `TargetEvolution`, `BezierPull` and
  `CellularAutomaton` allocate per table cycle; EKS allocates and frees
  a table per note.

## 2. The root: `Component`

**Proposed.** Part 2 gave signal nodes two bases, Generator and
Processor. The twenty-four structural types fit neither: a `Partials`
or an evolution is a part that plugs into a slot, not a signal. Today
they fake being signals.

Your C# had the shape already: `MComponent` was the root, and
`ValueSource` derived from it. So:

```
                     Component           self-description: the pin registry (Part 2 §3),
                         │               type name, family; what the registry creates
                         │               and the graph holds
            ┌────────────┴────────────┐
       ValueSource                  the parts
       next(t), compute,            Partials, Formant, the spectra,
       prepare, start               ExpandRule, the evolution holders:
            │                       each implements its interface
     ┌──────┴──────┐                (IPartials, IFormant, IEvolutionHolder)
 Generator     Processor            and nothing of ValueSource
```

- The registry creates `Component`s. The graph holds `Component`s. A
  wire into a `Slot<IPartials>` is type-checked against the interface,
  as Part 2 decided; whether the thing is also a ValueSource no longer
  matters, because it is not.
- The parts lose their fake `next`, `current`, `prepare` and `start`.
- `ValueSource` keeps exactly the signal contract.

Name: `Component`, because it is yours. It is the one place a name from
the C# comes back unchanged.

## 3. The families

**Proposed; the table is yours to edit in place.** One classification,
declared where a type is registered; folder, colour and the create menu
follow from it (target architecture, section 3.2).

| Family | Base | Members |
|---|---|---|
| Oscillators | Generator (`WaveSource`) | Sine, Saw, Triangle, Pulse, FM |
| Noise | Generator | White, Pink, Red, Layered Red, Blue, Violet, Velvet, Perlin, Crackle, Murmuration, Wander 1, Wander 2, Wander 3 |
| Wavetable | Generator; parts | Wavetable, Hybrid KS; parts: Pluck, Averaging, EKS, Reed, Bowed String, Brass, Reaction-Diffusion, Sort Erosion, Cellular Automaton, Histogram Equalize, Bezier Pull, Bit Rotate, Target (new node) |
| Additive | Generator; parts | Additive; parts: Full Partials, Sequence Partials, Explicit Partials, Composite Partials, Expand Rule, Formant, Formant Spectrum, Fixed Spectrum, Band Spectrum, Formant Sequence |
| Physical | Processor | KS String, Delay Line, Allpass Resonator, Mesh 2D, Pierce Filter, Bow Table |
| Filters | Processor | BW Lowpass, BW Highpass, BW Bandpass, SVF, Biquad, Delay, Reverb, Limiter, Hammer Bank, Distortion, Shaper |
| Envelopes | Generator | Envelope (one type; AR, AS, ASR, ADS, ADR, ADSR become its presets), Phased, Segment, Vibrato |
| Modulators | mixed | Var (Generator), Range (Generator, until retired), Curve (Processor), Name Gate (Generator), Repeating (Processor) |
| Combiners | Processor | Combined, Crossfade, Multi, Multiplex |
| Utility | mixed | Constant, Static Var, Static Range, Wormhole (a pass-through for the editor) |

Notes on the placements that moved:

- "Physical" is today's menu family "Loop", renamed because the members
  are physical-model building blocks; the loop itself is wiring. If you
  prefer "Loop", change the word.
- Shaper and Distortion move from "Loop" and "Generators" to Filters,
  since both process a signal.
- Vibrato moves from Envelopes to... it stays under Envelopes as a
  modulation generator; say so if you want it under Modulators.
- The six preset envelopes disappear from the menu as types and reappear
  as presets of Envelope (section 6).

## 4. Shared DSP primitives in `core/dsp`

Each primitive becomes a small struct with no virtual functions, held by
value inside the nodes that use it, with `set(...)` for its coefficients
and `process(x)` for one sample. Composition, not inheritance: a KS
string *has* a DC blocker, it is not one.

| Primitive | Written today in | Becomes |
|---|---|---|
| Band-limited step (BLEP) | saw, pulse | `Blep` |
| First-order tuning allpass (Thiran) | wavetable ×2, allpass resonator, EKS evolution | `TuningAllpass` |
| DC blocker | allpass resonator, KS string ×2 | `DcBlocker` |
| Bow friction table (Friedlander) | bow table, KS string, bowed-string evolution | `BowFriction` |
| Biquad coefficient recipes (RBJ) | KS string, allpass resonator, brass evolution, biquad, mesh | `Biquad` with named recipes |
| One-pole time constant | hammer bank, limiter, KS string | `OnePole` |
| Butterworth section | low-pass and high-pass pairs in `filters.h` | one `ButterworthSection` with the pole type as a parameter |

Byte-identity: the copies must be compared before they are merged. Where
two copies compute the same formula the merge is exact. Where they differ
(a different DC-blocker corner, say) the primitive takes the difference as
a parameter, so each node keeps its number, and the report says so. No
node changes sound in this section.

## 5. The evolution holders disappear

**Why they are called holders.** An evolution (`PluckEvolution` and the
rest) is the algorithm: a `WaveEvolution` whose `evolve(table, index)`
the wavetable calls every sample. It has no pins and no descriptors; it
is not a node. To wire one in the editor, each was wrapped in a second
class (`PluckEvolutionSource`) whose only job is to hold the evolution
and show its settings as a node; the wavetable `dynamic_cast`s its
`evolution` input to `IEvolutionHolder` and asks the wrapper for the
object. Two classes per evolution.

The wrapper exists because the only kind of node was ValueSource and
nobody wanted `next()` and `prepare()` on an algorithm object. With
`Component` as the root (section 2) that reason is gone:

- Each evolution is a `Component` that implements the `WaveEvolution`
  interface directly. Its settings are declared once as Part 2's
  members and read where they are used; today they are baked in at
  construction and the wrapper rebuilds the object on every change.
- `IEvolutionHolder` and the cast are deleted. The wavetable holds a
  `Slot<WaveEvolution>`, type-checked at wiring.
- The string-typed loader path was the only other place that built an
  evolution directly, and it is deleted (target architecture, section 5).
- `TargetEvolution` is registered like the others, which is what makes
  it reachable.

Thirteen classes replace twenty-six. (An earlier draft proposed a
template over the wrapper; that would have kept both layers and only
removed the copies.)

Lesson for a Java / C# reader: wrapping an object to give it an
interface it was never written for is the Adapter pattern, and it earns
its place when you cannot change the wrapped class. Here we own both
classes, so the adapter is a layer with no job.

## 6. The envelope presets become one type; the alias goes

- The six preset classes are deleted. `Envelope` already builds its
  stages from a `preset` setting, and 1,126 nodes on disk use it that
  way; the 176 nodes that name a preset class are migrated on disk to
  `Envelope` with the matching `preset`, the same way paramMap is.
- `KSPianoString` is deleted as a name; the 116 nodes that use it are
  migrated to `KSString`.

Both migrations are byte-identical by construction (same stages, same
class) and the null gate proves it.

## 7. The additive classes

- `BasicAdditiveSource` and `AdditiveSource2` are deleted (decided,
  target architecture section 5); the one baseline patch on
  `AdditiveSource2` is migrated.
- `AdditiveSource` is a Generator with `Slot<IPartials>` and
  `Slot<IFormant>`.
- `Partials` (800 lines, about 85 members, seven responsibilities per
  the review) is split into files first (base, Full, Sequence, Explicit,
  Composite, ExpandRule), which is mechanical. Splitting the class
  itself by responsibility (layout, the motion layer, the shimmer layer,
  expansion) is its own checkpoint, designed when the files are apart
  and the duplication between the motion and shimmer layers is visible.

## 8. One type per file

The rule for the node folders: one type per file, the file named after
the type, grouped in a folder per family. In particular:

- `wave_evolution.h` becomes the `WaveEvolution` base, the `EvolutionNode`
  template, and one file per evolution.
- `noise_sources.h` becomes one file per noise type; the shaping block
  copied between white and red noise goes to one place.
- `filters.h` becomes the Butterworth section, the three Butterworth
  filters, and the delay filter.
- `partials.h` as in section 7.
- `ks_string.h` is one class and stays one file; its four mechanisms are
  a later question.

## 9. Real-time fixes, node by node

Under Part 2's rule (`prepare` may allocate, `start` and `next` may not)
and its allocation test:

- Wavetable: the table is sized in `prepare`, not on the first sample.
- Hybrid KS: the DFT scratch is sized in `prepare`.
- `HistogramEqualize`, `TargetEvolution`, `BezierPull`,
  `CellularAutomaton`: their per-cycle vectors become members sized in
  `prepare`.
- EKS: its per-note table becomes a member sized once.
- `RepeatingSource` and Vibrato: already fixed by the `prepare` / `start`
  split.

None of these changes a sample. The allocation test is what proves it.

## 10. Checkpoints

| Checkpoint | What lands | Expected render changes |
|---|---|---|
| 3.1 | `Component` root; the parts leave `ValueSource` | none |
| 3.2 | Families declared at registration; folders; the create menu generated; colours through `Theme` | none |
| 3.3 | `core/dsp` primitives, merged one at a time with the copies compared first | none; a differing copy becomes a parameter |
| 3.4 | Each evolution becomes a `Component`; the thirteen wrappers and `IEvolutionHolder` deleted; `TargetEvolution` registered; the string-typed form deleted and two patches converted | none |
| 3.5 | Envelope presets collapsed; `KSPianoString` migrated | none; type names change on disk |
| 3.6 | One type per file | none |
| 3.7 | The real-time fixes | none; the allocation test goes green for these nodes |
| 3.8 | `Partials` split by responsibility | designed then; byte-identical target |
