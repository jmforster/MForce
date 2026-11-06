#pragma once
// Crackle noise generator: chaotic map producing vinyl-like crackle.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// CrackleNoiseSource — chaotic map producing vinyl-crackle-like sounds.
// y[n] = |chaos * y[n-1] - y[n-2] - 0.05|
//   chaos near 1.0 = gentle crackle
//   chaos near 1.5 = aggressive crackle
//   chaos near 2.0 = chaotic noise
// ---------------------------------------------------------------------------
struct CrackleNoiseSource final : Generator {
  Param chaos{this, "chaos", 1.5f, 1.0f, 2.0f};

  explicit CrackleNoiseSource(uint32_t seed = 0xC8AC'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "CrackleNoiseSource"; }

  void on_start(int) override {
    y1_ = rng_.value() * 0.1f;
    y2_ = rng_.value() * 0.1f;
  }

  float compute() override {
    const float c = chaos.current();
    float y0 = std::fabs(c * y1_ - y2_ - 0.05f);
    y0 = std::clamp(y0, -1.0f, 1.0f);
    y2_ = y1_;
    y1_ = y0;
    return y0 * 2.0f - 1.0f;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float y1_{0.0f};
  float y2_{0.0f};
};

} // namespace mforce
