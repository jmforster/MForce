#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <cmath>

namespace mforce {

// Smith (1986) memoryless bow friction curve, verbatim from STK BowTable
// (docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md). Input is
// the differential velocity dv (bow minus string); output is the injected
// new velocity dv * rc(dv), with
//   rc(dv) = clamp( (|(dv + offset) * slope| + 0.75)^-4, min, max ).
// slope is the bow-pressure knob (STK: 5 - 4*pressure, i.e. high pressure
// = low slope = wide friction pulse); offset skews stick/slip asymmetry.
// Both are pins so pressure is playable per note or mid-note.
struct BowTableSource final : Processor {
  Param slope {this, "slope",  3.0f, 0.05f, 10.0f, "pressure"};
  Param offset{this, "offset", 0.0f, -1.0f, 1.0f};
  Setting<float> minOutput{this, "minOutput", 0.01f, 0.0f, 1.0f};
  Setting<float> maxOutput{this, "maxOutput", 0.98f, 0.0f, 1.0f};

  const char* type_name() const override { return "BowTable"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  void on_prepare(const RenderContext&, int) override { cur_ = 0.0f; }

  float compute() override {
    const float dv = input.current();
    const float s = std::fabs((dv + offset.current()) * slope.current()) + 0.75f;
    float rc = std::pow(s, -4.0f);
    rc = std::clamp(rc, minOutput.value, maxOutput.value);
    return dv * rc;
  }
};

} // namespace mforce
