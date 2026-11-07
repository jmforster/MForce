#pragma once
// SequencePartials: a Partials.
#include "mforce/source/additive/partials.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mforce {

// ---------------------------------------------------------------------------
// SequencePartials — linear multiplier sequences with evolving spacing.
// Ported from legacy SequencePartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct SequencePartials final : Partials {
  // Every setting here shapes the partial arrays: structural, rebuilt at prepare.
  Setting<int>   maxPartials{this, "maxPartials", 30, 1, 200};
  Setting<float> minMult1   {this, "minMult1",    1.0f, 0.01f, 100.0f, SettingRole::Structural};
  Setting<float> minMult2   {this, "minMult2",    1.0f, 0.01f, 100.0f, SettingRole::Structural};
  Setting<float> incr1      {this, "incr1",       1.0f, 0.01f, 100.0f, SettingRole::Structural};
  Setting<float> incr2      {this, "incr2",       1.0f, 0.01f, 100.0f, SettingRole::Structural};
  Setting<float> unitPO1    {this, "unitPO1",     0.0f, 0.0f,  1.0f,   SettingRole::Structural};
  Setting<float> unitPO2    {this, "unitPO2",     0.0f, 0.0f,  1.0f,   SettingRole::Structural};

  SequencePartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays(); }

  const char* type_name() const override { return "SequencePartials"; }

  // A structural setting reshapes the arrays, rebuilt at the next prepare.
  void on_setting_changed(std::string_view name) override {
    Partials::on_setting_changed(name);
    if (name == "maxPartials") maxPartials.value = std::max(1, maxPartials.value);
    if (structural_setting(name)) arrayUpdateReq_ = true;
  }

protected:
  void init_arrays() override {
    int n = maxPartials.value;
    layout_.mult1.resize(n); layout_.mult2.resize(n);
    layout_.ampl1.assign(n, 1.0f); layout_.ampl2.assign(n, 1.0f);
    layout_.po1.resize(n); layout_.po2.resize(n);

    for (int i = 0; i < n; ++i) {
      layout_.mult1[i] = minMult1.value + float(i) * incr1.value;
      layout_.mult2[i] = minMult2.value + float(i) * incr2.value;
      layout_.po1[i] = std::fmod((layout_.mult1[i] - 1.0f) * unitPO1.value, 1.0f);
      layout_.po2[i] = std::fmod((layout_.mult2[i] - 1.0f) * unitPO2.value, 1.0f);
    }
  }
};

} // namespace mforce
