#pragma once
// EKSEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/dsp/tuning_allpass.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// EKSEvolution — Extended Karplus-Strong (Jaffe & Smith 1983).
// Adds pick position, pick direction, string stiffness, decay stretch,
// and drum blend to the basic KS averaging loop.
// ---------------------------------------------------------------------------
struct EKSEvolution final : WaveEvolution {
  Setting<float> pickPosition {this, "pickPosition",  0.0f, 0.0f, 1.0f};    // 0=bridge, 0.5=middle (removes even harmonics), 1=nut
  Setting<float> pickDirection{this, "pickDirection", 0.0f, 0.0f, 0.95f};   // 0=down, higher=more up-pick filtering
  Setting<float> stiffness    {this, "stiffness",     0.0f, 0.0f, 0.1f};    // string inharmonicity (allpass dispersion)
  Setting<float> decayStretch {this, "decayStretch",  0.5f, 0.0f, 1.0f};    // 0..1, 0.5=standard KS averaging
  Setting<float> drumBlend    {this, "drumBlend",     1.0f, 0.0f, 1.0f};    // 1=string, 0.5=snare, 0=pure drum

  explicit EKSEvolution(uint32_t seed = 0xEE45'0000u) : rng_(seed) {}

  const char* type_name() const override { return "EKSEvolution"; }

  void on_prepare(const RenderContext&, int maxLen) override { filtered_.reserve(size_t(maxLen)); }
  void adjust(float frequency) override {
    // Stiffness allpass coefficient: higher frequency = more dispersion
    float coeff = 0.0f;
    if (stiffness.value > 0.0f) {
      // Approximate: coefficient grows with harmonic number
      // Use a simple model: C = stiffness * (freq/1000)^0.5
      coeff = stiffness.value * std::sqrt(frequency / 1000.0f);
      coeff = std::clamp(coeff, 0.0f, 0.9f);
    }
    stiffness_.set_coefficient(coeff);
    stiffness_.reset();
  }

  void shape_excitation(std::vector<float>& values) override {
    int len = int(values.size());
    if (len < 2) return;

    // Pick-position comb filter: H(z) = 1 - z^(-floor(beta*N + 0.5))
    // Removes harmonics at multiples of 1/beta
    if (pickPosition.value > 0.001f && pickPosition.value < 0.999f) {
      int delay = int(std::floor(pickPosition.value * float(len) + 0.5f));
      if (delay >= 1 && delay < len) {
        filtered_.assign(size_t(len), 0.0f);   // within the capacity prepare reserved
        for (int i = 0; i < len; ++i) {
          int di = i - delay;
          if (di < 0) di += len;
          filtered_[i] = values[i] - values[di];
        }
        std::copy(filtered_.begin(), filtered_.end(), values.begin());   // never a new buffer for the owner's table
      }
    }

    // Pick-direction lowpass: H(z) = (1-p) / (1 - p*z^-1)
    if (pickDirection.value > 0.001f) {
      float p = std::clamp(pickDirection.value, 0.0f, 0.95f);
      float g = 1.0f - p;
      float prev = 0.0f;
      for (int i = 0; i < len; ++i) {
        float y = g * values[i] + p * prev;
        prev = y;
        values[i] = y;
      }
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());
    if (len < 2) return;

    int next = (index + 1) % len;

    // Weighted average (decay stretch): y = (1-S)*v[n] + S*v[n+1]
    float avg = (1.0f - decayStretch.value) * values[index] + decayStretch.value * values[next];

    // Drum blend: with probability (1-drumBlend)/2, flip sign
    if (drumBlend.value < 1.0f) {
      float flipProb = (1.0f - drumBlend.value) * 0.5f;
      if (rng_.value() < flipProb) {
        avg = -avg;
      }
    }

    // String stiffness allpass: y[n] = C*x[n] + x[n-1] - C*y[n-1]
    if (stiffness_.coefficient() > 0.0f) avg = stiffness_.process(avg);

    values[index] = avg;
  }

private:
  Randomizer rng_;
  std::vector<float> filtered_;   // the pick-position comb's scratch, reserved at prepare
  dsp::TuningAllpass stiffness_;   // the string's dispersion
};

} // namespace mforce
