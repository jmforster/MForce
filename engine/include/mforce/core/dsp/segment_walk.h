#pragma once
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce::dsp {

// A smoothed random walk with meta-randomised segments: each segment runs
// from `cur` to `target` over `len` samples on a smoothstep, and at its end
// the next target and length are drawn from the walk's own stream. evolve > 0
// makes each new segment re-roll its own length and excursion, so the
// movement is non-stationary ("the character of the movement itself changes
// over time"). The primitive under the partials' motion, shimmer and trade
// layers; it knows nothing of partials.
struct SegmentWalk {
  float cur{0.0f}, target{0.0f};
  int pos{0}, len{1};

  static int seg_len(int baseLen, float evolve, Randomizer& rng) {
    float f = (evolve <= 0.0f) ? 1.0f
                               : std::pow(1.0f + 3.0f * evolve, rng.valuePN());
    int len = int(float(baseLen) * f);
    return len < 1 ? 1 : len;
  }

  void init(int baseLen, float evolve, Randomizer& rng) {
    cur = rng.valuePN();
    target = rng.valuePN();
    len = seg_len(baseLen, evolve, rng);
    pos = int(rng.range(0.0f, float(len)));  // stagger segment phase
  }

  float advance(int baseLen, float evolve, Randomizer& rng) {
    if (pos >= len) {
      cur = target;
      float amp = 1.0f + 0.7f * evolve * rng.valuePN();
      target = rng.valuePN() * amp;
      len = seg_len(baseLen, evolve, rng);
      pos = 0;
    }
    float u = float(pos) / float(len);
    float s = u * u * (3.0f - 2.0f * u);  // smoothstep → low-pass movement
    ++pos;
    return cur + (target - cur) * s;
  }
};

} // namespace mforce::dsp
