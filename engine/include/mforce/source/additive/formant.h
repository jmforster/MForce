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
  // The additive source drives a Formant through fmt_prepare() and fmt_next(),
  // never through next(), so the four pins are Manual: this node prepares
  // them and advances them there.
  Param frequency{this, "frequency", 1000.0f, 1.0f,  20000.0f, "hz",  Pull::Manual};
  Param gain     {this, "gain",      1.0f,    0.0f,  10.0f,    "0-1", Pull::Manual};
  Param width    {this, "width",     500.0f,  1.0f,  10000.0f, "hz",  Pull::Manual};
  Param power    {this, "power",     2.0f,    0.01f, 10.0f,    "exp", Pull::Manual};

  const char* type_name() const override { return "Formant"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void on_prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float compute() override { fmt_next(); return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    frequency.prepare(ctx, frames);
    gain.prepare(ctx, frames);
    width.prepare(ctx, frames);
    power.prepare(ctx, frames);
  }

  void fmt_next() override {
    frequency.advance(); gain.advance(); width.advance(); power.advance();
    currFreq_ = frequency.current();
    currGain_ = gain.current();
    float w   = width.current();
    currPow_  = power.current();
    loFreq_   = currFreq_ - w * 0.5f;
    hiFreq_   = currFreq_ + w * 0.5f;
  }

  bool contains(float freq) const override { return freq > loFreq_ && freq < hiFreq_; }

  // ANTI-RESULT (run 14, don't retry): these std::pow calls look like the same
  // hot-loop CRT-call bug that truncf and exp2 turned out to be — get_gain runs
  // per in-band partial per sample, times the formants in the spectrum, and
  // every formant in the repo's vowel families (579 of 579) uses power 2, so a
  // bit-exact t*t fast path was written and measured. It buys NOTHING: 1.01x on
  // liar_220 and 1.00x on oo_ee_110 (72 partials at f0 110/220, the worst case
  // in the library), 0.89-1.09x noise across the ab_render_time set. Reverted.
  // The reason is the contains() gate one line up: only partials INSIDE a
  // formant band ever reach this, and that is a small minority of them. The
  // pow is real but it is not hot.
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

  const char* type_name() const override { return "FormantSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void on_prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float compute() override { fmt_next(); return 0.0f; }

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
  Array gains{this, "gains", nullptr, 0.0f, 0.0f, 1.0f};

  explicit FixedSpectrum(std::vector<float> values) { gains.values = std::move(values); }

  const char* type_name() const override { return "FixedSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  float compute() override { return 0.0f; }

  void fmt_prepare(const RenderContext& /*ctx*/, int /*frames*/) override {}
  void fmt_next() override {}

  bool contains(float /*freq*/) const override { return true; }

  float get_gain(float freq) const override {
    int idx = int(freq);
    if (idx < 0 || idx + 1 >= int(gains.values.size())) return 0.0f;
    float frac = freq - float(idx);
    return gains.values[idx] + (gains.values[idx + 1] - gains.values[idx]) * frac;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# BandSpectrum — frequency-band gain lookup.
// ---------------------------------------------------------------------------
struct BandSpectrum final : ValueSource, IFormant {
  // Driven through fmt_prepare() and fmt_next() by the additive source, never
  // through next(), so both pins are Manual: this node prepares and advances them.
  Param startFreq    {this, "startFreq",     0.0f, 0.0f,  20000.0f, nullptr, Pull::Manual};
  Param freqIncrement{this, "freqIncrement", 1.0f, 0.01f, 1000.0f,  nullptr, Pull::Manual};
  Array gains{this, "gains", nullptr, 0.0f, 0.0f, 1.0f};

  const char* type_name() const override { return "BandSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void on_prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float compute() override { fmt_next(); return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    startFreq.prepare(ctx, frames);
    freqIncrement.prepare(ctx, frames);
  }

  void fmt_next() override {
    startFreq.advance();
    freqIncrement.advance();
  }

  bool contains(float freq) const override {
    float sf = startFreq.current();
    float fi = freqIncrement.current();
    return freq >= sf && freq <= sf + fi * float(int(gains.values.size()) - 1);
  }

  float get_gain(float freq) const override {
    float sf = startFreq.current();
    float fi = freqIncrement.current();
    if (fi <= 0.0f) return 0.0f;

    float pos = (freq - sf) / fi;
    int idx = int(pos);
    if (idx < 0 || idx + 1 >= int(gains.values.size())) return 0.0f;

    float frac = pos - float(idx);
    return gains.values[idx] + (gains.values[idx + 1] - gains.values[idx]) * frac;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# FormantSequence — crossfade between formants over time.
// Driven by a blend ValueSource (0→1 morphs through all formants evenly).
// With N formants, blend=0 → formant[0], blend=1 → formant[N-1].
// ---------------------------------------------------------------------------
struct FormantSequence final : ValueSource, IFormant {
  // Driven through fmt_prepare() and fmt_next() by the additive source, never
  // through next(), so `blend` is Manual: this node prepares and advances it.
  Param blend{this, "blend", 0.0f, 0.0f, 1.0f, nullptr, Pull::Manual};

  // `spectra` is a list, held in `formants`, not a pin: it is served by name
  // here, and wiring it by set_param replaces the list while add_param appends.
  std::vector<std::shared_ptr<IFormant>> formants;

  const char* type_name() const override { return "FormantSequence"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"spectra", true},  // multi-input: accepts multiple FormantSpectrum connections
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name != "spectra") { ValueSource::set_param(name, std::move(src)); return; }
    // Single-set: replace all with one (used by generic wiring path)
    auto fmt = std::dynamic_pointer_cast<IFormant>(src);
    if (fmt) { formants.clear(); formants.push_back(std::move(fmt)); }
  }

  void add_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name != "spectra") { ValueSource::add_param(name, std::move(src)); return; }
    auto fmt = std::dynamic_pointer_cast<IFormant>(src);
    if (fmt) formants.push_back(std::move(fmt));
  }

  void clear_param(std::string_view name) override {
    if (name == "spectra") formants.clear();
    else ValueSource::clear_param(name);
  }

  void on_prepare(const RenderContext& ctx, int frames) override { fmt_prepare(ctx, frames); }
  float compute() override { fmt_next(); return 0.0f; }

  void fmt_prepare(const RenderContext& ctx, int frames) override {
    blend.prepare(ctx, frames);
    for (auto& f : formants) f->fmt_prepare(ctx, frames);
  }

  void fmt_next() override {
    blend.advance();
    for (auto& f : formants) f->fmt_next();

    int n = int(formants.size());
    if (n < 2) { fPtr_ = 0; fWeight_ = 0.0f; return; }

    float b = std::clamp(blend.current(), 0.0f, 1.0f) * float(n - 1);
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
