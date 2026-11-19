#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mforce {

// MultiplexSource: fans one template into `count` independent instances,
// each built by the closure it is given, called with the instance's index,
// and emits their mean per sample. The closure returns an unprepared
// instance's root; the multiplex prepares and starts it. With no instances
// it passes its wired `source` through.
//
// Rebuild: mark_dirty() or a change of `count` makes the next prepare()
// call the closure again for every instance.
struct MultiplexSource final : Processor {
    // Builds instance `instanceIdx` and returns its root.
    using InstanceBuilder = std::function<std::shared_ptr<ValueSource>(int instanceIdx)>;

    Setting<int> count{this, "count", 10, 1, 50};

    // set_template gives the node the builder its next prepare() calls once
    // per instance. With no template set, the node has no instances: it
    // passes the wired `source` (the inherited input) through, and only then
    // prepares and advances it, so the input is Manual.
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

    // The builder the next prepare() calls `count` times.
    void set_template(InstanceBuilder builder) {
        builder_ = std::move(builder);
        templateDirty_ = true;
    }

    // The builder, with the template's text and its base seed kept beside it.
    void set_template(std::string json, uint32_t baseSeed, InstanceBuilder builder) {
        templateJson_ = std::move(json);
        baseSeed_ = baseSeed;
        builder_ = std::move(builder);
        templateDirty_ = true;
    }

    // External dirty signal (UI sets this after editing template nodes).
    void mark_dirty() { templateDirty_ = true; }

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
        if (!builder_) { templateDirty_ = false; return; }
        instances_.reserve(count.value);
        for (int i = 0; i < count.value; ++i)
            instances_.push_back(builder_(i));
        templateDirty_ = false;
    }

    int appliedCount_{10};
    uint32_t baseSeed_{0};
    std::string templateJson_;
    InstanceBuilder builder_;
    std::vector<std::shared_ptr<ValueSource>> instances_;
    bool templateDirty_{true};
};

} // namespace mforce
