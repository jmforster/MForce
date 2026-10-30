#include "mforce/source/wavetable_source.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace mforce {

WavetableSource::WavetableSource(uint32_t inputSeed) {
  inputSource.set(std::make_shared<WhiteNoiseSource>(inputSeed));
}

void WavetableSource::on_prepare(const RenderContext& ctx) {
  speedFactor.prepare(ctx);
  inputSource.prepare(ctx);
  evolution.prepare(ctx);
  // The table and every evolution buffer are reserved here for the floor
  // frequency, so a note fills them within capacity (Part 2 section 4).
  maxLen_ = int(std::ceil(float(sample_rate()) / kMinTableHz)) + 1;
  values_.reserve(size_t(maxLen_));
  if (evolution_) evolution_->prepare(ctx, maxLen_);
}

void WavetableSource::on_start(int frames) {
  WaveSource::on_start(frames);
  speedFactor.start(frames);
  inputSource.start(frames);
  evolution.start(frames);
}

void WavetableSource::fill_table() {
  // A table shorter than one sample cannot be filled: the length is clamped
  // to one and the node reports, never throws (a render path).
  const float ratio = currFreq_ > 0.0f ? float(sample_rate()) / currFreq_ : 0.0f;
  int len = 1;
  if (ratio >= 1.0f) len = int(std::lround(ratio));
  else fault(Fault::TableClamped);
  if (len > maxLen_) len = maxLen_;   // a note below the floor gets the floor's length
  values_.resize(len);                // within the capacity prepare reserved

  for (int i = 0; i < len; ++i) {
    values_[i] = inputSource.step();
  }
}

float WavetableSource::wave() {
  if (!interpolate.value) {
    return compute_raw();
  } else {
    return compute_interpolated();
  }
}

float WavetableSource::compute_raw() {
  // On first sample, fill table and let evolution adjust
  if (ptr_ == 0) {
    baseFreq_ = currFreq_;
    fill_table();
    if (evolution_) {
      evolution_->adjust(currFreq_);
      evolution_->shape_excitation(values_);
    }
    // Tuning allpass: fractional delay for sub-sample pitch accuracy
    float idealLen = float(sample_rate()) / currFreq_;
    float frac = idealLen - float(int(values_.size()));
    if (frac > 0.001f && frac < 0.999f) {
      tuningCoeff_ = (1.0f - frac) / (1.0f + frac);
    } else {
      tuningCoeff_ = 0.0f;
    }
    tuningState_ = 0.0f;
    tuningPrevIn_ = 0.0f;
    tablePtr2_ = -1;
    readPos_ = -1.0f;  // advances to 0.0 on first step below
  }

  int len = int(values_.size());
  tablePtr2_ = (tablePtr2_ + 1) % len;

  if (evolution_) {
    evolution_->evolve(values_, tablePtr2_);
  }

  // Fractional read head — advance by currFreq_/baseFreq_ so pitch bends on
  // `frequency` bend the output. The loop itself runs at base pitch (above);
  // this is a resample on read. rateScale==1 (no bend) lands on integer
  // positions, so linear-interp collapses to values_[idx] and output matches
  // legacy behavior exactly.
  float rateScale = (baseFreq_ > 0.0f) ? currFreq_ / baseFreq_ : 1.0f;
  readPos_ += rateScale;
  while (readPos_ >= float(len)) readPos_ -= float(len);
  while (readPos_ < 0.0f) readPos_ += float(len);
  int idx = int(std::floor(readPos_));
  if (idx < 0) idx = 0;
  if (idx >= len) idx = len - 1;
  int idx2 = (idx + 1) % len;
  float fpos = readPos_ - float(idx);
  float out = values_[idx] + (values_[idx2] - values_[idx]) * fpos;

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
    float idealLen = float(sample_rate()) / currFreq_;
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
  float sf = speedFactor.next();

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
