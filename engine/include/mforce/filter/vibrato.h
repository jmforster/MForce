#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/red_noise_source.h"
#include "mforce/core/envelope_presets.h"   // ASEnvelope (attack→sustain ramp)
#include "mforce/core/range_source.h"
#include <algorithm>
#include <memory>
#include <string_view>

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

  explicit Vibrato(uint32_t seed = 0xF1B0'0000u) : seed_(seed) {}

  const char* type_name() const override { return "Vibrato"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  // The LFO chain is built from the settings once, and again only when a
  // setting that shapes it has changed; the chain is prepared at every call so
  // it follows the host's rate.
  void on_prepare(const RenderContext& ctx) override {
    if (!lfo_ || lfoDirty_) {
      build_lfo();
      lfoDirty_ = false;
    }
    lfo_->prepare(ctx);
  }

  void on_setting_changed(std::string_view) override {
    lfoDirty_ = true;
    if (prepared()) on_prepare(render_context());
  }

  // A note starts with the chain a freshly built one would have: the noise
  // back at its seed and the ramps laid out over this note.
  void on_start(int frames) override {
    enabled_ = float(frames) / float(sample_rate()) > threshold.value;
    if (enabled_) {
      lfo_->reset();
      lfo_->start(frames);
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
    auto speedRamp = std::make_shared<ASEnvelope>();
    speedRamp->set_setting("attack", attack.value);
    auto speedRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(speed.value), speedRamp, true);

    auto depthRamp = std::make_shared<ASEnvelope>();
    depthRamp->set_setting("attack", attack.value);
    auto depthRange = std::make_shared<RangeSource>(
        std::make_shared<ConstantSource>(0.0f),
        std::make_shared<ConstantSource>(depth.value), depthRamp, true);

    lfo_ = std::make_shared<RedNoiseSource>(seed_);
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

  uint32_t seed_;
  bool enabled_{false};
  bool lfoDirty_{true};

  std::shared_ptr<RedNoiseSource> lfo_;
};

} // namespace mforce
