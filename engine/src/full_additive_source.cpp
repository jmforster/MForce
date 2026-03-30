#include "mforce/source/additive/full_additive_source.h"
#include <cmath>
#include <algorithm>

namespace mforce {

FullAdditiveSource::FullAdditiveSource(int sampleRate, uint32_t seed)
: WaveSource(sampleRate), rng_(seed)
, multEnv_(std::make_shared<ConstantSource>(0.0f))
, amplEnv_(std::make_shared<ConstantSource>(0.0f))
, poEnv_(std::make_shared<ConstantSource>(0.0f))
, roEnv_(std::make_shared<ConstantSource>(0.0f))
, dtEnv_(std::make_shared<ConstantSource>(0.0f)) {}

void FullAdditiveSource::init_full_partials(
    int maxPartials, int minMult,
    float ew1, float ew2, float ow1, float ow2,
    float unitPO1, float unitPO2)
{
  int n = maxPartials;
  mult1_.resize(n); mult2_.resize(n);
  ampl1_.resize(n); ampl2_.resize(n);
  po1_.resize(n);   po2_.resize(n);

  for (int i = 0; i < n; ++i) {
    float m = float(minMult + i);
    mult1_[i] = m;

    // Even/odd amplitude weights (fundamental always 1)
    if (int(m) % 2 == 0) {
      ampl1_[i] = ew1;
      ampl2_[i] = ew2;
    } else {
      ampl1_[i] = (m == 1.0f) ? 1.0f : ow1;
      ampl2_[i] = (m == 1.0f) ? 1.0f : ow2;
    }

    // Phase offsets: (mult - 1) * unitPO
    po1_[i] = std::fmod((m - 1.0f) * unitPO1, 1.0f);
    po2_[i] = std::fmod((m - 1.0f) * unitPO2, 1.0f);
  }

  // No multiplier evolution for FullPartials
  mult2_ = mult1_;
}

void FullAdditiveSource::init_sequence_partials(
    int maxPartials,
    float minMult1, float minMult2,
    float incr1, float incr2,
    float unitPO1, float unitPO2)
{
  int n = maxPartials;
  mult1_.resize(n); mult2_.resize(n);
  ampl1_.resize(n); ampl2_.resize(n);
  po1_.resize(n);   po2_.resize(n);

  for (int i = 0; i < n; ++i) {
    mult1_[i] = minMult1 + incr1 * float(i);
    mult2_[i] = minMult2 + incr2 * float(i);
    ampl1_[i] = 1.0f;
    ampl2_[i] = 1.0f;
    po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1, 1.0f);
    po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2, 1.0f);
  }
}

void FullAdditiveSource::init_explicit_partials(
    std::vector<float> mult1, std::vector<float> mult2,
    std::vector<float> ampl1, std::vector<float> ampl2,
    float unitPO1, float unitPO2)
{
  mult1_ = std::move(mult1);
  mult2_ = std::move(mult2);
  ampl1_ = std::move(ampl1);
  ampl2_ = std::move(ampl2);

  int n = int(mult1_.size());
  po1_.resize(n);
  po2_.resize(n);

  for (int i = 0; i < n; ++i) {
    po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1, 1.0f);
    po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2, 1.0f);
  }
}

void FullAdditiveSource::prepare(int frames) {
  WaveSource::prepare(frames);
  multEnv_->prepare(frames);
  amplEnv_->prepare(frames);
  poEnv_->prepare(frames);
  roEnv_->prepare(frames);
  dtEnv_->prepare(frames);

  if (formant_) formant_->fmt_prepare(frames);
  if (formantWeight_) formantWeight_->prepare(frames);

  // Expand partials if rule is set
  if (hasExpand_) {
    if (origMult1_.empty()) {
      // First prepare: save originals before expanding
      origMult1_ = mult1_; origMult2_ = mult2_;
      origAmpl1_ = ampl1_; origAmpl2_ = ampl2_;
      origPo1_   = po1_;   origPo2_   = po2_;
    } else {
      // Subsequent prepare: restore from originals
      mult1_ = origMult1_; mult2_ = origMult2_;
      ampl1_ = origAmpl1_; ampl2_ = origAmpl2_;
      po1_   = origPo1_;   po2_   = origPo2_;
    }
    for (int r = 0; r <= expandRule_.recurse; ++r)
      apply_expand_rule();
  }

  int n = int(mult1_.size());
  partialPos_.assign(n, 0.0f);
  partialPO_.assign(n, 0.0f);
  partialLPO_.assign(n, 0.0f);

  init_detune_values();
}

