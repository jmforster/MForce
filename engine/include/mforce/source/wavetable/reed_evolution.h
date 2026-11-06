#pragma once
// ReedEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// ReedEvolution — reed-valve resonator (clarinet / sax).
// Ring-buffer-as-tube structure with reed nonlinearity. reedStiffness
// drives a tanh shaper on the bore signal → odd/asymmetric harmonics; breath
// is injected via a zero-mean noise term that seeds and sustains oscillation.
// ---------------------------------------------------------------------------
struct ReedEvolution final : WaveEvolution {
  // The two pins are advanced here, once per evolve(), so they are Manual.
  Param reedStiffness{this, "reedStiffness", 1.5f, 0.0f, 8.0f, "exp", Pull::Manual};   // tanh drive on bore signal (0 = linear tube)
  Input breath       {this, "breath", Input::Optional, "0-1", false, Pull::Manual};
  Setting<float> tubeLoss  {this, "tubeLoss",   0.02f, 0.0f,  0.5f};   // per-sample loop loss
  Setting<float> loopFilter{this, "loopFilter", 0.5f,  0.05f, 1.0f};   // one-pole loop lowpass coefficient (0..1; higher = brighter)

  explicit ReedEvolution(uint32_t seed = 0xBEED'BEEDu) : rng_(seed) {}

  const char* type_name() const override { return "ReedEvolution"; }

  void adjust(float /*frequency*/) override {
    lpState_ = 0.0f;
  }

  void shape_excitation(std::vector<float>& values) override {
    std::fill(values.begin(), values.end(), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());
    int next = (index + 1) % len;

    float avg = (values[index] + values[next]) * 0.5f;

    // One-pole lowpass on the loop; kills the square-wave buzz that tanh alone produces.
    lpState_ = loopFilter.value * avg + (1.0f - loopFilter.value) * lpState_;
    float loopOut = lpState_ * (1.0f - tubeLoss.value);

    float drive = reedStiffness.next();
    if (drive > 0.0f) loopOut = std::tanh(drive * loopOut);

    float b = breath.next();
    float noise = rng_.valuePN();
    values[index] = loopOut + b * noise;
  }

private:
  Randomizer rng_;
  float lpState_{0.0f};
};

} // namespace mforce
