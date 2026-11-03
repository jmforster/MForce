#pragma once
#include "mforce/core/dsp/constants.h"

namespace mforce::dsp {

// The first-order DC blocker y = r·y1 + x − x1: a zero at DC and a pole just
// inside the unit circle at r = 1 − 2π·corner/rate, so a unipolar input (an
// envelope driving a string) reaches the loop with its offset removed and a
// comb no longer rings at its k = 0 resonance.
struct DcBlocker {
  void set(float cornerHz, float sampleRate) {
    r_ = 1.0f - 2.0f * kPi * cornerHz / sampleRate;
  }
  void reset() { x1_ = y1_ = 0.0f; }
  float process(float x) {
    const float y = r_ * y1_ + x - x1_;
    x1_ = x;
    y1_ = y;
    return y;
  }

private:
  float r_{0.0f};
  float x1_{0.0f}, y1_{0.0f};
};

} // namespace mforce::dsp
