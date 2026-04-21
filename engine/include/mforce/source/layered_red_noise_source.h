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
    explicit LayeredRedNoiseSource(int sampleRate, uint32_t seed = 0x1A4E4EDDu)
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

    void rebuild_layers_(const RenderContext& /*ctx*/) {
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
