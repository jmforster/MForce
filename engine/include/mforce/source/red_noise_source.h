#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/core/smoothness_interpolator.h"

namespace mforce {

struct RedNoiseSource final : WaveSource {
  // Declared after WaveSource's three, in the order the old wave() advanced them.
  Param density          {this, "density",           0.5f, 0.0f, 1.0f, "0-1"};
  Param smoothness       {this, "smoothness",        0.5f, 0.0f, 1.0f, "0-1"};
  Param rampVariation    {this, "rampVariation",     0.0f, 0.0f, 1.0f, "0-1"};
  Param boost            {this, "boost",             0.0f, 0.0f, 1.0f, "0-1"};
  Param continuity       {this, "continuity",        0.0f, 0.0f, 1.0f, "0-1"};
  Param zeroCrossTendency{this, "zeroCrossTendency", 0.0f, 0.0f, 1.0f, "0-1"};

  explicit RedNoiseSource(int sampleRate, uint32_t seed = 0xC0FFEEu);

  const char* type_name() const override { return "RedNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }

protected:
  float wave() override;

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
