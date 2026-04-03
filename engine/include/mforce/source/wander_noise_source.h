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
struct WanderNoiseSource final : ValueSource {
  WanderNoiseSource(int sampleRate, uint32_t seed = 0xFA0D'0001u)
  : sampleRate_(sampleRate), rng_(seed) {}

  void set_amplitude(std::shared_ptr<ValueSource> s)  { amplitude_ = std::move(s); }
  void set_speed(std::shared_ptr<ValueSource> s)      { speed_ = std::move(s); }
  void set_deltaSpeed(std::shared_ptr<ValueSource> s) { deltaSpeed_ = std::move(s); }
  void set_slopeLimit(std::shared_ptr<ValueSource> s) { slopeLimit_ = std::move(s); }

  std::shared_ptr<ValueSource> get_amplitude() const  { return amplitude_; }
  std::shared_ptr<ValueSource> get_speed() const      { return speed_; }
  std::shared_ptr<ValueSource> get_deltaSpeed() const { return deltaSpeed_; }
  std::shared_ptr<ValueSource> get_slopeLimit() const { return slopeLimit_; }

  const char* type_name() const override { return "WanderNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  1.0f,  0.0f, 10.0f},
      {"speed",      1.0f,  0.0f, 100.0f},
      {"deltaSpeed", 1.0f,  0.0f, 100.0f},
      {"slopeLimit", 1.0f,  0.0f, 10.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_ = std::move(src); return; }
    if (name == "speed")      { speed_ = std::move(src); return; }
    if (name == "deltaSpeed") { deltaSpeed_ = std::move(src); return; }
    if (name == "slopeLimit") { slopeLimit_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "speed")      return speed_;
    if (name == "deltaSpeed") return deltaSpeed_;
    if (name == "slopeLimit") return slopeLimit_;
    return nullptr;
  }

  void prepare(int frames) override {
    if (amplitude_)  amplitude_->prepare(frames);
    if (speed_)      speed_->prepare(frames);
    if (deltaSpeed_) deltaSpeed_->prepare(frames);
    if (slopeLimit_) slopeLimit_->prepare(frames);
  }

  float next() override {
    if (speed_)      speed_->next();
    if (deltaSpeed_) deltaSpeed_->next();
    if (slopeLimit_) slopeLimit_->next();

    float ds = deltaSpeed_ ? deltaSpeed_->current() : 1.0f;
    float sl = slopeLimit_ ? slopeLimit_->current() : 1.0f;
    float sp = speed_      ? speed_->current()      : 1.0f;

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
  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> speed_;
  std::shared_ptr<ValueSource> deltaSpeed_;
  std::shared_ptr<ValueSource> slopeLimit_;
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

  void set_amplitude(std::shared_ptr<ValueSource> s)   { amplitude_ = std::move(s); }
  void set_minSpeed(std::shared_ptr<ValueSource> s)    { minSpeed_ = std::move(s); }
  void set_maxSpeed(std::shared_ptr<ValueSource> s)    { maxSpeed_ = std::move(s); }
  void set_reverseProb(std::shared_ptr<ValueSource> s) { reverseProb_ = std::move(s); }
  void set_retraceProb(std::shared_ptr<ValueSource> s) { retraceProb_ = std::move(s); }
  void set_retracePct(std::shared_ptr<ValueSource> s)  { retracePct_ = std::move(s); }

  std::shared_ptr<ValueSource> get_amplitude() const   { return amplitude_; }
  std::shared_ptr<ValueSource> get_minSpeed() const    { return minSpeed_; }
  std::shared_ptr<ValueSource> get_maxSpeed() const    { return maxSpeed_; }
  std::shared_ptr<ValueSource> get_reverseProb() const { return reverseProb_; }
  std::shared_ptr<ValueSource> get_retraceProb() const { return retraceProb_; }
  std::shared_ptr<ValueSource> get_retracePct() const  { return retracePct_; }

  const char* type_name() const override { return "WanderNoise2Source"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",   1.0f,  0.0f, 10.0f},
      {"minSpeed",    0.01f, 0.0f, 10.0f},
      {"maxSpeed",    0.1f,  0.0f, 10.0f},
      {"reverseProb", 0.0f,  0.0f, 1.0f},
      {"retraceProb", 0.0f,  0.0f, 1.0f},
      {"retracePct",  0.0f,  0.0f, 1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")   { amplitude_ = std::move(src); return; }
    if (name == "minSpeed")    { minSpeed_ = std::move(src); return; }
    if (name == "maxSpeed")    { maxSpeed_ = std::move(src); return; }
    if (name == "reverseProb") { reverseProb_ = std::move(src); return; }
    if (name == "retraceProb") { retraceProb_ = std::move(src); return; }
    if (name == "retracePct")  { retracePct_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")   return amplitude_;
    if (name == "minSpeed")    return minSpeed_;
    if (name == "maxSpeed")    return maxSpeed_;
    if (name == "reverseProb") return reverseProb_;
    if (name == "retraceProb") return retraceProb_;
    if (name == "retracePct")  return retracePct_;
    return nullptr;
  }

  void prepare(int frames) override {
    if (amplitude_)   amplitude_->prepare(frames);
    if (minSpeed_)    minSpeed_->prepare(frames);
    if (maxSpeed_)    maxSpeed_->prepare(frames);
    if (reverseProb_) reverseProb_->prepare(frames);
    if (retraceProb_) retraceProb_->prepare(frames);
    if (retracePct_)  retracePct_->prepare(frames);
  }

  float next() override {
    if (minSpeed_)    minSpeed_->next();
    if (maxSpeed_)    maxSpeed_->next();
    if (reverseProb_) reverseProb_->next();
    if (retraceProb_) retraceProb_->next();
    if (retracePct_)  retracePct_->next();

    float mnSpd = minSpeed_    ? minSpeed_->current()    : 1.0f;
    float mxSpd = maxSpeed_    ? maxSpeed_->current()    : 1.0f;
    float rProb = reverseProb_ ? reverseProb_->current() : 0.0f;
    float tProb = retraceProb_ ? retraceProb_->current() : 0.0f;
    float tPct  = retracePct_  ? retracePct_->current()  : 0.0f;

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
  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> minSpeed_;
  std::shared_ptr<ValueSource> maxSpeed_;
  std::shared_ptr<ValueSource> reverseProb_;
  std::shared_ptr<ValueSource> retraceProb_;
  std::shared_ptr<ValueSource> retracePct_;
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

  void set_amplitude(std::shared_ptr<ValueSource> s)  { amplitude_ = std::move(s); }
  void set_speed(std::shared_ptr<ValueSource> s)      { speed_ = std::move(s); }
  void set_deltaSpeed(std::shared_ptr<ValueSource> s) { deltaSpeed_ = std::move(s); }
  void set_slopeLimit(std::shared_ptr<ValueSource> s) { slopeLimit_ = std::move(s); }

  std::shared_ptr<ValueSource> get_amplitude() const  { return amplitude_; }
  std::shared_ptr<ValueSource> get_speed() const      { return speed_; }
  std::shared_ptr<ValueSource> get_deltaSpeed() const { return deltaSpeed_; }
  std::shared_ptr<ValueSource> get_slopeLimit() const { return slopeLimit_; }

  const char* type_name() const override { return "WanderNoise3Source"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  1.0f,  0.0f, 10.0f},
      {"speed",      1.0f,  0.0f, 100.0f},
      {"deltaSpeed", 1.0f,  0.0f, 100.0f},
      {"slopeLimit", 1.0f,  0.0f, 10.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_ = std::move(src); return; }
    if (name == "speed")      { speed_ = std::move(src); return; }
    if (name == "deltaSpeed") { deltaSpeed_ = std::move(src); return; }
    if (name == "slopeLimit") { slopeLimit_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "speed")      return speed_;
    if (name == "deltaSpeed") return deltaSpeed_;
    if (name == "slopeLimit") return slopeLimit_;
    return nullptr;
  }

  void prepare(int frames) override {
    if (amplitude_)  amplitude_->prepare(frames);
    if (speed_)      speed_->prepare(frames);
    if (deltaSpeed_) deltaSpeed_->prepare(frames);
    if (slopeLimit_) slopeLimit_->prepare(frames);

    value_ = 0.0f;
    slope_ = 0.0f;
    slopeChgDir_ = 1.0f;
  }

  float next() override {
    if (amplitude_)  amplitude_->next();
    if (speed_)      speed_->next();
    if (deltaSpeed_) deltaSpeed_->next();
    if (slopeLimit_) slopeLimit_->next();

    float sp = speed_      ? speed_->current()      : 1.0f;
    float ds = deltaSpeed_ ? deltaSpeed_->current() : 1.0f;
    float sl = slopeLimit_ ? slopeLimit_->current() : 1.0f;
    float amp = amplitude_ ? amplitude_->current()  : 1.0f;

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
  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> speed_;
  std::shared_ptr<ValueSource> deltaSpeed_;
  std::shared_ptr<ValueSource> slopeLimit_;
  int sampleRate_;
  float value_{0.0f};
  float slope_{0.0f};
  float slopeChgDir_{1.0f};
  float cur_{0.0f};
};

} // namespace mforce
