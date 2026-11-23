#pragma once
#include "mforce/core/randomizer.h"
#include <algorithm>

namespace mforce {

// shaped_draw: the signed, continuity-biased noise draw that white and red
// noise share. Called only after the caller's own density decide passes.
// Updates lastSign by reference; returns the drawn value.
inline float shaped_draw(Randomizer& rng, float zeroCrossTendency, float boost,
                         float continuity, float lastValue, float& lastSign) {
  if (rng.decide(zeroCrossTendency)) {
    lastSign = -lastSign;
  } else {
    lastSign = float(rng.sign());
    if (lastSign == 0.0f) lastSign = 1.0f;
  }
  const float b = std::clamp(boost, 0.0f, 1.0f);
  float v = rng.range(b, 1.0f) * lastSign;
  if (continuity != 0.0f) {
    const float influence = std::min(continuity, 0.999f);
    v = rng.range(lastValue, v, lastValue, influence);
  }
  return v;
}

} // namespace mforce
