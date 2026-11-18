#pragma once
#include "mforce/core/dsp_value_source.h"

namespace mforce {

// Ported from C# MForce.Sound.Source.RangeSource
// Maps a modulator (var) into the range [min, max].
// Var in [0,1]: value = min + var * (max - min)
// Var in [-1,1] (unnormalized): value = min + (var+1)/2 * (max - min)
//
// In the C++ port we always use the normalized formula (var in [0,1] maps
// linearly to [min,max]).  WaveSources that output [-1,1] should use
// normalized=false so the [-1,1] -> [0,1] adjustment is applied.
struct RangeSource final : Generator {
  Param min{this, "min", 0.0f, -10000.0f, 10000.0f};
  Param max{this, "max", 1.0f, -10000.0f, 10000.0f};
  Param var{this, "var", 0.0f, -10000.0f, 10000.0f, "0-1"};
  // Off by default: var is read as [-1, 1] and remapped to [0, 1].
  Setting<bool> normalized{this, "normalized", false, false, true};

  float compute() override {
    float v = var.current();
    if (!normalized.value) {
      // Legacy: WaveSource outputs [-1,1], remap to [0,1]
      v = (v + 1.0f) * 0.5f;
    }
    return min.current() + v * (max.current() - min.current());
  }

  const char* type_name() const override { return "RangeSource"; }
};

} // namespace mforce
