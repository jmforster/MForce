#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <cmath>

namespace mforce {

// Ported from C# MForce.Sound.Source.PulseSource
// Band-limited pulse wave via polynomial BLEP.
// dutyCycle: fraction of period spent "high" (0..1)
// bend: 0 = square wave, 1 = sawtooth-like slope on top
struct PulseSource final : WaveSource {
  Param dutyCycle{this, "dutyCycle", 0.5f,  0.01f, 0.99f, "0-1"};
  Param bend     {this, "bend",      0.0f, -1.0f,  1.0f,  "±1"};

  explicit PulseSource(int sampleRate) : WaveSource(sampleRate) {}
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

    return val + get_blep(currPos_) - get_blep(std::fmod(currPos_ + (1.0f - duty), 1.0f));
  }

private:
  static float get_blep(float pos, float phaseIncr) {
    if (pos < phaseIncr) {
      float t = pos / phaseIncr;
      return t + t - t * t - 1.0f;
    } else if (pos > 1.0f - phaseIncr) {
      float t = (pos - 1.0f) / phaseIncr;
      return t * t + t + t + 1.0f;
    }
    return 0.0f;
  }

  float get_blep(float pos) const { return get_blep(pos, currPhaseIncr_); }
};

} // namespace mforce
