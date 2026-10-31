#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <cassert>
#include <utility>
#include <vector>

namespace mforce {

// MultiplexSource: fans one template subgraph into N independent instances,
// each built with a perturbed seed, and emits sum(instances) / N per sample.
// The loader special-cases this node to populate instances_ at build time
// from an extracted JSON subtree (stored in templateJson_) via an
// InstanceBuilder closure that encodes seed-perturbation. The builder
// returns unprepared clones; the multiplex prepares and starts them.
//
// Live-edit rebuild (UI path): external code updates templateJson_ to the
// current UI serialization and sets templateDirty_; next prepare() rebuilds.
//
// ParamMap fan-out (play_note path): PitchedInstrument's paramMap binds to a
// top-level node whose counterpart exists in each clone. set_clone_param()
// stores a pending value + applies it immediately to any live clone, so
// frequency (etc.) updates propagate into all N instances.
struct MultiplexSource final : Processor {
    // Builder returns {root source, the clone's nodes by id}. The map gives
    // external code (paramMap fan-out) the handle to reach into each
    // clone's internals.
    using NodeMap = std::unordered_map<std::string, std::shared_ptr<Component>>;
    using InstanceBuilder = std::function<
        std::pair<std::shared_ptr<ValueSource>, NodeMap>(int instanceIdx)>;

    Setting<int> count{this, "count", 10, 1, 50};

    // The wired `source` (the inherited input) is a solo preview for the UI.
    // Without clones (the CLI loader's set_template path is the one that
    // produces real N-instance fan-out) the node passes it through so the UI
    // isn't silent, and only then does it prepare and advance it, so the
    // input is Manual.
    MultiplexSource() : Processor(Pull::Manual) {}

    const char* type_name() const override { return "MultiplexSource"; }

    // The base stores `count` before this runs; appliedCount_ is the count the
    // clones were last sized to, so only a real change forces a rebuild.
    void on_setting_changed(std::string_view name) override {
        if (name != "count") return;
        count.clamp();
        if (count.value != appliedCount_) {
            appliedCount_ = count.value;
            templateDirty_ = true;
        }
    }

    // Loader hook. The builder closure captures everything needed (subtree
    // JSON, base seed) to produce a freshly built root source
    // plus the clone's nodes by id for a given instance index.
    void set_template(std::string json, uint32_t baseSeed, InstanceBuilder builder) {
        templateJson_ = std::move(json);
        baseSeed_ = baseSeed;
        builder_ = std::move(builder);
        templateDirty_ = true;
    }

    // External dirty signal (UI sets this after editing template nodes).
    void mark_dirty() { templateDirty_ = true; }

    // ParamMap fan-out: a param on a node (by id) set across every clone at a
    // note's Setup, so the clones pick up the same note frequency as the
    // top-level node. The target is registered once, at prepare, as a slot:
    // registration finds or appends the entry and gives every clone's pin a
    // constant to write into, so the per-note write allocates nothing. The
    // slot's last value is replayed onto freshly built clones.
    int clone_param_slot(const std::string& nodeId, const std::string& paramName) {
        for (int i = 0; i < int(cloneParams_.size()); ++i)
            if (cloneParams_[size_t(i)].nodeId == nodeId && cloneParams_[size_t(i)].paramName == paramName)
                return i;
        cloneParams_.push_back({nodeId, paramName, 0.0f, false});
        ensure_clone_param_sources_(nodeId, paramName);
        return int(cloneParams_.size()) - 1;
    }
    void set_clone_param(int slot, float value) {
        assert(slot >= 0 && slot < int(cloneParams_.size()) && "a slot clone_param_slot gave out");
        CloneParam& p = cloneParams_[size_t(slot)];
        p.value = value;
        p.delivered = true;
        apply_clone_param_to_all_(p.nodeId, p.paramName, value);
    }

    void on_prepare(const RenderContext& ctx) override {
        if (templateDirty_) rebuild_();
        if (!instances_.empty()) {
            for (auto& inst : instances_)
                if (inst) inst->prepare(ctx);
        } else {
            input.prepare(ctx);   // UI solo-preview fallback
        }
    }

    void on_start(int frames) override {
        if (!instances_.empty()) {
            for (auto& inst : instances_)
                if (inst) inst->start(frames);
        } else {
            input.start(frames);   // UI solo-preview fallback
        }
    }

    float compute() override {
        if (!instances_.empty()) {
            float sum = 0.0f;
            int n = int(instances_.size());
            for (auto& inst : instances_)
                if (inst) sum += inst->next();
            return (n > 0) ? sum / float(n) : 0.0f;
        }
        return input.next();  // solo passthrough (UI preview); 0 when unwired
    }

private:
    void rebuild_() {
        instances_.clear();
        instanceNodes_.clear();
        if (!builder_) { templateDirty_ = false; return; }
        instances_.reserve(count.value);
        instanceNodes_.reserve(count.value);
        for (int i = 0; i < count.value; ++i) {
            auto pair = builder_(i);
            instances_.push_back(std::move(pair.first));
            instanceNodes_.push_back(std::move(pair.second));
        }
        // The fresh clones get a constant on every registered target's pin,
        // and the last delivered value where there was one.
        for (const CloneParam& p : cloneParams_) {
            ensure_clone_param_sources_(p.nodeId, p.paramName);
            if (p.delivered) apply_clone_param_to_all_(p.nodeId, p.paramName, p.value);
        }
        templateDirty_ = false;
    }

    // For each live clone, find the node with the given id and set the named
    // param's ConstantSource to `value` (an unwired pin gets one). Silently
    // no-ops when the node or param isn't found (e.g. paramMap points to
    // something outside the template subgraph) or the pin is wired to a
    // source other than a constant. A constant made after this node was
    // prepared is handed the same context, so a note-Setup push never
    // leaves a clone's pin unprepared; before prepare, the clones' own
    // prepare reaches it.
    // Every clone's pin for a registered target gets its constant now, so a
    // later write finds one there. Control side (prepare, rebuild).
    void ensure_clone_param_sources_(const std::string& nodeId, const std::string& paramName) {
        for (auto& instMap : instanceNodes_) {
            auto it = instMap.find(nodeId);
            if (it == instMap.end()) continue;
            if (prepared()) ensure_constant(*it->second, paramName, render_context());
            else            ensure_constant(*it->second, paramName);
        }
    }
    void apply_clone_param_to_all_(const std::string& nodeId,
                                   const std::string& paramName,
                                   float value) {
        for (auto& instMap : instanceNodes_) {
            auto it = instMap.find(nodeId);
            if (it == instMap.end()) continue;
            auto cs = prepared()
                ? ensure_constant(*it->second, paramName, render_context())
                : ensure_constant(*it->second, paramName);
            if (cs) cs->set(value);
        }
    }

    int appliedCount_{10};
    uint32_t baseSeed_{0};
    std::string templateJson_;
    InstanceBuilder builder_;
    std::vector<std::shared_ptr<ValueSource>> instances_;
    std::vector<NodeMap> instanceNodes_;
    // The registered paramMap fan-out targets, by slot: the last value
    // delivered to each, replayed onto fresh clones (a rebuild discards the
    // live ones).
    struct CloneParam {
        std::string nodeId;
        std::string paramName;
        float value{0.0f};
        bool delivered{false};
    };
    std::vector<CloneParam> cloneParams_;
    bool templateDirty_{true};
};

} // namespace mforce
