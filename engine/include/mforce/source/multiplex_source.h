#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
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
//
// ParamMap fan-out (play_note path): PitchedInstrument's paramMap binds to a
// top-level node whose counterpart exists in each clone. set_clone_param()
// stores a pending value + applies it immediately to any live clone, so
// frequency (etc.) updates propagate into all N instances.
struct MultiplexSource final : ValueSource {
    // Builder returns {root source, valueNodes map by id}. The map gives
    // external code (paramMap fan-out) the handle to reach into each
    // clone's internals.
    using ValueNodeMap = std::unordered_map<std::string, std::shared_ptr<ValueSource>>;
    using InstanceBuilder = std::function<
        std::pair<std::shared_ptr<ValueSource>, ValueNodeMap>(int instanceIdx)>;

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
    // plus valueNodes map for a given instance index.
    void set_template(std::string json, uint32_t baseSeed, InstanceBuilder builder) {
        templateJson_ = std::move(json);
        baseSeed_ = baseSeed;
        builder_ = std::move(builder);
        templateDirty_ = true;
    }

    // External dirty signal (UI sets this after editing template nodes).
    void mark_dirty() { templateDirty_ = true; }

    // ParamMap fan-out: set a param on a node (by id) across every clone.
    // Stores as pending so it applies to future rebuilds too. Called by
    // PitchedInstrument::play_note after setting the top-level paramMap
    // ConstantSource, so clones pick up the same note frequency.
    void set_clone_param(const std::string& nodeId,
                         const std::string& paramName,
                         float value) {
        pendingCloneParams_[nodeId + "." + paramName] =
            std::make_tuple(nodeId, paramName, value);
        apply_clone_param_to_all_(nodeId, paramName, value);
    }

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
        instanceValueNodes_.clear();
        if (!builder_) { templateDirty_ = false; return; }
        instances_.reserve(count_);
        instanceValueNodes_.reserve(count_);
        for (int i = 0; i < count_; ++i) {
            auto pair = builder_(i);
            instances_.push_back(std::move(pair.first));
            instanceValueNodes_.push_back(std::move(pair.second));
        }
        // Replay any pending paramMap fan-outs onto the freshly built clones.
        for (auto& [_, entry] : pendingCloneParams_) {
            const auto& [nodeId, paramName, value] = entry;
            apply_clone_param_to_all_(nodeId, paramName, value);
        }
        templateDirty_ = false;
    }

    // For each live clone, find the node with the given id and set the named
    // param's ConstantSource to `value`. Silently no-ops when the node or
    // param isn't found (e.g. paramMap points to something outside the
    // template subgraph).
    void apply_clone_param_to_all_(const std::string& nodeId,
                                   const std::string& paramName,
                                   float value) {
        for (auto& instMap : instanceValueNodes_) {
            auto it = instMap.find(nodeId);
            if (it == instMap.end()) continue;
            auto cur = it->second->get_param(paramName);
            auto cs = std::dynamic_pointer_cast<ConstantSource>(cur);
            if (cs) cs->set(value);
        }
    }

    int count_{10};
    uint32_t baseSeed_{0};
    std::string templateJson_;
    InstanceBuilder builder_;
    std::vector<std::shared_ptr<ValueSource>> instances_;
    std::vector<ValueNodeMap> instanceValueNodes_;
    // Pending paramMap fan-outs: keyed by "nodeId.paramName" so a repeated
    // set overwrites. Value is {nodeId, paramName, value} for replay on
    // rebuild (rebuilds discard live clones so pending needs to survive).
    std::unordered_map<std::string,
                       std::tuple<std::string, std::string, float>>
        pendingCloneParams_;
    std::shared_ptr<ValueSource> uiSource_;  // UI-wired source for solo preview
    bool templateDirty_{true};
    float cur_{0.0f};
};

} // namespace mforce
