#include "mforce/source/additive/full_additive_source.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mforce {

FullAdditiveSource::FullAdditiveSource(int sampleRate, uint32_t seed)
: WaveSource(sampleRate)
, formantFloor_(std::make_shared<ConstantSource>(1.0f))
, noiseRng_(seed) {}

void FullAdditiveSource::prepare(const RenderContext& ctx, int frames) {
  WaveSource::prepare(ctx, frames);

  if (partials_) partials_->partials_prepare(ctx, frames);
  if (formant_) formant_->fmt_prepare(ctx, frames);
  if (formantWeight_) formantWeight_->prepare(ctx, frames);
  if (formantFloor_) formantFloor_->prepare(ctx, frames);

  // --- NoiseBed per-note state. The noise runs from sample 0 of the note;
  // it's the TONE that is delayed (bedDelaySamples_) and then faded in.
  const float rate = float(ctx.sampleRate);
  bedDelaySamples_ = noiseBedDelay_ * rate;
  bedFadeSamples_  = std::max(1.0f, noiseBedFade_ * rate);
  bedZ1_ = bedZ2_ = 0.0f;
  if (noiseBedLevel_ > 0.0f) {
    // RBJ constant-peak-gain bandpass, Q = center / width.
    const float f  = std::clamp(noiseBedFreq_, 10.0f, 0.45f * rate);
    const float q  = std::max(0.05f, f / std::max(1.0f, noiseBedWidth_));
    const float w0 = 2.0f * 3.14159265358979323846f * f / rate;
    const float sn = std::sin(w0), cs = std::cos(w0);
    const float alpha = sn / (2.0f * q);
    const float a0 = 1.0f + alpha;
    bedB0_ =  alpha / a0;           // b1 = 0
    bedB2_ = -alpha / a0;
    bedA1_ = -2.0f * cs / a0;
    bedA2_ = (1.0f - alpha) / a0;
  }
}

float FullAdditiveSource::compute_wave_value() {
  if (!partials_) return 0.0f;

  // Advance partials envelopes
  partials_->partials_next();

  float fmtWt = 0.0f;
  float fmtFloor = 1.0f;
  if (formant_) {
    formant_->fmt_next();
    if (formantWeight_) {
      formantWeight_->next();
      fmtWt = formantWeight_->current();
    }
    if (formantFloor_) {
      formantFloor_->next();
      fmtFloor = formantFloor_->current();
    }
  }

  float phaseDiff = currPhase_ - lastPhase_;

  // One virtual call per sample; the per-partial loop lives inside Partials so
  // it can inline and hoist. Partials past CUTOFF are gated individually
  // there (a jittering partial near cutoff must not kill everything above it —
  // the old `break` assumed static ascending frequencies and chattered).
  float val = partials_->sum_partials(0.0f, currAmpl_, currFreq_, phaseDiff,
                                      formant_.get(), fmtWt, fmtFloor);

  // NoiseBed tone delay: gate the PARTIAL SUM (not the noise) until the
  // delay elapses, then smoothstep-fade over noiseBedFade. Partial phases
  // keep advancing above, so partials enter already-running — same principle
  // as onset dispersion. Inert at noiseBedDelay == 0.
  if (bedDelaySamples_ > 0.0f) {
    float t = (float(ptr_) - bedDelaySamples_) / bedFadeSamples_;
    if (t <= 0.0f)      val = 0.0f;
    else if (t < 1.0f) {
      float s = t * t * (3.0f - 2.0f * t);
      // fadePow shapes PERCEIVED attack: smoothstep rises fast early, so
      // even long fades sound quick. pow > 1 keeps the tone low longer
      // (time-to-audible scales with the exponent), same total fade length.
      if (noiseBedFadePow_ != 1.0f) s = std::pow(s, noiseBedFadePow_);
      val *= s;
    }
  }

  // Legacy normalization
  float out = val / 5.0f;

  // NoiseBed: white noise -> single 2nd-order bandpass, scaled by
  // noiseBedLevel * the amplitude param's current value (tone-referenced
  // level — the same amplitude factor the partial sum carries, so the bed
  // tracks the tone's level law rather than being an independent hiss).
  if (noiseBedLevel_ > 0.0f) {
    float white = noiseRng_.valuePN();
    float y = bedB0_ * white + bedZ1_;
    bedZ1_ = bedZ2_ - bedA1_ * y;          // b1 == 0
    bedZ2_ = bedB2_ * white - bedA2_ * y;
    out += y * noiseBedLevel_ * currAmpl_;
  }

  return out;
}

} // namespace mforce
