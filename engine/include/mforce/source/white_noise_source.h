#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.WhiteNoiseSource
// Simple per-sample random noise (not a WaveSource — no phase accumulator needed).
//
// The legacy C# node carried four connectable inputs — Amplitude, Density,
// Boost, Continuity — used every sample:
//   if (Rand.Decide(Density)) {
//     Value = Rand.Range(Boost, 1) * Rand.Sign();
//     if (Continuity != 0) Value = Rand.Range(LastVal, Value, min(Continuity, 0.999));
//   } else Value = 0;
//   Value *= Amplitude;
// The C++ port originally shipped `amplitude` only (backlog 21). This restores
// density/boost/continuity and adds `zeroCrossTendency` (new — legacy WhiteNoise
// had none; modelled on RedNoiseSource's sign logic), all as ValueSource pins.
//
// BYTE-IDENTICAL DEFAULTS: at density=1, boost=0, continuity=0, zeroCrossTendency=0
// the node reproduces the historical single-draw output `valuePN() * amplitude`
// EXACTLY — same rng_ draw, same sequence — so every existing patch (none set
// these keys) renders bit-for-bit unchanged. The shaping logic only engages when
// a param leaves its inert default, which changes the draw structure by design.
struct WhiteNoiseSource final : Generator {
  Param amplitude        {this, "amplitude",         1.0f, 0.0f, 10.0f, "0-1"};
  Param density          {this, "density",           1.0f, 0.0f, 1.0f,  "0-1"};
  Param boost            {this, "boost",             0.0f, 0.0f, 1.0f,  "0-1"};
  Param continuity       {this, "continuity",        0.0f, 0.0f, 1.0f,  "0-1"};
  Param zeroCrossTendency{this, "zeroCrossTendency", 0.0f, 0.0f, 1.0f,  "0-1"};

  explicit WhiteNoiseSource(uint32_t seed = 0x12345678u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "WhiteNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  float compute() override {
    const float amp  = amplitude.current();
    const float dens = density.current();
    const float bst  = boost.current();
    const float cont = continuity.current();
    const float zct  = zeroCrossTendency.current();

    // Fast path — reproduce the historical output byte-for-byte when nothing
    // is shaping the noise. One rng_ draw, exactly as before.
    if (dens >= 1.0f && bst <= 0.0f && cont <= 0.0f && zct <= 0.0f) {
      const float out = rng_.valuePN() * amp;
      lastVal_ = out;
      return out;
    }

    // Legacy-shaped path.
    float v;
    if (rng_.decide(dens)) {
      // Sign: random, or flipped from the last emitted value when
      // zeroCrossTendency draws (RedNoiseSource's rule; not in legacy WhiteNoise).
      if (rng_.decide(zct)) {
        lastSign_ = -lastSign_;
      } else {
        lastSign_ = float(rng_.sign());
        if (lastSign_ == 0.0f) lastSign_ = 1.0f;
      }
      // Magnitude in [boost, 1], signed. Boost excludes values near zero.
      const float b = std::clamp(bst, 0.0f, 1.0f);
      v = rng_.range(b, 1.0f) * lastSign_;
      // Continuity biases toward the previous output (min(cont, 0.999)).
      if (cont != 0.0f) {
        const float influence = std::min(cont, 0.999f);
        v = rng_.range(lastVal_, v, lastVal_, influence);
      }
    } else {
      v = 0.0f;
    }

    const float out = v * amp;
    lastVal_ = out;
    return out;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float lastVal_{0.0f};
  float lastSign_{-1.0f};   // legacy starts non-zero to "get going"
};

} // namespace mforce
