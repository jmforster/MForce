#pragma once
// ReactionDiffusionEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// ReactionDiffusionEvolution — Gray-Scott reaction-diffusion 1D.
// Two coupled chemicals u, v diffuse along the table and react. Tuning
// (feed, kill, Du, Dv) selects regimes: spots, stripes, mitosis, U-skate,
// chaos. v is read out as the audio sample (scaled to -1..1).
//
// Implementation: a snapshot of (u, v) is taken at each cycle boundary
// (index==0). Within a cycle, each evolve(N) reads the snapshot's neighbours
// and writes the new (u[N], v[N]); the snapshot is refreshed next cycle.
// This decouples per-sample updates from the diffusion stencil so the
// dynamics behave like proper synchronous RD rather than a propagating wave.
// ---------------------------------------------------------------------------
struct ReactionDiffusionEvolution final : WaveEvolution {
  Setting<float> feed    {this, "feed",     0.0367f, 0.0f,  0.1f};   // canonical "mitosis": 0.0367 / 0.0649
  Setting<float> kill    {this, "kill",     0.0649f, 0.0f,  0.1f};
  Setting<float> diffU   {this, "diffU",    0.16f,   0.0f,  1.0f};
  Setting<float> diffV   {this, "diffV",    0.08f,   0.0f,  1.0f};
  Setting<float> dt      {this, "dt",       1.0f,    0.05f, 2.0f};   // integration step
  Setting<int>   subSteps{this, "subSteps", 1,       1,     8};      // RD iterations per audio sample (>=1)

  const char* type_name() const override { return "ReactionDiffusionEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "subSteps") subSteps.value = std::max(1, subSteps.value);
  }

  void on_prepare(const RenderContext&, int maxLen) override {
    for (auto* v : {&u_, &v_, &uPrev_, &vPrev_}) v->reserve(size_t(maxLen));
  }
  void adjust(float /*frequency*/) override {}

  void shape_excitation(std::vector<float>& values) override {
    const int len = int(values.size());
    u_.assign(len, 1.0f);
    v_.assign(len, 0.0f);
    uPrev_.assign(len, 1.0f);
    vPrev_.assign(len, 0.0f);
    // Seed v with a small perturbation derived from the initial excitation.
    // Anything non-zero in v will get the reaction going.
    for (int i = 0; i < len; ++i) {
      v_[i]     = 0.25f * std::abs(values[i]);
      vPrev_[i] = v_[i];
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 3) { values[index] = 0.0f; return; }

    // Refresh snapshot at the start of each cycle.
    if (index == 0) {
      uPrev_ = u_;
      vPrev_ = v_;
    }

    const int im = (index - 1 + len) % len;
    const int ip = (index + 1) % len;

    float u = u_[index];
    float v = v_[index];
    for (int s = 0; s < subSteps.value; ++s) {
      const float lapU = uPrev_[im] - 2.0f * u + uPrev_[ip];
      const float lapV = vPrev_[im] - 2.0f * v + vPrev_[ip];
      const float uvv  = u * v * v;
      u += dt.value * (diffU.value * lapU - uvv + feed.value * (1.0f - u));
      v += dt.value * (diffV.value * lapV + uvv - (feed.value + kill.value) * v);
    }
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    u_[index] = u;
    v_[index] = v;

    // Centre v around zero so the wavetable swings ±. v sits in 0..1 but
    // tends to fill a sub-range, so use 2v-1 as a starting point and let
    // any drift produce DC the source can high-pass downstream.
    values[index] = 2.0f * v - 1.0f;
  }

private:
  std::vector<float> u_;
  std::vector<float> v_;
  std::vector<float> uPrev_;
  std::vector<float> vPrev_;
};

} // namespace mforce
