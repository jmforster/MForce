#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Wormhole — pure pass-through (2026-09-06, Matt's design; replaces the
// net-label tag experiment). A wormhole PAIR is two instances where the
// out-half's source refs the in-half; the UI hides the wire between two
// Wormholes and shows the span on hover, so long feedback/routing wires
// (the tap arc especially) collapse into two small named nodes wired
// pin-precisely at each end. Engine-side there is nothing special: it
// forwards its input, adds no state, no params ("pure glass" — the moment
// it grows a knob it stops being invisible; scalar math belongs to the
// Function-node design, dsp backlog 54). Memoryless, so DelayLine
// compensation costs it nothing (default phase_delay_at = 0).
struct WormholeSource final : ValueSource {
  const char* type_name() const override { return "Wormhole"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") source_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    cur_ = 0.0f;
  }

  float next() override {
    cur_ = source_ ? source_->next() : 0.0f;
    return cur_;
  }
  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_;
  float cur_{0.0f};
};

} // namespace mforce
