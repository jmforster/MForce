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

} // namespace mforce
