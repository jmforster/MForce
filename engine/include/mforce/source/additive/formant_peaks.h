#pragma once
// FormantPeaks: a formant of resonant peaks, the peaks as rows.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// FormantPeaks — resonant peaks as rows: four parallel arrays grouped as
// "peaks" (frequency, gain, width, power), one row per peak, the table the
// editor keeps at one length and the load refuses ragged. The gain at a
// frequency is the largest gain of the peaks whose band contains it; each
// peak is a power ramp from its band's lower edge up to its centre and down
// to the upper edge, and 0 outside. The band edges are derived from the rows
// at prepare and whenever an array changes, so the per-sample read is the
// cached edges and the row's values — no pin.
// ---------------------------------------------------------------------------
struct FormantPeaks final : Component, IFormant {
  Array frequency{this, "frequency", "peaks", 1000.0f, 1.0f,  20000.0f};
  Array gain     {this, "gain",      "peaks", 1.0f,    0.0f,  10.0f};
  Array width    {this, "width",     "peaks", 500.0f,  1.0f,  10000.0f};
  Array power    {this, "power",     "peaks", 2.0f,    0.01f, 10.0f};

  const char* type_name() const override { return "FormantPeaks"; }

  void prepare(const RenderContext&) override { cache_edges(); }
  void start(int) override {}
  void next() override {}
  void on_array_changed(std::string_view) override { cache_edges(); }

  // The rows the edges were cached for: the shortest of the four arrays.
  int peak_count() const { return int(lo_.size()); }

  bool contains(float freq) const override {
    for (size_t i = 0, n = lo_.size(); i < n; ++i)
      if (freq > lo_[i] && freq < hi_[i]) return true;
    return false;
  }

  float get_gain(float freq) const override {
    float g = 0.0f;
    for (size_t i = 0, n = lo_.size(); i < n; ++i)
      if (freq > lo_[i] && freq < hi_[i])
        g = std::max(g, peak_gain(i, freq));
    return g;
  }

private:
  // One peak's gain at a frequency: the ramp up to the centre, the ramp down
  // from it, each raised to the row's power and scaled by its gain.
  float peak_gain(size_t i, float freq) const {
    const float lo = lo_[i], hi = hi_[i];
    if (freq <= lo || freq >= hi) return 0.0f;
    const float f = frequency.values[i];
    const float p = power.values[i];
    const float gn = gain.values[i];
    if (freq < f)
      return std::pow((freq - lo) / (f - lo), p) * gn;
    return std::pow((hi - freq) / (hi - f), p) * gn;
  }
  void cache_edges() {
    const size_t n = std::min({frequency.values.size(), gain.values.size(),
                               width.values.size(), power.values.size()});
    lo_.resize(n);
    hi_.resize(n);
    for (size_t i = 0; i < n; ++i) {
      const float f = frequency.values[i];
      const float w = width.values[i];
      lo_[i] = f - w * 0.5f;
      hi_[i] = f + w * 0.5f;
    }
  }
  std::vector<float> lo_, hi_;
};

} // namespace mforce
