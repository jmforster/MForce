#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp/biquad.h"
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
    section_.reset();
    cur_ = 0.0f;
    pdHz_ = -1.0f;
    resFreq_ = resRad_ = -1.0f;
  }

  // The settings are the coefficients the user sees; the section is loaded
  // from them each sample, as the inline form read them. In resonance mode
  // the two pole coefficients are derived from the pins and written back into
  // the settings, so the editor and a saved patch show them.
  float compute() override {
    const float x = input.current();
    if (mode.value == kResonance) {
      frequency.next();
      radius.next();
      const float f = frequency.current(), r = radius.current();
      if (f != resFreq_ || r != resRad_) {
        resFreq_ = f; resRad_ = r;
        dsp::BiquadCoefficients poles;
        poles.set_resonance(f, r, float(sample_rate()), b0.value);
        a1.value = poles.a1();
        a2.value = poles.a2();
        pdHz_ = -1.0f;
      }
      section_.coefficients.set_raw(b0.value, 0.0f, 0.0f, a1.value, a2.value);
      return section_.process(x);
    }
    section_.coefficients.set_raw(b0.value, b1.value, b2.value, a1.value, a2.value);
    return section_.process(x);
  }

  // Phase delay in samples at `hz` (the coefficients' closed form), cached
  // on the frequency asked for.
  float phase_delay_at(float hz) override {
    if (hz <= 0.0f) return 0.0f;
    if (hz == pdHz_) return pd_;
    dsp::BiquadCoefficients c;
    c.set_raw(b0.value, b1.value, b2.value, a1.value, a2.value);
    pdHz_ = hz;
    pd_ = c.phase_delay_at(hz, float(sample_rate()));
    return pd_;
  }

private:
  static constexpr const char* kModeLabels[] = {"Raw", "Resonance", nullptr};

  dsp::Biquad section_;
  float resFreq_{-1.0f}, resRad_{-1.0f};
  float pdHz_{-1.0f}, pd_{0.0f};
};

} // namespace mforce
