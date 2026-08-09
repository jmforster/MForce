# Output Peak Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `soft_clip()` utility and apply it at both `Instrument::render` and `StereoMixer::render` so no rendering path can produce samples outside ±0.999. Eliminates the `excessive_clipping` failure mode without distorting already-clean renders.

**Architecture:** Single pure-function header `engine/include/mforce/render/limiter.h`. Narrow-knee tanh shaper (identity below 0.95, asymptotic to 0.99) with a hard ±0.999 clamp belt-and-suspenders. Called from the two summing layers. No API changes.

**Tech Stack:** C++17/20, existing CMake build.

**Spec reference:** `docs/superpowers/specs/2026-04-20-release-tail-and-peak-guard-design.md`.

**Validation philosophy:** Re-render `renders/_before_clip.wav` post-fix and check that `check.py` reports OK (no `excessive_clipping`). Re-render clean patches (`pluck_sanity`, `fadd_formant_test`) and verify bit-identical peak/rms. No automated tests — this repo has none.

---

## File Structure

**New files:**
- `engine/include/mforce/render/limiter.h` — the `soft_clip()` function.

**Modified files:**
- `engine/include/mforce/render/instrument.h` — call `soft_clip` in `Instrument::render` after volume scaling.
- `engine/src/mixer.cpp` — call `soft_clip` in `StereoMixer::render` after the accumulation loop.

---

## Task 1: Introduce `soft_clip` and wire into render paths

**Files:**
- Create: `engine/include/mforce/render/limiter.h`
- Modify: `engine/include/mforce/render/instrument.h`
- Modify: `engine/src/mixer.cpp`

- [ ] **Step 1: Create the limiter header**

```cpp
#pragma once
#include <cmath>

namespace mforce {

// Narrow-knee soft clipper. Identity below THR; asymptotic to CEIL.
// HARD is a belt-and-suspenders clamp so downstream volume scaling can't
// leak past int16 saturation.
inline float soft_clip(float x) {
    constexpr float THR  = 0.95f;
    constexpr float CEIL = 0.99f;
    constexpr float HARD = 0.999f;

    float ax = std::fabs(x);
    if (ax < THR) return x;

    float sign = (x < 0) ? -1.0f : 1.0f;
    float over = ax - THR;
    float room = CEIL - THR;
    float shaped = THR + room * std::tanh(over / room);
    float y = sign * shaped;

    if (y >  HARD) y =  HARD;
    if (y < -HARD) y = -HARD;
    return y;
}

} // namespace mforce
```

- [ ] **Step 2: Call from `Instrument::render`**

In `engine/include/mforce/render/instrument.h`, add the include at the top (alongside existing `mixer.h` include):

```cpp
#include "mforce/render/limiter.h"
```

Then replace the end of `Instrument::render` (the volume-scaling block) with:

Before:
```cpp
    if (volume != 1.0f) {
      for (int i = 0; i < frames; ++i)
        out[i] *= volume;
    }
  }
```

After:
```cpp
    if (volume != 1.0f) {
      for (int i = 0; i < frames; ++i)
        out[i] *= volume;
    }
    // Peak guard: bound output to ±0.999 with a smooth knee above 0.95.
    for (int i = 0; i < frames; ++i)
      out[i] = soft_clip(out[i]);
  }
```

- [ ] **Step 3: Call from `StereoMixer::render`**

In `engine/src/mixer.cpp`, add the include at the top:
```cpp
#include "mforce/render/limiter.h"
```

Then at the end of `StereoMixer::render`, after the per-channel accumulation loop closes, add a final clip pass:

Before (end of function):
```cpp
      outLR[i*2 + 0] += s * aL * gl;
      outLR[i*2 + 1] += s * aR * gr;
    }
  }
}
```

After:
```cpp
      outLR[i*2 + 0] += s * aL * gl;
      outLR[i*2 + 1] += s * aR * gr;
    }
  }
  // Peak guard: bound mix output to ±0.999.
  for (int i = 0; i < frames * 2; ++i)
    outLR[i] = soft_clip(outLR[i]);
}
```

- [ ] **Step 4: Build clean on Release**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli mforce_ui 2>&1 | tail -5
```

Expected: PASS. If any include-order issue surfaces, fix and rebuild.

- [ ] **Step 5: Regression render — confirm clean patches bit-identical**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/_t1_pluck.wav 2>&1 | tail -2
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_t1_fadd.wav 2>&1 | tail -2
```

Expected:
- pluck_sanity: `peak=0.593099 rms=0.0525044 nonzero=191998/240000`
- fadd_formant_test: `peak=0.179758 rms=0.0151559 nonzero=383958/384000`

If either peak differs, the soft-clip isn't identity below 0.95 — investigate. Both patches stay well under 0.95 so output must be bit-identical.

- [ ] **Step 6: Fix validation — re-render the clip demo**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --chords patches/reed_clarinet.json renders/_after_clip.wav 100 2 C:M F:M G:M C:M --volume 1.0 2>&1 | tail -4
python tools/metrics/check.py renders/_after_clip.wav 2>&1
```

Expected:
- CLI peak line still shows internal peak near 2.86 (raw sum before clip).
- BUT: WAV content peak after soft_clip ≤ 0.999.
- `check.py`: **OK** (or at most WARN — no `excessive_clipping` reason).

If `check.py` still reports FAIL `excessive_clipping`, the clip function isn't being called or the threshold is leaking. Debug before continuing.

- [ ] **Step 7: Cleanup + commit**

```bash
rm renders/_t1_pluck.wav renders/_t1_fadd.wav renders/_after_clip.wav renders/*.features.json 2>/dev/null
```

(The scratch files; keep `_before_clip.wav` for A/B comparison.)

```bash
git add engine/include/mforce/render/limiter.h engine/include/mforce/render/instrument.h engine/src/mixer.cpp
git commit -m "feat(render): soft-clip peak guard on Instrument + StereoMixer output

Adds soft_clip() (tanh-knee above 0.95, asymptotic to 0.99, hard
clamp at 0.999). Called at the end of Instrument::render and
StereoMixer::render so no render path can produce samples outside
±0.999.

Fixes the harsh-hard-clip distortion observed in chord renders
(raw peak >> 1.0 saturating to int16). Clean patches bit-identical
(pluck_sanity peak=0.593099, fadd_formant_test peak=0.179758).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Final A/B + listener handoff

**Files:** none (pure validation + regenerate the before/after WAV pair for Matt's ear).

- [ ] **Step 1: Regenerate the "after" demo**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --chords patches/reed_clarinet.json renders/_after_clip.wav 100 2 C:M F:M G:M C:M --volume 1.0 2>&1 | tail -4
python tools/metrics/check.py renders/_after_clip.wav 2>&1 | head -2
python tools/metrics/check.py renders/_before_clip.wav 2>&1 | head -2
```

Expected:
- After: OK, peak=0.99ish.
- Before: FAIL excessive_clipping, peak=1.000 (unchanged).

- [ ] **Step 2: Report to Matt**

Present both file paths and ask him to A/B listen. If he confirms the distortion is resolved (or acceptably softened), we're done. If the soft-clip character is too colored for his taste, tune THR/CEIL/width and rebuild.

---

## Self-review

- No placeholders — every step has the exact code or command.
- Code blocks show both the "before" and "after" of each edit so the implementer can locate the change point unambiguously.
- Validation is mechanical (render + check.py) plus one listener pass at the end. No automated test framework needed.
- Scope: 3 files, ~20 new lines of code, 2 include additions. Smallest meaningful engine-quality fix in this repo's history.
