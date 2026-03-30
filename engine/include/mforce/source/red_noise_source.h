#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/core/smoothness_interpolator.h"
#include <memory>

namespace mforce {

struct RedNoiseSource final : WaveSource {
  explicit RedNoiseSource(int sampleRate, uint32_t seed = 0xC0FFEEu);

  // These correspond 1:1 to your legacy parameters.
  std::shared_ptr<ValueSource> density;           // Decide(density)
  std::shared_ptr<ValueSource> smoothness;        // SmoothnessInterpolator smoothness
  std::shared_ptr<ValueSource> rampVariation;     // +/- around CurrFreq
  std::shared_ptr<ValueSource> boost;             // min abs value (0..1)
  std::shared_ptr<ValueSource> continuity;        // bias toward LastVal
  std::shared_ptr<ValueSource> zeroCrossTendency; // flip sign tendency

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  int   rampSize_{1};
  int   sampleCount_{0};
  float lastValue_{0.0f};
  float lastSign_{-1.0f};     // legacy starts non-zero to “get going”
  float nextValue_{0.0f};
  bool  zeroRamp_{false};

  Randomizer rng_;
  SmoothnessInterpolator interp_;
};

} // namespace mforce
