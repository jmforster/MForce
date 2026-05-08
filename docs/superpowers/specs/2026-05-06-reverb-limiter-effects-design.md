# Reverb + Limiter Effect Nodes — Design Spec

**Date:** 2026-05-06
**Status:** Approved (Matt) — Wave 1 of effect-node roadmap
**Predecessor:** Envelope accuracy on `main` through `73886e3`

---

## Goal

Add two new effect nodes to MForce's patch graph: **Reverb** and **Limiter**. Both follow the existing filter pattern (`DelayFilter`, `BWLowpassFilter`, `Vibrato`) — `ValueSource` subclasses that take a `source` input pin, expose scalar params via the self-describing surface, integrate with the inspector + JSON + Multiplex automatically.

Distinct from the existing render-stage `engine/include/mforce/render/limiter.h::soft_clip` which is a fixed safety belt at `Instrument::render` / `StereoMixer::render`. The new node is configurable, placeable anywhere in the patch graph.

## Out of scope (Wave 1)

- Stereo reverb (input is mono; user pans via SoundChannel)
- Convolution reverb (separate node, Wave 3 if needed)
- Compressor, chorus, parametric EQ — Wave 2/3

---

## Reverb (Schroeder/Freeverb-style)

### Topology

8 parallel feedback comb filters with damping (lowpass on the feedback path) → sum → 4 series allpass filters → wet/dry mix. Public-domain Freeverb tuning by Jezar at Dreampoint.

### Tunings (samples at 44.1 kHz; scaled by `sampleRate / 44100`)

- Combs: `1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617`
- Allpass: `556, 441, 341, 225`

### Per-comb processing

```
out  = buf[idx]
filt = out * damp2 + filt * damp1   // lowpass damping in feedback
buf[idx] = input + filt * feedback
idx = (idx + 1) % size
```

Where `feedback = roomSize * 0.28 + 0.7`, `damp1 = damping * 0.4`, `damp2 = 1 - damp1`.

### Per-allpass processing

```
bufout = buf[idx]
out = -input + bufout
buf[idx] = input + bufout * 0.5
idx = (idx + 1) % size
```

Allpass feedback fixed at 0.5 (Freeverb default).

### Output

```
combs_sum = sum(comb[i].process(input * 0.015)) for i in 0..7   // 0.015 = fixedgain
ap_out = allpass[3].process(allpass[2].process(allpass[1].process(allpass[0].process(combs_sum))))
output = ap_out * wet + input * dry
```

### Param surface

| Param | Type | Default | Range | Notes |
|---|---|---|---|---|
| source | input | — | — | audio input |
| roomSize | param | 0.5 | 0..1 | feedback amount, longer tail at higher values |
| damping | param | 0.5 | 0..1 | high-frequency rolloff in feedback path |
| wet | param | 0.33 | 0..1 | reverb level |
| dry | param | 0.4 | 0..1 | direct-signal level |

All params are `shared_ptr<ValueSource>` so they can be modulated by other graph nodes.

### State

- 8 comb buffers (sized per comb tuning, scaled to sample rate)
- 4 allpass buffers (sized per tuning)
- Per-comb `filt_` for damping smoothing
- Per-comb / per-allpass write pointers

All buffers allocated in `prepare()`; no heap in `next()`.

---

## Limiter (lookahead brick-wall)

### Topology

1. Track input peak with fast-attack / slow-release envelope follower.
2. Compute target gain: `(peak > threshold) ? threshold / peak : 1.0`.
3. Apply gain with instant attack / smoothed release.
4. Output = lookahead-delayed input × gain.

Lookahead delay (5ms default) means gain reduction kicks in *before* the loud sample reaches the output, yielding a transparent brick-wall.

### Per-sample

```
input_t = source->next()
abs_t   = |input_t|

// Peak follower (fast attack, slow release)
peak = max(abs_t, peak * release_coef)

// Gain target
target = (peak > threshold) ? threshold / peak : 1.0

// Gain smoothing (instant down, smooth up)
gain = (target < gain) ? target : gain + (target - gain) * release_smooth

// Lookahead-delayed output
buf[w] = input_t
out = buf[(w - lookahead_samples) % size] * gain
w = (w + 1) % size
```

### Param surface

| Param | Type | Default | Range | Notes |
|---|---|---|---|---|
| source | input | — | — | audio input |
| threshold | param | 0.95 | 0..1 | output ceiling |
| release | param | 0.05 | 0.001..1 | seconds; lower = faster pumping |

`lookahead` fixed at 5ms internally (240 samples at 48k). Attack is instant (no param). Could expose later.

### State

- Lookahead circular buffer (sample rate × 0.005 samples)
- `peak_` envelope follower
- `gain_` smoothed gain
- Write pointer

All allocated in `prepare()`; no heap in `next()`.

---

## File layout

- `engine/include/mforce/filter/reverb.h` — `Reverb` (header-only, ~150 lines).
- `engine/include/mforce/filter/limiter.h` — `Limiter` (header-only, ~80 lines).

(Existing `filter/filters.h` is at 440 lines; not extending it further.)

## Registration

`engine/src/source_registrations.cpp` adds:

```cpp
reg.register_type("Reverb",  SourceCategory::Filter,
    [](int sr, auto) { return std::make_shared<Reverb>(sr); });
reg.register_type("Limiter", SourceCategory::Filter,
    [](int sr, auto) { return std::make_shared<Limiter>(sr); });
```

No configurator needed — both use generic `wire_params_generic` for params.

## UI

Add menu entries in `tools/mforce_ui/main.cpp` Filters menu (around line 3438):

```cpp
menu_source("Reverb",  "Reverb");
menu_source("Limiter", "Limiter");
```

Inspector picks them up automatically via `param_descriptors()`.

## Verification

1. Build mforce_cli; render `Additive1.json` → confirm baseline parity (no Reverb/Limiter wired in, output identical).
2. Hand-write `patches/_fx_reverb_test.json`: a SineSource → Reverb (defaults) → SoundChannel. Render. Audio file should be longer than input (reverb tail) with reasonable peak/rms.
3. Hand-write `patches/_fx_limiter_test.json`: a hot-signal source (e.g. Pulse with high amplitude) → Limiter (threshold=0.5) → SoundChannel. Render. Output peak should not exceed 0.5 ± epsilon.

## Backward compat

Pure additions. No existing patch JSON or code changes.
