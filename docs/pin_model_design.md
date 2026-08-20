# The pin model — what a node setting can be driven by

Status: design, awaiting Matt's review · 2026-08-19
Companion evidence: `docs/config_pin_census.md`
Consumed by: `docs/perform_source_design.md` §7 P2

## 1. Decision summary

- A node's settings come in **two kinds that behave differently**, and the
  model now says so: **fixed pins** (pulled every sample) and **dynamic pins**
  (pushed once per note).
- **A dynamic pin belongs to the patch, not to the node type.** A setting has
  no pin until something drives it. This is what makes the model survivable:
  there are 212 float settings across the registry and only 34 are ever driven.
- **Promotion is a user act in the Settings pane**: click the grey pin beside a
  setting, it turns gold, the value widget is replaced by a label, and the pin
  appears on the node face.
- **A dynamic pin's value is set once per note. By definition, not by
  accident.** A per-block tier is a named future (§7), not an omission.
- **Eligibility is by data type: float means promotable.** Audited, not
  assumed — no float setting allocates. Revisit if a non-promotable float shows
  up in real patch work.
- Terminology (§8) is proposed but **not settled**.

## 2. The problem

`ValueSource` declares two separate descriptor sets. `param_descriptors()` /
`input_descriptors()` are connectable — the loader wires a `ValueSource` in and
the consumer pulls it every sample. `config_descriptors()` are plain numbers,
read at construction and updated only by `set_config`.

Nothing in the patch file distinguishes them: both are read out of the same
`params` object (`patch_loader.cpp:165`). Nothing in the UI distinguishes them
either, beyond configs rendering under a "Settings" heading in the Properties
panel (`tools/mforce_ui/main.cpp:6285`) rather than as pins on the node.

The split is not a musical distinction. It is a cost distinction that leaked
into the model: a setting is a config when changing it rebuilds internal state —
filter coefficients, envelope stages — so it cannot be pulled per sample.

That would be tolerable if configs were never driven. They are. Matt's approved
piano baseline drives five of them per note (`KSPianoString.t60`, `brightness`,
`dispersion`, `inharmGain`, `detune`), through `paramMap` curves, and the node
graph shows none of it. The mechanism exists but is invisible, dialog-only, and
expressible in exactly one place — the legacy `paramMap` block that
PerformSource P1 is retiring.

### 2.1 Why "give every config a pin" fails

From the census: 76 registered types, **212 float configs**. The partials family
carries 33, 33 and 29 of them respectively. A `FullPartials` node would wear 33
pins on top of its 8 envelope inputs.

### 2.2 Why that does not matter

Also from the census: across every patch in the tree, only **34 distinct
(type, target) pairs are ever driven**, and the demand is extremely
concentrated — `KSPianoString.t60` in 85 patches, `brightness` 78,
`dispersion` and `inharmGain` 68 each. Meanwhile the whole partials family
contributes six driven targets between them.

The surface is huge; the usage is tiny and stable. So the pin should follow the
usage, not the declaration.

## 3. The model

**Fixed pins** — today's `param_descriptors()` and `input_descriptors()`.
Declared by the type, present on every instance, pulled every sample. Unchanged
by this design.

**Dynamic pins** — a float setting that *this patch* drives. Not declared by the
type. Created by promotion, stored in the patch, pushed once per note.

A node type therefore has a fixed pin set and a *potential* dynamic pin set (its
eligible settings). A node **instance in a patch** has a fixed pin set and an
*actual* dynamic pin set, which is usually empty.

Consequence, stated plainly because it is a real change: two `KSPianoString`
nodes in different patches no longer look the same. That is correct — they are
doing different things, and today the graph hides the difference entirely.

## 4. Eligibility

**A setting is promotable if its type is float.**

The criterion that matters is whether `set_config` can be called between notes
without allocating or restructuring. All 43 `set_config` bodies in `engine/`
were audited, following their call graphs one level (full results in
`config_pin_census.md`):

- Everything that allocates is **int**-triggered: `LayeredRedNoiseSource.count`,
  `AdditiveSource2.partialCount`, and the buffer-sizing family `numBands`,
  `numCombs`, `maxPartials`, `subSteps`, `bins`.
- Float settings that rebuild do **not** allocate. `PluckEvolution.muting` and
  `AveragingEvolution.sampleCount`/`speed`/`decayFactor` reconstruct their
  evolution object, but both constructors are pure member init. `Partials`
  float settings set a dirty flag and defer array rebuilds to `update_arrays()`.
- One case is not *provably* allocation-free: the envelope presets
  (`AREnvelope.attack`, `ASEnvelope.attack`/`sustainLevel` and siblings)
  rebuild via `*static_cast<Envelope*>(this) = Envelope::make_ar(...)`, a
  whole-object assignment carrying a `std::vector<Stage>`. Stage count is
  identical every time, so in steady state the vector reuses capacity. Accepted:
  note-on is not the render loop — `apply_note_bindings` runs before
  `vg.source->prepare`, so a stray allocation there is live-playback jitter, not
  an audio-callback underrun.

No separate eligibility flag is added to `ConfigDescriptor`. If a
non-promotable float emerges from real patch work, add one then — not before.

## 5. The once-per-note contract

**A dynamic pin's value is evaluated once, at note-on, and is constant for the
life of the note.** This is definitional. It is what the gold pin *means*.

Two consequences worth stating so nobody has to rediscover them:

**Anything may feed a dynamic pin, and non-note sources get sampled once.** Wire
an LFO into `t60` and you get one LFO sample per note — a value that varies note
to note and not within a note. Well defined, and surprising the first time.
Today the only thing that ever feeds one is a curve off the note, and it is an
open question (§9) whether that should be enforced or merely expected.

