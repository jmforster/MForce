#include "mforce/additive_source2.h"
#include <cmath>
#include <algorithm>

namespace mforce {

AdditiveSource2::AdditiveSource2(int sampleRate, uint32_t seed)
: WaveSource(sampleRate), rng_(seed)
, phaseOffset_(std::make_shared<ConstantSource>(0.0f))
, freqVarDepth_(std::make_shared<ConstantSource>(0.0f))
, freqVarSpeed_(std::make_shared<ConstantSource>(0.0f))
, amplVarDepth_(std::make_shared<ConstantSource>(0.0f))
, amplVarSpeed_(std::make_shared<ConstantSource>(0.0f)) {}

void AdditiveSource2::set_partials(std::vector<float> idx, std::vector<float> ampl) {
  hasStart_ = false;
  endIdx_ = std::move(idx);
  endAmpl_ = std::move(ampl);
  int n = int(endIdx_.size());
  absAmpl_.assign(n, true);
  freqEnvRef_.assign(n, -1);
  amplEnvRef_.assign(n, -1);
}

void AdditiveSource2::set_partials(
    std::vector<float> si, std::vector<float> sa,
    std::vector<float> ei, std::vector<float> ea) {
  hasStart_ = true;
  startIdx_ = std::move(si);
  startAmpl_ = std::move(sa);
  endIdx_ = std::move(ei);
  endAmpl_ = std::move(ea);
  int n = int(endIdx_.size());
  absAmpl_.assign(n, true);
  freqEnvRef_.assign(n, -1);
  amplEnvRef_.assign(n, -1);
}

void AdditiveSource2::set_default_partials(int count) {
  hasStart_ = false;
  endIdx_.resize(count);
  endAmpl_.resize(count);
  absAmpl_.assign(count, false);
  for (int i = 0; i < count; ++i) {
    endIdx_[i] = float(i + 1);
    endAmpl_[i] = float(i + 1);
  }
  freqEnvRef_.assign(count, -1);
  amplEnvRef_.assign(count, -1);
}

bool AdditiveSource2::matches_filter(PartialFilter f, int pnum) const {
  switch (f) {
    case PartialFilter::All:      return true;
    case PartialFilter::Even:     return pnum % 2 == 0;
    case PartialFilter::Odd:      return pnum % 2 != 0;
    case PartialFilter::Mult3:    return pnum % 3 == 0;
    case PartialFilter::NonMult3: return pnum % 3 != 0;
  }
  return false;
}

void AdditiveSource2::assign_freq_envelope(
    std::shared_ptr<ValueSource> env, PartialFilter filter, int from, int to) {
  int idx = int(freqEnvs_.size());
  freqEnvs_.push_back(std::move(env));
  int n = int(endIdx_.size());
  for (int i = 0; i < n; ++i) {
    int pnum = i + 1;
    if (pnum >= from && pnum <= to && matches_filter(filter, pnum))
      freqEnvRef_[i] = idx;
  }
}

void AdditiveSource2::assign_ampl_envelope(
    std::shared_ptr<ValueSource> env, PartialFilter filter, int from, int to) {
  int idx = int(amplEnvs_.size());
  amplEnvs_.push_back(std::move(env));
  int n = int(endIdx_.size());
  for (int i = 0; i < n; ++i) {
    int pnum = i + 1;
    if (pnum >= from && pnum <= to && matches_filter(filter, pnum))
      amplEnvRef_[i] = idx;
  }
}

void AdditiveSource2::prepare(int frames) {
  WaveSource::prepare(frames);
  phaseOffset_->prepare(frames);
  freqVarDepth_->prepare(frames);
  freqVarSpeed_->prepare(frames);
  amplVarDepth_->prepare(frames);
  amplVarSpeed_->prepare(frames);

  for (auto& e : freqEnvs_) e->prepare(frames);
  for (auto& e : amplEnvs_) e->prepare(frames);

  int n = int(endIdx_.size());
  partialCount_ = std::min(n, int(12000.0f / std::max(currFreq_, 1.0f)));

  endFreq_.resize(partialCount_);
  if (hasStart_) startFreq_.resize(partialCount_);

  for (int i = 0; i < partialCount_; ++i) {
    endFreq_[i] = currFreq_ * endIdx_[i];
    if (hasStart_) startFreq_[i] = currFreq_ * startIdx_[i];
  }

  partialPos_.assign(partialCount_, 0.0f);

  // Init random walk state
  float fvp = freqVarDepth_->current();
  float avp = amplVarDepth_->current();
  freqOffset_.resize(partialCount_);
  amplOffset_.resize(partialCount_);
  freqVarDir_.resize(partialCount_);
  amplVarDir_.resize(partialCount_);
  for (int i = 0; i < partialCount_; ++i) {
    freqOffset_[i] = rng_.valuePN() * fvp;
    freqVarDir_[i] = (rng_.valuePN() >= 0.0f) ? 1 : -1;
    amplOffset_[i] = rng_.valuePN() * avp;
    amplVarDir_[i] = (rng_.valuePN() >= 0.0f) ? 1 : -1;
  }
}

float AdditiveSource2::compute_wave_value() {
  phaseOffset_->next();
  freqVarDepth_->next();
  freqVarSpeed_->next();
  amplVarDepth_->next();
  amplVarSpeed_->next();

  float po  = phaseOffset_->current();
  float fvp = freqVarDepth_->current();
  float fvs = freqVarSpeed_->current() / float(sampleRate_);
  float avp = amplVarDepth_->current();
  float avs = amplVarSpeed_->current() / float(sampleRate_);

  // Advance all unique envelopes once per sample
  for (auto& e : freqEnvs_) e->next();
  for (auto& e : amplEnvs_) e->next();

  constexpr float TAU = 2.0f * 3.14159265358979323846f;
  float val = 0.0f;

  for (int i = 0; i < partialCount_; ++i) {
    // Frequency variation (random walk)
    if (fvp > 0.0f) {
      freqOffset_[i] += fvp * 2.0f * fvs * float(freqVarDir_[i]);
      if (std::fabs(freqOffset_[i]) >= fvp) {
        freqOffset_[i] = fvp * float(freqVarDir_[i]);
        freqVarDir_[i] *= -1;
      }
    }

    // Amplitude variation (random walk)
    if (avp > 0.0f) {
      amplOffset_[i] += avp * 2.0f * avs * float(amplVarDir_[i]);
      if (std::fabs(amplOffset_[i]) >= avp) {
        amplOffset_[i] = avp * float(amplVarDir_[i]);
        amplVarDir_[i] *= -1;
      }
    }

    // Compute frequency
    float pFreq, pAmpl;

    if (hasStart_) {
      float fEnvVal = (freqEnvRef_[i] >= 0) ? freqEnvs_[freqEnvRef_[i]]->current() : 0.0f;
      float aEnvVal = (amplEnvRef_[i] >= 0) ? amplEnvs_[amplEnvRef_[i]]->current() : 0.0f;
      pFreq = startFreq_[i] + (endFreq_[i] - startFreq_[i]) * fEnvVal * (1.0f + freqOffset_[i]);
      pAmpl = startAmpl_[i] + (endAmpl_[i] - startAmpl_[i]) * aEnvVal * (1.0f + amplOffset_[i]);
    } else {
      pFreq = endFreq_[i] * (1.0f + freqOffset_[i]);

      if (absAmpl_[i]) {
        pAmpl = endAmpl_[i] * (1.0f + amplOffset_[i]);
      } else {
        // Rolloff mode: amplitude driven by envelope value
        float aEnvVal = (amplEnvRef_[i] >= 0) ? amplEnvs_[amplEnvRef_[i]]->current() : 1.0f;
        pAmpl = (1.0f / std::pow(float(i + 1), aEnvVal)) * std::pow(aEnvVal, 2.0f);
      }
    }

    // Phase advancement with per-partial offset
    partialPos_[i] += pFreq / float(sampleRate_) + float(i) * 0.00001f * std::fmod(po, 1.0f);

    val += std::sin(partialPos_[i] * TAU) * pAmpl;
  }

  return val / 5.0f;
}

} // namespace mforce
