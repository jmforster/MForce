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
  explicit FullAdditiveSource(int sampleRate, uint32_t seed = 0xADD2'0000u);

  const char* type_name() const override { return "AdditiveSource"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",     440.0f, 0.01f, 20000.0f, "hz"},
      {"amplitude",     1.0f,   0.0f,  10.0f,    "0-1"},
      {"phase",         0.0f,  -1.0f,  1.0f,     "cycles"},
      {"formantWeight", 0.0f,   0.0f,  1.0f,     "0-1"},
      {"formantFloor",  1.0f,   0.0f,  1.0f,     "0-1"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"formant"},
      {"partials"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "formant") {
      auto fmt = std::dynamic_pointer_cast<IFormant>(src);
      if (fmt) formant_ = std::move(fmt);
      return;
    }
    if (name == "formantWeight") { formantWeight_ = std::move(src); return; }
    if (name == "formantFloor")  { formantFloor_ = std::move(src); return; }
    if (name == "partials") {
      auto p = std::dynamic_pointer_cast<IPartials>(src);
      if (p) partials_ = std::move(p);
      return;
    }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "formant")       return std::dynamic_pointer_cast<ValueSource>(formant_);
    if (name == "formantWeight") return formantWeight_;
    if (name == "formantFloor")  return formantFloor_;
    if (name == "partials")      return std::dynamic_pointer_cast<ValueSource>(partials_);
    return WaveSource::get_param(name);
  }

  // --- NoiseBed configs (scalar so paramMap frequency-curves can drive them
  // per register — e.g. the clarinet noise-lead law 0.15/0.10/0.04 s by f0
  // is expressed as a patch-side curve on noiseBedDelay, never hardcoded).
  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"noiseBedLevel", ConfigType::Float, 0.0f,    0.0f,   1.0f},     // linear, rel tone; 0 = inert
      {"noiseBedFreq",  ConfigType::Float, 2900.0f, 20.0f,  16000.0f}, // bandpass center Hz
      {"noiseBedWidth", ConfigType::Float, 1500.0f, 10.0f,  12000.0f}, // bandpass width Hz (Q = freq/width)
      {"noiseBedDelay", ConfigType::Float, 0.0f,    0.0f,   2.0f},     // TONE delay sec (noise runs from t=0)
      {"noiseBedFade",  ConfigType::Float, 0.05f,   0.001f, 2.0f},     // tone fade-in sec after the delay
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "noiseBedLevel") { noiseBedLevel_ = value; return; }
    if (name == "noiseBedFreq")  { noiseBedFreq_  = value; return; }
    if (name == "noiseBedWidth") { noiseBedWidth_ = value; return; }
    if (name == "noiseBedDelay") { noiseBedDelay_ = value; return; }
    if (name == "noiseBedFade")  { noiseBedFade_  = value; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "noiseBedLevel") return noiseBedLevel_;
    if (name == "noiseBedFreq")  return noiseBedFreq_;
    if (name == "noiseBedWidth") return noiseBedWidth_;
    if (name == "noiseBedDelay") return noiseBedDelay_;
    if (name == "noiseBedFade")  return noiseBedFade_;
    return 0.0f;
  }

  // --- Formant ---
  void set_formant(std::shared_ptr<IFormant> f, std::shared_ptr<ValueSource> weight) {
    formant_ = std::move(f); formantWeight_ = std::move(weight);
  }

  // --- Partials ---
  void set_partials(std::shared_ptr<IPartials> p) { partials_ = std::move(p); }
  std::shared_ptr<IPartials> get_partials() const { return partials_; }

  void prepare(const RenderContext& ctx, int frames) override;

protected:
  float compute_wave_value() override;

private:
  // Partials (required — does all the per-partial math)
  std::shared_ptr<IPartials> partials_;

  // Formant (optional)
  std::shared_ptr<IFormant> formant_;
  std::shared_ptr<ValueSource> formantWeight_;
  std::shared_ptr<ValueSource> formantFloor_;  // out-of-band suppression, default 1.0 (= no cut)

  // --- NoiseBed (clarinet-analysis breath bed; inert at noiseBedLevel 0) ---
  // v1 simplification of the report's "formant-shaped" bed: the measured
  // residual IS band-passed hiss (centroid ~2.9 kHz, band ~1.5-4 kHz), so a
  // single 2nd-order bandpass at (noiseBedFreq, Q = freq/width) stands in for
  // sharing the formant/BandSpectrum path. Deviation is deliberate; revisit
  // if the bed should track a patch's formant stack.
  float noiseBedLevel_{0.0f};
  float noiseBedFreq_{2900.0f};
  float noiseBedWidth_{1500.0f};
  float noiseBedDelay_{0.0f};
  float noiseBedFade_{0.05f};

  Randomizer noiseRng_;  // seeded from the AdditiveSource seed (reproducible)

  // Per-note state (set in prepare)
  float bedB0_{0.0f}, bedB2_{0.0f}, bedA1_{0.0f}, bedA2_{0.0f};  // RBJ bandpass (b1 = 0)
  float bedZ1_{0.0f}, bedZ2_{0.0f};                              // transposed DF2 state
  float bedDelaySamples_{0.0f};
  float bedFadeSamples_{1.0f};
};

} // namespace mforce
