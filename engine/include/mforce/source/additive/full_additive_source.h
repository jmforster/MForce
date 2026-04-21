#pragma once
#include "mforce/core/dsp_wave_source.h"
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
      {"frequency",     440.0f, 0.01f, 20000.0f},
      {"amplitude",     1.0f,   0.0f,  10.0f},
      {"phase",         0.0f,  -1.0f,  1.0f},
      {"formantWeight", 0.0f,   0.0f,  1.0f},
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
    if (name == "partials")      return std::dynamic_pointer_cast<ValueSource>(partials_);
    return WaveSource::get_param(name);
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
};

} // namespace mforce
