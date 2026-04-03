#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.SawSource
// Band-limited sawtooth via polynomial BLEP.
struct SawSource final : WaveSource {
  explicit SawSource(int sampleRate) : WaveSource(sampleRate) {}
  const char* type_name() const override { return "SawSource"; }

protected:
  float compute_wave_value() override {
    float val = 2.0f * currPos_ - 1.0f;

    float blep = 0.0f;
    if (currPos_ < currPhaseIncr_) {
      float t = currPos_ / currPhaseIncr_;
      blep = t + t - t * t - 1.0f;
    } else if (currPos_ > 1.0f - currPhaseIncr_) {
      float t = (currPos_ - 1.0f) / currPhaseIncr_;
      blep = t * t + t + t + 1.0f;
    }

    return std::clamp(val - blep, -1.0f, 1.0f);
  }
};

} // namespace mforce
