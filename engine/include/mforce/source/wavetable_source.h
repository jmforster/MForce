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
  // Evolution via graph: wire an IEvolutionHolder node to the "evolution" input
  // Evolution via patch_loader: direct ownership for JSON-loaded patches
  void set_evolution(std::unique_ptr<WaveEvolution> evo) {
    ownedEvolution_ = std::move(evo);
    evolution_ = ownedEvolution_.get();
  }
  void set_interpolate(bool interp) { interpolate_ = interp; }
  void set_speed_factor(std::shared_ptr<ValueSource> sf) { speedFactor_ = std::move(sf); }

  const char* type_name() const override { return "WavetableSource"; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",    440.0f, 0.01f, 20000.0f, "hz"},
      {"amplitude",    1.0f,   0.0f,  10.0f,    "0-1"},
      {"phase",        0.0f,  -1.0f,  1.0f,     "cycles"},
      {"speedFactor",  1.0f,   0.01f, 10.0f,    "ratio"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"inputSource"},
      {"evolution"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "inputSource") { set_input_source(std::move(src)); return; }
    if (name == "speedFactor") { set_speed_factor(std::move(src)); return; }
    if (name == "evolution") {
      evolutionSrc_ = std::move(src);
      auto* holder = dynamic_cast<IEvolutionHolder*>(evolutionSrc_.get());
      evolution_ = holder ? holder->get_evolution() : nullptr;
      return;
    }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "inputSource") return inputSource_;
    if (name == "speedFactor") return speedFactor_;
    if (name == "evolution") return evolutionSrc_;
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

  void prepare(const RenderContext& ctx, int frames) override;

protected:
  float compute_wave_value() override;

private:
  void fill_table();
  float compute_raw();
  float compute_interpolated();

  std::shared_ptr<ValueSource>    inputSource_;
  std::shared_ptr<ValueSource>    speedFactor_;
  std::shared_ptr<ValueSource>    evolutionSrc_;  // keeps the evolution node alive
  std::unique_ptr<WaveEvolution>  ownedEvolution_;      // for patch_loader path
  WaveEvolution*                  evolution_{nullptr};  // raw ptr (from holder or owned)
  std::vector<float>              values_;
  int   tablePtr2_{-1};
  bool  interpolate_{false};
  // Tuning allpass state
  float tuningCoeff_{0.0f};
  float tuningState_{0.0f};
  float tuningPrevIn_{0.0f};
  // Pitch-bend support: readout is a fractional head that advances by
  // currFreq_/baseFreq_ per sample. baseFreq_ is captured at sample 0 and
  // locked for the note — it defines the ring buffer length and evolution
  // timebase. The loop itself (values_ contents + evolution + tuning allpass)
  // runs at base pitch; the bend is a resample on read. Adequate for
  // musical bends (≤ few semitones); breaks down for extreme excursions.
  float baseFreq_{0.0f};
  float readPos_{-1.0f};
};

} // namespace mforce
