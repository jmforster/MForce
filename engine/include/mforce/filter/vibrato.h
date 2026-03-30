#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/red_noise_source.h"
#include "mforce/core/envelope.h"
#include "mforce/core/range_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Envelope.Vibrato
// Modulates a frequency value with a RedNoise LFO whose speed and depth
// ramp up over time (controlled by envelopes).
//
// Output: frequency * (1 + lfo_output)
// Where lfo_output ramps from 0 to depth over the attack period.
//
// Parameters:
//   frequency:  base frequency to modulate (ValueSource)
//   speed:      vibrato rate in Hz
//   depth:      max modulation depth (fraction of frequency)
//   attack:     fraction of duration for depth/speed ramp-up
//   threshold:  minimum duration (seconds) below which vibrato is disabled
struct Vibrato final : ValueSource {
  Vibrato(int sampleRate, float speed, float depth, float attack,
          float threshold = 0.0f, float speedVar = 0.0f, float depthVar = 0.0f,
          uint32_t seed = 0xF1B0'0000u)
  : sampleRate_(sampleRate), speed_(speed), depth_(depth), attack_(attack),
    threshold_(threshold), speedVar_(speedVar), depthVar_(depthVar)
  {
    // Speed ramp: 1 Hz → target speed over attack period, sustain at target
    auto speedRamp = std::make_shared<Envelope>(Envelope::make_ar(sampleRate, attack));
    auto speedRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(speed),
        speedRamp, true);

    // Depth ramp: 0 → target depth over attack period
    auto depthRamp = std::make_shared<Envelope>(Envelope::make_ar(sampleRate, attack));
    auto depthRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(0.0f),
        std::make_shared<ConstantSource>(depth),
        depthRamp, true);

    // LFO: RedNoise with sine-like smoothness, speed and depth from envelopes
    lfo_ = std::make_shared<RedNoiseSource>(sampleRate, seed);
    lfo_->set_frequency(speedRange);
    lfo_->set_amplitude(depthRange);
    lfo_->smoothness = std::make_shared<ConstantSource>(1.0f);
    lfo_->rampVariation = std::make_shared<ConstantSource>(speedVar);
    lfo_->boost = std::make_shared<ConstantSource>(1.0f - depthVar);
  }

  std::shared_ptr<ValueSource> frequency;

  void prepare(int frames) override {
    float duration = float(frames) / float(sampleRate_);
    enabled_ = duration > threshold_;

    if (frequency) frequency->prepare(frames);

    if (enabled_) {
      lfo_->prepare(frames);
    }
  }

  float next() override {
    float freq = frequency ? (frequency->next(), frequency->current()) : 440.0f;

    if (enabled_) {
      float mod = lfo_->next();
      cur_ = freq * std::max(0.01f, 1.0f + mod);
    } else {
      cur_ = freq;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  float speed_, depth_, attack_, threshold_;
  float speedVar_, depthVar_;
  bool enabled_{false};

  std::shared_ptr<RedNoiseSource> lfo_;
  float cur_{0.0f};
};

} // namespace mforce
