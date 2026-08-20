# PerformSource — the performance interface as a graph citizen

**Status:** revised after Matt's annotation review (2026-08-18, interactive
pass over docs/perform_source_design_MF.txt — all points resolved).
**Supersedes:** the paramMap authoring model (paramMap becomes a load-time
converted legacy format; ParamSlot machinery retires at end of P1); the
PitchBendSource per-note graft (retired in phase 3); RangeSource (retirement
path in §2.2, library sweep in P4).
**Prior art:** legacy C# `ParameterMapping` (Function ported as curves
2026-07; Condition never ported — subsumed by step-shaped curves); AFM's
`swmidinote` (P/V/G/T outputs) + `swmidicc` + `swcustfun`, decoded from the
factory bank (docs/research/af_factory/).

## 1. Decision summary

1. **Layer taxonomy** (settled in review 2026-08-18):
   **Composition → Performance → Realization**, with Realization subdivided
   into **Setup** (push) and **Rendering** (pull).
   - *Composition* produces events — what a score captures.
   - *Performance* (the Performer) adds what a score does not capture —
     broken chords, mordent interpretation, swing/rush/lay-back — and
     selects the instrument.
   - `Instrument::play_note(...)` is the **Performance→Realization
     boundary** and the only way a note reaches an instrument. Notes never
     appear on the patch graph. (Deliberate divergence from AFM, which has
     no Composition/Performance layers.) This layer exists because software
     lacks reality's shortcut: IRL the performer's motion IS the sound
     production; here an explicit layer renders it.
   - *Realization/Setup*: voice acquired, NoteState written, envelopes
     prepared — everything that happens at play_note time. Push.
   - *Realization/Rendering*: the audio engine pulls next() per sample, to
     a device or a WAV. Pull. Live-only inputs (wheel, pressure, key-up)
     enter here directly, touching neither Composition nor Performance.
   - **Voice summing** is the final act of Rendering: each voice's output ×
     its voice gain, summed, soft-clipped. It exists twice today — inside
     `PitchedInstrument::render` (Piece/offline) and in the UI audio
     callback's `g_voices[]` loop (Live). Named here so the spec can refer
     to it without pretending it is a class; the duplication is a known
     wart of the audition-path-mismatch family, out of scope.

