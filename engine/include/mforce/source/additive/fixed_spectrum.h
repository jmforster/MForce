#pragma once
// FixedSpectrum: a fixed table of partial amplitudes.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# FixedSpectrum — full gain array indexed by integer frequency.
// ---------------------------------------------------------------------------
struct FixedSpectrum final : Component, IFormant {
  Array gains{this, "gains", nullptr, 0.0f, 0.0f, 1.0f};

  explicit FixedSpectrum(std::vector<float> values) { gains.values = std::move(values); }

  const char* type_name() const override { return "FixedSpectrum"; }

  void prepare(const RenderContext& /*ctx*/) override {}
  void start(int /*frames*/) override {}
  void next() override {}

  bool contains(float /*freq*/) const override { return true; }

  float get_gain(float freq) const override {
    int idx = int(freq);
    if (idx < 0 || idx + 1 >= int(gains.values.size())) return 0.0f;
    float frac = freq - float(idx);
    return gains.values[idx] + (gains.values[idx + 1] - gains.values[idx]) * frac;
  }
};

} // namespace mforce
