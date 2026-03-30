#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <cmath>

namespace mforce {

// Ported from C# MForce.Sound.Source.SineSource
// ComputeWaveValue: sin(CurrPos * 2 * PI)
struct SineSource final : WaveSource {
  explicit SineSource(int sampleRate) : WaveSource(sampleRate) {}

protected:
  float compute_wave_value() override {
    return std::sin(currPos_ * 2.0f * 3.14159265358979323846f);
  }
};

} // namespace mforce
