#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.TriangleSource
// Bias controls the peak position (0..1); 0.5 = symmetric triangle.
struct TriangleSource final : WaveSource {
  explicit TriangleSource(int sampleRate)
  : WaveSource(sampleRate)
  , bias_(std::make_shared<ConstantSource>(0.5f)) {}

  void set_bias(std::shared_ptr<ValueSource> b) { bias_ = std::move(b); }

  const char* type_name() const override { return "TriangleSource"; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 440.0f, 0.01f, 20000.0f},
      {"amplitude", 1.0f,   0.0f,  10.0f},
      {"phase",     0.0f,  -1.0f,  1.0f},
      {"bias",      0.5f,   0.0f,  1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "bias") { set_bias(std::move(src)); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "bias") return bias_;
    return WaveSource::get_param(name);
  }

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
