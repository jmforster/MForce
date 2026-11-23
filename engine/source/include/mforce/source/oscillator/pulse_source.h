#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/dsp/blep.h"
#include <cmath>

namespace mforce {

// Ported from C# MForce.Sound.Source.PulseSource
// Band-limited pulse wave via polynomial BLEP.
// dutyCycle: fraction of period spent "high" (0..1)
// bend: 0 = square wave, 1 = sawtooth-like slope on top
struct PulseSource final : WaveSource {
  Param dutyCycle{this, "dutyCycle", 0.5f,  0.01f, 0.99f, "0-1"};
  Param bend     {this, "bend",      0.0f, -1.0f,  1.0f,  "±1"};

  const char* type_name() const override { return "PulseSource"; }

protected:
  float wave() override {
    const float duty = dutyCycle.current();
    const float b    = bend.current();

    float val = 0.0f;
    if (currPos_ < duty) {
      val = 1.0f - (1.0f - currPos_ / duty) * b;
    } else {
      val = -1.0f - (duty - currPos_) * 2.0f * b;
    }

    // One step up at the wrap, one step down a duty cycle later.
    return val + dsp::blep(currPos_, currPhaseIncr_)
               - dsp::blep(std::fmod(currPos_ + (1.0f - duty), 1.0f), currPhaseIncr_);
  }
};

} // namespace mforce