2. New engine object **PerformSource**: one per voice graph. It is **not
   itself a ValueSource** — it is the per-voice backing store (NoteState,
   plus a reference to the instrument's InstrumentState; see §2.1). Its
   *outputs* (`.frequency`, `.velocity`, ...) are each an ordinary
   ValueSource — thin adapters reading the store. "Everything is a
   ValueSource" holds at the pin, where consumers pull. The outputs are
   leaves of the pull DAG — sources that pull nothing — *among* the graph's
   other leaves (constants, noise, Envelopes under Partials/Formants). A
   consumer may pull PerformSource for one pin and whole subtrees for
   others.

3. The write surface is **one call per note**:
   `voice.performSource.set_note(freqHz, velocity, durSamples, pitchCurve*)`
   — written at Realization/Setup. No per-note poke-tour of ParamSlot
   pointers into scattered ConstantSources.

4. UI node **PerformNode**: instantiable **in any group, any number of
   times**. Every instance is a visual representation of the same
   underlying per-voice PerformSource. Wires from a PerformNode are always
   group-local; zero cross-group mapping wires. It is not a portal *between
   graph locations* (AFM's `swconnect`, whose far end must be hunted down)
   — it is the graph's **front door to the Realization layer**, with a
   fixed, self-describing referent and a single writer contract. This is
   equally true in Piece and Live: the frequency was never graph-resident
   data in either mode. The modes differ only in *when deliveries stop* —
   Piece finishes all deliveries at Setup; Live keeps the door open during
   Rendering.

5. Transfer shapes become **nodes in the wire** (policy in §2.2): CurveNode
   for tabular/drawn transfers, existing CombinedSource for arithmetic,
   new named-shape nodes only past a deliberately high bar. The wire IS the
   binding — name-based target seeking has no role in the runtime model.

6. **Semantics are baked initially.** In phases 1-2 PerformSource outputs
   are constants for the duration of a note. Bit-compatible with today's
   audio (null-tested, §5), zero added per-sample cost — a constant output
   is indistinguishable from the ConstantSource it replaces, and the engine
   already pays that pull price on every constant pin of every patch.
   Liveness (phase 3) means the same adapters start returning time-varying
   values; consumers change zero code because they always pulled per
   sample. Scope limit: liveness flows through **pulled params only**
   (§2.1); config targets stay Setup-frozen.

7. **paramMap converts at load.** The loader converts every paramMap entry
   into PerformSource bindings (wires + transfer nodes) at load time; there
   is exactly **one runtime model**. ParamSlot machinery retires at the end
   of P1, gated by a null test over the entire patch library. Save emits
   the wiring format only — legacy patches silently format-upgrade on
   first save (consistent with the no-backcompat rule; accepted in review).

8. The **Mappings dialog and Curves tab become derived views** over the
   bindings in the graph — for all patches at once (P2), since after load
   conversion there is nothing else to show. One model, two views (local
   wires, global table). No view↔model sync surface exists.

Naming, settled: **PerformSource** (engine) / **PerformNode** (UI);
**NoteState** (per-voice store) / **InstrumentState** (per-instrument
store). "ChannelState"/"ControlState" rejected as scope-opaque.

## 2. Architecture

### 2.1 PerformSource, NoteState, InstrumentState

```
struct NoteState {          // per voice — facts about THIS note
  float frequency;          // Hz, base
  float velocity;           // 0..1
  int   durSamples;         // actual (Piece) or nominal (Live), see below
  CompiledPitchCurve bend;  // optional, from the Performer
  float pressure;           // per-finger (poly AT); mirrored from
                            // InstrumentState under channel-pressure hw
};

struct InstrumentState {    // one per instrument — facts about the
  float wheel;              // instrument as played: mod wheel, CCs,
  float pressure;           // channel pressure, (future) sustain pedal.
  ...                       // Outlives every note.
};

class PerformSource {       // per voice: NoteState + a ref to the
                            // instrument's InstrumentState
  void set_note(float freqHz, float velocity, int durSamples,
                const CompiledPitchCurve* bend);   // Realization/Setup
  // Outputs — each an ordinary ValueSource (leaf adapters):
  //   .frequency  — base * 2^(bend(t)/12) when bent (phase 3); constant
  //                 otherwise.
  //   .velocity   — constant per note.
  //   .pressure   — per-finger semantics ALWAYS: poly-AT hardware writes
  //                 NoteState directly; channel-pressure hardware mirrors
  //                 InstrumentState into every sounding voice's NoteState.
  //                 The patch author never knows which keyboard is attached.
  //   .wheel      — reads InstrumentState (one physical wheel, all voices).
  //   .gate       — deferred entirely (§6.1); not part of phases 1-3.
}
```

- **durSamples is actual (Piece) or nominal (Live).** Live notes lay
  envelope stages out against a nominal exactly as today (the Duration
  spinner value flowing into play_note_held); percent stages resolve
  against it, minSec/maxSec clamp absolutely, the gated expand stage holds
  until key-up jumps to release. **Stage layout commits at Setup** — a live
  note held past its nominal only stretches the sustain, which is correct
  and by design; the nominal is a real expressive input for live play (a
  short nominal is a snappier envelope word). `Stage.nominal` (§2.4)
  makes this patch-intrinsic.
- **Piece vs Live is a write discipline, not a graph property.** Piece:
  all writes at Setup (constants + precompiled envelopes). Live: writes
  continue during Rendering (wheel, pressure, key-up). The pull chains are
  identical and cannot tell the difference.
- **MPE note:** MPE's channel-per-note scheme maps trivially onto per-voice
  NoteState if a controller ever warrants it.

**Two classes of consumer parameters** — the honest scope of "just works":

1. **Pulled params** (`set_param`, `shared_ptr<ValueSource>`, consulted in
   `next()`): SVF cutoffs, amplitudes, most wiring. Verified: SVF re-pulls
   cutoff every sample and recomputes coefficients only on change
   (svf_source.h:100). Liveness flows through these automatically.
2. **Config params** (`set_config` scalars): KSPianoString's t60,
   brightness, dispersion — consumed at prepare into derived state (comb
   gains: `pow(10, -3·len/(sr·t60))`, ks_piano_string.h:440), then gone.
   No pointer to pull. The chain feeding a config target is evaluated
   **once at Setup** and pushed via `set_config` (this needs an
   "evaluate-this-subgraph-now" entry point — §6.3). **Phase 3 liveness is
   pulled-params only**; configs stay Setup-frozen. Block-rate re-config
   (re-evaluate config chains every N samples; ~3 pows/block for KS) is
   PARKED — noted with the t60 example, not scheduled.
   *Node-author credo (Matt):* at class-design time ask "will I ever want
   to modulate this?" — knee-jerk answer "maybe" per the everything-is-a-
   ValueSource credo; a legitimate "no" tier exists (envelope stage
   internals: min/max/pct/shape/power); and never say never (detune as an
   envelope target someday).

### 2.2 Transfer nodes

Category: unary value-in/value-out ValueSources usable in binding wires.

- **CurveNode** — the one new member. Knot list + interp
  (`linear|loglog`), one input (x), one output (y). Pure, stateless,
  evaluates on input change (SVF-style caching). This is our `swcustfun`.
  Editor: 2D knot canvas, ExplicitPartials precedent. Serializes the same
  knot JSON as today's paramMap curves — decode/diff tooling unaffected.
- **Arithmetic is CombinedSource**, already in the vocabulary: Multiplier
  is `Combined(x, k, multiply)`, OneMinus is `Combined(1, x, subtract)`,
  legacy `NestedFunction` is two nodes and a wire. No Function-flavor
  wrapper — the node graph IS the composition language (rejected in
  review: "wrapper — dumb," M.F.).
- **Named-shape nodes** earn existence only when a shape is (a) not
  expressible by knots + interp — transcendentals, `tanh` saturation — AND
  (b) recurring. Then it's a small registry ValueSource, AFM-style
  (`swpow`, `swchebyshev` tier). Note loglog knots cover power laws
  EXACTLY (a 2-knot loglog segment is y = k·xⁿ), so x², sqrt, 1/x need
  nothing new.
- **Envelope-over-x escape hatch:** if a curve ever genuinely needs
  per-segment shapes, CurveNode's evaluator adopts Envelope's stage
  machinery over the x-domain — an interior upgrade, not a sibling flavor.
  Distinguish: an envelope over *time* is a modulation source (wire it in;
  nothing to do with CurveNode); envelope-*style segments* over freq/vel
  is the CurveNode upgrade.
- **New capability, flagged:** today curves exist ONLY inside paramMap,
  evaluated once at Setup. A CurveNode in a live chain (LFO → CurveNode →
  cutoff) is the first runtime-pulled curve in the engine. Cost is one
  interp per input *change*, not per sample.
- **RangeSource retires** (155 patches; migration in P4):
  - Enveloped ranges (`Env → Range(80,500) → cutoff`) → `Envelope.minValue/
    maxValue` (§2.4). The flagship idiom, native.
  - Modulator ranges → 2-knot linear CurveNode `[[0,min],[1,max]]`
    (legacy ±1 mode: `[[-1,min],[1,max]]`). Exact same affine map.
  - Geometric ranges (octave-around-center) → 3-knot **loglog** CurveNode
    `[[-1,500],[0,1000],[1,2000]]` — octave-symmetric by construction,
    which the old linear RangeSource pairing never actually was.
  - Footnotes for the null test: RangeSource extrapolates outside the
    nominal input range, curves clamp at end knots (only matters if a
    patch overdrives `var`); RangeSource's min/max are themselves
    ValueSources — patches wiring sources into range ends (expected: ~0
    of 155) convert to CombinedSource algebra or keep RangeSource until
    the count is zero.
  - **VarSource is unchanged** — proportional variation around a center
    with a stateful relative mode (spacy-FM lineage); it is not a range
    mapper and gains no min/max.

### 2.3 PerformNode (UI)

- Instantiable N times per patch, any group. All instances render the same
  outputs; wires bind to the voice's single PerformSource at load/build.
- Deleting an instance deletes that visual terminal and its local wires,
  never the underlying source.
- Serializes like any node. No stash, no reconstruction transform, no
  round-trip risk of the 2026-08-13 damper-mangling class.
- Patch-mode only (§6.4).

### 2.4 Envelope in the PerformSource world

Two additions (both P1 — independent of liveness):

- **`minValue` / `maxValue`** — envelope output becomes
  `min + (max−min) · stageValue`. Defaults 0/1 (bit-compatible). Both are
  **ordinary params, i.e. pullable ValueSources** per the credo — so
  `PerformNode.velocity → CurveNode → Env.maxValue` is the garden-variety
  velocity-shapes-amplitude wiring, and maxValue is modulatable by
  anything (AFM's knob-plus-Mod-input pattern). Retires the interposed
  RangeSource idiom (§2.2).
- **`Stage.nominal`** (seconds, per stage) — the stage's authored length
  for Live playback, used where the actual duration is unknown: in Live
  mode each percent stage takes its nominal (clamped by minSec/maxSec as
  ever) instead of leaning on UI state; in Piece mode percent stages
  resolve against the actual duration, unchanged. Makes live envelope
  behavior **patch-intrinsic**; the Duration spinner demotes from
  load-bearing to override.

**The fader/timbre split** (decision): the voice-summing gain
`velocity × (1 + hiBoost·(log₁₀f−2)) × volume` stays OUTSIDE the graph.
All three factors are Realization-mixer facts, none is timbre: velocity =
the note's loudness (from NoteState, the same value patches may also wire
for timbre); hiBoost = per-patch high-note audibility compensation (a
keytrack curve wearing a scalar's clothing — conversion to an explicit
curve is BACKLOG dsp 19, parked); volume = per-patch pre-clip gain
staging. In-graph velocity wiring is for *timbral* response — harder ≠
merely louder. Hazard, documented: wiring velocity into a top-level
amplitude while the fader also applies it yields velocity² loudness —
author's rope.

## 3. Use case 1 — a bent note, score to samples

Scenario: a MelodicFigure note carries `Articulation: BendUp` (or a mordent
compiled as a bend — same machinery, per docs/pitch_modulation_design.md).

1. **Composition → Performance.** The score carries the articulation; the
   Performer compiles it to a `PitchCurve` (`semi[]/durn[]/trans` —
   existing v1 machinery, unchanged).
2. **Performance→Realization boundary.** Performer calls
   `instrument.play_note(noteNumber, velocity, duration, startTime,
   &curve)`. Signature exists today; unchanged.
3. **Realization/Setup.** The instrument acquires a voice (pool
   acquire/release semantics, 2026-08-18) and writes ONE object:
   `voice.performSource.set_note(note_to_freq(n), vel, durSamples,
   compile(curve))`. The compiled bend is an envelope over the note's
   duration, owned by the PerformSource. Nothing else is touched — no
   ParamSlot pokes, no PitchBendSource grafted into a mapped slot.
4. **Realization/Rendering.** The graph pulls:
   - `KSPianoString.frequency ← PerformNode.frequency`: emits
     `base × 2^(bend(t)/12)` sample by sample — the string bends.
   - Every **pulled** target wired from frequency (excitation SVF cutoffs,
     `vel_lp`, gains through CurveNodes) re-evaluates as the pitch moves —
     they track the bend continuously.
   - **Config** targets (t60, brightness — §2.1 class 2) were evaluated at
     Setup from the base note and stay fixed for the note. Better than
     today (today *everything* is frozen during a bend), not total;
     block-rate re-config is parked.
   - FM operators / vibrato wired around frequency compose unchanged —
     they modulate a base that now moves, which is the desired semantics.
5. **Output.** Voice summing applies the fader; `next()` pulls per sample
   as today; WAV or live buffer.

Contrast with today: the PitchCurve → Envelope → PitchBendSource graft
bends ONLY the slot it replaces (string frequency); every mapped parameter
stays frozen at the base note's value. In PerformSource world that
machinery deletes; pulled targets flow. Intentional, audible behavior
improvement — re-listen bend-using material in phase 3, per the
no-backcompat rule.

Note on baked phases 1-2: liveness off, `.frequency` returns the constant
base, bends ride the legacy PitchBendSource path until phase 3. Phases 1-2
change representation only; renders stay bit-identical (§5).

## 4. Use case 2 — mod wheel, key to samples

Scenario: patch wires `PerformNode.wheel → CurveNode → Vibrato.depth`.
Matt plays a note, then rolls the wheel mid-note.

1. **Input.** RtMidi delivers CC1 on its thread; `pump_midi` (or the
   phase-3 MIDI-thread path, BACKLOG dsp 18) sees `(0xB0, 1, value)`.
2. **Realization entry — InstrumentState.** The value lands in the
   instrument's InstrumentState — instrument-wide, one physical wheel.
   Write is serialized with the audio thread (under g_audioMutex or an
   atomic; single float, plan decides).
3. **PerformSource read (Rendering phase).** Every voice's `.wheel` output
   reads InstrumentState through a one-pole smoother (~5-15 ms) so a
   coarse 7-bit CC doesn't zipper. No per-note write involved — the wheel
   is a *stream*, not note data. Provenance asymmetry: articulation bend
   is scheduled score data arriving via `set_note` at Setup; the wheel is
   live state arriving in Rendering. Same wire downstream, different
   writer.
4. **Realization/Rendering.** The pull chain evaluates: wheel → curve →
   vibrato depth. Sound follows the hand. The wheel entered the stack
   directly in the Rendering phase — Composition and Performance were
   never involved, which is precisely what distinguishes Live input from
   Piece data.
5. **Offline render.** InstrumentState is constant (default 0) — a score
   render is reproducible and wheel-free. Captured-performance playback =
   a future comp-lane automation feature, out of scope.

Pressure routing per §2.1: poly-AT → NoteState directly (voice found by
midiNote, the same bookkeeping release_note_held uses); channel pressure →
InstrumentState, mirrored into every sounding voice's NoteState. The patch
wires `PerformNode.pressure` either way.

## 5. Impact inventory

- **engine/**: PerformSource + NoteState + InstrumentState; VoiceGraph
  gains `performSource`; loader builds it; play_note/prepare write it.
  Envelope gains minValue/maxValue (pullable) + Stage.nominal. CurveNode
  added to the registry. ParamSlot machinery RETIRES at end of P1.
  PitchBendSource graft retires in phase 3.
- **loader**: converts every paramMap entry to bindings at load (plain →
  wire; curve → CurveNode; vcurve → CurveNode×CurveNode→Multiply; isConfig
  → Setup-evaluated chain landing on a config pin — one visual language,
  two evaluation times, distinguished by pin type). Reads legacy patches
  forever; **save emits wiring format only** (silent format upgrade on
  first save — accepted).
- **Null-test gate (P1):** convert every library + baseline patch on load,
  render, byte-compare against the pre-conversion engine. Conversion bugs
  cannot hide. **Multiplex** gets its own line item (topMultiplex
  fan-out pushes the same value per clone, so a shared PerformSource ref
  should be identical — verified, not assumed).
- **UI**: PerformNode (multi-instance semantics — new for the editor),
  CurveNode + knot editor, Curves tab and Mappings dialog become derived
  views over bindings for ALL patches (P2). Green pin badges stay.
- **CLI/offline**: bit-identical through P1-P2 (null-tested). Phase 3
  changes bend rendering (§3 contrast). Bend-free scores stay
  bit-identical throughout.
- **Docs/tooling**: patch-diff and AF-decode tooling keep working
  (CurveNode serializes the same knot JSON).

## 6. Open issues

1. **Gate.** Promoting the existing gating machinery (`set_gated`/
   `gate_release` pokes from the voice layer) to a PerformSource `.gate`
   output that envelopes *watch* — would also let patch damper envelopes
   read gate directly. Deliberately OUT: big blast radius, works today,
   own brainstorm when its time comes.
2. **Velocity layering (the Condition heir).** IfNode/SwitchNode with
   `PerformNode.velocity` enables "hard-hammer chain above 0.8" — the
   timbre-family switching neither piano currently does. Enabled by this
   architecture, not designed here.
3. **Config-target chain evaluation** — the evaluate-once entry point
   (§2.1). Small; for the plan.
4. **PerformNode in NodeGraph mode.** No voices there; PerformNode is
   patch-mode only. NT_PARAMETER's survival in NodeGraph mode should be
   revisited when this lands.
5. **Duration.** No `.duration` output — nothing should pull duration at
   render time; percent-envelope needs flow through set_note + prepare.
   Held through review.
6. **PARKED: block-rate re-config** for config targets under liveness
   (the t60-tracks-a-bend case) — cheap for KS (~3 pows/block) but a real
   mechanism, per-source opt-in, and phase 3 ships without it.
7. **PARKED: RangeSource dynamic min/max stragglers** — patches wiring
   sources into range ends convert by hand (CombinedSource algebra) or
   keep RangeSource alive until the census hits zero.

## 7. Phases

- **P0** — this spec. ✓ (naming settled: PerformSource/PerformNode,
  NoteState/InstrumentState, Realization[Setup|Rendering].)
- **P1 (engine, baked)** — ✓ **LANDED 2026-08-18** (commits dfd6f1f..07bbbc2,
  plan_perform_source_p1.md executed inline). PerformSource + NoteState +
  PerformOut adapters; CurveNode (3 interp modes — note: the default
  paramMap curve interp is LogX, value-linear-over-log-x, so CurveNode has
  Linear|LogX|LogLog, a refinement over this spec's linear|loglog);
  Envelope minValue/maxValue + Stage.nominal; paramMap load-conversion
  (build_bindings: wire/push/bend-swap matrix, Multiplex voices push
  wholesale); ParamSlot machinery deleted. **Null gate: 196/196
  bit-identical** after both conversion and retirement, incl. all 7
  Multiplex patches and the 4 bend/slide baselines. InstrumentState
  deferred to P3 (YAGNI — first consumer is the wheel).
- **P2a (format)** — ✓ **LANDED 2026-08-19** (commits 004295e..db4aafe,
  `docs/plan_perform_source_p2a.md`). The `config`→`setting` rename;
  `PerformNode` JSON type (one node per field, all instances resolving to the
  voice's shared `PerformOut`); `dynamicPins`, a per-node object naming the
  settings a patch drives once per note; bend-swap membership reproduced from
  graph shape; `tools/parammap_to_wiring.py`; and
  `tools/null_gate_wiring.py`. **Conversion gate: 196/196 bit-identical**
  rendering from converted files, 117 of them carrying a paramMap. Legacy
  `paramMap` is still read and is not going away.

  P2 as originally written assumed a config story that
  **`docs/pin_model_design.md` replaced**: a driven setting is a *dynamic pin*,
  promoted per patch rather than declared per type, and it serialises as a
  distinct kind of pin. Read that spec first.

  Three latent bugs surfaced, none of them introduced by the phase — all were
  code that worked only because it had exactly one consumer or exactly one
  construction path, which wiring format multiplies:
  - **CurveNode loaded from JSON never wired its `source`.** Its branch claimed
    the generic param pass handled it; `wire_params_generic` is not automatic.
    Shipped in P1 under a green gate because P1 only ever built CurveNodes
    programmatically. Every converted patch would have had frozen curves.
  - **`PerformOut` cached state it did not have.** `RefSource::next()` returns
    `source->current()` without pulling, so every 2nd+ consumer read 0 —
    the common case in wiring format, where one PerformNode feeds many pins.
    Now a stateless view over `NoteState`.
  - **A third `build_graph` call site** — the throwaway graph that resolves
    mixer gain for patches with a `mix` node — passed no context and threw on
    every PerformNode. Found by the conversion gate; five Rhodes/FM patches.
- **P2b (UI)** — ✓ **LANDED 2026-08-19** (commits e7009e5..88cc197,
  `docs/plan_perform_source_p2b.md`). The editor speaks the pin model:
  `PerformNode` as an editor node, load converting a legacy `paramMap` into
  real graph nodes, `dynamicPins` through load and save, `CurveNode` knots
  modeled and editable, Curves and Mappings as derived views over the graph,
  and grey→gold promotion in the Settings pane. Round-trip gate green
  throughout — 0 id changes, 0 render diffs across 199 patches.

  Four things surfaced that the plan had not predicted:
  - **The `__` prefix collided with the UI's own reserved namespace.**
    `sanitize_unique_id` strips a leading `__` to stop human labels claiming
    it; that also renamed the converter's synthesized nodes on every round
    trip. Fixed by distinguishing an id that arrived from the file already
    synthesized from a label a person typed.
  - **Owned Formant children are not convertible.** A `FormantSpectrum`
    consumes them into a row table whose rows hold literal floats, so a driven
    param cannot be represented. Those entries stay in the paramMap, which is
    now RESIDUE rather than the model — conversion is per-entry, not
    all-or-nothing.
  - **Waveform previews needed a UI-side `set_note`.** A PerformNode's editor
    DSP is a stand-in constant, so previews would have drawn every note at
    440 Hz once the stash stopped driving retunes.
  - **The three-phase Envelope warning was two-thirds wrong.**
    `minValue`/`maxValue` already round-tripped through the jsonExtras
    verbatim carry; only `Stage.nominal` was lost, because stages ARE modeled
    and a model drops what it was never taught. Now fixed — the warning is
    retired.
- **P3 (liveness, pulled params only)** — articulated `.frequency`
  (retire PitchBendSource graft), InstrumentState + wheel + pressure with
  smoothers, MIDI plumbing. Re-listen bend-using material. Configs stay
  Setup-frozen (§6.6) — which is the same contract
  `pin_model_design.md` §5 makes definitional for dynamic pins.
  **Hole recorded 2026-08-19: bend tracking is per-node, and this phase does
  not say so.** `WavetableSource` follows a moving frequency per sample via a
  fractional read head (`wavetable_source.h:105`), but `KSPianoString` reads
  its frequency ONCE in `init_note()` (`ks_piano_string.h:384`) and never
  again — so an articulated `.frequency` is silently inert there, and a bend
  curve on a piano patch today moves a number nobody reads. Backlog 26(a).
  P3 must either make the inertness loud or scope itself to the nodes that
  can track.
- **P4 (cleanup sweep, optional)** — RangeSource migration across the 155
  patches; hiBoost → explicit curve (BACKLOG dsp 19); revisit NT_PARAMETER
  in NodeGraph mode.
