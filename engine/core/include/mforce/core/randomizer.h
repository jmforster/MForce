#pragma once
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// splitmix64 finalizer — bit-mixing step used to derive independent rng
// stream seeds. Full-avalanche: every input bit affects every output bit.
// ---------------------------------------------------------------------------
inline uint64_t splitmix64(uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

// Seed for stream (baseSeed, index, layerId) — e.g. one rng stream per
// (partial, noise-layer) pair in Partials, so no consumer's draws depend on
// how many draws other consumers made (evaluation-order independence).
// Two chained splitmix64 rounds; the 64-bit result is folded to the 32 bits
// mt19937 seeding consumes.
inline uint32_t stream_seed(uint32_t baseSeed, uint32_t index, uint32_t layerId) {
  uint64_t z = splitmix64((uint64_t(baseSeed) << 32) | uint64_t(layerId));
  z = splitmix64(z ^ uint64_t(index));
  return uint32_t(z >> 32) ^ uint32_t(z);
}

struct Randomizer {
  explicit Randomizer(uint32_t seed = 0x12345678u)
    : seed_(seed), rng(seed), uni01(0.0f, 1.0f) {}

  // Re-anchor the draw stream to the construction seed (per-note
  // determinism, onsets-v2 addendum 2026-09-20): an in-line note's Setup
  // re-anchors so each note's realization is pinned the way a fresh
  // voice's always was. Distribution reset for strict determinism.
  void reanchor() { rng.seed(seed_); uni01.reset(); }
  uint32_t seed_;

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

  // Random integer in [min, max] inclusive
  int int_range(int min, int max) {
    return min + int(value() * float(max - min + 1));
  }

  // Random direction: -1 or +1 (with probabilities for each, rest = 0)
  int direction(float upProb, float downProb) {
    float v = value();
    if (v < upProb) return 1;
    if (v < upProb + downProb) return -1;
    return 0;
  }

  // Select from array with equal probability
  int select_int(const std::vector<int>& values) {
    return values[int_range(0, int(values.size()) - 1)];
  }

  // Select from array with weighted probabilities
  int select_int(const std::vector<int>& values, const std::vector<float>& probs) {
    float v = value();
    float cumul = 0;
    for (int i = 0; i < int(values.size()); ++i) {
      cumul += probs[i];
      if (v <= cumul) return values[i];
    }
    return values.back();
  }

  std::mt19937 rng;
  std::uniform_real_distribution<float> uni01;
};

} // namespace mforce
