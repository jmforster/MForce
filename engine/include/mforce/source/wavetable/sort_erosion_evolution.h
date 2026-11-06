#pragma once
// SortErosionEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include <algorithm>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// SortErosionEvolution — distributed bubble-sort toward a sorted (saw) shape.
// Each evolve(N) does `swapsPerSample` adjacent compare-swap steps starting
// at a rotating pointer. Eventually the table converges to a monotonic ramp.
// `descending` flips the sort direction.
// ---------------------------------------------------------------------------
struct SortErosionEvolution final : WaveEvolution {
  Setting<int>  swapsPerSample{this, "swapsPerSample", 4, 1, 32};
  Setting<bool> descending    {this, "descending", false, false, true};

  const char* type_name() const override { return "SortErosionEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "swapsPerSample") swapsPerSample.value = std::max(1, swapsPerSample.value);
  }

  void adjust(float /*frequency*/) override {
    pos_ = 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2) return;
    for (int s = 0; s < swapsPerSample.value; ++s) {
      const int a = pos_;
      const int b = (pos_ + 1) % len;
      const bool out_of_order =
        descending.value ? (values[a] < values[b]) : (values[a] > values[b]);
      if (out_of_order) std::swap(values[a], values[b]);
      pos_ = (pos_ + 1) % len;
    }
    (void)index;
  }

private:
  int pos_{0};
};

} // namespace mforce
