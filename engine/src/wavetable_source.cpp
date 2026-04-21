#include "mforce/source/wavetable_source.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace mforce {

WavetableSource::WavetableSource(int sampleRate, uint32_t inputSeed)
: WaveSource(sampleRate)
, inputSource_(std::make_shared<WhiteNoiseSource>(inputSeed))
, speedFactor_(std::make_shared<ConstantSource>(1.0f)) {}

void WavetableSource::prepare(const RenderContext& ctx, int frames) {
  WaveSource::prepare(ctx, frames);
  speedFactor_->prepare(ctx, frames);
  inputSource_->prepare(ctx, frames);
  if (evolutionSrc_) evolutionSrc_->prepare(ctx, frames);
  if (evolution_) evolution_->set_sample_rate(sampleRate_);
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
  // On first sample, fill table and let evolution adjust
  if (ptr_ == 0) {
    fill_table();
    if (evolution_) {
      evolution_->adjust(currFreq_);
      evolution_->shape_excitation(values_);
    }
    // Tuning allpass: fractional delay for sub-sample pitch accuracy
    float idealLen = float(sampleRate_) / currFreq_;
    float frac = idealLen - float(int(values_.size()));
    if (frac > 0.001f && frac < 0.999f) {
      tuningCoeff_ = (1.0f - frac) / (1.0f + frac);
    } else {
      tuningCoeff_ = 0.0f;
    }
    tuningState_ = 0.0f;
    tuningPrevIn_ = 0.0f;
    tablePtr2_ = -1;
  }

  int len = int(values_.size());
  tablePtr2_ = (tablePtr2_ + 1) % len;

  if (evolution_) {
    evolution_->evolve(values_, tablePtr2_);
  }

  float out = values_[tablePtr2_];

  // Tuning allpass: y[n] = C*x[n] + x[n-1] - C*y[n-1]
  if (tuningCoeff_ != 0.0f) {
    float y = tuningCoeff_ * out + tuningPrevIn_ - tuningCoeff_ * tuningState_;
    tuningPrevIn_ = out;
    tuningState_ = y;
    out = y;
  }

  return out;
}

float WavetableSource::compute_interpolated() {
  if (ptr_ == 0) {
    fill_table();
    if (evolution_) {
      evolution_->adjust(currFreq_);
      evolution_->shape_excitation(values_);
    }
    float idealLen = float(sampleRate_) / currFreq_;
    float frac = idealLen - float(int(values_.size()));
    if (frac > 0.001f && frac < 0.999f) {
      tuningCoeff_ = (1.0f - frac) / (1.0f + frac);
    } else {
      tuningCoeff_ = 0.0f;
    }
    tuningState_ = 0.0f;
    tuningPrevIn_ = 0.0f;
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
  float frac2 = tablePtr - std::floor(tablePtr);
  float out = values_[samp1] + (values_[samp2] - values_[samp1]) * frac2;

  // Tuning allpass
  if (tuningCoeff_ != 0.0f) {
    float y = tuningCoeff_ * out + tuningPrevIn_ - tuningCoeff_ * tuningState_;
    tuningPrevIn_ = out;
    tuningState_ = y;
    out = y;
  }

  return out;
}

} // namespace mforce
