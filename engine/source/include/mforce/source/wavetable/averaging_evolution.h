#pragma once
// AveragingEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.AveragingEvolution
// Configurable averaging with decay factor, leading/trailing direction.
// ---------------------------------------------------------------------------
struct AveragingEvolution final : WaveEvolution {
  Setting<float> sampleCount{this, "sampleCount", 4.0f,   1.0f, 48.0f};
  Setting<float> speed      {this, "speed",       1.0f,   0.1f, 20.0f};
  Setting<float> decayFactor{this, "decayFactor", 0.999f, 0.9f, 1.0f};
  Setting<bool>  leading    {this, "leading",     false,  false, true};
  Setting<bool>  autoAdjust {this, "autoAdjust",  false,  false, true};

  explicit AveragingEvolution(uint32_t seed = 0xCAFE'BABEu) : rng_(seed) {}

  const char* type_name() const override { return "AveragingEvolution"; }

  void adjust(float frequency) override {
    sampleCount_ = sampleCount.value;
    decayFactor_ = decayFactor.value;

    if (autoAdjust.value) {
      if (frequency < 110.0f) {
        decayFactor_ = 1.0f - (1.0f - decayFactor.value) *
                       (1.0f + std::max(110.0f - frequency, 0.0f) / 10.0f);
      }
      if (frequency > 220.0f) {
        decayFactor_ = 1.0f - (1.0f - decayFactor.value) *
                       std::max(1.0f - (frequency - 220.0f) / 880.0f, 0.0f);
      }
      if (frequency > 440.0f) {
        sampleCount_ = float(sampleCount.value -
          ((sampleCount.value - 1.0f) - (sampleCount.value - 1.0f) *
           std::exp(-0.0008 * double(frequency - 440.0f))));
      }
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    int loops = rng_.floorOrCeiling(speed.value);
    for (int i = 0; i < loops; ++i) {
      average(values, index);
    }
  }

private:
  void average(std::vector<float>& values, int index) {
    int sampCount = rng_.floorOrCeiling(sampleCount_);
    if (sampCount < 1) return;

    int len = int(values.size());
    float sum = 0.0f;
    for (int i = 0; i < sampCount; ++i) {
      if (leading.value) {
        sum += values[(index + i) % len];
      } else {
        int idx = index - i;
        if (idx < 0) idx += len;
        sum += values[idx];
      }
    }
    values[index] = (sum / float(sampCount)) * decayFactor_;
  }

  float sampleCount_{2.0f};
  float decayFactor_{1.0f};
  Randomizer rng_;
};

} // namespace mforce
