#pragma once
#include "dsp_wave_source.h"
#include "wave_evolution.h"
#include "white_noise_source.h"
#include "randomizer.h"
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
