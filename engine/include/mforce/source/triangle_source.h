#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.TriangleSource
// Bias controls the peak position (0..1); 0.5 = symmetric triangle.
//
// `power` warps each of the two straight legs into a curve, modelled on
// RampSource's Expo/Inverse_Expo (MForce.Utility.Ramp). It is a SIGNED shape
// control, neutral at 1:
//   |power| <= 1 : linear — the historical triangle, byte-for-byte.
//   power  >  1  : concave legs, exponent `power`  (leg = t^power, as Ramp Expo).
//   power  < -1  : convex  legs, exponent |power|  (leg = 1-(1-t)^|power|, mirror).
// Default 1.0 keeps every existing patch bit-identical (none set `power`).
struct TriangleSource final : WaveSource {
  explicit TriangleSource(int sampleRate)
  : WaveSource(sampleRate)
  , bias_(std::make_shared<ConstantSource>(0.5f))
  , power_(std::make_shared<ConstantSource>(1.0f)) {}

  void set_bias(std::shared_ptr<ValueSource> b) { bias_ = std::move(b); }
  void set_power(std::shared_ptr<ValueSource> p) { power_ = std::move(p); }

  const char* type_name() const override { return "TriangleSource"; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 440.0f, 0.01f, 20000.0f, "hz"},
      {"amplitude", 1.0f,   0.0f,  10.0f,    "0-1"},
      {"phase",     0.0f,  -1.0f,  1.0f,     "cycles"},
      {"bias",      0.5f,   0.0f,  1.0f,     "0-1"},
      {"power",     1.0f,  -8.0f,  8.0f,     "shape"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "bias")  { set_bias(std::move(src)); return; }
    if (name == "power") { set_power(std::move(src)); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "bias")  return bias_;
    if (name == "power") return power_;
    return WaveSource::get_param(name);
  }

  void prepare(const RenderContext& ctx, int frames) override {
    WaveSource::prepare(ctx, frames);
    bias_->prepare(ctx, frames);
    power_->prepare(ctx, frames);
  }

protected:
  // Warp normalized leg position t in [0,1]. Identity in the neutral band.
  static float warp_leg(float t, float p) {
    const float e = std::fabs(p);
    if (p > 0.0f) return std::pow(t, e);              // concave (p > 1)
    return 1.0f - std::pow(1.0f - t, e);              // convex  (p < -1)
  }

  float compute_wave_value() override {
    bias_->next();
    power_->next();
    const float b = bias_->current();
    const float p = power_->current();

    // Neutral band: reproduce the legacy arithmetic exactly (byte-identical).
    if (std::fabs(p) <= 1.0f) {
      if (currPos_ <= b) {
        return -1.0f + currPos_ * (4.0f / b) / 2.0f;
      } else {
        return 1.0f - (currPos_ - b) * (4.0f / (1.0f - b)) / 2.0f;
      }
    }

    if (currPos_ <= b) {
      float t = std::clamp(currPos_ / b, 0.0f, 1.0f);
      return -1.0f + 2.0f * warp_leg(t, p);
    } else {
      float u = std::clamp((currPos_ - b) / (1.0f - b), 0.0f, 1.0f);
      return 1.0f - 2.0f * warp_leg(u, p);
    }
  }

private:
  std::shared_ptr<ValueSource> bias_;
  std::shared_ptr<ValueSource> power_;
};

} // namespace mforce
