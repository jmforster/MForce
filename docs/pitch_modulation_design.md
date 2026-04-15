# Pitch modulation design (v1)

## Status

Brainstorm complete 2026-04-13; sub-questions resolved 2026-04-14. **No code written yet.**

**Work order:**
1. Articulation enum → variant refactor (prep — Bend-up needs a clean home).
2. `PitchCurve` struct under `mforce/music/`.
3. `Instrument::play_note(..., const PitchCurve* curve = nullptr)` — compiles the curve into an `Envelope` and wires it into the frequency ValueSource chain pre-render, by interrogating `ParameterMapping` to locate the insertion point.
4. Gesture (1): bend-up-to-attack articulation.
5. Gesture (2): mordent-bend ornament.
6. JSON serialization for curve-carrying variants — deferred TODO.

## Motivating context

Today:
- Existing Ornaments (Mordent, Trill, Turn) realize via **discrete expansion** — the Performer emits multiple `play_note` calls with different pitches. This lives on the Ornament variant; see recent Performer commits.
- `Vibrato.h` exists as a DSP-level frequency modulator, baked into the voice graph at patch-build time. It is not per-note.
- `PitchedInstrument::play_note` sets the nominal frequency **once** via `SetParameter("frequency", hz)`, which drives a (potentially deep) chain of ParameterMappings into ValueSources. The actual frequency endpoint may be a `VarSource.Value` several levels down in the graph — the `ParameterMapping` layer hides this from callers.
- There is no per-note pitch bending of any kind.

We want to add continuous-pitch gestures: initial bends, mordent-style bends within one note, and (v2) cross-note glissando.

## Three-layer model

**Score layer** — `Ornament` is a **symbolic annotation** on a pitch, the kind of thing a notation engraver would draw: Trill, Mordent, Turn, Bend-up, Fall, Scoop. This is *intent*, not realization.

**Performer layer** — compiles Score-layer symbols → concrete gestures, given the target Instrument's capabilities:
- Mordent on piano → multiple `play_note` calls (discrete expansion, existing path).
- Mordent on guitar → single `play_note` with a `PitchCurve` (continuous path, new).
- Weirder gestures (e.g., attack-G → bend-A → dip-G → bend-A → deep-whammy) can skip the symbolic layer entirely and ship a hand-authored `PitchCurve` attached directly to the note. This is the **tablature escape hatch**.

**Instrument layer** — given an optional `PitchCurve`, compiles it into an `Envelope` and wires that Envelope into the frequency ValueSource chain **pre-render**, locating the insertion point by interrogating `ParameterMapping`. No render-time iteration, no new Instrument code path at audio rate — the existing voice graph pulls from the Envelope like any other modulator. The Instrument's pre-existing modulators (Vibrato, `VarSource` drift, etc.) continue to operate naturally on a now-time-varying base pitch. **Zero structural changes to any voice graph.**

## Tablature framing

Guitar solos are conventionally written in tablature with pre-bend / bend-then-attack / bend-up-and-back / release annotations. Those can eventually live as **custom EventTypes in the Event Sequence stream**, from which tablature can be regenerated as output. This defers the "arbitrary multi-point pitch contour as a first-class Score concept" problem indefinitely: any gesture that exceeds the named Ornament/Articulation vocabulary is a one-off `PitchCurve` directly on a Note, carried through the Event stream as custom data.

## Three gestures, in implementation order

1. **Bend-up-to-attack** (Articulation). Note attacks at a lower semitone offset and rises to nominal. Fully within one note, no cross-note coordination. Validates the entire DSP plumbing.
2. **Mordent-like bend ornament** (Ornament variant). Pitch rises or falls to an auxiliary and returns, all inside one note's sustain. Reuses (1)'s `PitchCurve` mechanism; adds new Ornament variant + Performer branching.
3. **Gliss from previous note** (Articulation). Two or more notes rendered in a single legato Instrument call; pitch envelope spans the inter-onset interval(s). Adds Performer look-ahead bundling and a new `play_legato_run` Instrument API.

