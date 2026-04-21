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
  virtual void fmt_prepare(const RenderContext& ctx, int frames) = 0;
  virtual void fmt_next() = 0;
  virtual bool contains(float freq) const = 0;
  virtual float get_gain(float freq) const = 0;
};

// ---------------------------------------------------------------------------
// Ported from C# Formant — spectral peak with evolving params.
// ---------------------------------------------------------------------------
struct Formant final : ValueSource, IFormant {
  Formant()
  : frequency_(std::make_shared<ConstantSource>(1000.0f))
  , gain_(std::make_shared<ConstantSource>(1.0f))
  , width_(std::make_shared<ConstantSource>(500.0f))
  , power_(std::make_shared<ConstantSource>(2.0f)) {}

  void set_frequency(std::shared_ptr<ValueSource> s) { frequency_ = std::move(s); }
  void set_gain(std::shared_ptr<ValueSource> s)      { gain_ = std::move(s); }
  void set_width(std::shared_ptr<ValueSource> s)     { width_ = std::move(s); }
  void set_power(std::shared_ptr<ValueSource> s)     { power_ = std::move(s); }

  std::shared_ptr<ValueSource> get_frequency() const { return frequency_; }
  std::shared_ptr<ValueSource> get_gain() const      { return gain_; }
  std::shared_ptr<ValueSource> get_width() const     { return width_; }
  std::shared_ptr<ValueSource> get_power() const     { return power_; }

  const char* type_name() const override { return "Formant"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 1000.0f, 1.0f,    20000.0f},
      {"gain",      1.0f,    0.0f,    10.0f},
      {"width",     500.0f,  1.0f,    10000.0f},
      {"power",     2.0f,    0.01f,   10.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "frequency") { frequency_ = std::move(src); return; }
    if (name == "gain")      { gain_ = std::move(src); return; }
    if (name == "width")     { width_ = std::move(src); return; }
    if (name == "power")     { power_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "frequency") return frequency_;
    if (name == "gain")      return gain_;
    if (name == "width")     return width_;
    if (name == "power")     return power_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    frequency_->prepare(ctx, frames);
    gain_->prepare(ctx, frames);
    width_->prepare(ctx, frames);
    power_->prepare(ctx, frames);
  }

  void fmt_next() override {
    frequency_->next(); gain_->next(); width_->next(); power_->next();
    currFreq_ = frequency_->current();
    currGain_ = gain_->current();
    float w   = width_->current();
    currPow_  = power_->current();
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
  std::shared_ptr<ValueSource> frequency_;
  std::shared_ptr<ValueSource> gain_;
  std::shared_ptr<ValueSource> width_;
  std::shared_ptr<ValueSource> power_;
  float currFreq_{1000.0f}, currGain_{1.0f}, currPow_{2.0f};
  float loFreq_{750.0f}, hiFreq_{1250.0f};
};

// ---------------------------------------------------------------------------
// Ported from C# FormantSpectrum — collection, max-gain across formants.
// ---------------------------------------------------------------------------
struct FormantSpectrum : ValueSource, IFormant {
  std::vector<std::shared_ptr<IFormant>> formants;

  const char* type_name() const override { return "FormantSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    for (auto& f : formants) f->fmt_prepare(ctx, frames);
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

  const char* type_name() const override { return "FixedSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(const RenderContext& /*ctx*/, int /*frames*/) override {}
  void fmt_next() override {}

  bool contains(float /*freq*/) const override { return true; }

  float get_gain(float freq) const override {
    int idx = int(freq);
    if (idx < 0 || idx + 1 >= int(gainValues.size())) return 0.0f;
    float frac = freq - float(idx);
    return gainValues[idx] + (gainValues[idx + 1] - gainValues[idx]) * frac;
  }

  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"gains", nullptr, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }
  void set_array(std::string_view name, std::vector<float> v) override {
    if (name == "gains") gainValues = std::move(v);
  }
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "gains") return gainValues;
    return {};
  }
};

// ---------------------------------------------------------------------------
// Ported from C# BandSpectrum — frequency-band gain lookup.
// ---------------------------------------------------------------------------
struct BandSpectrum final : ValueSource, IFormant {
  std::vector<float> gainValues;

