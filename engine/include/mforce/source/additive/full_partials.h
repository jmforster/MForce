#pragma once
// FullPartials: a Partials.
#include "mforce/source/additive/partials.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mforce {

// ---------------------------------------------------------------------------
// FullPartials — integer harmonics 1..N with even/odd weight control.
// Ported from legacy FullPartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct FullPartials final : Partials {
  // Every setting here shapes the partial arrays: structural, rebuilt at prepare.
  Setting<int>   maxPartials{this, "maxPartials", 30, 1, 200};
  Setting<int>   minMult    {this, "minMult",     1,  1, 100};
  Setting<float> evenWeight1{this, "evenWeight1", 1.0f, 0.0f, 10.0f, SettingRole::Structural};
  Setting<float> evenWeight2{this, "evenWeight2", 1.0f, 0.0f, 10.0f, SettingRole::Structural};
  Setting<float> oddWeight1 {this, "oddWeight1",  1.0f, 0.0f, 10.0f, SettingRole::Structural};
  Setting<float> oddWeight2 {this, "oddWeight2",  1.0f, 0.0f, 10.0f, SettingRole::Structural};
  Setting<float> unitPO1    {this, "unitPO1",     0.0f, 0.0f, 1.0f,  SettingRole::Structural};
  Setting<float> unitPO2    {this, "unitPO2",     0.0f, 0.0f, 1.0f,  SettingRole::Structural};

  FullPartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays(); }

  const char* type_name() const override { return "FullPartials"; }

  // A structural setting reshapes the arrays, rebuilt at the next prepare.
  void on_setting_changed(std::string_view name) override {
    Partials::on_setting_changed(name);
    if (name == "maxPartials") maxPartials.value = std::max(1, maxPartials.value);
    if (name == "minMult")     minMult.value = std::max(1, minMult.value);
    if (structural_setting(name)) arrayUpdateReq_ = true;
  }

protected:
  void init_arrays() override {
    int n = maxPartials.value;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.resize(n); ampl2_.resize(n);
    po1_.resize(n);   po2_.resize(n);

    for (int i = 0; i < n; ++i) {
      float m = float(minMult.value + i);
      mult1_[i] = m;

      // Even/odd amplitude weights (fundamental always 1) — legacy FullPartials.cs
      if (int(std::trunc(m)) % 2 == 0) {
        ampl1_[i] = evenWeight1.value;
        ampl2_[i] = evenWeight2.value;
      } else {
        ampl1_[i] = (m == 1.0f) ? 1.0f : oddWeight1.value;
        ampl2_[i] = (m == 1.0f) ? 1.0f : oddWeight2.value;
      }

      // Phase offsets: (mult - 1) * unitPO
      po1_[i] = std::fmod((m - 1.0f) * unitPO1.value, 1.0f);
      po2_[i] = std::fmod((m - 1.0f) * unitPO2.value, 1.0f);
    }

    // No multiplier evolution for FullPartials
    mult2_ = mult1_;
  }
};

} // namespace mforce
