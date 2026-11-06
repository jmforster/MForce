#pragma once
// FormantSequence: a sequence of spectra over the note.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

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
  Slot<IFormant> spectra{this, "spectra", "an IFormant part: a FormantSpectrum", true};

  const char* type_name() const override { return "FormantSequence"; }

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