Each gesture is strictly more complex than the prior.

**v1 scope = (1) + (2).** **v2 scope = (3).**

## `PitchCurve` representation

Parallel arrays plus a single transition-width scalar:

```
PitchCurve {
  semi:  float[]   // semitone offset from the note's nominal pitch at each node
  durn:  float[]   // fraction of total note (or bundle) duration spent at semi[i]
  trans: float     // fractional width of the ramp straddling each inter-node boundary
                   //   (half on each side)
}
```

Ramp shape for v1 = **linear only**.

### Worked example

`semi=[0, 2, 0]`, `durn=[0.3, 0.4, 0.3]`, `trans=0.2`. For a note of total duration T:

| Interval       | Behavior       |
|----------------|----------------|
| `[0, 0.2T]`    | hold at 0      |
| `[0.2T, 0.4T]` | ramp 0 → +2    |
| `[0.4T, 0.6T]` | hold at +2     |
| `[0.6T, 0.8T]` | ramp +2 → 0    |
| `[0.8T, T]`    | hold at 0      |

### Fill semantics

Following the Envelope `Stage` convention: exactly one `durn[i]` may be `0`, meaning "expand to fill the remaining fraction."

**v1 restriction:** the zero entry, if present, must be the **last** entry. This lets the renderer compute fill as `1 - sum(non-zero durns)` without two-ended math. Once we allow zero at arbitrary positions, the renderer will need to compute lengths from both ends toward the zero.

### Validation (strict, at load time)

Reject — do not silently clamp.

- If no zero in `durn`: `sum(durn) == 1.0` (within float epsilon).
- If one zero (v1: only at last index): `sum(non-zero durns) < 1.0`.
- More than one zero: error.
- Zero at any non-last index (v1): error.
- `trans/2 ≤ durn[i]` on each side `i` has a neighbor:
  - `trans ≤ 2 · durn[0]` at the front,
  - `trans ≤ 2 · durn[last]` at the back,
  - `trans ≤ 2 · durn[i]` for interior nodes,
  - `trans/2 ≤ fill_duration` for the fill entry.

### Semantic notes

- `semi[i]` is an **offset** from the note's nominal pitch. `[0]` means "no change"; `[-2, 0]` means "start 2 semitones flat, slide up to nominal."
- A **`nullptr`** / absent `PitchCurve` = today's constant-pitch behavior. No special no-op-curve sentinel; the Instrument just takes a different code path when the pointer is null.

## Gesture shapes in v1

**Bend-up-to-attack** (articulation): `semi=[-2, 0]`, `durn=[0.15, 0]`, `trans=0.1`
→ attacks at -2 semitones, rises to nominal within the first ~15% of the note, holds nominal for the remainder.

**Mordent-up-and-back** (ornament): `semi=[0, 2, 0]`, `durn=[0.1, 0.15, 0]`, `trans≈0.08`
→ sits at 0, bumps to +2 and returns within the first 25%, holds 0 for the remaining 75%.

**Pre-bent attack that releases** (articulation variant): `semi=[2, 0]`, `durn=[0.3, 0]`, `trans=0.1`
→ note starts at +2, releases to nominal around the 30% mark, holds nominal.

## Instrument / Performer contract

Performer hands the Instrument musical-unit data only. The Instrument compiles the curve to an Envelope and wires it into the voice graph pre-render.

Signature:

```cpp
void Instrument::play_note(Note note, int durSamples, float velocity,
                           const PitchCurve* curve = nullptr);
```

When `curve != nullptr`:
1. Compile `PitchCurve` → `Envelope` (stages from `semi[]`/`durn[]`, linear ramps from `trans`).
2. Interrogate `ParameterMapping` to locate where `frequency` resolves in this voice's ValueSource graph.
3. Insert the Envelope as a modulator on the nominal pitch at that point (combine multiplicatively: `2^(semiOffset/12)`).
4. Render proceeds exactly as today — the graph pulls from the Envelope like any other ValueSource.

