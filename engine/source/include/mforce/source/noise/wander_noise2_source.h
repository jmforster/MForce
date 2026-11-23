#pragma once
// Wander noise 2 generator: step-based random walk with retrace.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.WanderNoise2Source
// Step-based random walk with probabilistic direction reversal and retrace.
//   minSpeed/maxSpeed: step size range per sample
//   reverseProb:       probability of flipping direction each sample
//   retraceProb:       probability of a retrace step
//   retracePct:        size of retrace step
// ---------------------------------------------------------------------------
struct WanderNoise2Source final : Generator {
  // `amplitude`: unpulled, as in WanderNoiseSource.
  Param amplitude  {this, "amplitude",   1.0f,  0.0f, 10.0f, nullptr, Pull::Manual};
  Param minSpeed   {this, "minSpeed",    0.01f, 0.0f, 10.0f};
  Param maxSpeed   {this, "maxSpeed",    0.1f,  0.0f, 10.0f};
  Param reverseProb{this, "reverseProb", 0.0f,  0.0f, 1.0f};
  Param retraceProb{this, "retraceProb", 0.0f,  0.0f, 1.0f};
  Param retracePct {this, "retracePct",  0.0f,  0.0f, 1.0f};

  explicit WanderNoise2Source(uint32_t seed = 0xFA0D'0002u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WanderNoise2Source"; }

  // As WanderNoiseSource: the Manual amplitude pin.
  void on_prepare(const RenderContext& ctx) override { amplitude.prepare(ctx); }
  void on_start(int frames) override { amplitude.start(frames); }

  float compute() override {
    const float mnSpd = minSpeed.current();
    const float mxSpd = maxSpeed.current();
    const float rProb = reverseProb.current();
    const float tProb = retraceProb.current();
    const float tPct  = retracePct.current();

    float delta;

    if (rng_.decide(tProb)) {
      delta = tPct * (1.0f - direction_);
    } else {
      if (rng_.decide(rProb)) {
        direction_ = -direction_;
      }
      delta = rng_.range(mnSpd, mxSpd) * direction_;
    }

    if (std::fabs(value_ + delta) < 1.0f) {
      value_ += delta;
    } else {
      value_ = direction_;
      direction_ = -direction_;
    }

    return value_;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float direction_{1.0f};
  float value_{0.0f};
};

} // namespace mforce
