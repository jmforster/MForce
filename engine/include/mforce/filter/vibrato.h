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

  // The LFO chain is built from the settings at the first prepare; the chain
  // (and a pending one) is prepared at every call so it follows the host's
  // rate.
  void on_prepare(const RenderContext& ctx) override {
    if (!lfo_) lfo_ = build_lfo();
    lfo_->prepare(ctx);
    if (pending_) pending_->prepare(ctx);
  }

  // A setting that shapes the chain builds the NEXT chain here, on the
  // thread that changed the setting (a note's Setup, the UI); the note that
  // is playing keeps its chain, as it always did, and the next start swaps
  // the pending one in. The chain retired at the last swap is released
  // here too, so the audio thread neither allocates nor frees.
  void on_setting_changed(std::string_view) override {
    if (!prepared()) return;          // the first prepare builds from the settings
    retired_.reset();
    pending_ = build_lfo();
    pending_->prepare(render_context());
  }

  // A note starts with the chain a freshly built one would have: the noise
  // back at its seed and the ramps laid out over this note.
  void on_start(int frames) override {
    if (pending_) {
      retired_ = std::move(lfo_);
      lfo_ = std::move(pending_);
    }
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
  // Build a RedNoise LFO chain from the current scalar config. Speed ramps
  // 1 Hz → speed and depth ramps 0 → depth over the attack, then both HOLD for
  // the rest of the note — that's an ASEnvelope (attack→sustain).
  std::shared_ptr<RedNoiseSource> build_lfo() const {
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

    auto lfo = std::make_shared<RedNoiseSource>(seed_);
    lfo->frequency.set(speedRange);
    lfo->amplitude.set(depthRange);
    lfo->density.set(std::make_shared<ConstantSource>(1.0f));   // every ramp carries a value
    lfo->smoothness.set(std::make_shared<ConstantSource>(1.0f));
    lfo->rampVariation.set(std::make_shared<ConstantSource>(speedVar.value));
    lfo->boost.set(std::make_shared<ConstantSource>(std::clamp(1.0f - depthVar.value, 0.0f, 1.0f)));
    lfo->zeroCrossTendency.set(std::make_shared<ConstantSource>(zeroCrossTendency.value));
    return lfo;
  }

  // Per-note determinism (onsets-v2 addendum): forward to the LFO's rng.
  void reseed() override { if (lfo_) lfo_->reseed(); }

  uint32_t seed_;
  bool enabled_{false};

  std::shared_ptr<RedNoiseSource> lfo_;        // the chain the current note plays
  std::shared_ptr<RedNoiseSource> pending_;    // built on a setting change; swapped in at the next start
  std::shared_ptr<RedNoiseSource> retired_;    // the chain the last swap replaced; freed on the next setting change
};

} // namespace mforce