  BandSpectrum()
  : startFreq_(std::make_shared<ConstantSource>(0.0f))
  , freqIncrement_(std::make_shared<ConstantSource>(1.0f)) {}

  void set_startFreq(std::shared_ptr<ValueSource> s)     { startFreq_ = std::move(s); }
  void set_freqIncrement(std::shared_ptr<ValueSource> s) { freqIncrement_ = std::move(s); }
  std::shared_ptr<ValueSource> get_startFreq() const     { return startFreq_; }
  std::shared_ptr<ValueSource> get_freqIncrement() const { return freqIncrement_; }

  const char* type_name() const override { return "BandSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"startFreq",     0.0f, 0.0f, 20000.0f},
      {"freqIncrement", 1.0f, 0.01f, 1000.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "startFreq")     { startFreq_ = std::move(src); return; }
    if (name == "freqIncrement") { freqIncrement_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "startFreq")     return startFreq_;
    if (name == "freqIncrement") return freqIncrement_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    startFreq_->prepare(ctx, frames);
    freqIncrement_->prepare(ctx, frames);
  }

  void fmt_next() override {
    startFreq_->next();
    freqIncrement_->next();
  }

  bool contains(float freq) const override {
    float sf = startFreq_->current();
    float fi = freqIncrement_->current();
    return freq >= sf && freq <= sf + fi * float(int(gainValues.size()) - 1);
  }

  float get_gain(float freq) const override {
    float sf = startFreq_->current();
    float fi = freqIncrement_->current();
    if (fi <= 0.0f) return 0.0f;

    float pos = (freq - sf) / fi;
    int idx = int(pos);
    if (idx < 0 || idx + 1 >= int(gainValues.size())) return 0.0f;

    float frac = pos - float(idx);
    return gainValues[idx] + (gainValues[idx + 1] - gainValues[idx]) * frac;
  }

  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"gains", nullptr, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }
  void set_array(std::string_view name, std::vector<float> v) override {
    if (name == "gains") gainValues = std::move(v);
  }
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "gains") return gainValues;
    return {};
  }

private:
  std::shared_ptr<ValueSource> startFreq_;
  std::shared_ptr<ValueSource> freqIncrement_;
};

// ---------------------------------------------------------------------------
// Ported from C# FormantSequence — crossfade between formants over time.
// Driven by a blend ValueSource (0→1 morphs through all formants evenly).
// With N formants, blend=0 → formant[0], blend=1 → formant[N-1].
// ---------------------------------------------------------------------------
struct FormantSequence final : ValueSource, IFormant {
  std::vector<std::shared_ptr<IFormant>> formants;

  FormantSequence() : blend_(std::make_shared<ConstantSource>(0.0f)) {}

  void set_blend(std::shared_ptr<ValueSource> s) { blend_ = std::move(s); }
  std::shared_ptr<ValueSource> get_blend() const { return blend_; }

  const char* type_name() const override { return "FormantSequence"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"blend", 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"spectra", true},  // multi-input: accepts multiple FormantSpectrum connections
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "blend") { blend_ = std::move(src); return; }
    if (name == "spectra") {
      // Single-set: replace all with one (used by generic wiring path)
      auto fmt = std::dynamic_pointer_cast<IFormant>(src);
      if (fmt) { formants.clear(); formants.push_back(std::move(fmt)); }
      return;
    }
  }

  void add_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "spectra") {
      auto fmt = std::dynamic_pointer_cast<IFormant>(src);
      if (fmt) formants.push_back(std::move(fmt));
    }
  }

  void clear_param(std::string_view name) override {
    if (name == "spectra") formants.clear();
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "blend") return blend_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float next() override { fmt_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    blend_->prepare(ctx, frames);
    for (auto& f : formants) f->fmt_prepare(ctx, frames);
  }

  void fmt_next() override {
    blend_->next();
    for (auto& f : formants) f->fmt_next();

    int n = int(formants.size());
    if (n < 2) { fPtr_ = 0; fWeight_ = 0.0f; return; }

    float b = std::clamp(blend_->current(), 0.0f, 1.0f) * float(n - 1);
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
  std::shared_ptr<ValueSource> blend_;
  int fPtr_{0};
  float fWeight_{0.0f};
};

} // namespace mforce
