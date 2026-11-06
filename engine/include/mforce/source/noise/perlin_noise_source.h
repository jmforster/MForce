#pragma once
// Perlin noise generator: coherent gradient noise at audio rate.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// PerlinNoiseSource — coherent gradient noise at audio rate.
// Produces smoothly varying noise with controllable texture.
// At low speed = organic modulation source.
// At high speed = noise with controllable smoothness.
// ---------------------------------------------------------------------------
struct PerlinNoiseSource final : Generator {
  Param speed      {this, "speed",       440.0f, 0.01f, 20000.0f};
  Param octaves    {this, "octaves",     4.0f,   1.0f,  8.0f};
  Param persistence{this, "persistence", 0.5f,   0.0f,  1.0f};
  Param lacunarity {this, "lacunarity",  2.0f,   1.0f,  4.0f};

  explicit PerlinNoiseSource(uint32_t seed = 0xAE21'0001u) : rng_(seed) {
    for (int i = 0; i < 512; ++i) perm_[i] = int(rng_.value() * 256.0f) & 255;
  }

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "PerlinNoiseSource"; }

  void on_start(int) override { pos_ = 0.0; }

  float compute() override {
    const float spd = speed.current();
    const int oct = std::clamp(int(octaves.current()), 1, 8);
    const float pers = persistence.current();
    const float lac = lacunarity.current();

    pos_ += double(spd) / double(sample_rate());

    float total = 0.0f;
    float amplitude = 1.0f;
    float maxAmp = 0.0f;
    double freq = 1.0;

    for (int i = 0; i < oct; ++i) {
      total += amplitude * noise1d(float(pos_ * freq));
      maxAmp += amplitude;
      amplitude *= pers;
      freq *= double(lac);
    }

    return (maxAmp > 0.0f) ? total / maxAmp : 0.0f;
  }

private:
  float noise1d(float x) const {
    int xi = int(std::floor(x)) & 255;
    float xf = x - std::floor(x);
    float u = fade(xf);
    float g0 = grad1d(perm_[xi], xf);
    float g1 = grad1d(perm_[xi + 1], xf - 1.0f);
    return lerp(g0, g1, u);
  }

  static float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
  }
  static float lerp(float a, float b, float t) { return a + t * (b - a); }
  static float grad1d(int hash, float x) { return (hash & 1) ? x : -x; }

  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  int perm_[512];
  double pos_{0.0};
};

} // namespace mforce
