#pragma once
// Violet noise generator: differentiated white noise.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"

namespace mforce {

// ---------------------------------------------------------------------------
// VioletNoiseSource — +6dB/octave (spectral inverse of brown/red).
// Differentiated white noise: output = current_white - previous_white.
// Spectral SHAPE is fixed by definition; only level is modulatable.
// ---------------------------------------------------------------------------
struct VioletNoiseSource final : Generator {
  Param amplitude{this, "amplitude", 1.0f, 0.0f, 10.0f};

  explicit VioletNoiseSource(uint32_t seed = 0xF100'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "VioletNoiseSource"; }

  void on_start(int) override { prev_ = 0.0f; }

  float compute() override {
    const float w = rng_.valuePN();
    float out = w - prev_;
    prev_ = w;
    out *= 0.5f * amplitude.current();
    return out;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float prev_{0.0f};
};

} // namespace mforce
