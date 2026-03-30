#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.Additive.IFormant
// Interface for frequency-dependent gain shaping.
// ---------------------------------------------------------------------------
struct IFormant {
  virtual ~IFormant() = default;
  virtual void fmt_prepare(int frames) = 0;
  virtual void fmt_next() = 0;
  virtual bool contains(float freq) const = 0;
  virtual float get_gain(float freq) const = 0;
};

// ---------------------------------------------------------------------------
// Ported from C# Formant — spectral peak with evolving params.
// ---------------------------------------------------------------------------
struct Formant final : ValueSource, IFormant {
  std::shared_ptr<ValueSource> frequency;
  std::shared_ptr<ValueSource> gain;
  std::shared_ptr<ValueSource> width;
  std::shared_ptr<ValueSource> power;

  Formant()
  : frequency(std::make_shared<ConstantSource>(1000.0f))
  , gain(std::make_shared<ConstantSource>(1.0f))
  , width(std::make_shared<ConstantSource>(500.0f))
  , power(std::make_shared<ConstantSource>(2.0f)) {}

  void prepare(int frames) override { fmt_prepare(frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(int frames) override {
    frequency->prepare(frames);
    gain->prepare(frames);
    width->prepare(frames);
    power->prepare(frames);
  }

  void fmt_next() override {
    frequency->next(); gain->next(); width->next(); power->next();
    currFreq_ = frequency->current();
    currGain_ = gain->current();
    float w   = width->current();
    currPow_  = power->current();
    loFreq_   = currFreq_ - w * 0.5f;
    hiFreq_   = currFreq_ + w * 0.5f;
  }

  bool contains(float freq) const override { return freq > loFreq_ && freq < hiFreq_; }

  float get_gain(float freq) const override {
    if (freq <= loFreq_ || freq >= hiFreq_) return 0.0f;
    if (freq < currFreq_)
      return std::pow((freq - loFreq_) / (currFreq_ - loFreq_), currPow_) * currGain_;
    return std::pow((hiFreq_ - freq) / (hiFreq_ - currFreq_), currPow_) * currGain_;
  }

private:
  float currFreq_{1000.0f}, currGain_{1.0f}, currPow_{2.0f};
  float loFreq_{750.0f}, hiFreq_{1250.0f};
};

// ---------------------------------------------------------------------------
// Ported from C# FormantSpectrum — collection, max-gain across formants.
// ---------------------------------------------------------------------------
struct FormantSpectrum : ValueSource, IFormant {
  std::vector<std::shared_ptr<IFormant>> formants;

  void prepare(int frames) override { fmt_prepare(frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(int frames) override {
    for (auto& f : formants) f->fmt_prepare(frames);
  }

  void fmt_next() override {
    for (auto& f : formants) f->fmt_next();
  }

  bool contains(float freq) const override {
    for (auto& f : formants)
      if (f->contains(freq)) return true;
    return false;
  }

  float get_gain(float freq) const override {
    float g = 0.0f;
    for (auto& f : formants)
      if (f->contains(freq))
        g = std::max(g, f->get_gain(freq));
    return g;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# FixedSpectrum — full gain array indexed by integer frequency.
// ---------------------------------------------------------------------------
struct FixedSpectrum final : ValueSource, IFormant {
  std::vector<float> gainValues;

  explicit FixedSpectrum(std::vector<float> values) : gainValues(std::move(values)) {}

  void prepare(int /*frames*/) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(int /*frames*/) override {}
  void fmt_next() override {}

  bool contains(float /*freq*/) const override { return true; }

  float get_gain(float freq) const override {
    int idx = int(freq);
    if (idx < 0 || idx + 1 >= int(gainValues.size())) return 0.0f;
    float frac = freq - float(idx);
    return gainValues[idx] + (gainValues[idx + 1] - gainValues[idx]) * frac;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# BandSpectrum — frequency-band gain lookup.
// ---------------------------------------------------------------------------
struct BandSpectrum final : ValueSource, IFormant {
  std::shared_ptr<ValueSource> startFreq;
  std::shared_ptr<ValueSource> freqIncrement;
  std::vector<float> gainValues;

  BandSpectrum()
  : startFreq(std::make_shared<ConstantSource>(0.0f))
  , freqIncrement(std::make_shared<ConstantSource>(1.0f)) {}

  void prepare(int frames) override { fmt_prepare(frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(int frames) override {
    startFreq->prepare(frames);
    freqIncrement->prepare(frames);
  }

  void fmt_next() override {
    startFreq->next();
    freqIncrement->next();
  }

  bool contains(float freq) const override {
    float sf = startFreq->current();
    float fi = freqIncrement->current();
    return freq >= sf && freq <= sf + fi * float(int(gainValues.size()) - 1);
  }

  float get_gain(float freq) const override {
    float sf = startFreq->current();
    float fi = freqIncrement->current();
    if (fi <= 0.0f) return 0.0f;

    float pos = (freq - sf) / fi;
    int idx = int(pos);
    if (idx < 0 || idx + 1 >= int(gainValues.size())) return 0.0f;

    float frac = pos - float(idx);
    return gainValues[idx] + (gainValues[idx + 1] - gainValues[idx]) * frac;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# FormantSequence — crossfade between formants over time.
// Driven by a blend ValueSource (0→1 morphs through all formants evenly).
// With N formants, blend=0 → formant[0], blend=1 → formant[N-1].
// ---------------------------------------------------------------------------
struct FormantSequence final : ValueSource, IFormant {
  std::vector<std::shared_ptr<IFormant>> formants;
  std::shared_ptr<ValueSource> blend;  // 0..1

  FormantSequence() : blend(std::make_shared<ConstantSource>(0.0f)) {}

  void prepare(int frames) override { fmt_prepare(frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(int frames) override {
    blend->prepare(frames);
    for (auto& f : formants) f->fmt_prepare(frames);
  }

  void fmt_next() override {
    blend->next();
    for (auto& f : formants) f->fmt_next();

    int n = int(formants.size());
    if (n < 2) { fPtr_ = 0; fWeight_ = 0.0f; return; }

    float b = std::clamp(blend->current(), 0.0f, 1.0f) * float(n - 1);
    fPtr_ = std::min(int(b), n - 2);
    fWeight_ = b - float(fPtr_);
  }

  bool contains(float freq) const override {
    if (formants.empty()) return false;
    int n = int(formants.size());
    int hi = std::min(fPtr_ + 1, n - 1);
    return formants[fPtr_]->contains(freq) || formants[hi]->contains(freq);
  }

  float get_gain(float freq) const override {
    if (formants.empty()) return 0.0f;
    int n = int(formants.size());
    int hi = std::min(fPtr_ + 1, n - 1);

    float g0 = formants[fPtr_]->contains(freq) ? formants[fPtr_]->get_gain(freq) : 0.0f;
    float g1 = formants[hi]->contains(freq)    ? formants[hi]->get_gain(freq)    : 0.0f;

    return g0 * (1.0f - fWeight_) + g1 * fWeight_;
  }

private:
  int fPtr_{0};
  float fWeight_{0.0f};
};

} // namespace mforce
