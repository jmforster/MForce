#include "mforce/source/wavetable_source.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace mforce {

WavetableSource::WavetableSource(int sampleRate, uint32_t inputSeed)
: WaveSource(sampleRate)
, inputSource_(std::make_shared<WhiteNoiseSource>(inputSeed))
, speedFactor_(std::make_shared<ConstantSource>(1.0f)) {}

void WavetableSource::prepare(int frames) {
  WaveSource::prepare(frames);
  speedFactor_->prepare(frames);
  inputSource_->prepare(frames);
}

void WavetableSource::fill_table() {
  if (currFreq_ <= 0.0f || float(sampleRate_) / currFreq_ < 1.0f) {
    throw std::runtime_error("WavetableSource: invalid frequency for table fill");
  }

  int len = int(std::lround(float(sampleRate_) / currFreq_));
  values_.resize(len);

  for (int i = 0; i < len; ++i) {
    values_[i] = inputSource_->next();
  }
}

float WavetableSource::compute_wave_value() {
  if (!interpolate_) {
    return compute_raw();
  } else {
    return compute_interpolated();
  }
}

float WavetableSource::compute_raw() {
  // Legacy: on first sample, fill table and let evolution adjust
  if (ptr_ == 0) {
    fill_table();
    if (evolution_) {
      evolution_->adjust(currFreq_);
    }
    tablePtr2_ = -1;
  }

  int len = int(values_.size());
  tablePtr2_ = (tablePtr2_ + 1) % len;

  if (evolution_) {
    evolution_->evolve(values_, tablePtr2_);
  }

  return values_[tablePtr2_];
}

float WavetableSource::compute_interpolated() {
  if (ptr_ == 0) {
    fill_table();
    if (evolution_) {
      evolution_->adjust(currFreq_);
    }
  }

  int len = int(values_.size());
  float sf = speedFactor_->next();

  float tablePtr = float(len) * currPos_ * sf;
  int samp1 = int(std::floor(tablePtr));
  samp1 = samp1 % len;
  if (samp1 < 0) samp1 += len;
  int samp2 = (samp1 + 1) % len;

  if (evolution_) {
    evolution_->evolve(values_, samp1);
  }

  // Linear interpolation
  float frac = tablePtr - std::floor(tablePtr);
  return values_[samp1] + (values_[samp2] - values_[samp1]) * frac;
}

} // namespace mforce
