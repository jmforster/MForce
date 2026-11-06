#include "mforce/source/noise/red_noise_source.h"
#include "mforce/source/noise/shaped_draw.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace mforce {

RedNoiseSource::RedNoiseSource(uint32_t seed)
: rng_(seed), interp_(0.0f, false) {
  // Phase is meaningless; legacy passes phase=0 via base(ampl,freq,0).
}

void RedNoiseSource::reset() {
  rampSize_ = 1;
  sampleCount_ = 0;
  lastValue_ = 0.0f;
  lastSign_ = -1.0f;
  nextValue_ = 0.0f;
  zeroRamp_ = false;
  rng_.reanchor();
}

float RedNoiseSource::wave() {
  interp_.setSmoothness(smoothness.current());

  // "Check if done with current ramp, due to either normal completion or significant frequency increase"
  // if (SampleCount >= RampSize || RampSize > Rate / CurrFreq)
  if (sampleCount_ >= rampSize_ || float(rampSize_) > float(sample_rate()) / currFreq_) {

    // Set up for next ramp
    // Legacy has a weird SampleCount > -RampSize branch; SampleCount is never negative in practice.
    lastValue_ = nextValue_;

    const float rv = rampVariation.current();

    // RampSize calculation:
    // RampSize = Round( Rate / Rand.Range(CurrFreq*(1-rv), CurrFreq*(1+rv)) / 2 )
    // which is Round( 2*Rate / RandRange(...) )
    float fmin = currFreq_ * (1.0f - rv);
    float fmax = currFreq_ * (1.0f + rv);
    if (fmin <= 0.0f) fmin = 0.0001f;
    if (fmax <= fmin) fmax = fmin + 0.0001f;

    float denom = rng_.range(fmin, fmax);

    float rsFloat = (float(sample_rate()) / denom) / 2.0f;   // = Rate / (2*denom)

    if (currFreq_ > 500.0f) {
      // high freq: biased floor/ceiling to reduce pitch quantization
      rampSize_ = rng_.floorOrCeiling(rsFloat);
    } else {
      rampSize_ = int(std::lround(rsFloat));
    }
    if (rampSize_ < 1) rampSize_ = 1;

    sampleCount_ = 0;

    if (zeroRamp_) {
      lastValue_ = 0.0f;
      nextValue_ = 0.0f;
      zeroRamp_ = false;
    } else if (rng_.decide(density.current())) {

      nextValue_ = shaped_draw(rng_, zeroCrossTendency.current(), boost.current(),
                               continuity.current(), lastValue_, lastSign_);

    } else {
      nextValue_ = 0.0f;
      zeroRamp_ = true;
    }
  }

  float pos = float(sampleCount_) / float(rampSize_);
  float val = interp_.interpolate(lastValue_, nextValue_, pos);

  sampleCount_++;

  return val;
}

} // namespace mforce
