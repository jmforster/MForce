#pragma once
// DistortedSource: a waveshaping distortion Processor.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# DistortedSource — waveshaping distortion.
// Shifts, amplifies, and clips the input. Density controls probability of effect.
struct DistortedSource final : Processor {
  // After the inherited `source`, in the order they are advanced.
  Param density  {this, "density",   1.0f, 0.0f,   1.0f,   "0-1"};
  Param gain     {this, "gain",      1.0f, 0.0f,   100.0f, "gain"};
  Param shift    {this, "shift",     0.0f, -1.0f,  1.0f,   "±1"};
  Param amplitude{this, "amplitude", 1.0f, 0.0f,   10.0f,  "0-1"};

  explicit DistortedSource(uint32_t seed = 0xD1570000u) : rng_(seed) {}

  const char* type_name() const override { return "DistortedSource"; }

  float compute() override {
    float s = input.current();
    float d = density.current();
    float g = gain.current();
    float sh = shift.current();
    float a = amplitude.current();

    if (rng_.decide(d)) {
      float shifted = s + sh;
      return std::min(std::fabs(shifted) * (1.0f + g), 1.0f)
           * (shifted >= 0.0f ? 1.0f : -1.0f) * a;
    }
    return 0.0f;
  }

private:
  Randomizer rng_;
};

} // namespace mforce
