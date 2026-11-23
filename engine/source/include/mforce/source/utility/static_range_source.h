#pragma once
// StaticRangeSource: a random value in a range drawn once per note.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Follows the C# StaticRangeSource — random value in [min,max] drawn once per note, at start().
struct StaticRangeSource final : Generator {
  Setting<float> min {this, "min",  0.0f, -10000.0f, 10000.0f};
  Setting<float> max {this, "max",  1.0f, -10000.0f, 10000.0f};
  Setting<float> bias{this, "bias", 0.0f, -1.0f,     1.0f};

  explicit StaticRangeSource(uint32_t seed = 0x57A70002u) : rng_(seed) {}

  const char* type_name() const override { return "StaticRangeSource"; }

  void on_start(int /*frames*/) override {
    value_ = rng_.range(min.value, max.value, bias.value);
    cur_ = value_;
  }

  float compute() override { return value_; }

private:
  Randomizer rng_;
  float value_{0.0f};
};

} // namespace mforce
