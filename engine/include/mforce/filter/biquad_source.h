#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <memory>

namespace mforce {

// Raw-coefficient biquad section: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2
// (Direct Form I; a0 normalized to 1). The port vehicle for fitted filter
// sections from reference models — first use: STK Bowed's one-pole string
// loss (b1=b2=a2=0) and the six Maestre violin-body sections
// (docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md).
// Coefficients are settings, not pins: fitted constants, no legal
// interpolation between two fitted filters coefficient-by-coefficient.
// Resonance mode (STK BiQuad::setResonance, used by Brass's lip filter):
// a1/a2 derive per sample from the frequency/radius pins —
// a1 = -2 r cos(2 pi f / sr), a2 = r^2 — while b0 stays the gain and
// b1/b2 are ignored. Keytrackable two-pole resonator.
struct BiquadSource final : ValueSource {
  BiquadSource(int sampleRate)
    : sampleRate_(sampleRate),
      source_(std::make_shared<ConstantSource>(0.0f)),
      frequency_(std::make_shared<ConstantSource>(220.0f)),
      radius_(std::make_shared<ConstantSource>(0.997f)) {}

  const char* type_name() const override { return "Biquad"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 220.0f, 1.0f, 20000.0f, "hz"},
      {"radius",    0.997f, 0.0f, 0.99999f, "0-1"},
    };
    return descs;
  }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr const char* kModeLabels[] = {"Raw", "Resonance", nullptr};
    static constexpr SettingDescriptor descs[] = {
      {"mode", SettingType::Int, 0.0f, 0.0f, 1.0f, kModeLabels},
      {"b0", SettingType::Float, 1.0f, -10.0f, 10.0f},
      {"b1", SettingType::Float, 0.0f, -10.0f, 10.0f},
      {"b2", SettingType::Float, 0.0f, -10.0f, 10.0f},
      {"a1", SettingType::Float, 0.0f, -2.0f, 2.0f},
      {"a2", SettingType::Float, 0.0f, -1.0f, 1.0f},
    };
    return descs;
  }
  void set_setting(std::string_view name, float v) override {
    if      (name == "mode") resonanceMode_ = (int(v) == 1);
    else if (name == "b0") b0_ = v;
    else if (name == "b1") b1_ = v;
    else if (name == "b2") b2_ = v;
    else if (name == "a1") a1_ = v;
    else if (name == "a2") a2_ = v;
  }
  float get_setting(std::string_view name) const override {
    if (name == "mode") return resonanceMode_ ? 1.0f : 0.0f;
    if (name == "b0") return b0_;
    if (name == "b1") return b1_;
    if (name == "b2") return b2_;
    if (name == "a1") return a1_;
    if (name == "a2") return a2_;
    return 0.0f;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    source_ = std::move(src);
    else if (name == "frequency") frequency_ = std::move(src);
    else if (name == "radius")    radius_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "frequency") return frequency_;
    if (name == "radius")    return radius_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    frequency_->prepare(ctx, frames);
    radius_->prepare(ctx, frames);
    x1_ = x2_ = y1_ = y2_ = 0.0f;
    cur_ = 0.0f;
    pdHz_ = -1.0f;
    resFreq_ = resRad_ = -1.0f;
  }

  float next() override {
    const float x = source_ ? source_->next() : 0.0f;
    if (resonanceMode_) {
      frequency_->next();
      radius_->next();
      const float f = frequency_->current(), r = radius_->current();
      if (f != resFreq_ || r != resRad_) {
        resFreq_ = f; resRad_ = r;
        a1_ = -2.0f * r * std::cos(2.0f * 3.14159265358979323846f
                                   * f / float(sampleRate_));
        a2_ = r * r;
        pdHz_ = -1.0f;
      }
      const float y = b0_ * x - a1_ * y1_ - a2_ * y2_;
      y2_ = y1_; y1_ = y;
      cur_ = y;
      return cur_;
    }
    const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_; x1_ = x;
    y2_ = y1_; y1_ = y;
    cur_ = y;
    return cur_;
  }
  float current() const override { return cur_; }

  // Phase delay in samples at `hz`, from the closed-form H(e^{jw}). A
  // net sign inversion (e.g. negative b0 folding a reflection) contributes
  // +-pi of phase that is polarity, not delay — arg() is wrapped to
  // (-pi/2, pi/2] modulo pi before dividing by w so inverted sections
  // report the same delay as their positive twins.
  float phase_delay_at(float hz) override {
    if (hz <= 0.0f) return 0.0f;
    if (hz == pdHz_) return pd_;
    constexpr float pi = 3.14159265358979323846f;
    const float w = 2.0f * pi * hz / float(sampleRate_);
    const float c1 = std::cos(w), s1 = std::sin(w);
    const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
    const float nr = b0_ + b1_ * c1 + b2_ * c2;
    const float ni = -(b1_ * s1 + b2_ * s2);
    const float dr = 1.0f + a1_ * c1 + a2_ * c2;
    const float di = -(a1_ * s1 + a2_ * s2);
    float ph = std::atan2(ni, nr) - std::atan2(di, dr);
    while (ph >  0.5f * pi) ph -= pi;   // polarity fold (see above)
    while (ph <= -0.5f * pi) ph += pi;
    pdHz_ = hz;
    pd_ = -ph / w;
    return pd_;
  }

private:
  int sampleRate_;
  std::shared_ptr<ValueSource> source_, frequency_, radius_;
  bool resonanceMode_{false};
  float b0_{1.0f}, b1_{0.0f}, b2_{0.0f}, a1_{0.0f}, a2_{0.0f};
  float x1_{0.0f}, x2_{0.0f}, y1_{0.0f}, y2_{0.0f};
  float cur_{0.0f};
  float resFreq_{-1.0f}, resRad_{-1.0f};
  float pdHz_{-1.0f}, pd_{0.0f};
};

} // namespace mforce
