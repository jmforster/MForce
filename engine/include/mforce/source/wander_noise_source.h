#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <memory>

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

  WanderNoiseSource(int sampleRate, uint32_t seed = 0xFA0D'0001u)
  : sampleRate_(sampleRate), rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WanderNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  // The Manual amplitude pin is prepared by the node (migration form; the
  // noise family's task splits this into the two hooks).
  void on_prepare(const RenderContext& ctx, int frames) override {
    amplitude.prepare(ctx, frames);
  }

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
    float localVal = (lastVal_ + 1.0f) * 0.5f + sp / float(sampleRate_) * slope_;

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
  int sampleRate_;
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float slope_{0.0f};
  float direction_{1.0f};
  float lastVal_{0.5f};   // legacy init
};

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

  WanderNoise2Source(int sampleRate, uint32_t seed = 0xFA0D'0002u)
  : sampleRate_(sampleRate), rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WanderNoise2Source"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  // As WanderNoiseSource: the Manual amplitude pin, migration form.
  void on_prepare(const RenderContext& ctx, int frames) override {
    amplitude.prepare(ctx, frames);
  }

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
  int sampleRate_;
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float direction_{1.0f};
  float value_{0.0f};
};

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

  explicit WanderNoise3Source(int sampleRate) : sampleRate_(sampleRate) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WanderNoise3Source"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_prepare(const RenderContext&, int) override {
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
    float slopeChgAmt = ds / float(sampleRate_);

    // Bounce slope change direction if limit exceeded
    if (std::fabs(slope_ + slopeChgAmt * slopeChgDir_) > sl) {
      slopeChgDir_ = -slopeChgDir_;
    }

    slope_ += slopeChgAmt * slopeChgDir_;

    value_ = lastVal + sp / float(sampleRate_) * slope_;

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
  int sampleRate_;
  float value_{0.0f};
  float slope_{0.0f};
  float slopeChgDir_{1.0f};
};

} // namespace mforce
