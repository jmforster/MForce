#include "mforce/red_noise_source.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace mforce {

RedNoiseSource::RedNoiseSource(int sampleRate, uint32_t seed)
: WaveSource(sampleRate), rng_(seed), interp_(0.0f, false) {
  density           = std::make_shared<ConstantSource>(1.0f);
  smoothness        = std::make_shared<ConstantSource>(1.0f);
  rampVariation     = std::make_shared<ConstantSource>(1.0f);
  boost             = std::make_shared<ConstantSource>(0.0f);
  continuity        = std::make_shared<ConstantSource>(0.0f);
  zeroCrossTendency = std::make_shared<ConstantSource>(0.0f);

  // Phase is meaningless; legacy passes phase=0 via base(ampl,freq,0).
}

void RedNoiseSource::prepare(int frames) {
  WaveSource::prepare(frames);
  density->prepare(frames);
  smoothness->prepare(frames);
  rampVariation->prepare(frames);
  boost->prepare(frames);
  continuity->prepare(frames);
  zeroCrossTendency->prepare(frames);
}

float RedNoiseSource::compute_wave_value() {
  // Legacy advances these every sample
  density->next();
  smoothness->next();
  rampVariation->next();
  boost->next();
  continuity->next();
  zeroCrossTendency->next();

  interp_.setSmoothness(smoothness->current());

  // "Check if done with current ramp, due to either normal completion or significant frequency increase"
  // if (SampleCount >= RampSize || RampSize > Rate / CurrFreq)
  if (sampleCount_ >= rampSize_ || float(rampSize_) > float(sampleRate_) / currFreq_) {

    // Set up for next ramp
    // Legacy has a weird SampleCount > -RampSize branch; SampleCount is never negative in practice.
    lastValue_ = nextValue_;

    const float rv = rampVariation->current();

    // RampSize calculation:
    // RampSize = Round( Rate / Rand.Range(CurrFreq*(1-rv), CurrFreq*(1+rv)) / 2 )
    // which is Round( 2*Rate / RandRange(...) )
    float fmin = currFreq_ * (1.0f - rv);
    float fmax = currFreq_ * (1.0f + rv);
    if (fmin <= 0.0f) fmin = 0.0001f;
    if (fmax <= fmin) fmax = fmin + 0.0001f;

    float denom = rng_.range(fmin, fmax);

    float rsFloat = (float(sampleRate_) / denom) / 2.0f;   // = Rate / (2*denom)

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
    } else if (rng_.decide(density->current())) {

      // LastSign = Decide(ZCT) ? -LastSign : Rand.Sign()
      if (rng_.decide(zeroCrossTendency->current())) {
        lastSign_ = -lastSign_;
      } else {
        lastSign_ = float(rng_.sign());
        if (lastSign_ == 0.0f) lastSign_ = 1.0f; // avoid 0 sign edge
      }

      // NextValue = Rand.Range(Boost, 1.0) * LastSign
      float b = boost->current();
      b = std::clamp(b, 0.0f, 1.0f);
      nextValue_ = rng_.range(b, 1.0f) * lastSign_;

      // Continuity: NextValue = Rand.Range(LastVal, NextValue, min(cont,0.999))
      float cont = continuity->current();
      if (cont != 0.0f) {
        float influence = std::min(cont, 0.999f);
        // NOTE: legacy uses LastVal from SingleValueSource (previous output sample).
        // In our C++ port, WaveSource doesn't track LastVal; we approximate continuity bias using lastValue_.
        // If you want perfect equivalence, we can add LastVal tracking to SingleValueSource layer.
        nextValue_ = rng_.range(lastValue_, nextValue_, lastValue_, influence);
      }

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
