#pragma once
#include "dsp_value_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.RangeSource
// Maps a modulator (var) into the range [min, max].
// Var in [0,1]: value = min + var * (max - min)
// Var in [-1,1] (unnormalized): value = min + (var+1)/2 * (max - min)
//
// In the C++ port we always use the normalized formula (var in [0,1] maps
// linearly to [min,max]).  WaveSources that output [-1,1] should use
// normalized=false so the [-1,1] -> [0,1] adjustment is applied.
struct RangeSource final : ValueSource {
  RangeSource(std::shared_ptr<ValueSource> min,
              std::shared_ptr<ValueSource> max,
              std::shared_ptr<ValueSource> var,
              bool varNormalized = true)
  : min_(std::move(min)), max_(std::move(max)),
    var_(std::move(var)), varNormalized_(varNormalized) {}

  void prepare(int frames) override {
    min_->prepare(frames);
    max_->prepare(frames);
    var_->prepare(frames);
  }

  float next() override {
    min_->next();
    max_->next();
    var_->next();

    float v = var_->current();
    if (!varNormalized_) {
      // Legacy: WaveSource outputs [-1,1], remap to [0,1]
      v = (v + 1.0f) * 0.5f;
    }

    cur_ = min_->current() + v * (max_->current() - min_->current());
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> min_;
  std::shared_ptr<ValueSource> max_;
  std::shared_ptr<ValueSource> var_;
  bool varNormalized_{true};
  float cur_{0.0f};
};

} // namespace mforce
