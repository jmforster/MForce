#pragma once
// ExplicitPartials: a Partials.
#include "mforce/source/additive/partials.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mforce {

// ---------------------------------------------------------------------------
// ExplicitPartials — user-specified multiplier and amplitude arrays.
// Ported from legacy ExplicitPartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct ExplicitPartials final : Partials {
  Setting<int>   maxPartials{this, "maxPartials", 16, 1, 200};
  Setting<bool>  evolve     {this, "evolve",      true, false, true};
  Setting<float> unitPO1    {this, "unitPO1",     0.0f, 0.0f, 1.0f};
  Setting<float> unitPO2    {this, "unitPO2",     0.0f, 0.0f, 1.0f};

  // Four parallel arrays grouped as "partials" — the UI table keeps them equal
  // length. Column order pairs start/end per partial: Mult1, Ampl1, Mult2, Ampl2.
  // When evolve=false, _2 columns are kept mirrored from _1 (UI disables them).
  // These are the user's copies; init_arrays() derives the working arrays (and
  // the phase offsets) from them, and again after an expansion.
  Array mult1{this, "mult1", "partials", 1.0f, 0.0f, 200.0f};
  Array ampl1{this, "ampl1", "partials", 1.0f, 0.0f, 10.0f};
  Array mult2{this, "mult2", "partials", 1.0f, 0.0f, 200.0f};
  Array ampl2{this, "ampl2", "partials", 1.0f, 0.0f, 10.0f};

  ExplicitPartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays_defaults(); }

  const char* type_name() const override { return "ExplicitPartials"; }

  void on_setting_changed(std::string_view name) override {
    Partials::on_setting_changed(name);
    if (name == "maxPartials") {
      maxPartials.value = std::max(1, maxPartials.value);
      init_arrays_defaults();
      snapshot_originals();
    } else if (name == "evolve" && !evolve.value) {
      // When turning evolve off, mirror _1 -> _2 so rendering stops drifting.
      mult2.values = mult1.values;
      ampl2.values = ampl1.values;
      init_arrays();
      snapshot_originals();
    }
  }

  void on_array_changed(std::string_view name) override {
    if (!evolve.value) {
      if (name == "mult1") mult2.values = mult1.values;
      if (name == "ampl1") ampl2.values = ampl1.values;
    }
    maxPartials.value = int(mult1.values.size());
    init_arrays();
    snapshot_originals();
  }

  // Falls back to the working arrays (init_arrays_defaults populates mult1_
  // etc., not the user's copies) so the UI sees current effective values.
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "mult1") return mult1.values.empty() ? mult1_ : mult1.values;
    if (name == "mult2") return mult2.values.empty() ? mult2_ : mult2.values;
    if (name == "ampl1") return ampl1.values.empty() ? ampl1_ : ampl1.values;
    if (name == "ampl2") return ampl2.values.empty() ? ampl2_ : ampl2.values;
    return {};
  }

protected:
  void init_arrays() override {
    if (mult1.values.empty()) {
      init_arrays_defaults();
      return;
    }
    mult1_ = mult1.values;
    mult2_ = mult2.values;
    ampl1_ = ampl1.values;
    ampl2_ = ampl2.values;

    int n = int(mult1_.size());
    po1_.resize(n);
    po2_.resize(n);
    for (int i = 0; i < n; ++i) {
      po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1.value, 1.0f);
      po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2.value, 1.0f);
    }
  }

private:
  void init_arrays_defaults() {
    int n = maxPartials.value;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.resize(n); ampl2_.resize(n);
    po1_.resize(n);   po2_.resize(n);
    for (int i = 0; i < n; ++i) {
      float m = float(i + 1);
      mult1_[i] = m; mult2_[i] = m;
      ampl1_[i] = 1.0f; ampl2_[i] = 1.0f;
      po1_[i] = 0.0f; po2_[i] = 0.0f;
    }
  }
};

} // namespace mforce
