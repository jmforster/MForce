#pragma once
#include "mforce/core/dsp_wave_source.h"
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

  const char* type_name() const override { return "PulseSource"; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",  440.0f, 0.01f, 20000.0f, "hz"},
      {"amplitude",  1.0f,   0.0f,  10.0f,    "0-1"},
      {"phase",      0.0f,  -1.0f,  1.0f,     "cycles"},
      {"dutyCycle",  0.5f,   0.01f, 0.99f,    "0-1"},
      {"bend",       0.0f,  -1.0f,  1.0f,     "±1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "dutyCycle") { set_duty_cycle(std::move(src)); return; }
    if (name == "bend")     { set_bend(std::move(src)); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "dutyCycle") return dutyCycle_;
    if (name == "bend")     return bend_;
    return WaveSource::get_param(name);
  }

  void prepare(const RenderContext& ctx, int frames) override {
    WaveSource::prepare(ctx, frames);
    dutyCycle_->prepare(ctx, frames);
    bend_->prepare(ctx, frames);
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
