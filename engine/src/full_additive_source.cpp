#include "mforce/source/additive/full_additive_source.h"
#include <cmath>
#include <limits>

namespace mforce {

FullAdditiveSource::FullAdditiveSource(int sampleRate, uint32_t /*seed*/)
: WaveSource(sampleRate) {}

void FullAdditiveSource::prepare(int frames) {
  WaveSource::prepare(frames);

  if (partials_) partials_->partials_prepare(frames);
  if (formant_) formant_->fmt_prepare(frames);
  if (formantWeight_) formantWeight_->prepare(frames);
}

float FullAdditiveSource::compute_wave_value() {
  if (!partials_) return 0.0f;

  // Advance partials envelopes
  partials_->partials_next();

  float fmtWt = 0.0f;
  if (formant_) {
    formant_->fmt_next();
    if (formantWeight_) {
      formantWeight_->next();
      fmtWt = formantWeight_->current();
    }
  }

  float phaseDiff = currPhase_ - lastPhase_;
  int n = partials_->partial_count();
  float val = 0.0f;

  for (int i = 0; i < n; ++i) {
    float v = partials_->get_partial_value(
        currAmpl_, currFreq_, phaseDiff, i,
        formant_.get(), fmtWt);

    if (std::isnan(v)) {
      // Past cutoff — remaining partials assumed in ascending order
      break;
    }
    val += v;
  }

  // Legacy normalization
  return val / 5.0f;
}

} // namespace mforce
