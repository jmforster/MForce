# Plan — PerformSource P3: liveness (pulled params only)

Spec: `docs/perform_source_design.md` §2.1, §6.6, phase list "P3".
Written 2026-08-20 (Fable 5, interactive). Prior phases: P1 2026-08-18,
P2a/P2b 2026-08-19 — all landed.

## Scope (from the spec, not invented here)

1. **Articulated `.frequency`** — `PerformOut::Frequency` returns
   `base * 2^(bend(t)/12)` when the note carries a bend; the P1
   `PitchBendSource` graft (bendSwaps, both loaders' graft-membership
   detection) retires.
2. **InstrumentState + wheel + pressure**, with smoothers; new
   `PerformNode` fields.
3. **MIDI plumbing** — CC1 mod wheel + channel pressure into
   InstrumentState from the UI's existing RtMidi pump.
4. **The 26a hole made LOUD** — bend tracking is per-node
   (`WavetableSource` follows per sample, `KSPianoString` reads once in
   `init_note`); the spec requires P3 to "make the inertness loud or scope
   itself to nodes that can track". We make it loud at load time.

Out of scope, stated: per-block config re-evaluation (§6.6 — configs stay
Setup-frozen; same contract as dynamic pins); actually making KSPianoString
bendable (26a proper); MIDI pitch-wheel (0xE0 — InstrumentState has no field
for it in the spec; deferred with a note); poly aftertouch (0xA0 — NoteState
.pressure per-finger semantics exist in the spec, but no hardware to test;
channel pressure mirrors to every voice, which is the v1 the spec allows);
CC64 sustain (already deferred in pump_midi, unchanged).

## The clock problem, and the answer

The graft evaluated bend inside `PitchBendSource::next()` — an envelope
advanced by consumer pulls. That breaks under sharing: two consumers = the
envelope steps twice per sample = bend plays 2x fast. P2a already fought
this class (PerformOut made stateless because `RefSource::next()` doesn't
pull). An articulated frequency needs a time cursor that does NOT belong to
whoever happens to pull first.

**Answer: an explicit per-voice sample clock.** `PerformSource::tick()`
advances the bend envelope (and the wheel/pressure smoothers) exactly once
per sample, called by the render drivers — the loops that already call
`source->next()` once per sample per voice:

- `PitchedInstrument::play_note` render loop (engine, scheduled path)
- the UI audio callback's voice mix (`g_voices[v]`)
- `--dump-playback` keyboard-path loop

`StreamingVoice` carries the voice's `performSource` so streaming callers
can tick it. `PerformOut` stays a stateless idempotent view — it reads the
post-tick cached values, so N consumers, RefSource-wrapped or not, all see
the same sample's truth. The double-advance bug class dies with the graft.

## Tasks

**T1 — engine: articulated frequency, graft retired.**
- `PerformSource`: `set_note(freq, vel, dur, std::shared_ptr<Envelope> bend)`;
  `tick()`; `frequency()` returns `base * exp2(bendSemis/12)` under a bend
  and **the untouched base when bend is null** (no multiply — unbent renders
  stay bit-identical, which is the null-gate contract).
- `PerformOut` Frequency reads it. Velocity unchanged.
- `apply_note_bindings`: compile the curve into the PerformSource; delete
  the graft delivery. Delete `VoiceGraph::bendSwaps`, `BendSwap`, the
  `direct_freq` graft-membership walk in `bind_wiring`, and the equivalent
  in `build_bindings` (legacy paramMap path — same voice struct, same
  retirement). `PitchBendSource` struct deleted; `compile_pitch_curve` and
  the Performer compilers in pitch_bend.h stay (they are the bend's source).
- KNOWN BEHAVIOR CHANGE, from the spec: a curve-fed frequency pin now
  follows the bend through the curve (the graft deliberately skipped curve
  chains, reproducing legacy). Bent material re-renders differently and
  goes to Matt's ears — "Re-listen bend-using material" is P3's own line.

**T2 — engine: inertness made loud.**
- New virtual `ValueSource::tracks_frequency_live()`, default true;
  `KSPianoString` overrides false (frequency read once, ks_piano_string.h
  init_note). At wiring/binding time, when a frequency-rooted chain lands
  on a pin of a non-tracking node, stderr names it once per load:
  bend/articulation will be inert there. No behavior change — 26a stays
  open as the real project.

**T3 — engine: InstrumentState + wheel + pressure.**
- `struct InstrumentState { std::atomic<float> wheel{0}, pressure{0}; }`,
  one per `PitchedInstrument` (shared_ptr), handed to every voice's
  PerformSource at pool build.
- `PerformSource::tick()` runs one-pole smoothers (~10 ms tau at the
  patch sample rate) toward the atomic targets; smoothed values are what
  `PerformOut` Wheel/Pressure report. Channel-pressure hardware thereby
  mirrors into every sounding voice, which is the spec's stated semantic.
- `PerformNode` JSON field grows `wheel|pressure`; loader parse + adapters.

**T4 — UI: MIDI + PerformNode fields.**
- `pump_midi`: handle 2-byte messages (channel pressure 0xD0 is 2 bytes —
  the current `msg.size() < 3` guard drops it); CC1 (0xB0/1) → wheel;
  0xD0 → pressure. Writes go to the live instrument's InstrumentState
  (atomic stores, UI thread; audio thread reads in tick).
- UI PerformNode: field combo frequency|velocity|wheel|pressure, save/load,
  stand-in preview constants (wheel/pressure preview at 0 — silent until
  driven, matching an untouched controller).

**T5 — gates + review queue.**
- Build both targets, `--stamp` 0.
- Null gates per the 2026-08-20 scope rule: `patches/baselines/` +
  `patches/library/` — unbent bit-identical required; the bend/slide
  baselines are EXPECTED to differ (T1 behavior change) and get diffed,
  re-rendered, and queued as a [listen] REVIEW item with before/after WAVs.
- `rt_smoke.py` between edits; live keyboard sanity via --dump-playback.

## Order

T1 → T2 → T3 (engine, one rebuild cycle) → T4 (UI) → T5 (gates).
