#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.VarSource
// Varies around a center value by a percentage, driven by a modulator.
//   Absolute mode:  value = val * (1 + var * varPct)
//   Relative mode:  value = lastVal + delta * (1 + var * varPct)
struct VarSource final : Generator {
  Param val   {this, "val",    1.0f, -10000.0f, 10000.0f};
  Param var   {this, "var",    0.0f, -10000.0f, 10000.0f, "±1"};
  Param varPct{this, "varPct", 0.0f, 0.0f,      1.0f,     "0-1"};
  Setting<bool> absolute{this, "absolute", true, false, true};

  VarSource(std::shared_ptr<ValueSource> v,
            std::shared_ptr<ValueSource> vr,
            std::shared_ptr<ValueSource> vrPct) {
    val.set(std::move(v));
    var.set(std::move(vr));
    varPct.set(std::move(vrPct));
  }
  VarSource(std::shared_ptr<ValueSource> v,
            std::shared_ptr<ValueSource> vr,
            std::shared_ptr<ValueSource> vrPct,
            bool isAbsolute)
  : VarSource(std::move(v), std::move(vr), std::move(vrPct)) {
    absolute.value = isAbsolute;
  }

  float compute() override {
    float out;
    if (absolute.value) {
      out = val.current() * (1.0f + var.current() * varPct.current());
    } else {
      const float delta = val.current() - lastVal_;
      out = lastVal_ + delta * (1.0f + var.current() * varPct.current());
    }
    lastVal_ = out;
    return out;
  }

  const char* type_name() const override { return "VarSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

private:
  float lastVal_{0.0f};
};

} // namespace mforce
