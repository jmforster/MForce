#pragma once
// StaticVarSource: a random value drawn once per note.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# StaticVarSource — random value drawn once per note, at start().
// value = baseValue * (1 + random * varPct), optionally biased.
struct StaticVarSource final : Generator {
  Setting<float> baseValue{this, "baseValue", 1.0f, -10000.0f, 10000.0f};
  Setting<float> varPct   {this, "varPct",    0.0f, 0.0f,      1.0f};
  Setting<float> bias     {this, "bias",      0.0f, -1.0f,     1.0f};

  explicit StaticVarSource(uint32_t seed = 0x57A70001u) : rng_(seed) {}

  const char* type_name() const override { return "StaticVarSource"; }

  void on_start(int /*frames*/) override {
    float r = rng_.valuePN() * varPct.value;
    if (bias.value != 0.0f) r = r * (1.0f - std::fabs(bias.value)) + bias.value * varPct.value;
    value_ = baseValue.value * (1.0f + r);
    cur_ = value_;
  }

  float compute() override { return value_; }

private:
  Randomizer rng_;
  float value_{0.0f};
};

} // namespace mforce
