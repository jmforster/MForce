# Output Peak Guard — Design Spec

**Status:** Narrowed draft.
**Author:** Claude (DSP/UI).
**Date:** 2026-04-20.

---

## History

An earlier draft of this spec bundled "release tail truncation" with
the peak-guard work. On closer inspection the release-tail story
wasn't an engine defect — `Envelope::make_adsr` (envelope.h:156) clamps
attack/decay to sensible minimums and expands sustain to fill, so the
envelope always reaches 0 at the end of `durSamples`. What sounded
like a hard cut in the pluck demo was the patch's own short release
(`"release": 0.1` → ~33 ms at the test tempo), not a rendering
truncation. Spec narrowed to the one actual engine bug.

## Problem

`Instrument::render` and `StereoMixer::render` sum voices / channels
directly with no peak guard. Wherever the accumulated float exceeds
±1.0, the 16-bit WAV writer saturates, producing harsh hard-clip
distortion. Confirmed audible on 2026-04-20 with
`reed_clarinet` + 4-note chord progression at full volume: raw peak
**2.86**, clipped to 1.0, listener reports classic digital crunch on
every chord. `check.py` flags FAIL `excessive_clipping`.

## Goals

1. **Soft-clipper before WAV write.** Smooth knee replacing hard int16
   saturation on over-threshold sums.
2. **Identity below threshold.** Signals that don't exceed the knee
   threshold pass through bit-identical to today. No distortion cost
   for clean renders.
3. **Belt-and-suspenders hard-clamp** at ±0.999 downstream of the soft
   clipper, so float edge cases can't still hit int16 saturation.
4. **Applied at both layers** — `Instrument::render` (covers single-
   instrument renders via `run_patch` / `run_chords` paths) and
   `StereoMixer::render` (covers multi-instrument mixes via
   `run_play` / `run_compose` / similar).

## Non-goals

- Loudness normalization (LUFS, RMS target).
- Look-ahead limiter or attack/release envelope on the clipper.
- User-tunable threshold via JSON (internal constant for now).
- Any release-tail / envelope / voice-lifecycle change.
- Any check in WAV writer itself — keep writer dumb, keep shaping in
  the render layer.

## Design

### The shaper

Single pure function in a new header `engine/include/mforce/render/limiter.h`:

```cpp
inline float soft_clip(float x) {
    constexpr float THR  = 0.95f;   // identity below this
    constexpr float CEIL = 0.99f;   // asymptote
    constexpr float HARD = 0.999f;  // belt-and-suspenders clamp

    float ax = std::fabs(x);
    if (ax < THR) return x;

    float sign = (x < 0) ? -1.0f : 1.0f;
    float over = ax - THR;
    float room = CEIL - THR;
    float shaped = THR + room * std::tanh(over / room);
    float y = sign * shaped;

    // Hard safety clamp: float edge cases can't leak past CEIL in theory
    // but tanh rounding + subsequent multiplication by volume/gain could.
    if (y >  HARD) y =  HARD;
    if (y < -HARD) y = -HARD;
    return y;
}
```

- Input < 0.95 → output is the input. Bit-identical.
- Input ≥ 0.95 → `tanh((x-0.95)/0.04)` maps `[0.95, ∞)` smoothly into
  `[0.95, 0.99)`.
- 0.999 final clamp catches float edge cases post-volume scaling.

**Why 0.95/0.99:** narrow knee, transparent below threshold, ~0.1 dB
of saturation ceiling. Wider knees (e.g. 0.90/0.99) trade transparency
for a warmer saturation character; Matt can tune after first listen if
he wants more color. Starting transparent because the immediate need is
"don't hard-clip", not "add character".

### Application points

**`Instrument::render`** (`engine/include/mforce/render/instrument.h:30`):
after the note-sum + volume-scale loop, before writing to `out`.

```cpp
if (volume != 1.0f) {
    for (int i = 0; i < frames; ++i)
        out[i] *= volume;
}
// Peak guard — applied after volume so the clip threshold is measured
// at the final instrument output level.
for (int i = 0; i < frames; ++i)
    out[i] = soft_clip(out[i]);
```

**`StereoMixer::render`** (`engine/src/mixer.cpp:11`): after
`outLR[i*2 + 0] += …; outLR[i*2 + 1] += …;` accumulation, before
returning.

```cpp
for (int i = 0; i < frames * 2; ++i)
    outLR[i] = soft_clip(outLR[i]);
```

**Why both layers.** An instrument internally clipping its own sum
would leave harsh content in `renderedNotes` that a downstream mixer
can't repair. A mixer-only clip lets a solo-instrument render path
(`run_patch`, `run_chords`) hit int16 saturation directly. Applying at
both keeps every output path bounded.

**Perf cost.** One `tanh` + one compare per sample per layer. For a
4-sec stereo render at 48k: 384k invocations per layer = ~1 ms on a
modern CPU. Negligible for offline; still fine for realtime.

## Validation

**Before/after on the established demos:**

1. `renders/_before_clip.wav` (reed_clarinet, full volume): pre-fix
   peak 2.86 saturated to 1.0, `check.py` FAIL excessive_clipping.
   Post-fix: re-render → raw peak still 2.86 internally, but
   post-clipper peak ≤ 0.99, `check.py` OK. Matt listens: distortion
   replaced by warmer saturation (still audibly loud, no longer
   *broken*).
2. `renders/_before_voicesteal.wav` (bend_test_pluck, volume 0.4): was
   already clean at peak 0.73 < 0.95 threshold. Post-fix: bit-identical
   output (clip function returns identity below threshold).
3. `renders/pluck_sanity.wav` (peak 0.593): post-fix bit-identical.
   `check.py` numbers unchanged.

**Regression sweep:** all existing `patches/*.json` renders used today
by the test set stay OK or WARN (no new FAILs introduced).

## Resolved open questions

- **Soft-clip threshold/ceiling:** 0.95/0.99 (narrow knee, transparent).
- **Hard-clamp safety net:** yes, at ±0.999 inside `soft_clip` itself.
- **Apply to DrumKit too:** yes, automatically — `DrumKit` inherits
  `Instrument::render`, so a single clip in `Instrument::render` covers
  both paths.
- **Apply in WAV writer instead?** No. Keep the writer mechanical; do
  shaping in the render layer so downstream consumers (future realtime
  path, future metrics pipeline, etc.) get bounded float output.
- **Configurable threshold per patch?** Not in MVP. Constants in header
  for now; revisit if a patch design case motivates it.

## Done when

1. `engine/include/mforce/render/limiter.h` exists, defines
   `soft_clip()`.
2. `Instrument::render` and `StereoMixer::render` call it in the places
   shown above.
3. Re-rendered `_before_clip.wav`: `check.py` reports OK (no
   `excessive_clipping`). Matt listens and confirms distortion is gone
   or acceptably softened.
4. Re-rendered `pluck_sanity.wav`: peak=0.593 rms=0.0525 — unchanged.
5. Build clean on Release.
