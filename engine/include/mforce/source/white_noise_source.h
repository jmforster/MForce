#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.WhiteNoiseSource
// Simple per-sample random noise (not a WaveSource — no phase accumulator needed).
struct WhiteNoiseSource final : ValueSource {
  explicit WhiteNoiseSource(uint32_t seed = 0x12345678u)
  : rng_(seed), amplitude_(std::make_shared<ConstantSource>(1.0f)) {}

  const char* type_name() const override { return "WhiteNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  // `amplitude` was missing here until 2026-08-04 — patches that set it were
  // silently ignored by the loader (found by tools/lint_patches.py). Default
  // 1.0 keeps every patch that does NOT set it bit-identical.
  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude", 1.0f, 0.0f, 10.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude") amplitude_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    amplitude_->prepare(ctx, frames);
  }

  float next() override {
    cur_ = rng_.valuePN() * amplitude_->next();  // [-1, 1] scaled
    return cur_;
  }

  float current() const override { return cur_; }

private:
  Randomizer rng_;
  std::shared_ptr<ValueSource> amplitude_;
  float cur_{0.0f};
};

} // namespace mforce
