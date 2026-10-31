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
struct SVFSource final : Processor {
  enum Mode { kLowpass = 0, kHighpass = 1, kBandpass = 2, kLowpass1P = 3, kHighpass1P = 4 };

  Param cutoffFreq{this, "cutoffFreq", 1000.0f, 1.0f, 24000.0f, "hz"};
  Param resonance {this, "resonance",  1.0f,    0.5f, 40.0f,    nullptr};
  Setting<int>  mode{this, "mode", kLowpass, kLowpass, kHighpass1P, kModeLabels};
  // Divide the output by the resonance (AF "brightness" semantics:
  // resonance shapes color while the level stays put).
  Setting<bool> normalize{this, "normalize", false, false, true};

  const char* type_name() const override { return "SVFSource"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "mode") mode.clamp();
  }

  // The cached coefficients and phase delay derive from the rate; a new rate
  // drops them. A repeat prepare at the same rate keeps them: they carry
  // across notes.
  void on_prepare(const RenderContext&) override {
    if (builtRate_ == sample_rate()) return;
    builtRate_ = sample_rate();
    lastFc_ = lastRes_ = -1.0f;
    pdHz_ = pdFc_ = pdRes_ = -1.0f;
  }

  void on_start(int) override {
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
    lp1_ = 0.0f;
  }

  float compute() override {
    float in  = input.current();
    float fc  = cutoffFreq.current();
    float res = resonance.current();
    fc  = std::clamp(fc, 1.0f, float(sample_rate()) * 0.49f);
    res = std::clamp(res, 0.5f, 40.0f);

    if (fc != lastFc_ || res != lastRes_) {
      const float g = std::tan(3.14159265358979323846f * fc / float(sample_rate()));
      const float k = 1.0f / res;
      a1_ = 1.0f / (1.0f + g * (g + k));
      a2_ = g * a1_;
      a3_ = g * a2_;
      k_ = k;
      lastFc_ = fc;
      lastRes_ = res;
    }

    // 1-pole modes (AF "Filter 1P": 6 dB/oct, no resonance) share the
    // cutoff smoothing but bypass the 2-pole core.
    if (mode.value == kLowpass1P || mode.value == kHighpass1P) {
      const float g1 = std::tan(3.14159265358979323846f * lastFc_ / float(sample_rate()));
      const float a = g1 / (1.0f + g1);
      lp1_ += a * (in - lp1_);
      return (mode.value == kLowpass1P) ? lp1_ : in - lp1_;
    }

    const float v3 = in - ic2eq_;
    const float v1 = a1_ * ic1eq_ + a2_ * v3;          // bandpass
    const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3; // lowpass
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    float out;
    switch (mode.value) {
      case kHighpass: out = in - k_ * v1 - v2; break;
      case kBandpass: out = v1; break;
      default:        out = v2; break;
    }
    if (normalize.value) out /= std::max(lastRes_, 0.5f);
    return out;
  }

  // Exact phase delay of the ZDF core at `hz`. The TPT integrator equals the
  // bilinear transform of the analog SVF, so H(e^{jw}) is the analog response
  // evaluated at O = tan(w/2)/g — closed form, no impulse measurement needed.
  // Uses the coefficient state actually filtering (lastFc_/lastRes_), which
  // exists only after the first next(); until then reports 0. Cached on
  // (hz, fc, res, mode) so per-sample callers pay one comparison.
  float phase_delay_at(float hz) override {
    if (hz <= 0.0f || lastFc_ < 0.0f) return 0.0f;
    if (hz == pdHz_ && lastFc_ == pdFc_ && lastRes_ == pdRes_ && mode.value == pdMode_)
      return pd_;
    constexpr float pi = 3.14159265358979323846f;
    const float w = 2.0f * pi * hz / float(sample_rate());
    const float tw = std::tan(std::min(0.5f * w, 0.49f * pi));
    float d;
    if (mode.value == kLowpass1P || mode.value == kHighpass1P) {
      // The 1P path is the leaky integrator y += a(x-y), NOT a TPT one-pole:
      // H_lp = a/(1 - b z^-1), b = 1-a. Its phase differs from the bilinear
      // form (measured: 7.15 vs 3.75 samples at fc=2k, hz=400), so use the
      // difference equation's own transfer function.
      const float g1 = std::tan(pi * lastFc_ / float(sample_rate()));
      const float b = 1.0f - g1 / (1.0f + g1);
      const float argDen = std::atan2(b * std::sin(w), 1.0f - b * std::cos(w));
      d = (mode.value == kLowpass1P)
          ? argDen / w
          : argDen / w - 0.5f * pi / w + 0.5f;  // hp = b(1-z^-1)/(1-b z^-1)
    } else {
      const float o = tw / std::tan(pi * lastFc_ / float(sample_rate()));
      // Denominator 1 - O^2 + j*k*O shared by all three taps; numerators
      // contribute 0 (LP), +90 deg (BP), or 180 deg (HP) of constant phase.
      const float argD = std::atan2(k_ * o, 1.0f - o * o);
      if      (mode.value == kLowpass)  d = argD / w;
      else if (mode.value == kBandpass) d = (argD - 0.5f * pi) / w;
      else                              d = (argD - pi) / w;
    }
    pdHz_ = hz; pdFc_ = lastFc_; pdRes_ = lastRes_; pdMode_ = mode.value; pd_ = d;
    return d;
  }

private:
  static constexpr const char* kModeLabels[] = {"Lowpass", "Highpass", "Bandpass",
                                                "Lowpass1P", "Highpass1P", nullptr};

  int builtRate_{0};
  float a1_{0.0f}, a2_{0.0f}, a3_{0.0f}, k_{1.0f};
  float lastFc_{-1.0f}, lastRes_{-1.0f};
  float pdHz_{-1.0f}, pdFc_{-1.0f}, pdRes_{-1.0f}, pd_{0.0f};
  int pdMode_{-1};
  float ic1eq_{0.0f}, ic2eq_{0.0f};
  float lp1_{0.0f};
};

} // namespace mforce
