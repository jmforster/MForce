#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// HammerBank — piano-hammer excitation shaper (dsp run 24, AF-piano port).
//
// From Balazs Gyutai's Alpha Forever piano description: "The hammer is built
// of 4 svf's (bandpasses) with fast decaying resonance. They are tuned to
// harmonic frequencies of the note." The input is NOT a noise burst but a
// simple decaying envelope (wire an Envelope node to `source`).
//
// Implementation: up to 4 Chamberlin state-variable bandpass filters at
// harm_i * frequency. Resonance (Q) starts at resStart and decays
// exponentially to resEnd with time constant resDecay — the bands ring
// briefly and then widen, which is what turns a dull envelope thump into a
// pitched hammer strike. Per-band gain follows harm^bandTilt.
//
// Per-note behavior: frequency is captured at the first next() after
// prepare() (same lazy-init convention as WavetableSource), so paramMap
// "frequency" retunes the bank per note. Real-time safe: no heap after
// construction, all state in fixed arrays.
// ---------------------------------------------------------------------------
struct HammerBank final : ValueSource {

  explicit HammerBank(int sampleRate) : sampleRate_(sampleRate) {
    frequency_ = std::make_shared<ConstantSource>(220.0f);
  }

  const char* type_name() const override { return "HammerBank"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 220.0f, 10.0f, 8000.0f, "hz"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"numBands",  ConfigType::Int,   4.0f,   1.0f,  4.0f},
      {"harm1",     ConfigType::Float, 1.0f,   0.1f,  32.0f},
      {"harm2",     ConfigType::Float, 2.0f,   0.1f,  32.0f},
      {"harm3",     ConfigType::Float, 3.0f,   0.1f,  32.0f},
      {"harm4",     ConfigType::Float, 4.0f,   0.1f,  32.0f},
      {"resStart",  ConfigType::Float, 20.0f,  0.5f,  200.0f},
      {"resEnd",    ConfigType::Float, 1.5f,   0.5f,  50.0f},
      {"resDecay",  ConfigType::Float, 0.010f, 0.0005f, 0.5f},
      {"bandTilt",  ConfigType::Float, -0.5f,  -3.0f, 3.0f},
      {"direct",    ConfigType::Float, 0.0f,   0.0f,  1.0f},
      {"gain",      ConfigType::Float, 1.0f,   0.0f,  8.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_    = std::move(src); return; }
    if (name == "frequency") { frequency_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "frequency") return frequency_;
    return nullptr;
  }

  void set_config(std::string_view name, float v) override {
    if (name == "numBands") { numBands_ = std::clamp(int(v), 1, kMaxBands); return; }
    if (name == "harm1")    { harm_[0] = v; return; }
    if (name == "harm2")    { harm_[1] = v; return; }
    if (name == "harm3")    { harm_[2] = v; return; }
    if (name == "harm4")    { harm_[3] = v; return; }
    if (name == "resStart") { resStart_ = v; return; }
    if (name == "resEnd")   { resEnd_   = std::max(v, 0.5f); return; }
    if (name == "resDecay") { resDecay_ = std::max(v, 1e-4f); return; }
    if (name == "bandTilt") { bandTilt_ = v; return; }
    if (name == "direct")   { direct_   = v; return; }
    if (name == "gain")     { gain_     = v; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "numBands") return float(numBands_);
    if (name == "harm1")    return harm_[0];
    if (name == "harm2")    return harm_[1];
    if (name == "harm3")    return harm_[2];
    if (name == "harm4")    return harm_[3];
    if (name == "resStart") return resStart_;
    if (name == "resEnd")   return resEnd_;
    if (name == "resDecay") return resDecay_;
    if (name == "bandTilt") return bandTilt_;
    if (name == "direct")   return direct_;
    if (name == "gain")     return gain_;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_)    source_->prepare(ctx, frames);
    if (frequency_) frequency_->prepare(ctx, frames);
    for (int i = 0; i < kMaxBands; ++i) { low_[i] = 0.0f; band_[i] = 0.0f; }
    initialized_ = false;
    cur_ = 0.0f;
  }

  float next() override {
    if (frequency_) frequency_->next();
    float x = 0.0f;
    if (source_) { source_->next(); x = source_->current(); }

    if (!initialized_) {
      float f0 = frequency_ ? frequency_->current() : 220.0f;
      f0 = std::clamp(f0, 10.0f, float(sampleRate_) * 0.45f);
      const float fMax = float(sampleRate_) * 0.16f;  // Chamberlin stability
      float norm = 0.0f;
      for (int i = 0; i < numBands_; ++i) {
        float fc = std::clamp(harm_[i] * f0, 10.0f, fMax);
        fCoeff_[i] = 2.0f * std::sin(3.14159265f * fc / float(sampleRate_));
        bandGain_[i] = std::pow(std::max(harm_[i], 0.1f), bandTilt_);
        norm += bandGain_[i];
      }
      if (norm > 0.0f)
        for (int i = 0; i < numBands_; ++i) bandGain_[i] /= norm;
      qDelta_ = std::max(resStart_ - resEnd_, 0.0f);
      qDecayMul_ = std::exp(-1.0f / (resDecay_ * float(sampleRate_)));
      initialized_ = true;
    }

    // Decaying resonance: Q(t) = resEnd + (resStart - resEnd) * exp(-t/tau)
    float q = resEnd_ + qDelta_;
    qDelta_ *= qDecayMul_;
    float damp = 1.0f / q;

    float sum = 0.0f;
    for (int i = 0; i < numBands_; ++i) {
      low_[i]  += fCoeff_[i] * band_[i];
      float high = x - low_[i] - damp * band_[i];
      band_[i] += fCoeff_[i] * high;
      sum += band_[i] * bandGain_[i];
    }

    cur_ = gain_ * sum + direct_ * x;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  static constexpr int kMaxBands = 4;

  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> frequency_;
  int sampleRate_;

  // Config
  int   numBands_{4};
  float harm_[kMaxBands]{1.0f, 2.0f, 3.0f, 4.0f};
  float resStart_{20.0f};
  float resEnd_{1.5f};
  float resDecay_{0.010f};
  float bandTilt_{-0.5f};
  float direct_{0.0f};
  float gain_{1.0f};

  // Per-note state
  bool  initialized_{false};
  float fCoeff_[kMaxBands]{};
  float bandGain_[kMaxBands]{};
  float qDelta_{0.0f};
  float qDecayMul_{1.0f};
  float low_[kMaxBands]{};
  float band_[kMaxBands]{};
  float cur_{0.0f};
};

} // namespace mforce
