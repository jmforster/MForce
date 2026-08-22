#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.TriangleSource
// Bias controls the peak position (0..1); 0.5 = symmetric triangle.
//
// `power` warps the two straight legs into curves, modelled on RampSource's
// Expo/Inverse_Expo (MForce.Utility.Ramp). Signed shape control, neutral at 1:
//   |power| <= 1 : linear — the historical triangle, byte-for-byte.
//   power  >  1  : CONCAVE sides — legs pinch inward (spiky), exponent `power`.
//   power  < -1  : CONVEX  sides — legs bow outward (domed), exponent |power|.
// Default 1.0 keeps every existing patch bit-identical (none set `power`).
//
// By default both sides bend the SAME way: the falling leg is the mirror of the
// rising leg about the peak, so the waveform stays symmetric. Setting the
// `asymmetric` flag restores the earlier per-leg warp (each leg warped from its
// own start), which bends the two sides in opposite senses — a "shark fin".
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

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      // Off: symmetric legs (both sides bend the same way). On: the earlier
      // per-leg warp, which bends the two sides oppositely ("shark fin").
      {"asymmetric", SettingType::Bool, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "asymmetric") { asymmetric_ = (value != 0.0f); return; }
    WaveSource::set_setting(name, value);
  }
  float get_setting(std::string_view name) const override {
    if (name == "asymmetric") return asymmetric_ ? 1.0f : 0.0f;
    return WaveSource::get_setting(name);
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
  // Warp normalized leg position x in [0,1]. Identity in the neutral band.
  static float warp_leg(float x, float p) {
    const float e = std::fabs(p);
    if (p > 0.0f) return std::pow(x, e);              // concave (p > 1)
    return 1.0f - std::pow(1.0f - x, e);              // convex  (p < -1)
  }

  float compute_wave_value() override {
    bias_->next();
    power_->next();
    const float b = bias_->current();
    const float p = power_->current();

    // Neutral band: reproduce the legacy arithmetic exactly (byte-identical),
    // identical in both symmetric and asymmetric modes.
    if (std::fabs(p) <= 1.0f) {
      if (currPos_ <= b) {
        return -1.0f + currPos_ * (4.0f / b) / 2.0f;
      } else {
        return 1.0f - (currPos_ - b) * (4.0f / (1.0f - b)) / 2.0f;
      }
    }

    if (currPos_ <= b) {
      // Rising leg: -1 -> +1 as t goes 0 -> 1 (same in both modes).
      float t = std::clamp(currPos_ / b, 0.0f, 1.0f);
      return -1.0f + 2.0f * warp_leg(t, p);
    } else {
      float u = std::clamp((currPos_ - b) / (1.0f - b), 0.0f, 1.0f);
      if (asymmetric_) {
        // Each leg warped from its own start — bends the sides oppositely.
        return 1.0f - 2.0f * warp_leg(u, p);
      }
      // Symmetric: falling leg mirrors the rising leg about the peak, so both
      // sides bend the same way.
      return -1.0f + 2.0f * warp_leg(1.0f - u, p);
    }
  }

private:
  std::shared_ptr<ValueSource> bias_;
  std::shared_ptr<ValueSource> power_;
  bool asymmetric_{false};
};

} // namespace mforce
