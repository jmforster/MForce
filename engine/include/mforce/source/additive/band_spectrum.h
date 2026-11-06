#pragma once
// BandSpectrum: a spectrum made of frequency bands.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

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

} // namespace mforce
