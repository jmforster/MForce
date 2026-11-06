#pragma once
// Velvet noise generator: sparse random impulse sequence.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// VelvetNoiseSource — sparse random impulse sequence.
// Only occasional samples are non-zero (±1), rest are zero.
// Useful as KS excitation or for reverb-like textures.
// ---------------------------------------------------------------------------
struct VelvetNoiseSource final : Generator {
  Param density  {this, "density",   2000.0f, 1.0f, 48000.0f};
  Param amplitude{this, "amplitude", 1.0f,    0.0f, 10.0f};

  explicit VelvetNoiseSource(uint32_t seed = 0xFE17'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "VelvetNoiseSource"; }

  void on_start(int) override {
    counter_ = 0;
    recompute_gap();
  }

  float compute() override {
    const float d = density.current();
    const float a = amplitude.current();
    float out = 0.0f;
    if (counter_ >= nextImpulse_) {
      out = (rng_.value() < 0.5f ? a : -a);
      counter_ = 0;
      // Recompute gap from current density
      float avgGap = (d > 0.0f) ? float(sample_rate()) / d : float(sample_rate());
      nextImpulse_ = std::max(1, int(avgGap * (0.5f + rng_.value())));
    }
    counter_++;
    return out;
  }

private:
  void recompute_gap() {
    nextImpulse_ = std::max(1, sample_rate() / 2000);
  }

  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  int counter_{0};
  int nextImpulse_{0};
};

} // namespace mforce
