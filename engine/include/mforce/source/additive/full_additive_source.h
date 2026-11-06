#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/band_spectrum.h"
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
  // while a formant is wired, so both are Manual: the node prepares them and
  // advances them in wave(). `formant` and `partials` are slots — parts the
  // node drives through their interfaces, type-checked when wired.
  Param formantWeight{this, "formantWeight", 0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param formantFloor {this, "formantFloor",  1.0f, 0.0f, 1.0f, "0-1", Pull::Manual};  // out-of-band suppression, 1.0 = no cut
  Slot<IFormant> formant{this, "formant", "an IFormant part"};
  Slot<IPartials> partials{this, "partials", "an IPartials part"};

  // --- NoiseBed configs (scalar so paramMap frequency-curves can drive them
  // per register — e.g. the clarinet noise-lead law 0.15/0.10/0.04 s by f0
  // is expressed as a patch-side curve on noiseBedDelay, never hardcoded).
  Setting<float> noiseBedLevel  {this, "noiseBedLevel",   0.0f,    0.0f,   1.0f};      // linear, rel tone; 0 = inert
  Setting<float> noiseBedFreq   {this, "noiseBedFreq",    2900.0f, 20.0f,  16000.0f};  // bandpass center Hz
  Setting<float> noiseBedWidth  {this, "noiseBedWidth",   1500.0f, 10.0f,  12000.0f};  // bandpass width Hz (Q = freq/width)
  Setting<float> noiseBedDelay  {this, "noiseBedDelay",   0.0f,    0.0f,   2.0f};      // TONE delay sec (noise runs from t=0)
  Setting<float> noiseBedFadePow{this, "noiseBedFadePow", 1.0f,    0.25f,  8.0f};
  Setting<float> noiseBedFade   {this, "noiseBedFade",    0.05f,   0.001f, 2.0f};      // tone fade-in sec after the delay

  explicit FullAdditiveSource(uint32_t seed = 0xADD2'0000u);

  const char* type_name() const override { return "AdditiveSource"; }

  void on_prepare(const RenderContext& ctx) override;
  void on_start(int frames) override;

protected:
  float wave() override;

private:
  // --- NoiseBed (clarinet-analysis breath bed; inert at noiseBedLevel 0) ---
  // v1 simplification of the report's "formant-shaped" bed: the measured
  // residual IS band-passed hiss (centroid ~2.9 kHz, band ~1.5-4 kHz), so a
  // single 2nd-order bandpass at (noiseBedFreq, Q = freq/width) stands in for
  // sharing the formant/BandSpectrum path. Deviation is deliberate; revisit
  // if the bed should track a patch's formant stack.
  Randomizer noiseRng_;  // seeded from the AdditiveSource seed (reproducible)

  // Per-note state (set in start)
  float bedB0_{0.0f}, bedB2_{0.0f}, bedA1_{0.0f}, bedA2_{0.0f};  // RBJ bandpass (b1 = 0)
  float bedZ1_{0.0f}, bedZ2_{0.0f};                              // transposed DF2 state
  float bedDelaySamples_{0.0f};
  float bedFadeSamples_{1.0f};
};

} // namespace mforce
