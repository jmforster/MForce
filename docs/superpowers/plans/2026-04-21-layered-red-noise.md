# LayeredRedNoiseSource Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `LayeredRedNoiseSource` — a convenience generator node that owns N (1–16, default 3) internal `RedNoiseSource` children and emits their straight sum per sample. Per-layer `frequency` + `amplitude` edited via an inspector table; other RedNoise params hardcoded. No input pins, only output.

**Architecture:** Single new header `engine/include/mforce/source/layered_red_noise_source.h` with the `LayeredRedNoiseSource` class. Registered in `SourceCategory::Generator`. UI menu entry under the existing "Noise" submenu. Per-layer internal seeds derived via `baseSeed ^ (i * 0x9E3779B9u)` — plays nicely with Multiplex's per-instance perturbation for free.

**Tech Stack:** C++17/20, existing CMake build. No new deps.

**Spec reference:** `docs/superpowers/specs/2026-04-21-layered-red-noise-design.md` (resolutions: default 3 rows, max 16, array-length authoritative at load, new-row defaults freq=7/amp=0.05, hardcoded per-layer `density=1, smoothness=1, rampVariation=0.5, boost=0, zeroCrossTendency=0, continuity=0`).

**Validation philosophy:** CLI render-path tests. Divergence: count=3 vs count=1 with matching amplitude totals → RMS ratios differ predictably. Reproducibility: same patch → bit-identical. Multiplex compatibility: nested in a Multiplex template, clones diverge.

**Not in scope:** Per-row density/smoothness/etc. sliders (deferred per spec). Adding `CombineOp::Sum` to `CombinedSource` (parked as follow-up cleanup).

---

## File Structure

**New files:**
- `engine/include/mforce/source/layered_red_noise_source.h` — the class.

**Modified files:**
- `engine/src/source_registrations.cpp` — register `LayeredRedNoiseSource`.
- `tools/mforce_ui/main.cpp` — add menu entry under "Noise".

---

## Task 1: LayeredRedNoiseSource class

**Files:**
- Create: `engine/include/mforce/source/layered_red_noise_source.h`

