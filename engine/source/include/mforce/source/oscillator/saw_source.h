#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/dsp/blep.h"
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.SawSource
// Band-limited sawtooth via polynomial BLEP.
struct SawSource final : WaveSource {
  const char* type_name() const override { return "SawSource"; }

protected:
  float wave() override {
    const float val = 2.0f * currPos_ - 1.0f;
    return std::clamp(val - dsp::blep(currPos_, currPhaseIncr_), -1.0f, 1.0f);
  }
};

} // namespace mforce
