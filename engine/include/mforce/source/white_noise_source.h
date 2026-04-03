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
  : rng_(seed) {}

  const char* type_name() const override { return "WhiteNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void prepare(int /*frames*/) override {}

  float next() override {
    cur_ = rng_.valuePN();  // [-1, 1]
    return cur_;
  }

  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float cur_{0.0f};
};

} // namespace mforce
