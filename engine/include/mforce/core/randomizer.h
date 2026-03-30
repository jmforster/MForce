#pragma once
#include <random>
#include <cmath>
#include <algorithm>

namespace mforce {

struct Randomizer {
  explicit Randomizer(uint32_t seed = 0x12345678u) : rng(seed), uni01(0.0f, 1.0f) {}

  float value() { return uni01(rng); }                 // [0,1]
  float valuePN() { return value() * 2.0f - 1.0f; }    // [-1,1]

  float range(float min, float max) {
    return min + (max - min) * value();
  }

  float range(float min, float max, float bias) {
    return range(min, max, bias, 1.0f);
  }

  float range(float min, float max, float bias, float influence) {
    float mix = std::min(value() * influence, 1.0f);
    return range(min, max) * (1.0f - mix) + bias * mix;
  }

  bool decide(float val) { return value() <= val; }    // true if random <= val

  int sign() {
    float s = value() - 0.5f;
    return (s > 0) ? 1 : (s < 0 ? -1 : 0);
  }

  int floorOrCeiling(float v) {
    float frac = v - std::floor(v);
    if (decide(frac)) return int(std::ceil(v));
    return int(std::floor(v));
  }

  std::mt19937 rng;
  std::uniform_real_distribution<float> uni01;
};

} // namespace mforce
