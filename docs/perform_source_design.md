# PerformSource — the performance interface as a graph citizen

**Status:** spec for review (brainstormed 2026-08-18, Matt + Claude)
**Supersedes:** the paramMap authoring model for *new* patches (paramMap remains
the supported legacy compiled form); the PitchBendSource per-note graft
(retired in phase 3).
**Prior art:** legacy C# `ParameterMapping` (Function ported as curves 2026-07;
Condition never ported — subsumed by step-shaped curves); AFM's `swmidinote`
(P/V/G/T outputs) + `swmidicc` + `swcustfun`, decoded from the factory bank
(docs/research/af_factory/).

## 1. Decision summary

1. The compose/perform separation **stays**: `Instrument::play_note(...)` is
   the only way a note reaches an instrument. Notes never appear on the patch
   graph. (This is the deliberate divergence from AFM, which has no
   composition layer.)
2. New engine object **PerformSource**: one instance **per voice graph**,
   holding the performance values for the note that voice is playing —
   frequency (articulated), velocity, gate; later pressure/wheel. All graph
   components that depend on performance values **pull** them through ordinary
   ValueSource references rooted at the voice's PerformSource.
3. The write surface is **one call per note**:
   `voice.performSource.set_note(freqHz, velocity, durSamples, pitchCurve*)`.
   No more per-note poke-tour of ParamSlot pointers into scattered
   ConstantSources.
