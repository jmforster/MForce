#pragma once
// FormantSpectrum: a spectrum built from a list of IFormant parts.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# FormantSpectrum — collection, max-gain across formants.
// ---------------------------------------------------------------------------
struct FormantSpectrum : Component, IFormant {
  // The formants it spans: the JSON key "formants" is an array of refs.
  Slot<IFormant> formants{this, "formants", "an IFormant part: a Formant or a spectrum", true};

  const char* type_name() const override { return "FormantSpectrum"; }

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

} // namespace mforce
