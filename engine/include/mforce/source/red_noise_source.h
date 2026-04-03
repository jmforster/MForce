#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/core/smoothness_interpolator.h"
#include <memory>

namespace mforce {

struct RedNoiseSource final : WaveSource {
  explicit RedNoiseSource(int sampleRate, uint32_t seed = 0xC0FFEEu);

  void set_density(std::shared_ptr<ValueSource> s)           { density_ = std::move(s); }
  void set_smoothness(std::shared_ptr<ValueSource> s)        { smoothness_ = std::move(s); }
  void set_rampVariation(std::shared_ptr<ValueSource> s)     { rampVariation_ = std::move(s); }
  void set_boost(std::shared_ptr<ValueSource> s)             { boost_ = std::move(s); }
  void set_continuity(std::shared_ptr<ValueSource> s)        { continuity_ = std::move(s); }
  void set_zeroCrossTendency(std::shared_ptr<ValueSource> s) { zeroCrossTendency_ = std::move(s); }

  std::shared_ptr<ValueSource> get_density() const           { return density_; }
  std::shared_ptr<ValueSource> get_smoothness() const        { return smoothness_; }
  std::shared_ptr<ValueSource> get_rampVariation() const     { return rampVariation_; }
  std::shared_ptr<ValueSource> get_boost() const             { return boost_; }
  std::shared_ptr<ValueSource> get_continuity() const        { return continuity_; }
  std::shared_ptr<ValueSource> get_zeroCrossTendency() const { return zeroCrossTendency_; }

  const char* type_name() const override { return "RedNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",          440.0f,  0.01f, 20000.0f},
      {"amplitude",          1.0f,    0.0f,  10.0f},
      {"phase",              0.0f,   -1.0f,  1.0f},
      {"density",            0.5f,    0.0f,  1.0f},
      {"smoothness",         0.5f,    0.0f,  1.0f},
      {"rampVariation",      0.0f,    0.0f,  1.0f},
      {"boost",              0.0f,    0.0f,  1.0f},
      {"continuity",         0.0f,    0.0f,  1.0f},
      {"zeroCrossTendency",  0.0f,    0.0f,  1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "density")           { density_ = std::move(src); return; }
    if (name == "smoothness")        { smoothness_ = std::move(src); return; }
    if (name == "rampVariation")     { rampVariation_ = std::move(src); return; }
    if (name == "boost")             { boost_ = std::move(src); return; }
    if (name == "continuity")        { continuity_ = std::move(src); return; }
    if (name == "zeroCrossTendency") { zeroCrossTendency_ = std::move(src); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "density")           return density_;
    if (name == "smoothness")        return smoothness_;
    if (name == "rampVariation")     return rampVariation_;
    if (name == "boost")             return boost_;
    if (name == "continuity")        return continuity_;
    if (name == "zeroCrossTendency") return zeroCrossTendency_;
    return WaveSource::get_param(name);
  }

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  std::shared_ptr<ValueSource> density_;
  std::shared_ptr<ValueSource> smoothness_;
  std::shared_ptr<ValueSource> rampVariation_;
  std::shared_ptr<ValueSource> boost_;
  std::shared_ptr<ValueSource> continuity_;
  std::shared_ptr<ValueSource> zeroCrossTendency_;
  int   rampSize_{1};
  int   sampleCount_{0};
  float lastValue_{0.0f};
  float lastSign_{-1.0f};     // legacy starts non-zero to “get going”
  float nextValue_{0.0f};
  bool  zeroRamp_{false};

  Randomizer rng_;
  SmoothnessInterpolator interp_;
};

} // namespace mforce
