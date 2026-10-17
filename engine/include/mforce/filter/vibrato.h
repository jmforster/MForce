#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/red_noise_source.h"
#include "mforce/core/envelope_presets.h"   // ASEnvelope (attack→sustain ramp)
#include "mforce/core/range_source.h"
#include <memory>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Envelope.Vibrato
// Modulates a frequency value with a RedNoise LFO whose speed and depth
// ramp up over the attack and then HOLD for the rest of the note.
//
// Output: frequency * (1 + lfo), lfo in [-depth, +depth]
//
// Parameters:
//   frequency:          base frequency to modulate (ValueSource pin)
//   speed:              vibrato rate in Hz
//   depth:              max modulation depth (fraction of frequency)
//   attack:             fraction of note duration for depth/speed ramp-in
//   threshold:          minimum note duration (s) below which vibrato is off
//   speedVar:           per-half-cycle rate jitter (0=metronomic, →RedNoise rampVariation)
//   depthVar:           per-half-cycle depth jitter (0=constant, →RedNoise boost=1-depthVar)
//   zeroCrossTendency:  1=strict alternation (regular wobble, legacy default),
//                       0=organic sign wander. Legacy Vibrato hardcoded 1.0.
struct Vibrato final : Generator {
  Param frequency{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  Setting<float> speed            {this, "speed",             5.0f,  0.01f, 20.0f};
  Setting<float> depth            {this, "depth",             0.02f, 0.0f,  1.0f};
  Setting<float> attack           {this, "attack",            0.3f,  0.0f,  1.0f};
  Setting<float> threshold        {this, "threshold",         0.0f,  0.0f,  10.0f};
  Setting<float> speedVar         {this, "speedVar",          0.0f,  0.0f,  1.0f};
  Setting<float> depthVar         {this, "depthVar",          0.0f,  0.0f,  1.0f};
  Setting<float> zeroCrossTendency{this, "zeroCrossTendency", 1.0f, 0.0f,  1.0f};

  Vibrato(int sampleRate, float speedHz = 5.0f, float depthFrac = 0.02f, float attackFrac = 0.3f,
          float thresholdSec = 0.0f, float speedVarFrac = 0.0f, float depthVarFrac = 0.0f,
          float zeroCrossTend = 1.0f, uint32_t seed = 0xF1B0'0000u)
  : sampleRate_(sampleRate), seed_(seed) {
    speed.value = speedHz;
    depth.value = depthFrac;
    attack.value = attackFrac;
    threshold.value = thresholdSec;
    speedVar.value = speedVarFrac;
    depthVar.value = depthVarFrac;
    zeroCrossTendency.value = zeroCrossTend;
  }

  const char* type_name() const override { return "Vibrato"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  void on_prepare(const RenderContext& ctx, int frames) override {
    float duration = float(frames) / float(sampleRate_);
    enabled_ = duration > threshold.value;

    if (enabled_) {
      build_lfo();           // rebuild from current config scalars
      lfo_->prepare(ctx, frames);
    }
  }

  float compute() override {
    float freq = frequency.current();

    if (enabled_) {
      float mod = lfo_->advance();
      return freq * std::max(0.01f, 1.0f + mod);
    }
    return freq;
  }

private:
  // Build the RedNoise LFO chain from the current scalar config. Speed ramps
  // 1 Hz → speed and depth ramps 0 → depth over the attack, then both HOLD for
  // the rest of the note — that's an ASEnvelope (attack→sustain).
  void build_lfo() {
    auto speedRamp = std::make_shared<ASEnvelope>(sampleRate_);
    speedRamp->set_setting("attack", attack.value);
    auto speedRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(speed.value), speedRamp, true);

    auto depthRamp = std::make_shared<ASEnvelope>(sampleRate_);
    depthRamp->set_setting("attack", attack.value);
    auto depthRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(0.0f),
        std::make_shared<ConstantSource>(depth.value), depthRamp, true);

    lfo_ = std::make_shared<RedNoiseSource>(sampleRate_, seed_);
    lfo_->frequency.set(speedRange);
    lfo_->amplitude.set(depthRange);
    lfo_->density.set(std::make_shared<ConstantSource>(1.0f));   // every ramp carries a value
    lfo_->smoothness.set(std::make_shared<ConstantSource>(1.0f));
    lfo_->rampVariation.set(std::make_shared<ConstantSource>(speedVar.value));
    lfo_->boost.set(std::make_shared<ConstantSource>(std::clamp(1.0f - depthVar.value, 0.0f, 1.0f)));
    lfo_->zeroCrossTendency.set(std::make_shared<ConstantSource>(zeroCrossTendency.value));
  }

  // Per-note determinism (onsets-v2 addendum): forward to the LFO's rng.
  void reseed() override { if (lfo_) lfo_->reseed(); }

  int sampleRate_;
  uint32_t seed_;
  bool enabled_{false};

  std::shared_ptr<RedNoiseSource> lfo_;
};

} // namespace mforce
