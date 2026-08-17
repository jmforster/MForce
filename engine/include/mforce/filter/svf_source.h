#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>

#include "mforce/core/dsp_value_source.h"

namespace mforce {

// ---------------------------------------------------------------------------
// SVFSource — 2-pole state-variable filter (TPT/zero-delay-feedback form),
// selectable Lowpass / Highpass / Bandpass tap, resonance as a modulatable
// param, optional output normalization by the resonance ("divide by res").
//
// Built for the AFNoding-031 piano rebuild (docs/research/afpiano_scratch/
// RECIPE.md): that patch uses SVFs in four roles our Butterworth filters
// cannot express — a resonant excitation lowpass whose resonance IS the
// "brightness" control (output divided by res so brightness changes color,
// not level), a post-string highpass, and a velocity-tracked resonant
// lowpass. One node, one selected tap; a patch needing LP and HP of the
// same signal instantiates two nodes, exactly as his graphs do.
//
// Real-time safe: no allocation in next(); tan() only re-runs when cutoff
// or resonance actually move (same guard as BWLowpassFilter, 2026-08-12
// CPU-audit convention).
// ---------------------------------------------------------------------------
struct SVFSource final : ValueSource {
  enum Mode { kLowpass = 0, kHighpass = 1, kBandpass = 2 };

  explicit SVFSource(int sampleRate) : sampleRate_(sampleRate) {}

  const char* type_name() const override { return "SVFSource"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"cutoffFreq", 1000.0f, 1.0f, 24000.0f, "hz"},
      {"resonance",  1.0f,    0.5f, 40.0f,    nullptr},
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
    static const char* const kModeLabels[] = {"Lowpass", "Highpass",
                                              "Bandpass", nullptr};
    static const ConfigDescriptor descs[] = {
      {"mode",      ConfigType::Int,  float(kLowpass), 0.0f, 2.0f, kModeLabels},
      // Divide the output by the resonance (AF "brightness" semantics:
      // resonance shapes color while the level stays put).
      {"normalize", ConfigType::Bool, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "cutoffFreq") { cutoffFreq_ = std::move(src); return; }
    if (name == "resonance")  { resonance_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "cutoffFreq") return cutoffFreq_;
    if (name == "resonance")  return resonance_;
    return nullptr;
  }

  void set_config(std::string_view name, float v) override {
    if (name == "mode")      { mode_ = std::clamp(int(v), 0, 2); return; }
    if (name == "normalize") { normalize_ = (v != 0.0f); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "mode")      return float(mode_);
    if (name == "normalize") return normalize_ ? 1.0f : 0.0f;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_)     source_->prepare(ctx, frames);
    if (cutoffFreq_) cutoffFreq_->prepare(ctx, frames);
    if (resonance_)  resonance_->prepare(ctx, frames);
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
  }

  float next() override {
    float in  = source_ ? (source_->next(), source_->current()) : 0.0f;
    float fc  = cutoffFreq_ ? (cutoffFreq_->next(), cutoffFreq_->current()) : 1000.0f;
    float res = resonance_ ? (resonance_->next(), resonance_->current()) : 1.0f;
    fc  = std::clamp(fc, 1.0f, float(sampleRate_) * 0.49f);
    res = std::clamp(res, 0.5f, 40.0f);

    if (fc != lastFc_ || res != lastRes_) {
      const float g = std::tan(3.14159265358979323846f * fc / float(sampleRate_));
      const float k = 1.0f / res;
      a1_ = 1.0f / (1.0f + g * (g + k));
      a2_ = g * a1_;
      a3_ = g * a2_;
      k_ = k;
      lastFc_ = fc;
      lastRes_ = res;
    }

    const float v3 = in - ic2eq_;
    const float v1 = a1_ * ic1eq_ + a2_ * v3;          // bandpass
    const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3; // lowpass
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    float out;
    switch (mode_) {
      case kHighpass: out = in - k_ * v1 - v2; break;
      case kBandpass: out = v1; break;
      default:        out = v2; break;
    }
    if (normalize_) out /= std::max(lastRes_, 0.5f);
    cur_ = out;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> cutoffFreq_;
  std::shared_ptr<ValueSource> resonance_;
  int sampleRate_;
  int mode_{kLowpass};
  bool normalize_{false};
  float a1_{0.0f}, a2_{0.0f}, a3_{0.0f}, k_{1.0f};
  float lastFc_{-1.0f}, lastRes_{-1.0f};
  float ic1eq_{0.0f}, ic2eq_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
