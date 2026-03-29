#pragma once
#include "dsp_wave_source.h"
#include <cmath>
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.PulseSource
// Band-limited pulse wave via polynomial BLEP.
// dutyCycle: fraction of period spent "high" (0..1)
// bend: 0 = square wave, 1 = sawtooth-like slope on top
struct PulseSource final : WaveSource {
  explicit PulseSource(int sampleRate)
  : WaveSource(sampleRate)
  , dutyCycle_(std::make_shared<ConstantSource>(0.5f))
  , bend_(std::make_shared<ConstantSource>(0.0f)) {}

  void set_duty_cycle(std::shared_ptr<ValueSource> d) { dutyCycle_ = std::move(d); }
  void set_bend(std::shared_ptr<ValueSource> b) { bend_ = std::move(b); }

  void prepare(int frames) override {
    WaveSource::prepare(frames);
    dutyCycle_->prepare(frames);
    bend_->prepare(frames);
  }

protected:
  float compute_wave_value() override {
    float duty = dutyCycle_->next();
    float bend = bend_->next();

    // Use current() after next() — matches legacy GetNext() pattern
    duty = dutyCycle_->current();
    bend = bend_->current();

    float val = 0.0f;
    if (currPos_ < duty) {
      val = 1.0f - (1.0f - currPos_ / duty) * bend;
    } else {
      val = -1.0f - (duty - currPos_) * 2.0f * bend;
    }

    return val + get_blep(currPos_) - get_blep(std::fmod(currPos_ + (1.0f - duty), 1.0f));
  }

private:
  static float get_blep(float pos, float phaseIncr) {
    if (pos < phaseIncr) {
      float t = pos / phaseIncr;
      return t + t - t * t - 1.0f;
    } else if (pos > 1.0f - phaseIncr) {
      float t = (pos - 1.0f) / phaseIncr;
      return t * t + t + t + 1.0f;
    }
    return 0.0f;
  }

  float get_blep(float pos) const { return get_blep(pos, currPhaseIncr_); }

  std::shared_ptr<ValueSource> dutyCycle_;
  std::shared_ptr<ValueSource> bend_;
};

} // namespace mforce
