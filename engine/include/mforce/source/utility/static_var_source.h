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
  float baseValue{1.0f};
  float varPct{0.0f};
  float bias{0.0f};

  StaticVarSource(float base, float pct, float b = 0.0f, uint32_t seed = 0x57A70001u)
  : baseValue(base), varPct(pct), bias(b), rng_(seed) {}

  const char* type_name() const override { return "StaticVarSource"; }

  void on_start(int /*frames*/) override {
    float r = rng_.valuePN() * varPct;
    if (bias != 0.0f) r = r * (1.0f - std::fabs(bias)) + bias * varPct;
    value_ = baseValue * (1.0f + r);
    cur_ = value_;
  }

  float compute() override { return value_; }

private:
  Randomizer rng_;
  float value_{0.0f};
};

} // namespace mforce