- [ ] **Step 1: Write the class**

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/red_noise_source.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace mforce {

// Convenience generator: owns N internal RedNoiseSources and emits their
// straight sum per sample. Per-layer frequency + amplitude configurable via
// an inspector table; other RedNoise params are hardcoded (see defaults
// below). No input pins — modulation is entirely from internal noise.
//
// Intended primarily as a humanizing modulator (e.g. `var` input on
// VarSource) where a single noise layer would feel too periodic.
struct LayeredRedNoiseSource final : ValueSource {
    explicit LayeredRedNoiseSource(int sampleRate, uint32_t seed = 0xLAYEREDu)
    : sampleRate_(sampleRate), baseSeed_(seed) {
        // Default 3 rows: slow drift + medium wobble + fast flutter.
        frequencies_ = {0.5f, 2.0f, 8.0f};
        amplitudes_  = {1.0f, 1.0f, 1.0f};
        count_ = 3;
    }

    const char* type_name() const override { return "LayeredRedNoiseSource"; }
    SourceCategory category() const override { return SourceCategory::Generator; }

    std::span<const ConfigDescriptor> config_descriptors() const override {
        static constexpr ConfigDescriptor descs[] = {
            {"count", ConfigType::Int, 3.0f, 1.0f, 16.0f},
        };
        return descs;
    }

    std::span<const ArrayDescriptor> array_descriptors() const override {
        static constexpr ArrayDescriptor descs[] = {
            {"frequency", "layers", 7.0f,  0.01f, 100.0f},
            {"amplitude", "layers", 0.05f, 0.0f,  10.0f},
        };
        return descs;
    }

    void set_config(std::string_view name, float value) override {
        if (name == "count") {
            int n = std::max(1, std::min(16, int(value)));
            if (n != count_) {
                count_ = n;
                resize_arrays_to_count_();
                layersDirty_ = true;
            }
        }
    }

    float get_config(std::string_view name) const override {
        if (name == "count") return float(count_);
        return 0.0f;
    }

    void set_array(std::string_view name, std::vector<float> values) override {
        if (name == "frequency") {
            frequencies_ = std::move(values);
            count_ = int(frequencies_.size());
            sync_arrays_();
            layersDirty_ = true;
        } else if (name == "amplitude") {
            amplitudes_ = std::move(values);
            count_ = int(amplitudes_.size());
            sync_arrays_();
            layersDirty_ = true;
        }
    }

    std::vector<float> get_array(std::string_view name) const override {
        if (name == "frequency") return frequencies_;
        if (name == "amplitude") return amplitudes_;
        return {};
    }

    void prepare(const RenderContext& ctx, int frames) override {
        if (layersDirty_) rebuild_layers_(ctx);
        for (auto& l : layers_) if (l) l->prepare(ctx, frames);
    }

    float next() override {
        float sum = 0.0f;
        for (auto& l : layers_) if (l) sum += l->next();
        cur_ = sum;
        return cur_;
    }

    float current() const override { return cur_; }

private:
    // Per-layer hardcoded RedNoise params (matches spec #5 resolution).
    static constexpr float LAYER_DENSITY          = 1.0f;
    static constexpr float LAYER_SMOOTHNESS       = 1.0f;
    static constexpr float LAYER_RAMP_VARIATION   = 0.5f;
    static constexpr float LAYER_BOOST            = 0.0f;
    static constexpr float LAYER_CONTINUITY       = 0.0f;
    static constexpr float LAYER_ZERO_CROSS_TEND  = 0.0f;

    void resize_arrays_to_count_() {
        frequencies_.resize(count_, 7.0f);
        amplitudes_.resize(count_, 0.05f);
    }

    void sync_arrays_() {
        int n = int(frequencies_.size());
        if (int(amplitudes_.size()) < n) amplitudes_.resize(n, 0.05f);
        else if (int(amplitudes_.size()) > n) amplitudes_.resize(n);
        if (int(frequencies_.size()) < n) frequencies_.resize(n, 7.0f);
    }

    void rebuild_layers_(const RenderContext& ctx) {
        layers_.clear();
        int n = int(frequencies_.size());
        layers_.reserve(n);
        for (int i = 0; i < n; ++i) {
            uint32_t layerSeed = baseSeed_ ^ (uint32_t(i) * 0x9E3779B9u);
            auto rn = std::make_shared<RedNoiseSource>(sampleRate_, layerSeed);
            rn->set_param("frequency", std::make_shared<ConstantSource>(frequencies_[i]));
            rn->set_param("amplitude", std::make_shared<ConstantSource>(amplitudes_[i]));
            rn->set_param("density",           std::make_shared<ConstantSource>(LAYER_DENSITY));
            rn->set_param("smoothness",        std::make_shared<ConstantSource>(LAYER_SMOOTHNESS));
            rn->set_param("rampVariation",     std::make_shared<ConstantSource>(LAYER_RAMP_VARIATION));
            rn->set_param("boost",             std::make_shared<ConstantSource>(LAYER_BOOST));
            rn->set_param("continuity",        std::make_shared<ConstantSource>(LAYER_CONTINUITY));
            rn->set_param("zeroCrossTendency", std::make_shared<ConstantSource>(LAYER_ZERO_CROSS_TEND));
            layers_.push_back(std::move(rn));
        }
        layersDirty_ = false;
    }

    int sampleRate_;
    uint32_t baseSeed_;
    int count_{3};
    std::vector<float> frequencies_;
    std::vector<float> amplitudes_;
    std::vector<std::shared_ptr<RedNoiseSource>> layers_;
    bool layersDirty_{true};
    float cur_{0.0f};
};

} // namespace mforce
```

**Note on the seed literal `0xLAYEREDu`:** that's not valid hex (L, A, Y, R, D aren't hex digits). Use `0x1A4E4EDDu` as a "looks-like-LAYERED" seed, or any memorable constant. Pick something and move on.

- [ ] **Step 2: Fix the seed literal**

In the class definition above, replace `0xLAYEREDu` with `0x1A4E4EDDu` (or another valid 32-bit constant — whatever you prefer).

- [ ] **Step 3: Build engine (compile check)**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine 2>&1 | tail -3
```

