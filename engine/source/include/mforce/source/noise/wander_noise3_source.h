#pragma once
// Wander noise 3 generator: deterministic second-order slope walk.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.WanderNoise3Source
// Second-order slope control, fully deterministic (no random per sample).
// Same params as v1 but slope change is deltaSpeed/Rate (not random).
// Output: (value*2-1) * amplitude, mapping internal [0,1] to [-1,1].
// Fix: legacy had value starting at -1 with [0,1] mapping, causing -3 output.
// Corrected to start at 0 and clamp at 0 (not -1).
// ---------------------------------------------------------------------------
struct WanderNoise3Source final : Generator {
  Param amplitude {this, "amplitude",  1.0f, 0.0f, 10.0f};
  Param speed     {this, "speed",      1.0f, 0.0f, 100.0f};
  Param deltaSpeed{this, "deltaSpeed", 1.0f, 0.0f, 100.0f};
  Param slopeLimit{this, "slopeLimit", 1.0f, 0.0f, 10.0f};

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WanderNoise3Source"; }

  void on_start(int) override {
    value_ = 0.0f;
    slope_ = 0.0f;
    slopeChgDir_ = 1.0f;
  }

  float compute() override {
    const float sp = speed.current();
    const float ds = deltaSpeed.current();
    const float sl = slopeLimit.current();
    const float amp = amplitude.current();

    float lastVal = value_;

    // Deterministic slope change
    float slopeChgAmt = ds / float(sample_rate());

    // Bounce slope change direction if limit exceeded
    if (std::fabs(slope_ + slopeChgAmt * slopeChgDir_) > sl) {
      slopeChgDir_ = -slopeChgDir_;
    }

    slope_ += slopeChgAmt * slopeChgDir_;

    value_ = lastVal + sp / float(sample_rate()) * slope_;

    // Boundary clamp with state reset (legacy behavior)
    if (value_ > 1.0f) {
      value_ = 1.0f;
      slope_ = 0.0f;
      slopeChgDir_ = -1.0f;
    }
    if (value_ < 0.0f) {
      value_ = 0.0f;
      slope_ = 0.0f;
      slopeChgDir_ = 1.0f;
    }

    // Legacy output mapping: (value*2-1)*amplitude
    return (value_ * 2.0f - 1.0f) * amp;
  }

private:
  float value_{0.0f};
  float slope_{0.0f};
  float slopeChgDir_{1.0f};
};

} // namespace mforce