**Silent inertness is the failure mode to guard against.** A driven setting that
the node ignores is this project's recurring bug class. It already exists in the
wild: `KSPianoString` reads its frequency once in `init_note()`
(`ks_piano_string.h:384`) and never again, so a bend curve on a piano patch
moves a number nobody reads, while `WavetableSource` tracks bends per sample
(`wavetable_source.h:105`). Filed as backlog 26. Promotion must not add new
instances of it.

## 6. The interaction

In the Properties panel, every eligible setting gets a small **grey pin** beside
it. Click it:

- the pin turns **gold**,
- the value-entry widget is replaced by a label naming what drives it,
- a gold pin appears on the node face, in a dynamic-pin section below the fixed
  pins.

Clicking again demotes: the wire is removed, the widget returns holding the
value the setting had.

Precedent exists for the widget-to-label half — `mapping_badge`
(`tools/mforce_ui/main.cpp:6114`) already replaces a driven setting's widget
with a `<curve>` label and suppresses editing, because "the mapping stomps the
scalar at every note-on." Promotion generalises that from a dialog-driven
special case into the model.

Structural settings (int, bool, enum) get no grey pin. `CombinedSource.operation`
is a dropdown and stays one.

Gold must be visibly distinct from fixed-pin styling. That is not decoration:
it is what tells you the value is frozen for the note, and it is what will
eventually explain why bend moves a string's pitch but not its decay time.

## 7. Cadence tiers

The model recognises four, of which the engine currently implements three:

| tier | set when | mechanism |
|---|---|---|
| fixed | patch load | scalar settings |
| **per note** | note-on | **dynamic pins — this design** |
| per block | every ~10.7 ms | does not exist |
| per sample | every sample | fixed pins |

The engine has no block cadence at all: its only rhythm is
`prepare(ctx, durSamples)` once per note and `next()` per sample. The 512-frame
block lives solely in the UI's audio callback. Introducing the tier is backlog
26(b) and `perform_source_design.md` §6.6; it is **out of scope here** and this
design does not depend on it.

## 8. Terminology (proposed, NOT settled)

"Config" has no ancestor. Legacy C# carried the distinction in its type
signatures — `SetFrequency(ISingleValueSource)` versus `SetSpeed(float)` — and
needed no collective noun. The C++ port could not use signatures (explicit
registries, no reflection), so the second bucket needed a name for the first
time, and got one describing how the engine handles it rather than what it is.
Legacy's only `config` is `MConfig`, a global sample-rate and range holder.

Proposal, aligning code with what the UI already says on screen:

- `config` → **setting** (`SettingDescriptor`, `setting_descriptors()`,
  `set_setting`). The Properties panel already says "Settings".
- A promoted setting is a **dynamic pin**. A non-promotable one is a
  **structural setting**.

Cost: 3 virtuals, 1 struct, 1 enum, and ~500 occurrences across **29 files**,
of which `partials.h` (96), `wave_evolution.h` (90), `envelope_presets.h` (66)
and `tools/mforce_ui/main.cpp` (53) are the bulk.

**Zero patch files change** — the format has no `config` key; settings are read
out of the same `params` object as pins.

**Almost zero user-facing text.** Corrected 2026-08-19 during execution: an
earlier draft of this section claimed *zero*, which was wrong. The Mappings
dialog annotates a target with `"  (config)"` in two places
(`tools/mforce_ui/main.cpp:4775` and `:4970`), so the word does reach the
screen and becomes `"(setting)"`. The Properties heading was already
"Settings".

No rendered audio changes, so the null gate holds the whole rename honest for
free — and a missed identifier is a compile error, not a silent bug.

Deliberately separable: the rename can land before, after, or never, without
affecting anything else here.

## 9. Open questions

1. **What may feed a dynamic pin?** Today, only a curve off the note. Options:
   enforce that, or allow anything and let the once-per-note contract explain
   the result. Matt: "it might be that Curve is the *only* thing these guys can
   get." Must be settled before the UI can author connections.
2. **Demotion semantics.** When a dynamic pin is demoted, the widget returns
   holding *some* value. The setting's static value, or the last value the
   chain produced? The former is predictable, the latter is what you just heard.
3. **The rename** (§8).

## 10. Non-goals

- The per-block tier (§7).
- Making `KSPianoString` bendable (backlog 26a) — a separate and much harder
  problem than recomputing a gain.
- Reclassifying which settings *should* be floats. Backlog 24 asks a related
  but distinct question: whether each attribute materially affects the sound.
- Any change to fixed pins.

## 11. What this changes downstream

`perform_source_design.md` §7 **P2** depended on a config story this design
replaces. P2's engine content survives — the `PerformNode` JSON type, building
the voice adapters before `build_graph`, the converter, the conversion null
gate. What changes is how a chain landing on a setting is expressed, in the
file and on screen.

**Serialization follows the model.** A dynamic pin is a distinct kind of pin, so
it serialises distinctly — a per-node object listing the settings this patch
drives, separate from `params`. Worth recording why, because the same shape was
proposed and rightly rejected a day earlier: proposed as a workaround for a
loader detail (node construction reads `params` before descriptors are known,
and `nlohmann`'s `value()` throws on an object where it wants a number), it was
a kludge. Proposed as the serialisation of a real distinction in the model, it
is just what the model says. The loader detail stops being the reason and
becomes a corroboration.

P3 and P4 are unaffected — P3 already freezes configs at Setup, which is this
contract; P4 touches RangeSource and hiBoost, neither of which is a setting.

One hole P3 should record: it retires the `PitchBendSource` graft in favour of
an articulated `.frequency`, but §5 shows bend tracking is per-node —
`WavetableSource` follows it, `KSPianoString` ignores it. P3 will work on some
nodes and be silently inert on others, and currently says so nowhere.
