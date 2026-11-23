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
    layout_.mult1.resize(n); layout_.mult2.resize(n);
    layout_.ampl1.resize(n); layout_.ampl2.resize(n);
    layout_.po1.resize(n);   layout_.po2.resize(n);

    for (int i = 0; i < n; ++i) {
      float m = float(minMult.value + i);
      layout_.mult1[i] = m;

      // Even/odd amplitude weights (fundamental always 1) — legacy FullPartials.cs
      if (int(std::trunc(m)) % 2 == 0) {
        layout_.ampl1[i] = evenWeight1.value;
        layout_.ampl2[i] = evenWeight2.value;
      } else {
        layout_.ampl1[i] = (m == 1.0f) ? 1.0f : oddWeight1.value;
        layout_.ampl2[i] = (m == 1.0f) ? 1.0f : oddWeight2.value;
      }

      // Phase offsets: (mult - 1) * unitPO
      layout_.po1[i] = std::fmod((m - 1.0f) * unitPO1.value, 1.0f);
      layout_.po2[i] = std::fmod((m - 1.0f) * unitPO2.value, 1.0f);
    }

    // No multiplier evolution for FullPartials
    layout_.mult2 = layout_.mult1;
  }
};

} // namespace mforce
