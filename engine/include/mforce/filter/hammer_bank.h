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
struct HammerBank final : Processor {
  Param frequency{this, "frequency", 220.0f, 10.0f, 8000.0f, "hz"};
  Setting<int>   numBands{this, "numBands", 4, 1, kMaxBands};
  Setting<float> harm1   {this, "harm1",    1.0f,   0.1f,    32.0f};
  Setting<float> harm2   {this, "harm2",    2.0f,   0.1f,    32.0f};
  Setting<float> harm3   {this, "harm3",    3.0f,   0.1f,    32.0f};
  Setting<float> harm4   {this, "harm4",    4.0f,   0.1f,    32.0f};
  Setting<float> resStart{this, "resStart", 20.0f,  0.5f,    200.0f};
  Setting<float> resEnd  {this, "resEnd",   1.5f,   0.5f,    50.0f};
  Setting<float> resDecay{this, "resDecay", 0.010f, 0.0005f, 0.5f};
  Setting<float> bandTilt{this, "bandTilt", -0.5f,  -3.0f,   3.0f};
  Setting<float> direct  {this, "direct",   0.0f,   0.0f,    1.0f};
  Setting<float> gain    {this, "gain",     1.0f,   0.0f,    8.0f};

  explicit HammerBank(int sampleRate) : sampleRate_(sampleRate) {}

  const char* type_name() const override { return "HammerBank"; }

  // numBands is clamped to the bank size; resEnd and resDecay are floored
  // (they have no ceiling).
  void on_setting_changed(std::string_view name) override {
    if      (name == "numBands") numBands.clamp();
    else if (name == "resEnd")   resEnd.value   = std::max(resEnd.value, 0.5f);
    else if (name == "resDecay") resDecay.value = std::max(resDecay.value, 1e-4f);
  }

  void on_prepare(const RenderContext&, int) override {
    for (int i = 0; i < kMaxBands; ++i) { low_[i] = 0.0f; band_[i] = 0.0f; }
    initialized_ = false;
    cur_ = 0.0f;
  }

  float compute() override {
    const float x = input.current();

    if (!initialized_) {
      const float harm[kMaxBands] = {harm1.value, harm2.value, harm3.value, harm4.value};
      float f0 = frequency.current();
      f0 = std::clamp(f0, 10.0f, float(sampleRate_) * 0.45f);
      const float fMax = float(sampleRate_) * 0.16f;  // Chamberlin stability
      float norm = 0.0f;
      for (int i = 0; i < numBands.value; ++i) {
        float fc = std::clamp(harm[i] * f0, 10.0f, fMax);
        fCoeff_[i] = 2.0f * std::sin(3.14159265f * fc / float(sampleRate_));
        bandGain_[i] = std::pow(std::max(harm[i], 0.1f), bandTilt.value);
        norm += bandGain_[i];
      }
      if (norm > 0.0f)
        for (int i = 0; i < numBands.value; ++i) bandGain_[i] /= norm;
      qDelta_ = std::max(resStart.value - resEnd.value, 0.0f);
      qDecayMul_ = std::exp(-1.0f / (resDecay.value * float(sampleRate_)));
      initialized_ = true;
    }

    // Decaying resonance: Q(t) = resEnd + (resStart - resEnd) * exp(-t/tau)
    float q = resEnd.value + qDelta_;
    qDelta_ *= qDecayMul_;
    float damp = 1.0f / q;

    float sum = 0.0f;
    for (int i = 0; i < numBands.value; ++i) {
      low_[i]  += fCoeff_[i] * band_[i];
      float high = x - low_[i] - damp * band_[i];
      band_[i] += fCoeff_[i] * high;
      sum += band_[i] * bandGain_[i];
    }

    return gain.value * sum + direct.value * x;
  }

private:
  static constexpr int kMaxBands = 4;

  int sampleRate_;

  // Per-note state
  bool  initialized_{false};
  float fCoeff_[kMaxBands]{};
  float bandGain_[kMaxBands]{};
  float qDelta_{0.0f};
  float qDecayMul_{1.0f};
  float low_[kMaxBands]{};
  float band_[kMaxBands]{};
};

} // namespace mforce
