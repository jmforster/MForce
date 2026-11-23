#pragma once
// FormantSequence: a crossfade through formants over the note.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// FormantSequence — a crossfade between formants over time, driven by a
// blend signal: 0 is the first, 1 the last, and the values between morph
// through the list evenly. Any Formant may be a member. Ported from C#
// FormantSequence.
// ---------------------------------------------------------------------------
struct FormantSequence final : Formant {
  // The owner drives `blend` through the base; it is Manual.
  Param blend{this, "blend", 0.0f, 0.0f, 1.0f, nullptr, Pull::Manual};
  // The formants it crossfades through, in wiring order.
  Slot<Formant> spectra{this, "spectra", "a Formant part", true};

  const char* type_name() const override { return "FormantSequence"; }

  float gain(float freq) const override {
    const auto& f = spectra.views;
    if (f.empty()) return 0.0f;
    int n = int(f.size());
    int hi = std::min(fPtr_ + 1, n - 1);

    float g0 = f[fPtr_]->gain(freq);
    float g1 = f[hi]->gain(freq);

    return g0 * (1.0f - fWeight_) + g1 * fWeight_;
  }

protected:
  // The hooks: after the base has prepared, started or advanced `blend`,
  // the members, then the two neighbours and the weight between them.
  void on_prepare(const RenderContext& ctx) override {
    for (auto* f : spectra.views) f->prepare(ctx);
  }

  void on_start(int frames) override {
    for (auto* f : spectra.views) f->start(frames);
  }

  void on_next() override {
    for (auto* f : spectra.views) f->next();

    int n = int(spectra.views.size());
    if (n < 2) { fPtr_ = 0; fWeight_ = 0.0f; return; }

    float b = std::clamp(blend.current(), 0.0f, 1.0f) * float(n - 1);
    fPtr_ = std::min(int(b), n - 2);
    fWeight_ = b - float(fPtr_);
  }

private:
  int fPtr_{0};
  float fWeight_{0.0f};
};

} // namespace mforce
