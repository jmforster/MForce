#pragma once
#include "dsp_value_source.h"
#include "randomizer.h"
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
struct WanderNoiseSource final : ValueSource {
  WanderNoiseSource(int sampleRate, uint32_t seed = 0xFA0D'0001u)
  : sampleRate_(sampleRate), rng_(seed) {}

  std::shared_ptr<ValueSource> amplitude;
  std::shared_ptr<ValueSource> speed;
  std::shared_ptr<ValueSource> deltaSpeed;
  std::shared_ptr<ValueSource> slopeLimit;

  void prepare(int frames) override {
    if (amplitude)  amplitude->prepare(frames);
    if (speed)      speed->prepare(frames);
    if (deltaSpeed) deltaSpeed->prepare(frames);
    if (slopeLimit) slopeLimit->prepare(frames);
  }

  float next() override {
    if (speed)      speed->next();
    if (deltaSpeed) deltaSpeed->next();
    if (slopeLimit) slopeLimit->next();

    float ds = deltaSpeed ? deltaSpeed->current() : 1.0f;
    float sl = slopeLimit ? slopeLimit->current() : 1.0f;
    float sp = speed      ? speed->current()      : 1.0f;

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

    cur_ = localVal * 2.0f - 1.0f;
    lastVal_ = cur_;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  Randomizer rng_;
  float slope_{0.0f};
  float direction_{1.0f};
  float lastVal_{0.5f};   // legacy init
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.WanderNoise2Source
// Step-based random walk with probabilistic direction reversal and retrace.
//   minSpeed/maxSpeed: step size range per sample
//   reverseProb:       probability of flipping direction each sample
//   retraceProb:       probability of a retrace step
//   retracePct:        size of retrace step
// ---------------------------------------------------------------------------
struct WanderNoise2Source final : ValueSource {
  WanderNoise2Source(int sampleRate, uint32_t seed = 0xFA0D'0002u)
  : sampleRate_(sampleRate), rng_(seed) {}

  std::shared_ptr<ValueSource> amplitude;
  std::shared_ptr<ValueSource> minSpeed;
  std::shared_ptr<ValueSource> maxSpeed;
  std::shared_ptr<ValueSource> reverseProb;
  std::shared_ptr<ValueSource> retraceProb;
  std::shared_ptr<ValueSource> retracePct;

  void prepare(int frames) override {
    if (amplitude)   amplitude->prepare(frames);
    if (minSpeed)    minSpeed->prepare(frames);
    if (maxSpeed)    maxSpeed->prepare(frames);
    if (reverseProb) reverseProb->prepare(frames);
    if (retraceProb) retraceProb->prepare(frames);
    if (retracePct)  retracePct->prepare(frames);
  }

  float next() override {
    if (minSpeed)    minSpeed->next();
    if (maxSpeed)    maxSpeed->next();
    if (reverseProb) reverseProb->next();
    if (retraceProb) retraceProb->next();
    if (retracePct)  retracePct->next();

    float mnSpd = minSpeed    ? minSpeed->current()    : 1.0f;
    float mxSpd = maxSpeed    ? maxSpeed->current()    : 1.0f;
    float rProb = reverseProb ? reverseProb->current() : 0.0f;
    float tProb = retraceProb ? retraceProb->current() : 0.0f;
    float tPct  = retracePct  ? retracePct->current()  : 0.0f;

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

    cur_ = value_;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  Randomizer rng_;
  float direction_{1.0f};
  float value_{0.0f};
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.WanderNoise3Source
// Second-order slope control, fully deterministic (no random per sample).
// Same params as v1 but slope change is deltaSpeed/Rate (not random).
// Output: (value*2-1) * amplitude, mapping internal [0,1] to [-1,1].
// Fix: legacy had value starting at -1 with [0,1] mapping, causing -3 output.
// Corrected to start at 0 and clamp at 0 (not -1).
// ---------------------------------------------------------------------------
struct WanderNoise3Source final : ValueSource {
  WanderNoise3Source(int sampleRate)
  : sampleRate_(sampleRate) {}

  std::shared_ptr<ValueSource> amplitude;
  std::shared_ptr<ValueSource> speed;
  std::shared_ptr<ValueSource> deltaSpeed;
  std::shared_ptr<ValueSource> slopeLimit;

  void prepare(int frames) override {
    if (amplitude)  amplitude->prepare(frames);
    if (speed)      speed->prepare(frames);
    if (deltaSpeed) deltaSpeed->prepare(frames);
    if (slopeLimit) slopeLimit->prepare(frames);

    value_ = 0.0f;
    slope_ = 0.0f;
    slopeChgDir_ = 1.0f;
  }

  float next() override {
    if (amplitude)  amplitude->next();
    if (speed)      speed->next();
    if (deltaSpeed) deltaSpeed->next();
    if (slopeLimit) slopeLimit->next();

    float sp = speed      ? speed->current()      : 1.0f;
    float ds = deltaSpeed ? deltaSpeed->current() : 1.0f;
    float sl = slopeLimit ? slopeLimit->current() : 1.0f;
    float amp = amplitude ? amplitude->current()  : 1.0f;

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
    cur_ = (value_ * 2.0f - 1.0f) * amp;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  float value_{0.0f};
  float slope_{0.0f};
  float slopeChgDir_{1.0f};
  float cur_{0.0f};
};

} // namespace mforce
