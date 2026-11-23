#pragma once
// TargetEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/dsp/constants.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// TargetEvolution — morphing Karplus-Strong.
// Instead of averaging toward silence (standard KS), averages toward a
// target waveform. The table continuously evolves from noisy excitation
// toward the target shape. NOT a crossfade — the table is modified in-place,
// so each cycle derives from the previous one by continuous transformation.
// The target is one period synthesised at prepare from harmonic amplitudes
// (fundamental first), peak-normalised, and resampled to each note's table
// length; holdCycles of plain averaging precede the morph.
// ---------------------------------------------------------------------------
struct TargetEvolution final : WaveEvolution {
  Array          targetPartials{this, "targetPartials", nullptr, 0.0f, -1.0f, 1.0f};   // harmonic amplitudes of the target, fundamental first
  Setting<int>   targetLength  {this, "targetLength",   1024, 16, 8192};              // samples in the synthesised period
  Setting<float> morphRate     {this, "morphRate",      0.001f, 0.0001f, 0.1f};       // how fast the morph progresses (0.0001 = slow, 0.01 = fast)
  Setting<int>   holdCycles    {this, "holdCycles",     3, 0, 100};                   // full table cycles of pure KS before the morph begins
  Setting<float> decayFactor   {this, "decayFactor",    0.998f, 0.9f, 1.0f};

  TargetEvolution() { targetPartials.values = {1.0f}; }   // a sine until told otherwise

  const char* type_name() const override { return "TargetEvolution"; }

  void on_prepare(const RenderContext&, int maxLen) override {
    synthesise_target();
    resampledTarget_.reserve(size_t(maxLen));
  }
  void adjust(float) override {
    sampleCount_ = 0;
    morphAmount_ = 0.0f;
    cycleCount_ = 0;
    tableLen_ = 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());

    // Track cycles (one full pass through the table)
    if (tableLen_ != len) {
      tableLen_ = len;
      resample_target(len);
    }

    sampleCount_++;
    if (sampleCount_ >= len) {
      sampleCount_ = 0;
      cycleCount_++;
    }

    // Standard KS averaging (2-sample)
    int next = (index + 1) % len;
    float averaged = (values[index] + values[next]) * 0.5f;

    if (cycleCount_ < holdCycles.value || target_.empty()) {
      // Pure KS phase — organic attack (and the whole note when there is no target)
      values[index] = averaged * decayFactor.value;
    } else {
      // Morph phase — converge toward target
      morphAmount_ = std::min(morphAmount_ + morphRate.value, 1.0f);
      float t = resampledTarget_[index];
      values[index] = averaged * (1.0f - morphAmount_) + t * morphAmount_;
      // Slight decay prevents energy buildup during transition
      values[index] *= (1.0f - (1.0f - decayFactor.value) * (1.0f - morphAmount_));
    }
  }

private:
  // One period from the harmonic amplitudes, peak-normalised: a sum of sines
  // in float. Empty partials or a non-positive length leave no target.
  void synthesise_target() {
    const int tLen = targetLength.value;
    const std::vector<float>& partials = targetPartials.values;
    target_.clear();
    if (partials.empty() || tLen <= 0) return;
    target_.assign(size_t(tLen), 0.0f);
    constexpr float TAU = 2.0f * dsp::kPi;
    for (int i = 0; i < tLen; ++i) {
      float pos = float(i) / float(tLen);
      for (int h = 0; h < int(partials.size()); ++h)
        target_[i] += partials[h] * std::sin(pos * TAU * float(h + 1));
    }
    float peak = 0.0f;
    for (float v : target_) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0f) for (float& v : target_) v /= peak;
  }
  // The target at the note's table length, linearly interpolated, within the
  // capacity prepare reserved.
  void resample_target(int len) {
    if (target_.empty()) return;
    resampledTarget_.resize(len);
    for (int i = 0; i < len; ++i) {
      float pos = float(i) / float(len) * float(target_.size());
      int idx0 = int(pos) % int(target_.size());
      int idx1 = (idx0 + 1) % int(target_.size());
      float frac = pos - std::floor(pos);
      resampledTarget_[i] = target_[idx0] + (target_[idx1] - target_[idx0]) * frac;
    }
  }

  std::vector<float> target_;            // the synthesised period
  std::vector<float> resampledTarget_;   // target_ at the note's table length

  float morphAmount_{0.0f};
  int sampleCount_{0};
  int cycleCount_{0};
  int tableLen_{0};
};

} // namespace mforce