Expected: PASS. If there are errors with `ConstantSource` not being recognized, the header needs `#include "mforce/core/dsp_value_source.h"` which already includes ConstantSource — should be fine. If `RedNoiseSource` constructor signature mismatches, check the actual signature in `red_noise_source.h:10`.

---

## Task 2: Register + UI menu

**Files:**
- Modify: `engine/src/source_registrations.cpp`
- Modify: `tools/mforce_ui/main.cpp`

- [ ] **Step 1: Add registration**

In `source_registrations.cpp`, add include near the other `noise` source includes:
```cpp
#include "mforce/source/layered_red_noise_source.h"
```

And register near the other noise-family entries:
```cpp
reg.register_type("LayeredRedNoiseSource", SourceCategory::Generator,
    [](int sr, auto seed) {
        return std::make_shared<LayeredRedNoiseSource>(sr, seed.value_or(0x1A4E4EDDu));
    });
```

Place it in the Noise block (search for `"RedNoiseSource"` registration; insert your new one right after it).

- [ ] **Step 2: Add menu entry**

In `tools/mforce_ui/main.cpp`, find the Noise submenu (grep `BeginMenu("Noise")`). Add the Layered Red entry right after the existing noise entries:

```cpp
// inside the Noise submenu BeginMenu block
menu_source("Layered Red", "LayeredRedNoiseSource");
```

Pick a sensible position (I'd put it after Murmuration, before the ";" end of the noise list, so it's grouped with the other color/character noise entries).

- [ ] **Step 3: Build everything**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli mforce_ui 2>&1 | tail -5
```

Expected: PASS across all three targets. If `mforce_ui` is locked (running), ask Matt to close it.

---

## Task 3: Validation

**Files:**
- Create: `patches/lrn_test.json` (for divergence + reproducibility tests; keep if useful, delete if scratch)

- [ ] **Step 1: Write a minimal test patch**

```json
{
  "sampleRate": 48000,
  "seconds": 2,
  "graph": {
    "nodes": [
      { "id": "lrn1", "type": "LayeredRedNoiseSource",
        "params": { "count": 3,
                    "frequency": [0.5, 2.0, 8.0],
                    "amplitude": [0.3, 0.3, 0.3] } },
      { "id": "ch1", "type": "SoundChannel",
        "inputs": { "source": "lrn1" },
        "params": { "volume": 1.0, "pan": 0.0 } },
      { "id": "mix", "type": "StereoMixer",
        "inputs": { "channels": ["ch1"] },
        "params": { "gainL": 1.0, "gainR": 1.0 } }
    ],
    "output": "mix"
  }
}
```

- [ ] **Step 2: Render and sanity-check**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/lrn_test.json renders/_lrn.wav 2>&1 | tail -3
python tools/metrics/check.py renders/_lrn.wav 2>&1 | head -2
```

Expected: OK from check.py, non-zero peak/rms, non-silent output. If peak is 0, the layers aren't summing — debug.

- [ ] **Step 3: Reproducibility check**

Render twice, compare:
```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/lrn_test.json renders/_lrn_a.wav 2>&1 | tail -2
build/tools/mforce_cli/Release/mforce_cli.exe patches/lrn_test.json renders/_lrn_b.wav 2>&1 | tail -2
cmp renders/_lrn_a.wav renders/_lrn_b.wav && echo "REPRODUCIBLE" || echo "NON-REPRODUCIBLE"
```

