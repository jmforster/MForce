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
// A part: the additive source that holds one drives it — prepare once per
// build, start once per note, next once per sample — and reads its gain per
// partial. A formant prepares, starts and pulls its own pins in those calls.
struct IFormant {
  virtual ~IFormant() = default;
  virtual void prepare(const RenderContext& ctx) = 0;
  virtual void start(int frames) = 0;
  virtual void next() = 0;
  virtual bool contains(float freq) const = 0;
  virtual float get_gain(float freq) const = 0;
};

// ---------------------------------------------------------------------------
// Ported from C# Formant — spectral peak with evolving params.
// ---------------------------------------------------------------------------
struct Formant final : Component, IFormant {
  // The owner drives a Formant through prepare(), start() and next(); the
  // four pins are Manual and this part prepares, starts and pulls them there.
  Param frequency{this, "frequency", 1000.0f, 1.0f,  20000.0f, "hz",  Pull::Manual};
  Param gain     {this, "gain",      1.0f,    0.0f,  10.0f,    "0-1", Pull::Manual};
  Param width    {this, "width",     500.0f,  1.0f,  10000.0f, "hz",  Pull::Manual};
  Param power    {this, "power",     2.0f,    0.01f, 10.0f,    "exp", Pull::Manual};

  const char* type_name() const override { return "Formant"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& ctx) override {
    frequency.prepare(ctx);
    gain.prepare(ctx);
    width.prepare(ctx);
    power.prepare(ctx);
  }

  void start(int frames) override {
    frequency.start(frames);
    gain.start(frames);
    width.start(frames);
    power.start(frames);
  }

  void next() override {
    frequency.next(); gain.next(); width.next(); power.next();
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
struct FormantSpectrum : Component, IFormant {
  // The formants it spans: the JSON key "formants" is an array of refs.
  Slot<IFormant> formants{this, "formants", nullptr, true};

  const char* type_name() const override { return "FormantSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& ctx) override {
    for (auto* f : formants.views) f->prepare(ctx);
  }

  void start(int frames) override {
    for (auto* f : formants.views) f->start(frames);
  }

  void next() override {
    for (auto* f : formants.views) f->next();
  }

  bool contains(float freq) const override {
    for (auto* f : formants.views)
      if (f->contains(freq)) return true;
    return false;
  }

  float get_gain(float freq) const override {
    float g = 0.0f;
    for (auto* f : formants.views)
      if (f->contains(freq))
        g = std::max(g, f->get_gain(freq));
    return g;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# FixedSpectrum — full gain array indexed by integer frequency.
// ---------------------------------------------------------------------------
struct FixedSpectrum final : Component, IFormant {
  Array gains{this, "gains", nullptr, 0.0f, 0.0f, 1.0f};

  explicit FixedSpectrum(std::vector<float> values) { gains.values = std::move(values); }

  const char* type_name() const override { return "FixedSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& /*ctx*/) override {}
  void start(int /*frames*/) override {}
  void next() override {}

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
struct BandSpectrum final : Component, IFormant {
  // The owner drives it through prepare(), start() and next(); both pins are
  // Manual and this part prepares, starts and pulls them there.
  Param startFreq    {this, "startFreq",     0.0f, 0.0f,  20000.0f, nullptr, Pull::Manual};
  Param freqIncrement{this, "freqIncrement", 1.0f, 0.01f, 1000.0f,  nullptr, Pull::Manual};
  Array gains{this, "gains", nullptr, 0.0f, 0.0f, 1.0f};

  const char* type_name() const override { return "BandSpectrum"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& ctx) override {
    startFreq.prepare(ctx);
    freqIncrement.prepare(ctx);
  }

  void start(int frames) override {
    startFreq.start(frames);
    freqIncrement.start(frames);
  }

  void next() override {
    startFreq.next();
    freqIncrement.next();
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
struct FormantSequence final : Component, IFormant {
  // The owner drives it through prepare(), start() and next(); `blend` is
  // Manual and this part prepares, starts and pulls it there.
  Param blend{this, "blend", 0.0f, 0.0f, 1.0f, nullptr, Pull::Manual};
  // The spectra it crossfades through, in wiring order.
  Slot<IFormant> spectra{this, "spectra", nullptr, true};

  const char* type_name() const override { return "FormantSequence"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void prepare(const RenderContext& ctx) override {
    blend.prepare(ctx);
    for (auto* f : spectra.views) f->prepare(ctx);
  }

  void start(int frames) override {
    blend.start(frames);
    for (auto* f : spectra.views) f->start(frames);
  }

  void next() override {
    blend.next();
    for (auto* f : spectra.views) f->next();

    int n = int(spectra.views.size());
    if (n < 2) { fPtr_ = 0; fWeight_ = 0.0f; return; }

    float b = std::clamp(blend.current(), 0.0f, 1.0f) * float(n - 1);
    fPtr_ = std::min(int(b), n - 2);
    fWeight_ = b - float(fPtr_);
  }

  bool contains(float freq) const override {
    const auto& f = spectra.views;
    if (f.empty()) return false;
    int n = int(f.size());
    int hi = std::min(fPtr_ + 1, n - 1);
    return f[fPtr_]->contains(freq) || f[hi]->contains(freq);
  }

  float get_gain(float freq) const override {
    const auto& f = spectra.views;
    if (f.empty()) return 0.0f;
    int n = int(f.size());
    int hi = std::min(fPtr_ + 1, n - 1);

    float g0 = f[fPtr_]->contains(freq) ? f[fPtr_]->get_gain(freq) : 0.0f;
    float g1 = f[hi]->contains(freq)    ? f[hi]->get_gain(freq)    : 0.0f;

    return g0 * (1.0f - fWeight_) + g1 * fWeight_;
  }

private:
  int fPtr_{0};
  float fWeight_{0.0f};
};

} // namespace mforce
