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
struct Vibrato final : ValueSource {
  Vibrato(int sampleRate, float speed = 5.0f, float depth = 0.02f, float attack = 0.3f,
          float threshold = 0.0f, float speedVar = 0.0f, float depthVar = 0.0f,
          float zeroCrossTendency = 1.0f, uint32_t seed = 0xF1B0'0000u)
  : sampleRate_(sampleRate), speed_(speed), depth_(depth), attack_(attack),
    threshold_(threshold), speedVar_(speedVar), depthVar_(depthVar),
    zct_(zeroCrossTendency), seed_(seed) {}

  void set_frequency(std::shared_ptr<ValueSource> s) { frequency_ = std::move(s); }
  std::shared_ptr<ValueSource> get_frequency() const { return frequency_; }

  const char* type_name() const override { return "Vibrato"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 440.0f, 0.01f, 20000.0f, "hz"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "frequency") { frequency_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "frequency") return frequency_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"speed",             ConfigType::Float, 5.0f,   0.01f, 20.0f},
      {"depth",             ConfigType::Float, 0.02f,  0.0f,  1.0f},
      {"attack",            ConfigType::Float, 0.3f,   0.0f,  1.0f},
      {"threshold",         ConfigType::Float, 0.0f,   0.0f,  10.0f},
      {"speedVar",          ConfigType::Float, 0.0f,   0.0f,  1.0f},
      {"depthVar",          ConfigType::Float, 0.0f,   0.0f,  1.0f},
      {"zeroCrossTendency", ConfigType::Float, 1.0f,   0.0f,  1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float v) override {
    if      (name == "speed")             speed_ = v;
    else if (name == "depth")             depth_ = v;
    else if (name == "attack")            attack_ = v;
    else if (name == "threshold")         threshold_ = v;
    else if (name == "speedVar")          speedVar_ = v;
    else if (name == "depthVar")          depthVar_ = v;
    else if (name == "zeroCrossTendency") zct_ = v;
  }

  float get_config(std::string_view name) const override {
    if (name == "speed")             return speed_;
    if (name == "depth")             return depth_;
    if (name == "attack")            return attack_;
    if (name == "threshold")         return threshold_;
    if (name == "speedVar")          return speedVar_;
    if (name == "depthVar")          return depthVar_;
    if (name == "zeroCrossTendency") return zct_;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    float duration = float(frames) / float(sampleRate_);
    enabled_ = duration > threshold_;

    if (frequency_) frequency_->prepare(ctx, frames);

    if (enabled_) {
      build_lfo();           // rebuild from current config scalars
      lfo_->prepare(ctx, frames);
    }
  }

  float next() override {
    float freq = frequency_ ? (frequency_->next(), frequency_->current()) : 440.0f;

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
  // Build the RedNoise LFO chain from the current scalar config. Speed ramps
  // 1 Hz → speed and depth ramps 0 → depth over the attack, then both HOLD for
  // the rest of the note — that's an ASEnvelope (attack→sustain).
  void build_lfo() {
    auto speedRamp = std::make_shared<ASEnvelope>(sampleRate_);
    speedRamp->set_config("attack", attack_);
    auto speedRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(speed_), speedRamp, true);

    auto depthRamp = std::make_shared<ASEnvelope>(sampleRate_);
    depthRamp->set_config("attack", attack_);
    auto depthRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(0.0f),
        std::make_shared<ConstantSource>(depth_), depthRamp, true);

    lfo_ = std::make_shared<RedNoiseSource>(sampleRate_, seed_);
    lfo_->set_frequency(speedRange);
    lfo_->set_amplitude(depthRange);
    lfo_->set_smoothness(std::make_shared<ConstantSource>(1.0f));
    lfo_->set_rampVariation(std::make_shared<ConstantSource>(speedVar_));
    lfo_->set_boost(std::make_shared<ConstantSource>(std::clamp(1.0f - depthVar_, 0.0f, 1.0f)));
    lfo_->set_zeroCrossTendency(std::make_shared<ConstantSource>(zct_));
  }

  std::shared_ptr<ValueSource> frequency_;
  int sampleRate_;
  float speed_, depth_, attack_, threshold_;
  float speedVar_, depthVar_, zct_;
  uint32_t seed_;
  bool enabled_{false};

  std::shared_ptr<RedNoiseSource> lfo_;
  float cur_{0.0f};
};

} // namespace mforce
