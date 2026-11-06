#pragma once
// HistogramEqualizeEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include <algorithm>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// HistogramEqualizeEvolution — gradually pull the wavetable toward a
// uniform amplitude distribution via histogram equalization. Each cycle,
// a histogram + CDF are rebuilt over the current table. Each sample is
// then nudged toward CDF(sample)*2-1 by `rate`. Convergence: sine → tri-ish,
// noise → roughly uniformly distributed waveform.
// ---------------------------------------------------------------------------
struct HistogramEqualizeEvolution final : WaveEvolution {
  Setting<int>   bins{this, "bins", 64,       4,    256};
  Setting<float> rate{this, "rate", 0.00005f, 0.0f, 0.1f};

  const char* type_name() const override { return "HistogramEqualizeEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "bins") bins.value = std::clamp(bins.value, 4, 256);
  }

  void on_prepare(const RenderContext&, int) override {
    cdf_.assign(size_t(std::max(2, bins.value)), 0.0f);
    counts_.assign(size_t(std::max(2, bins.value)), 0);
  }
  void adjust(float /*frequency*/) override {
    cdf_.assign(std::max(2, bins.value), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2 || bins.value < 2) return;

    // Rebuild CDF at the start of each cycle.
    if (index == 0) rebuild_cdf(values);

    // Map current sample value to a bin and look up its CDF target.
    const int b = sample_to_bin(values[index]);
    const float target = cdf_[b] * 2.0f - 1.0f;
    values[index] += rate.value * (target - values[index]);
  }

private:
  int sample_to_bin(float v) const {
    const float t = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f; // 0..1
    int b = int(t * float(bins.value));
    if (b >= bins.value) b = bins.value - 1;
    return b;
  }

  void rebuild_cdf(const std::vector<float>& values) {
    if (int(cdf_.size()) != bins.value) cdf_.assign(bins.value, 0.0f);
    counts_.assign(size_t(bins.value), 0);   // within the capacity prepare reserved
    for (float v : values) counts_[sample_to_bin(v)]++;
    const float invN = 1.0f / float(values.size());
    float running = 0.0f;
    for (int i = 0; i < bins.value; ++i) {
      running += float(counts_[i]) * invN;
      cdf_[i] = running;
    }
  }

  std::vector<float> cdf_;
  std::vector<int>   counts_;   // rebuild_cdf's histogram, sized at prepare
};

} // namespace mforce
