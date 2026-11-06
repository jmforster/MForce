#pragma once
// StaticRangeSource: a random value in a range drawn once per note.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# StaticRangeSource — random value in [min,max] drawn once per note, at start().
struct StaticRangeSource final : Generator {
  float min{0.0f}, max{1.0f}, bias{0.0f};

  StaticRangeSource(float mn, float mx, float b = 0.0f, uint32_t seed = 0x57A70002u)
  : min(mn), max(mx), bias(b), rng_(seed) {}

  const char* type_name() const override { return "StaticRangeSource"; }

  void on_start(int /*frames*/) override {
    value_ = rng_.range(min, max, bias);
    cur_ = value_;
  }

  float compute() override { return value_; }

private:
  Randomizer rng_;
  float value_{0.0f};
};

} // namespace mforce
