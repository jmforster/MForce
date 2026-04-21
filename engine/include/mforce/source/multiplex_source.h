#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mforce {

// MultiplexSource: fans one template subgraph into N independent instances,
// each built with a perturbed seed, and emits sum(instances) / N per sample.
// The loader special-cases this node to populate instances_ at build time
// from an extracted JSON subtree (stored in templateJson_) via an
// InstanceBuilder closure that encodes seed-perturbation + sampleRate.
//
// Live-edit rebuild (UI path): external code updates templateJson_ to the
// current UI serialization and sets templateDirty_; next prepare() rebuilds.
struct MultiplexSource final : ValueSource {
    using InstanceBuilder = std::function<std::shared_ptr<ValueSource>(int instanceIdx)>;

    const char* type_name() const override { return "MultiplexSource"; }
    SourceCategory category() const override { return SourceCategory::Combiner; }

    std::span<const InputDescriptor> input_descriptors() const override {
        static constexpr InputDescriptor descs[] = {
            {"source", false},  // single template source
        };
        return descs;
    }

    std::span<const ConfigDescriptor> config_descriptors() const override {
        static constexpr ConfigDescriptor descs[] = {
            {"count", ConfigType::Int, 10.0f, 1.0f, 50.0f},
        };
        return descs;
    }

    void set_config(std::string_view name, float value) override {
        if (name == "count") {
            int newCount = std::max(1, std::min(50, int(value)));
            if (newCount != count_) {
                count_ = newCount;
                templateDirty_ = true;
            }
        }
    }

    float get_config(std::string_view name) const override {
        if (name == "count") return float(count_);
        return 0.0f;
    }

    // UI wiring path: when the user connects a source to the "source" pin,
    // store it so next() can fall back to passing it through solo. The CLI
    // loader's set_template path is the one that produces real N-instance
    // fan-out; without that, we pass the wired source through as a solo
    // preview so the UI isn't silent.
    void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
        if (name == "source") uiSource_ = std::move(src);
    }

    std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
        if (name == "source") return uiSource_;
        return nullptr;
    }

    // Loader hook. The builder closure captures everything needed (subtree
    // JSON, base seed, sample rate) to produce a freshly built root source
    // for a given instance index with the correct seed perturbation.
    void set_template(std::string json, uint32_t baseSeed, InstanceBuilder builder) {
        templateJson_ = std::move(json);
        baseSeed_ = baseSeed;
        builder_ = std::move(builder);
        templateDirty_ = true;
    }

    // External dirty signal (UI sets this after editing template nodes).
    void mark_dirty() { templateDirty_ = true; }

    void prepare(const RenderContext& ctx, int frames) override {
        if (templateDirty_) rebuild_();
        if (!instances_.empty()) {
            for (auto& inst : instances_)
                if (inst) inst->prepare(ctx, frames);
        } else if (uiSource_) {
            // UI solo-preview fallback
            uiSource_->prepare(ctx, frames);
        }
    }

    float next() override {
        if (!instances_.empty()) {
            float sum = 0.0f;
            int n = int(instances_.size());
            for (auto& inst : instances_)
                if (inst) sum += inst->next();
            cur_ = (n > 0) ? sum / float(n) : 0.0f;
        } else if (uiSource_) {
            cur_ = uiSource_->next();  // solo passthrough (UI preview)
        } else {
            cur_ = 0.0f;
        }
        return cur_;
    }

    float current() const override { return cur_; }

private:
    void rebuild_() {
        instances_.clear();
        if (!builder_) { templateDirty_ = false; return; }
        instances_.reserve(count_);
        for (int i = 0; i < count_; ++i) {
            instances_.push_back(builder_(i));
        }
        templateDirty_ = false;
    }

    int count_{10};
    uint32_t baseSeed_{0};
    std::string templateJson_;
    InstanceBuilder builder_;
    std::vector<std::shared_ptr<ValueSource>> instances_;
    std::shared_ptr<ValueSource> uiSource_;  // UI-wired source for solo preview
    bool templateDirty_{true};
    float cur_{0.0f};
};

} // namespace mforce