4. UI node **PerformNode**: instantiable **in any group, any number of
   times**. Every instance is a *visual representation of the same underlying
   per-voice PerformSource* — a repeated terminal of a well-known global, not
   a portal. Wires from a PerformNode are always group-local; zero cross-group
   mapping wires. (Contrast AFM's `swconnect` portals: name-based indirection
   to *anywhere*. A PerformNode's referent is singular and unambiguous.)
5. Transfer shapes become **nodes in the wire**: `CurveNode` (knot list +
   interp mode, the current curve data as a node), later `IfNode`/`SwitchNode`
   for velocity layering. The wire IS the binding — `resolve_param_map`'s
   name-based target seeking has no role in new patches.
6. **Semantics are baked initially.** PerformSource outputs are constants for
   the duration of a note (written at prepare). Bit-compatible with today's
   audio, zero added per-sample cost. Liveness (bend flowing through curves,
   wheel, pressure) is phase 3: the same outputs start varying in time and
   the same pull chains just work. No second migration.
7. **paramMap becomes the legacy compiled form.** The loader keeps reading it
   via the existing ParamSlot path, untouched, indefinitely. New patches
   author PerformNode wirings. An auto-upgrade tool (paramMap → wiring) is
   optional phase 4.
8. The **Curves tab becomes a generated view** over PerformSource-rooted
   bindings in the graph — enumerate, list, click-to-edit the same nodes.
   One model, two views (local wires, global table). The Mappings dialog
   retires with it. No view↔model sync surface exists, because there is no
   second model.

Naming: **PerformSource** (engine) / **PerformNode** (UI) used throughout
this doc. Alternatives considered: InputNode (collides with future group
input pins), NoteIn (too narrow once wheel/pressure arrive). Final call is
Matt's.

## 2. Architecture

### 2.1 PerformSource (engine)

```
class PerformSource {
  // Written once per note (perform/render boundary):
  void set_note(float freqHz, float velocity, int durSamples,
                const CompiledPitchCurve* bend /*nullable*/);
  // Channel-wide live state, shared by all voices (phase 3):
  //   wheel, pressure — read from an instrument-level ControlState.

  // Outputs, each an ordinary ValueSource the graph can reference:
  //   .frequency  — Hz. With bend: base * 2^(bend(t)/12). Without: constant.
  //   .velocity   — 0..1 constant per note.
  //   .gate       — 1 while held, 0 after release (deferred — see §6.1;
  //                 not part of phases 1-3).
  //   .wheel, .pressure — phase 3.
}
```

- One PerformSource per `VoiceGraph`, built by the loader alongside the graph
  clone. `voicePool[i].performSource`.
- `prepare_voice`/`play_note` write it instead of poking ParamSlots (for
  new-style patches; legacy patches keep the ParamSlot path).
- Outputs are pull-evaluated. A constant-valued output costs what a
  ConstantSource costs. Nothing is evaluated at audio rate until phase 3
  makes an output time-varying.
- Config slots (`isConfig` targets — e.g. residue curves delivered via
  `set_config`, which rebuild state at prepare): these cannot be pull-wired.
  Prepare evaluates the relevant chain once (PerformSource value → transfer
  nodes → scalar) and calls `set_config` with the result. Same timing as
  today, different lookup root. (Spec issue found while writing: the chain
  evaluator for config targets needs a "evaluate this sub-graph once, now"
  entry point. Small; flagged for the plan.)

### 2.2 CurveNode (engine + UI)

- The existing curve data (knots + `interp: linear|loglog`) as a ValueSource:
  one input (x), one output (y). Pure, stateless.
- Editor: 2D knot editor, ExplicitPartials-editor precedent. Same JSON shape
  as today's paramMap curves, so decode/diff tooling keeps working.
- An exponential-shape need beyond loglog does NOT grow a new stage system:
  if per-segment shapes are ever genuinely needed, CurveNode's evaluator
  becomes the Envelope stage machinery over an x-domain (curve = Envelope
  with a different x-axis). Named escape hatch, YAGNI today.
- Today's `vcurve` (freq-map × vel-map) becomes visible composition:
  `PerformNode.frequency → CurveNode_A ↘`
  `                                    Multiply → target pin`
  `PerformNode.velocity  → CurveNode_B ↗`

### 2.3 PerformNode (UI)

- Node type instantiable N times per patch, any group. All instances render
  the same outputs; wires from any instance bind to the voice's single
  PerformSource at load/build time.
- Delete of an instance deletes only that visual terminal (and its local
  wires), never the underlying source.
- Save serializes PerformNode instances + wires exactly like any node — no
  special-cased stash, no reconstruction transform, no round-trip risk of the
  2026-08-13 damper-mangling class.

## 3. Use case 1 — a bent note, score to samples

Scenario: a MelodicFigure note carries `Articulation: BendUp` (or a mordent
compiled as a bend — same machinery, per docs/pitch_modulation_design.md).

1. **Compose layer.** Performer encounters the articulation, compiles it to a
   `PitchCurve` (`semi[]/durn[]/trans` — existing v1 machinery, unchanged).
2. **Compose/perform boundary.** Performer calls
   `instrument.play_note(noteNumber, velocity, duration, startTime, &curve)`.
   Signature exists today; unchanged.
3. **Perform/render boundary.** The instrument acquires a voice (pool
   acquire/release semantics, 2026-08-18) and writes ONE object:
   `voice.performSource.set_note(note_to_freq(n), vel, durSamples,
   compile(curve))`. The compiled bend is an envelope over the note's
   duration, owned by the PerformSource. Nothing else is touched — no
   ParamSlot pokes, no PitchBendSource grafted into a mapped slot.
4. **Render.** The graph pulls:
   - `KSPianoString.frequency ← PerformNode.frequency`: emits
     `base × 2^(bend(t)/12)` sample by sample — the string bends.
   - `KSPianoString.t60 ← CurveNode(t60 shape) ← PerformNode.frequency`:
     **the keytrack curve re-evaluates as the pitch moves** — damping,
     brightness, excitation color track the bend continuously.
   - FM operators / vibrato wired around frequency compose unchanged — they
     modulate a base that now moves, which is the desired semantics (same
     conclusion as pitch_modulation_design's ParameterMapping note).
5. **Output.** `next()` pulls per sample as today; WAV or live buffer.

Contrast with today: the PitchCurve → Envelope → PitchBendSource graft bends
ONLY the slot it replaces (string frequency); every curve-mapped parameter
(t60, brightness, excitation cutoff) stays frozen at the base note's value
for the whole bend. In PerformSource world that machinery deletes and the
frozen-parameter artifact disappears. This is an intentional, audible
behavior improvement — flag any bend-using regression scores for re-listen
in phase 3, per the no-backcompat rule.

Note on baked phase 1: with liveness off, step 4's frequency output is the
constant base (bends still ride the legacy PitchBendSource path until
phase 3). The use case above describes the phase-3 end state; phases 1-2
change representation only.

## 4. Use case 2 — mod wheel, key to samples

Scenario: patch wires `PerformNode.wheel → CurveNode → Vibrato.depth`.
Matt plays a note, then rolls the wheel mid-note.

1. **Input.** RtMidi delivers CC1 on its thread; `pump_midi` (or the phase-3
   MIDI-thread path, BACKLOG dsp 18) sees `(0xB0, 1, value)`.
2. **Instrument boundary.** The value lands in an instrument-level
   `ControlState { wheel, pressure, ... }` — channel-wide, NOT per-voice
   (all sounding voices hear the same wheel). Write is serialized with the
   audio thread (under g_audioMutex or an atomic with relaxed ordering —
   single float, torn reads impossible on x86; plan decides).
3. **Perform/render boundary.** Every voice's PerformSource `.wheel` output
   reads ControlState through a one-pole smoother (~5-15 ms) so a coarse
   7-bit CC doesn't zipper. No per-note write involved — the wheel is a
   *stream*, not note data. This is the provenance asymmetry from the
   brainstorm: articulation bend is scheduled score data arriving via
   `set_note`; the wheel is live state arriving via ControlState. Same wire
   downstream, different writer.
4. **Render.** The pull chain evaluates per sample: wheel → curve → vibrato
   depth. Sound follows the hand.
5. **Offline render.** ControlState is constant (default 0) — a score render
   is reproducible and wheel-free. If captured live performance ever needs
   offline reproduction, that's an automation-lane feature in the comp lane
   (performance capture), out of scope here.

Polyphonic aftertouch (per-note pressure), if ever wanted: it IS note data —
arrives per voice, lives on PerformSource beside velocity, written by the
MIDI layer keyed on the sounding note. Channel pressure = ControlState like
the wheel. Both fit without new architecture.

## 5. Impact inventory

- **engine/**: PerformSource class; VoiceGraph gains `performSource`; loader
  builds it; `play_note`/`prepare_voice` write it for new-style patches.
  Legacy ParamSlot path untouched. PitchBendSource graft retired in phase 3.
- **loader**: recognizes PerformNode/CurveNode types; paramMap read path
  unchanged (legacy). Patches may contain either style; mixing in one patch
  is legal during migration (paramMap entries and wired bindings must not
  target the same pin — loader error).
- **UI**: PerformNode (multi-instance semantics — new for the node editor),
  CurveNode + knot editor, Curves tab rewritten as generated view,
  Mappings dialog retired (phase 2). Green pin badges stay (they now mean
  "bound via wire to PerformSource" or "legacy paramMap binding").
- **CLI/offline**: no behavior change in phases 1-2. Phase 3 changes bend
  rendering (see §3 contrast). Renders of bend-free scores stay
  bit-identical throughout.
- **Docs/tooling**: patch-diff and AF-decode tooling keep working (CurveNode
  serializes the same knot JSON).

## 6. Open issues (found while writing, per the spec's purpose)

1. **gate output vs envelope gating.** Today held notes work via
   `set_gated`/`gate_release` on Envelope objects, driven by the UI voice
   layer. A PerformSource `.gate` output could eventually subsume that
   (envelopes watch gate instead of being poked), which would also let the
   damper env in patches read gate directly. Deliberately OUT of this spec —
   phase 5+ candidate, big blast radius, needs its own brainstorm.
2. **Velocity layering (the Condition heir).** `IfNode`/`SwitchNode` with
   `PerformNode.velocity` input enables "hard-hammer chain above 0.8" —
   the timbre-family switching neither piano currently does. Enabled by this
   architecture, not designed here.
3. **Config-target chain evaluation** (§2.1) — needs the evaluate-once entry
   point in the plan.
4. **Per-voice PerformNode identity in NodeGraph mode.** NodeGraph mode has
   no voices. PerformNode is patch-mode only (like the retired Parameter
   node's NT_PARAMETER survives in NodeGraph mode for keyboard playability —
   that oddity should be revisited when this lands).
5. **Duration.** Percent-based envelopes need durSamples at prepare; that
   flows through `set_note` and existing prepare machinery. No PerformSource
   `.duration` output planned — nothing should pull duration at render time.
   Challenge in review if a use case disagrees.

## 7. Phases

- **P0** — this spec; naming decision.
- **P1** — engine PerformSource + CurveNode, baked semantics; loader support;
  new patches authorable in JSON by hand. Validation: hand-convert one
  library patch (Piano_bright is the stress case — 9 mappings, vcurves,
  config targets) and null-test against its paramMap twin.
- **P2** — UI: PerformNode, CurveNode editor, Curves tab as generated view.
  Retire Mappings dialog.
- **P3** — liveness: articulated frequency (retire PitchBendSource graft),
  ControlState + wheel/pressure. Re-listen bend-using material.
- **P4 (optional)** — paramMap → wiring auto-upgrade tool for the library.
