#pragma once
// Wander noise generator: random walk with slope-based momentum.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.WanderNoiseSource
// Random walk with slope-based momentum. Output in [-1, 1].
// Internal state works in [0,1]; boundary hits bounce slope/direction.
//   speed:      overall rate of change (scaled by 1/sampleRate)
//   deltaSpeed: max random change to slope per sample
//   slopeLimit: max absolute slope before direction reverses
// ---------------------------------------------------------------------------
struct WanderNoiseSource final : Generator {
  // `amplitude` is accepted and prepared, but the walk never applies it, so
  // the node leaves it unpulled (Manual): the base neither prepares nor
  // advances a Manual pin.
  Param amplitude {this, "amplitude",  1.0f, 0.0f, 10.0f, nullptr, Pull::Manual};
  Param speed     {this, "speed",      1.0f, 0.0f, 100.0f};
  Param deltaSpeed{this, "deltaSpeed", 1.0f, 0.0f, 100.0f};
  Param slopeLimit{this, "slopeLimit", 1.0f, 0.0f, 10.0f};

  explicit WanderNoiseSource(uint32_t seed = 0xFA0D'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WanderNoiseSource"; }

  // The Manual amplitude pin is prepared and started by the node.
  void on_prepare(const RenderContext& ctx) override { amplitude.prepare(ctx); }
  void on_start(int frames) override { amplitude.start(frames); }

  float compute() override {
    const float ds = deltaSpeed.current();
    const float sl = slopeLimit.current();
    const float sp = speed.current();

    // Random slope delta
    float delta = rng_.range(0.0f, ds);

    // Bounce direction if slope would exceed limit
    if (std::fabs(slope_ + delta * direction_) > sl) {
      direction_ = -direction_;
    }

    slope_ += delta * direction_;

    // Legacy: works in [0,1] internal space
    // localVal = (lastVal+1)/2 + speed/Rate * slope
    float localVal = (lastVal_ + 1.0f) * 0.5f + sp / float(sample_rate()) * slope_;

    if (localVal > 1.0f) {
      localVal = 1.0f;
      slope_ = -slope_;
      direction_ = -1.0f;
    }
    if (localVal < 0.0f) {
      localVal = 0.0f;
      slope_ = -slope_;
      direction_ = 1.0f;
    }

    const float out = localVal * 2.0f - 1.0f;
    lastVal_ = out;
    return out;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float slope_{0.0f};
  float direction_{1.0f};
  float lastVal_{0.5f};   // legacy init
};

} // namespace mforce
