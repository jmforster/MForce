#pragma once
#include "mforce/core/dsp/constants.h"
#include <cmath>

namespace mforce::dsp {

// The leaky integrator y += a·(x − y): a one-pole lowpass whose coefficient
// the caller hands over per call, because every owner reads it from a live
// setting (a string's brightness, a resonator's damping). a = 1 passes the
// input through; a near 0 holds the last value.
struct OnePole {
  void reset() { y_ = 0.0f; }
  float process(float x, float a) {
    y_ += a * (x - y_);
    return y_;
  }
  float value() const { return y_; }

private:
  float y_{0.0f};
};

// The per-sample multiplier that decays a value to e⁻¹ of itself over
// `samples` samples: the pole a one-pole settles with.
inline float pole_for_samples(float samples) {
  return std::exp(-1.0f / samples);
}

// The same pole for a time constant in seconds at the host's rate.
inline float pole_for_seconds(float seconds, float sampleRate) {
  return std::exp(-1.0f / (seconds * sampleRate));
}

// The pole of a first-order section whose corner sits at `hz`.
inline float pole_for_cutoff_hz(float hz, float sampleRate) {
  return std::exp(-2.0f * kPi * hz / sampleRate);
}

} // namespace mforce::dsp
