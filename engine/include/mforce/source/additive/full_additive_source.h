#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/partials.h"
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// AdditiveSource — thin source that sums partials via IPartials interface.
// Ported from legacy AdditiveSource.cs (the thin loop over Partials).
//
// All per-partial rendering (envelopes, rolloff, detune, expand) lives in
// the Partials object. This source just owns frequency/amplitude/phase,
// an optional formant, and loops over partials->get_partial_value().
//
// The class name is FullAdditiveSource for file/header backward compat,
// but type_name() returns "AdditiveSource" and the old "FullAdditiveSource"
// name is kept as an alias in the registry.
// ---------------------------------------------------------------------------
struct FullAdditiveSource final : WaveSource {
  // After WaveSource's three. The formant weight and floor are read only
  // while a formant is wired, and `formant` and `partials` are structural
  // inputs the node calls into rather than values, so all four are Manual:
  // the node prepares them and advances them in wave(), in this order. The
  // two structural wires are type-checked in set_param, which keeps a typed
  // view of the part it holds.
  Param formantWeight{this, "formantWeight", 0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param formantFloor {this, "formantFloor",  1.0f, 0.0f, 1.0f, "0-1", Pull::Manual};  // out-of-band suppression, 1.0 = no cut
  Input formant {this, "formant",  Input::Optional, nullptr, false, Pull::Manual};
  Input partials{this, "partials", Input::Optional, nullptr, false, Pull::Manual};

  // --- NoiseBed configs (scalar so paramMap frequency-curves can drive them
  // per register — e.g. the clarinet noise-lead law 0.15/0.10/0.04 s by f0
  // is expressed as a patch-side curve on noiseBedDelay, never hardcoded).
  Setting<float> noiseBedLevel  {this, "noiseBedLevel",   0.0f,    0.0f,   1.0f};      // linear, rel tone; 0 = inert
  Setting<float> noiseBedFreq   {this, "noiseBedFreq",    2900.0f, 20.0f,  16000.0f};  // bandpass center Hz
  Setting<float> noiseBedWidth  {this, "noiseBedWidth",   1500.0f, 10.0f,  12000.0f};  // bandpass width Hz (Q = freq/width)
  Setting<float> noiseBedDelay  {this, "noiseBedDelay",   0.0f,    0.0f,   2.0f};      // TONE delay sec (noise runs from t=0)
  Setting<float> noiseBedFadePow{this, "noiseBedFadePow", 1.0f,    0.25f,  8.0f};
  Setting<float> noiseBedFade   {this, "noiseBedFade",    0.05f,   0.001f, 2.0f};      // tone fade-in sec after the delay

  explicit FullAdditiveSource(int sampleRate, uint32_t seed = 0xADD2'0000u);

  const char* type_name() const override { return "AdditiveSource"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  // Wiring `formant` or `partials` takes the part's interface; a source that
  // does not implement it is ignored.
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "formant") {
      if (auto f = std::dynamic_pointer_cast<IFormant>(src)) { formant_ = std::move(f); formant.set(std::move(src)); }
      return;
    }
    if (name == "partials") {
      if (auto p = std::dynamic_pointer_cast<IPartials>(src)) { partials_ = std::move(p); partials.set(std::move(src)); }
      return;
    }
    WaveSource::set_param(name, std::move(src));
  }

  void on_prepare(const RenderContext& ctx, int frames) override;

protected:
  float wave() override;

private:
  // The parts the `partials` and `formant` inputs hold, as the interfaces the
  // node calls. Partials do all the per-partial math and are required; a
  // formant is optional.
  std::shared_ptr<IPartials> partials_;
  std::shared_ptr<IFormant>  formant_;

  // --- NoiseBed (clarinet-analysis breath bed; inert at noiseBedLevel 0) ---
  // v1 simplification of the report's "formant-shaped" bed: the measured
  // residual IS band-passed hiss (centroid ~2.9 kHz, band ~1.5-4 kHz), so a
  // single 2nd-order bandpass at (noiseBedFreq, Q = freq/width) stands in for
  // sharing the formant/BandSpectrum path. Deviation is deliberate; revisit
  // if the bed should track a patch's formant stack.
  Randomizer noiseRng_;  // seeded from the AdditiveSource seed (reproducible)

  // Per-note state (set in prepare)
  float bedB0_{0.0f}, bedB2_{0.0f}, bedA1_{0.0f}, bedA2_{0.0f};  // RBJ bandpass (b1 = 0)
  float bedZ1_{0.0f}, bedZ2_{0.0f};                              // transposed DF2 state
  float bedDelaySamples_{0.0f};
  float bedFadeSamples_{1.0f};
};

} // namespace mforce
