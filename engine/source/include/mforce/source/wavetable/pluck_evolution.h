#pragma once
// PluckEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.PluckEvolution
// Modified Karplus-Strong: frequency-dependent averaging window + speed.
// ---------------------------------------------------------------------------
struct PluckEvolution final : WaveEvolution {
  Setting<float> muting{this, "muting", 0.0f, 0.0f, 1.0f};

  explicit PluckEvolution(uint32_t seed = 0xDEAD'BEEFu) : rng_(seed) {}

  const char* type_name() const override { return "PluckEvolution"; }

  void adjust(float frequency) override {
    // SampleCount = Round(Max(24 - 4*log10(f)^2, 2)) + muting*20
    double logF = std::log10(double(frequency));
    sampleCount_ = float(std::round(std::max(24.0 - 4.0 * logF * logF, 2.0))) + muting.value * 20.0f;

    // Speed
    speed_ = 1.0f + muting.value * 2.0f;
    if (frequency < 674.0f) {
      speed_ = float(std::max(9.0 - std::pow(logF, 2.0), 1.0)) + muting.value * 4.0f;
    } else if (frequency > 880.0f) {
      speed_ = float(std::pow(8.67 / std::pow(logF, 2.0), 4.0)) + muting.value * 4.0f;
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    int loops = rng_.floorOrCeiling(speed_);
    int len = int(values.size());

    for (int i = 0; i < loops; ++i) {
      average(values, (index + i) % len);
    }

    if (speed_ < 1.0f) {
      speed_ *= 1.001f;  // legacy: slowly accelerate sub-1 speeds
    }
  }

private:
  void average(std::vector<float>& values, int index) {
    int sampCount = rng_.floorOrCeiling(sampleCount_);
    if (sampCount < 1) sampCount = 1;

    int len = int(values.size());
    float sum = 0.0f;
    for (int i = 0; i < sampCount; ++i) {
      sum += values[(index + i) % len];
    }
    values[index] = sum / float(sampCount);
  }

  float sampleCount_{2.0f};
  float speed_{1.0f};
  Randomizer rng_;
};

} // namespace mforce