void FullAdditiveSource::apply_expand_rule() {
  const auto& er = expandRule_;
  int origN = int(mult1_.size());
  int newN = origN * (er.count * 2 + 1);

  std::vector<float> m1(newN), m2(newN), a1(newN), a2(newN), p1(newN), p2(newN);

  for (int i = 0; i < origN; ++i) {
    int base = i * (er.count * 2 + 1);

    // Left sub-partials
    for (int j = 0; j < er.count; ++j) {
      int idx = base + j;
      float semis1 = float(er.count - j) * er.spacing1;
      float semis2 = float(er.count - j) * er.spacing2;

      m1[idx] = mult1_[i] / std::pow(2.0f, semis1 / 12.0f) * (1.0f + rng_.valuePN() * er.dt1);
      m2[idx] = mult2_[i] / std::pow(2.0f, semis2 / 12.0f) * (1.0f + rng_.valuePN() * er.dt2);

      float t = float(j) / float(er.count);
      a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
      a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);

      p1[idx] = std::fmod(po1_[i] + float(j + 1) / float(er.count) * er.po1, 1.0f);
      p2[idx] = std::fmod(po2_[i] + float(j + 1) / float(er.count) * er.po2, 1.0f);
    }

    // Primary (center)
    int cIdx = base + er.count;
    m1[cIdx] = mult1_[i]; m2[cIdx] = mult2_[i];
    a1[cIdx] = ampl1_[i]; a2[cIdx] = ampl2_[i];
    p1[cIdx] = po1_[i];   p2[cIdx] = po2_[i];

    // Right sub-partials
    for (int j = 0; j < er.count; ++j) {
      int idx = base + er.count + 1 + j;
      float semis1 = float(j + 1) * er.spacing1;
      float semis2 = float(j + 1) * er.spacing2;

      m1[idx] = mult1_[i] * std::pow(2.0f, semis1 / 12.0f);
      m2[idx] = mult2_[i] * std::pow(2.0f, semis2 / 12.0f);

      float t = float(er.count - j - 1) / float(er.count);
      a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
      a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);

      p1[idx] = std::fmod(po1_[i] + float(er.count - j) / float(er.count) * er.po1, 1.0f);
      p2[idx] = std::fmod(po2_[i] + float(er.count - j) / float(er.count) * er.po2, 1.0f);
    }
  }

  mult1_ = std::move(m1); mult2_ = std::move(m2);
  ampl1_ = std::move(a1); ampl2_ = std::move(a2);
  po1_   = std::move(p1); po2_   = std::move(p2);
}

void FullAdditiveSource::init_detune_values() {
  int n = int(mult1_.size());
  dtVals_.resize(n);
  for (int i = 0; i < n; ++i) {
    // Legacy: sign < 0 → range(-0.5, 0), else → range(0, 1)
    dtVals_[i] = (rng_.sign() < 0) ? rng_.range(-0.5f, 0.0f) : rng_.range(0.0f, 1.0f);
  }
}

float FullAdditiveSource::compute_wave_value() {
  // Advance all 5 envelopes once per sample
  multEnv_->next();
  amplEnv_->next();
  poEnv_->next();
  roEnv_->next();
  dtEnv_->next();

  float multE = multEnv_->current();
  float amplE = amplEnv_->current();
  float poE   = poEnv_->current();
  float roE   = roEnv_->current();
  float dtE   = dtEnv_->current();

  float fmtWt = 0.0f;
  if (formant_) {
    formant_->fmt_next();
    if (formantWeight_) {
      formantWeight_->next();
      fmtWt = formantWeight_->current();
    }
  }

  float phaseDiff = currPhase_ - lastPhase_;
  int n = int(mult1_.size());
  float val = 0.0f;

  for (int i = 0; i < n; ++i) {
    float v = get_partial_value(currAmpl_, currFreq_, phaseDiff, i,
                                amplE, multE, poE, roE, dtE, fmtWt);
    if (std::isnan(v)) {
      // Past cutoff — remaining partials will also be past cutoff
      // (assumes partials in ascending frequency order)
      break;
    }
    val += v;
  }

  // Legacy normalization
  return val / 5.0f;
}

float FullAdditiveSource::get_partial_value(
    float amplitude, float frequency, float phaseDiff, int i,
    float amplE, float multE, float poE, float roE, float dtE,
    float fmtWt)
{
  // Multiplier (can evolve between mult1 and mult2)
  float pmult = mult1_[i] + (mult2_[i] - mult1_[i]) * multE;

  // Phase offset (can evolve)
  float ppo = po1_[i] + (po2_[i] - po1_[i]) * poE;

  // Frequency = multiplier * base freq * (1 + detune)
  float dt = (dt1_ + (dt2_ - dt1_) * dtE) * dtVals_[i];
  float pfreq = pmult * frequency * (1.0f + dt);

  // Past cutoff → NaN signal to caller
  if (pfreq > CUTOFF) return std::numeric_limits<float>::quiet_NaN();

  constexpr float TAU = 2.0f * 3.14159265358979323846f;

  // Advance partial position
  partialPos_[i] = std::fmod(
      partialPos_[i] + pfreq / float(sampleRate_) + phaseDiff + (ppo - partialLPO_[i]),
      1.0f);
  if (partialPos_[i] < 0.0f) partialPos_[i] += 1.0f;
  partialLPO_[i] = ppo;

  // Amplitude with rolloff
  float ro = ro1_ + (ro2_ - ro1_) * roE;
  float rolloff = (ro == 0.0f) ? 1.0f : (1.0f / std::pow(pmult, ro));

  // Formant factor
  float fmtFactor = 1.0f;
  if (formant_ && fmtWt > 0.0f) {
    if (formant_->contains(pfreq)) {
      fmtFactor = 1.0f - fmtWt + fmtWt * formant_->get_gain(pfreq);
    } else {
      fmtFactor = 1.0f - fmtWt;
    }
  }

  // Fade out partials near cutoff (within 1000 Hz)
  float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;

  float pampl = amplitude *
      (ampl1_[i] + (ampl2_[i] - ampl1_[i]) * amplE) *
      rolloff * fmtFactor * fade;

  return std::sin(partialPos_[i] * TAU) * pampl;
}

} // namespace mforce