When `curve == nullptr`: nominal frequency is set once, as today.

The existing `ParameterMapping` layer means a patch with `Oscillator.frequency ← Vibrato ← VarSource(...)` keeps working — the VarSource's modulation now operates on a time-varying base, which is the desired behavior.

`PitchCurve` is consumed during `play_note` (read, compiled to Envelope). Passing `const PitchCurve*` is sufficient — no shared ownership needed.

## Composition rules

- **Bend-up-to-attack** (articulation) and **Gliss-from-previous** (articulation) are **mutually exclusive** on the same note. Both are statements about how this note arrives at pitch; asserting both is a spec error, not a "first wins."
- **Mordent-bend** (ornament) may coexist with either attack articulation — it lives in the sustain portion of the note.

## Data model direction (agreed, not yet implemented)

`Articulation` today is a pointwise enum. Adding `Bend-up`, and later `Slide`, forces it to become a `std::variant` with per-kind state — the same path `Ornament` already walked. Pointwise kinds (Staccato, Marcato) become payload-less variants; kinds with state (Bend-up, Slide) carry a `PitchCurve` or curve parameters.

Articulation also becomes **structural** rather than purely pointwise: `Slide` inherently references the previous note. Stating this explicitly in the data model (via the variant payload) is the right acknowledgment — no separate "this articulation couples to neighbors" flag needed.

## Deferred / future work

- **KS-pluck pitch bending.** `WavetableSource` (Karplus-Strong plucks, EKS, pluck evolution) reads frequency **only on first sample** to size its ring buffer, then just cycles the buffer with evolution. Frequency overrides have no audible effect. Fix requires restructuring to a variable-tap delay-line with fractional interpolation — architectural surgery, collides with existing tuning-allpass and evolution call sites. Deferred as a dedicated spike. Confirmed audibly 2026-04-14: `bend_test.json` (saw) bends correctly; `bend_test_pluck_2.json` + `bend_mordent_test_pluck_2.json` (pluck) sound identical to non-bent versions. v1 is effectively "bends work on stateless oscillators only."
- **Gliss / cross-note legato.** Performer look-ahead scans forward from note N; accumulates the bundle while subsequent notes carry `Slide`; emits one `play_legato_run(pitches[], segmentDurations[], bendCurves[])` when it hits a non-`Slide`. New Instrument API; single voice, no re-attack, pitch envelope assembled from the bend curves.
- **Per-segment ramp shape.** Generalize `trans: float` to `Stage<semitone>[]` with per-segment `(duration, targetValue, Ramp)`. This matches the `Stage`/`Ramp` concept Matt intends to live in Base/Util — the same building block used by Envelopes. Parallel-array v1 is a flat/linear-only special case.
- **Per-boundary transition width.** Even without per-segment shape, promoting `trans: float` to `trans: float[N-1]` allows sharp attack + smooth release on the same curve. Cheap extension.
- **Zero-duration at non-last indices.** Enable once the renderer is willing to compute fill duration from both ends.
- **Additional Score-layer ornaments.** Scoop, fall, plop, doit — each with its own compile-to-`PitchCurve` realization per instrument.
- **Arbitrary multi-point curves as a first-class Score concept.** Intentionally deferred. The tablature escape hatch (custom EventTypes in the Event Sequence stream) is the long-term answer.

## Sub-questions — resolved 2026-04-14

- **Location:** `mforce/music/`. Musical units (semitones, fractions of note duration).
- **Update cadence:** moot. Curve is compiled to an Envelope at `play_note` time and wired into the graph pre-render; no render-time iteration inside the Instrument.
- **Ownership:** `const PitchCurve*` at the Performer → Instrument boundary. Curve is consumed on entry; Envelope lives in the voice graph from there.
- **JSON serialization:** deferred TODO. Will align with Event Sequence conventions when curve-carrying Articulation/Ornament variants need to round-trip.
- **Articulation → variant refactor:** lands *first*, before the `PitchCurve` struct, so Bend-up has a clean home.
