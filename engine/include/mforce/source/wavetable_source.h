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
  // After WaveSource's three. The wavetable advances speedFactor only in the
  // interpolating read and inputSource only while it fills the table, and
  // never advances `evolution`, so all three are Manual; this node prepares
  // them. `evolution` is a structural input: the wire is type-checked in
  // set_param, which keeps a typed pointer to the evolution it holds.
  Param speedFactor{this, "speedFactor", 1.0f, 0.01f, 10.0f, "ratio", Pull::Manual};
  Input inputSource{this, "inputSource", Input::Optional, nullptr, false, Pull::Manual};
  Input evolution  {this, "evolution",   Input::Optional, nullptr, false, Pull::Manual};
  Setting<bool> interpolate{this, "interpolate", false, false, true};

  explicit WavetableSource(int sampleRate, uint32_t inputSeed = 0xC0FFEEu);

  // Evolution via graph: wire an IEvolutionHolder node to the "evolution" input
  // Evolution via patch_loader: direct ownership for JSON-loaded patches
  void set_evolution(std::unique_ptr<WaveEvolution> evo) {
    ownedEvolution_ = std::move(evo);
    evolution_ = ownedEvolution_.get();
  }

  const char* type_name() const override { return "WavetableSource"; }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    WaveSource::set_param(name, std::move(src));
    if (name == "evolution") {
      auto* holder = dynamic_cast<IEvolutionHolder*>(evolution.source.get());
      evolution_ = holder ? holder->get_evolution() : nullptr;
    }
  }

  void on_prepare(const RenderContext& ctx, int frames) override;

protected:
  float wave() override;

private:
  void fill_table();
  float compute_raw();
  float compute_interpolated();

  std::unique_ptr<WaveEvolution>  ownedEvolution_;      // for patch_loader path
  WaveEvolution*                  evolution_{nullptr};  // raw ptr: into the holder `evolution` keeps alive, or ownedEvolution_
  std::vector<float>              values_;
  int   tablePtr2_{-1};
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