Expected: `REPRODUCIBLE` (bit-identical).

- [ ] **Step 4: Multiplex compatibility check**

```json
{
  "sampleRate": 48000,
  "seconds": 2,
  "graph": {
    "nodes": [
      { "id": "lrn1", "type": "LayeredRedNoiseSource",
        "params": { "count": 3,
                    "frequency": [0.5, 2.0, 8.0],
                    "amplitude": [0.3, 0.3, 0.3] } },
      { "id": "mux1", "type": "MultiplexSource",
        "params": { "count": 2, "source": { "ref": "lrn1" } } },
      { "id": "ch1", "type": "SoundChannel",
        "inputs": { "source": "mux1" },
        "params": { "volume": 1.0, "pan": 0.0 } },
      { "id": "mix", "type": "StereoMixer",
        "inputs": { "channels": ["ch1"] },
        "params": { "gainL": 1.0, "gainR": 1.0 } }
    ],
    "output": "mix"
  }
}
```

Save as `patches/lrn_mux_test.json` and:

```bash
# count=2
build/tools/mforce_cli/Release/mforce_cli.exe patches/lrn_mux_test.json renders/_lrn_mux2.wav 2>&1 | tail -2
python tools/metrics/check.py renders/_lrn_mux2.wav 2>&1 | head -1

# count=1
sed -i 's/"count": 2/"count": 1/' patches/lrn_mux_test.json
build/tools/mforce_cli/Release/mforce_cli.exe patches/lrn_mux_test.json renders/_lrn_mux1.wav 2>&1 | tail -2
python tools/metrics/check.py renders/_lrn_mux1.wav 2>&1 | head -1
sed -i 's/"count": 1/"count": 2/' patches/lrn_mux_test.json

python -c "
import json
r1 = json.load(open('renders/_lrn_mux1.features.json'))['features']['rms']
r2 = json.load(open('renders/_lrn_mux2.features.json'))['features']['rms']
print(f'rms(mux1)={r1:.4f}  rms(mux2)={r2:.4f}  ratio={r2/r1:.3f}  (expect ~0.707)')
"
```

Expected: ratio ≈ 0.707 (Mux:2 clones of LayeredRed have independently seeded layers; summing decorrelates by √2).

- [ ] **Step 5: Cleanup scratch + commit**

```bash
rm renders/_lrn*.wav renders/_lrn*.features.json patches/lrn_mux_test.json
# Keep lrn_test.json as a reference patch, or delete if Matt prefers clean patches/.
```

```bash
git add engine/include/mforce/source/layered_red_noise_source.h \
        engine/src/source_registrations.cpp \
        tools/mforce_ui/main.cpp \
        patches/lrn_test.json
git commit -m "feat(source): LayeredRedNoiseSource — convenience N-layer noise summer

Owns N (1..16, default 3) internal RedNoiseSources with per-layer
frequency + amplitude (table-edited). Other RedNoise params hardcoded
(density=1, smoothness=1, rampVariation=0.5, boost=0, continuity=0,
zeroCrossTendency=0).

Per-layer seed derived as baseSeed ^ (i * 0x9E3779B9u), giving
divergent streams within a single instance and composing correctly
with Multiplex's per-instance perturbation.

Registered under Generator category; menu entry under Noise submenu
labeled 'Layered Red'.

Validation: reproducibility bit-identical; Multiplex:2 clones show
rms ratio ≈ 0.707 (seeded divergence intact).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-review

- No placeholders — full class implementation, full test patches, exact commands.
- Seed literal `0xLAYEREDu` called out as invalid and replaced with `0x1A4E4EDDu`.
- UI integration leverages existing array-table + config machinery; only change is a single `menu_source` line.
- Validation covers the three things that matter: produces audio, reproducible, composes with Multiplex.
- Total file footprint: one new header (~130 lines), two tiny patches to existing files.
