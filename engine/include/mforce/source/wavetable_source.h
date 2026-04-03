#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/source/wave_evolution.h"
#include "mforce/source/white_noise_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <vector>

namespace mforce {

// Ported from C# MForce.Sound.Source.WavetableSource
// On first sample: fills a table (Rate/CurrFreq entries) from an input source.
// Each sample: reads from table; if evolution is set, modifies table in-place.
// With PluckEvolution this implements Karplus-Strong plucked string synthesis.
struct WavetableSource final : WaveSource {
  explicit WavetableSource(int sampleRate, uint32_t inputSeed = 0xC0FFEEu);

  void set_input_source(std::shared_ptr<ValueSource> src) { inputSource_ = std::move(src); }
  void set_evolution(std::unique_ptr<WaveEvolution> evo) { evolution_ = std::move(evo); }
  void set_interpolate(bool interp) { interpolate_ = interp; }
  void set_speed_factor(std::shared_ptr<ValueSource> sf) { speedFactor_ = std::move(sf); }

  const char* type_name() const override { return "WavetableSource"; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",    440.0f, 0.01f, 20000.0f},
      {"amplitude",    1.0f,   0.0f,  10.0f},
      {"phase",        0.0f,  -1.0f,  1.0f},
      {"speedFactor",  1.0f,   0.01f, 10.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"inputSource"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "inputSource") { set_input_source(std::move(src)); return; }
    if (name == "speedFactor") { set_speed_factor(std::move(src)); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "inputSource") return inputSource_;
    if (name == "speedFactor") return speedFactor_;
    return WaveSource::get_param(name);
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"interpolate", ConfigType::Bool, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "interpolate") { set_interpolate(value != 0.0f); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "interpolate") return interpolate_ ? 1.0f : 0.0f;
    return 0.0f;
  }

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  void fill_table();
  float compute_raw();
  float compute_interpolated();

  std::shared_ptr<ValueSource>    inputSource_;
  std::shared_ptr<ValueSource>    speedFactor_;
  std::unique_ptr<WaveEvolution>  evolution_;
  std::vector<float>              values_;
  int   tablePtr2_{-1};
  bool  interpolate_{false};
};

} // namespace mforce
