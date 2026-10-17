#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/red_noise_source.h"
#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

namespace mforce {

// Convenience generator: owns N internal RedNoiseSources and emits their
// straight sum per sample. Per-layer frequency + amplitude configurable via
// an inspector table; other RedNoise params are hardcoded (see defaults
// below). No input pins — modulation is entirely from internal noise.
//
// Intended primarily as a humanizing modulator (e.g. `var` input on
// VarSource) where a single noise layer would feel too periodic.
struct LayeredRedNoiseSource final : Generator {
    Setting<int> count{this, "count", 3, 1, 16};
    // The two arrays are the layer table. A row added by a resize takes the
    // pin's default.
    Array frequency{this, "frequency", "layers", 7.0f,  0.01f, 100.0f};
    Array amplitude{this, "amplitude", "layers", 0.05f, 0.0f,  10.0f};

    explicit LayeredRedNoiseSource(int sampleRate, uint32_t seed = 0x1A4E4EDDu)
    : sampleRate_(sampleRate), baseSeed_(seed) {
        // Default 3 rows: slow drift + medium wobble + fast flutter.
        frequency.values = {0.5f, 2.0f, 8.0f};
        amplitude.values = {1.0f, 1.0f, 1.0f};
    }

    // No unit range: the layers are summed.
    const char* type_name() const override { return "LayeredRedNoiseSource"; }
    SourceCategory category() const override { return SourceCategory::Generator; }

    // The base stores `count` before this runs, so layerCount_ keeps the
    // count the arrays were last sized to.
    void on_setting_changed(std::string_view name) override {
        if (name == "count") {
            int n = std::max(1, std::min(16, count.value));
            if (n != layerCount_) {
                layerCount_ = n;
                resize_arrays_to_count_();
                layersDirty_ = true;
            }
            count.value = layerCount_;
        }
    }

    void on_array_changed(std::string_view name) override {
        if (name == "frequency") {
            layerCount_ = int(frequency.values.size());
            sync_arrays_();
            layersDirty_ = true;
        } else if (name == "amplitude") {
            layerCount_ = int(amplitude.values.size());
            sync_arrays_();
            layersDirty_ = true;
        }
        count.value = layerCount_;
    }

    void on_prepare(const RenderContext& ctx, int frames) override {
        if (layersDirty_) rebuild_layers_(ctx);
        for (auto& l : layers_) if (l) l->prepare(ctx, frames);
    }

    // Per-note determinism (onsets-v2 addendum): forward to the layers.
    void reseed() override {
        for (auto& l : layers_) if (l) l->reseed();
    }

    float compute() override {
        float sum = 0.0f;
        for (auto& l : layers_) if (l) sum += l->advance();
        return sum;
    }

private:
    // Per-layer hardcoded RedNoise params (matches spec #5 resolution).
    static constexpr float LAYER_DENSITY          = 1.0f;
    static constexpr float LAYER_SMOOTHNESS       = 1.0f;
    static constexpr float LAYER_RAMP_VARIATION   = 0.5f;
    static constexpr float LAYER_BOOST            = 0.0f;
    static constexpr float LAYER_CONTINUITY       = 0.0f;
    static constexpr float LAYER_ZERO_CROSS_TEND  = 0.0f;

    void resize_arrays_to_count_() {
        frequency.values.resize(layerCount_, frequency.def);
        amplitude.values.resize(layerCount_, amplitude.def);
    }

    void sync_arrays_() {
        int n = int(frequency.values.size());
        if (int(amplitude.values.size()) < n) amplitude.values.resize(n, amplitude.def);
        else if (int(amplitude.values.size()) > n) amplitude.values.resize(n);
        if (int(frequency.values.size()) < n) frequency.values.resize(n, frequency.def);
    }

    void rebuild_layers_(const RenderContext& /*ctx*/) {
        layers_.clear();
        int n = int(frequency.values.size());
        layers_.reserve(n);
        for (int i = 0; i < n; ++i) {
            uint32_t layerSeed = baseSeed_ ^ (uint32_t(i) * 0x9E3779B9u);
            auto rn = std::make_shared<RedNoiseSource>(sampleRate_, layerSeed);
            rn->frequency.set(std::make_shared<ConstantSource>(frequency.values[i]));
            rn->amplitude.set(std::make_shared<ConstantSource>(amplitude.values[i]));
            rn->density.set(std::make_shared<ConstantSource>(LAYER_DENSITY));
            rn->smoothness.set(std::make_shared<ConstantSource>(LAYER_SMOOTHNESS));
            rn->rampVariation.set(std::make_shared<ConstantSource>(LAYER_RAMP_VARIATION));
            rn->boost.set(std::make_shared<ConstantSource>(LAYER_BOOST));
            rn->continuity.set(std::make_shared<ConstantSource>(LAYER_CONTINUITY));
            rn->zeroCrossTendency.set(std::make_shared<ConstantSource>(LAYER_ZERO_CROSS_TEND));
            layers_.push_back(std::move(rn));
        }
        layersDirty_ = false;
    }

    int sampleRate_;
    uint32_t baseSeed_;
    int layerCount_{3};
    std::vector<std::shared_ptr<RedNoiseSource>> layers_;
    bool layersDirty_{true};
};

} // namespace mforce
