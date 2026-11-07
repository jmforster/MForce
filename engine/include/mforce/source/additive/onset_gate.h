#pragma once
// OnsetGate: the per-partial onset delay and fade of the onset dispersion.
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mforce {

// Onset dispersion: a partial stays silent until its per-note delay, then
// fades in on a smoothstep. Phase keeps advancing while gated, so partials
// "enter" as already-running oscillators rather than all striking at t = 0.
// tilt orders the delays by partial height (+1 low-first so high partials
// bloom later, −1 the reverse, 0 random). A value type the owner holds:
// prepare sizes it, reset and start run per note, advance counts the
// note's samples, apply gates one partial's amplitude.
struct OnsetGate {
  void prepare(int n) { delay_.assign(size_t(n), 0.0f); }

  // Every note: the sample count restarts, whether or not the gate is on.
  void reset() { sampleIdx_ = 0; }

  // A note with the gate on: the delays drawn from the layer's stream, the
  // fade length fixed. Fills within the capacity prepare gave it.
  void start(int n, uint32_t noteBase, uint32_t layer, float spread, float tilt,
             float fade, float rate) {
    Randomizer onsetRng(stream_seed(noteBase, 0, layer));
    delay_.resize(size_t(n));
    float at = std::fabs(tilt);
    for (int i = 0; i < n; ++i) {
      float u = onsetRng.value();
      float r = (n > 1) ? float(i) / float(n - 1) : 0.0f;
      float ordered = (tilt >= 0.0f) ? r : 1.0f - r;
      delay_[size_t(i)] = ((1.0f - at) * u + at * ordered) * spread * rate;
    }
    fadeSamples_ = std::max(1.0f, fade * rate);
  }

  void advance() { ++sampleIdx_; }

  // Silent before the delay (an assignment, not a multiply: the zero's sign
  // is the zero's), a smoothstep fade across fadeSamples, unity after.
  float apply(int index, float pampl) const {
    float t = (float(sampleIdx_) - delay_[size_t(index)]) / fadeSamples_;
    if (t <= 0.0f) pampl = 0.0f;
    else if (t < 1.0f) pampl *= t * t * (3.0f - 2.0f * t);
    return pampl;
  }

private:
  std::vector<float> delay_;
  float fadeSamples_{1.0f};
  int sampleIdx_{0};
};

} // namespace mforce
