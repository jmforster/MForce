#pragma once
// Blue noise generator: differentiated pink noise.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/noise/pink_noise_source.h"

namespace mforce {

// ---------------------------------------------------------------------------
// BlueNoiseSource — +3dB/octave (spectral inverse of pink).
// Differentiated pink noise: output = current_pink - previous_pink.
// Spectral SHAPE is fixed by definition; only level is modulatable.
// ---------------------------------------------------------------------------
struct BlueNoiseSource final : Generator {
  Param amplitude{this, "amplitude", 1.0f, 0.0f, 10.0f};

  explicit BlueNoiseSource(uint32_t seed = 0xB100'0001u)
  : pink_(PinkNoiseSource::DEFAULT_ROWS, seed) {}

  // No unit range: the difference of two pink samples can reach +-2.
  const char* type_name() const override { return "BlueNoiseSource"; }

  void on_prepare(const RenderContext& ctx) override { pink_.prepare(ctx); }

  void on_start(int frames) override {
    pink_.start(frames);
    prev_ = 0.0f;
  }

  float compute() override {
    const float p = pink_.next();
    const float out = (p - prev_) * amplitude.current();
    prev_ = p;
    return out;
  }

private:
  PinkNoiseSource pink_;
  float prev_{0.0f};
};

} // namespace mforce
