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
struct BiquadSource final : Processor {
  enum Mode { kRaw = 0, kResonance = 1 };

  // frequency and radius are read only in resonance mode, so the node pulls
  // them itself (Manual): the base neither prepares nor advances a Manual pin.
  Param frequency{this, "frequency", 220.0f,  1.0f, 20000.0f, "hz",  Pull::Manual};
  Param radius   {this, "radius",    0.997f,  0.0f, 0.99999f, "0-1", Pull::Manual};
  Setting<int>   mode{this, "mode", kRaw, kRaw, kResonance, kModeLabels};
  Setting<float> b0{this, "b0", 1.0f, -10.0f, 10.0f};
  Setting<float> b1{this, "b1", 0.0f, -10.0f, 10.0f};
  Setting<float> b2{this, "b2", 0.0f, -10.0f, 10.0f};
  Setting<float> a1{this, "a1", 0.0f,  -2.0f,  2.0f};
  Setting<float> a2{this, "a2", 0.0f,  -1.0f,  1.0f};

  const char* type_name() const override { return "Biquad"; }

  void on_prepare(const RenderContext& ctx) override {
    frequency.prepare(ctx);
    radius.prepare(ctx);
  }

  void on_start(int frames) override {
    frequency.start(frames);
    radius.start(frames);
    x1_ = x2_ = y1_ = y2_ = 0.0f;
    cur_ = 0.0f;
    pdHz_ = -1.0f;
    resFreq_ = resRad_ = -1.0f;
  }

  float compute() override {
    const float x = input.current();
    if (mode.value == kResonance) {
      frequency.next();
      radius.next();
      const float f = frequency.current(), r = radius.current();
      if (f != resFreq_ || r != resRad_) {
        resFreq_ = f; resRad_ = r;
        a1.value = -2.0f * r * std::cos(2.0f * 3.14159265358979323846f
                                        * f / float(sample_rate()));
        a2.value = r * r;
        pdHz_ = -1.0f;
      }
      const float y = b0.value * x - a1.value * y1_ - a2.value * y2_;
      y2_ = y1_; y1_ = y;
      return y;
    }
    const float y = b0.value * x + b1.value * x1_ + b2.value * x2_
                  - a1.value * y1_ - a2.value * y2_;
    x2_ = x1_; x1_ = x;
    y2_ = y1_; y1_ = y;
    return y;
  }

  // Phase delay in samples at `hz`, from the closed-form H(e^{jw}). A
  // net sign inversion (e.g. negative b0 folding a reflection) contributes
  // +-pi of phase that is polarity, not delay — arg() is wrapped to
  // (-pi/2, pi/2] modulo pi before dividing by w so inverted sections
  // report the same delay as their positive twins.
  float phase_delay_at(float hz) override {
    if (hz <= 0.0f) return 0.0f;
    if (hz == pdHz_) return pd_;
    constexpr float pi = 3.14159265358979323846f;
    const float w = 2.0f * pi * hz / float(sample_rate());
    const float c1 = std::cos(w), s1 = std::sin(w);
    const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
    const float nr = b0.value + b1.value * c1 + b2.value * c2;
    const float ni = -(b1.value * s1 + b2.value * s2);
    const float dr = 1.0f + a1.value * c1 + a2.value * c2;
    const float di = -(a1.value * s1 + a2.value * s2);
    float ph = std::atan2(ni, nr) - std::atan2(di, dr);
    while (ph >  0.5f * pi) ph -= pi;   // polarity fold (see above)
    while (ph <= -0.5f * pi) ph += pi;
    pdHz_ = hz;
    pd_ = -ph / w;
    return pd_;
  }

private:
  static constexpr const char* kModeLabels[] = {"Raw", "Resonance", nullptr};

  float x1_{0.0f}, x2_{0.0f}, y1_{0.0f}, y2_{0.0f};
  float resFreq_{-1.0f}, resRad_{-1.0f};
  float pdHz_{-1.0f}, pd_{0.0f};
};

} // namespace mforce
