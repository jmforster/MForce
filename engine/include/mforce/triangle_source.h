#pragma once
#include "dsp_wave_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.TriangleSource
// Bias controls the peak position (0..1); 0.5 = symmetric triangle.
struct TriangleSource final : WaveSource {
  explicit TriangleSource(int sampleRate)
  : WaveSource(sampleRate)
  , bias_(std::make_shared<ConstantSource>(0.5f)) {}

  void set_bias(std::shared_ptr<ValueSource> b) { bias_ = std::move(b); }

  void prepare(int frames) override {
    WaveSource::prepare(frames);
    bias_->prepare(frames);
  }

protected:
  float compute_wave_value() override {
    bias_->next();
    float b = bias_->current();

    if (currPos_ <= b) {
      return -1.0f + currPos_ * (4.0f / b) / 2.0f;
    } else {
      return 1.0f - (currPos_ - b) * (4.0f / (1.0f - b)) / 2.0f;
    }
  }

private:
  std::shared_ptr<ValueSource> bias_;
};

} // namespace mforce
