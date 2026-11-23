#pragma once
// BezierPullEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// BezierPullEvolution — pull each sample toward a cubic Bezier curve
// defined by four control values p0..p3 over t in [0,1]. The Bezier is
// resampled to the table length once per note, then each sample drifts
// toward its target by `rate`. Endpoint: the table becomes the Bezier shape.
// ---------------------------------------------------------------------------
struct BezierPullEvolution final : WaveEvolution {
  Setting<float> p0  {this, "p0",   -1.0f,    -2.0f, 2.0f};
  Setting<float> p1  {this, "p1",    0.5f,    -2.0f, 2.0f};
  Setting<float> p2  {this, "p2",   -0.5f,    -2.0f, 2.0f};
  Setting<float> p3  {this, "p3",    1.0f,    -2.0f, 2.0f};
  Setting<float> rate{this, "rate",  0.00005f, 0.0f, 0.1f};

  const char* type_name() const override { return "BezierPullEvolution"; }

  void on_prepare(const RenderContext&, int maxLen) override { target_.reserve(size_t(maxLen)); }
  void adjust(float /*frequency*/) override {
    needsRebuild_ = true;
  }

  void shape_excitation(std::vector<float>& values) override {
    rebuild_target(int(values.size()));
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2) return;
    if (needsRebuild_ || int(target_.size()) != len) rebuild_target(len);
    values[index] += rate.value * (target_[index] - values[index]);
  }

private:
  void rebuild_target(int len) {
    target_.resize(len);
    for (int i = 0; i < len; ++i) {
      const float t = float(i) / float(len);
      const float u = 1.0f - t;
      target_[i] = u*u*u*p0.value + 3.0f*u*u*t*p1.value + 3.0f*u*t*t*p2.value + t*t*t*p3.value;
    }
    needsRebuild_ = false;
  }

  std::vector<float> target_;
  bool needsRebuild_{true};
};

} // namespace mforce
