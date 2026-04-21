#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <cstdint>

namespace mforce {

// Voss-McCartney pink noise generator.
// Ported from C# MForce.Math.PinkNoiseSource.
//
// Maintains N rows of random values. Each step, the row whose index
// matches the number of trailing zeros in a counter gets replaced.
// Row 0 updates every 2 samples, row 1 every 4, row 2 every 8, etc.,
// producing a 1/f spectral rolloff. An extra white noise sample is
// added each step for high-frequency energy.
struct PinkNoiseSource final : ValueSource {
  static constexpr int DEFAULT_ROWS   = 20;
  static constexpr int RANDOM_BITS    = 24;
  static constexpr int RANDOM_SHIFT   = 32 - RANDOM_BITS;

  explicit PinkNoiseSource(int numRows = DEFAULT_ROWS, uint32_t seed = 0xF10C'0001u)
  : rng_(seed), rows_(numRows, 0) {
    int pMax = (numRows + 1) * (1 << (RANDOM_BITS - 1));
    indexMask_ = (1 << numRows) - 1;
    scaleFactor_ = 1.0 / double(pMax);
  }

  const char* type_name() const override { return "PinkNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void prepare(const RenderContext& ctx, int frames) override {
    // Reset state for a fresh render
    index_ = 0;
    runningSum_ = 0;
    for (auto& r : rows_) r = 0;
  }

  float next() override {
    index_ = (index_ + 1) & indexMask_;

    if (index_ != 0) {
      // Count trailing zeros to pick which row to replace
      int numZeros = 0;
      int n = index_;
      while ((n & 1) == 0) {
        n >>= 1;
        numZeros++;
      }

      // Replace that row's value in the running sum
      runningSum_ -= rows_[numZeros];
      int newRandom = int(rng_.rng()) >> RANDOM_SHIFT;
      runningSum_ += newRandom;
      rows_[numZeros] = newRandom;
    }

    // Add extra white noise sample
    int extra = int(rng_.rng()) >> RANDOM_SHIFT;
    int sum = runningSum_ + extra;

    cur_ = float(scaleFactor_ * double(sum));
    return cur_;
  }

  float current() const override { return cur_; }

private:
  Randomizer rng_;
  std::vector<int> rows_;
  int index_{0};
  int indexMask_{0};
  int runningSum_{0};
  double scaleFactor_{1.0};
  float cur_{0.0f};
};

} // namespace mforce
