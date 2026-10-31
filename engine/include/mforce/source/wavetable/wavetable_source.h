#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/source/noise/white_noise_source.h"
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
  // interpolating read and inputSource only while it fills the table, so
  // both are Manual; this node prepares them. `evolution` is a slot: the
  // holder it takes is driven through its interface, and the evolution the
  // holder carries is read whenever the slot changes.
  Param speedFactor{this, "speedFactor", 1.0f, 0.01f, 10.0f, "ratio", Pull::Manual};
  Input inputSource{this, "inputSource", Input::Optional, nullptr, false, Pull::Manual};
  Slot<IEvolutionHolder> evolution{this, "evolution", "an evolution holder"};
  Setting<bool> interpolate{this, "interpolate", false, false, true};

  explicit WavetableSource(uint32_t inputSeed = 0xC0FFEEu);

  // Evolution via graph: wire an IEvolutionHolder node into the `evolution` slot
  // Evolution via patch_loader: direct ownership for JSON-loaded patches
  void set_evolution(std::unique_ptr<WaveEvolution> evo) {
    ownedEvolution_ = std::move(evo);
    refresh_evolution();
  }

  const char* type_name() const override { return "WavetableSource"; }

  // The evolution read per sample: the holder's, or the one the loader's
  // string form owns. Refreshed the moment the slot changes.
  void on_slot_changed(std::string_view) override { refresh_evolution(); }

  void on_prepare(const RenderContext& ctx) override;
  void on_start(int frames) override;

protected:
  float wave() override;

private:
  void refresh_evolution() {
    auto* holder = evolution.get();
    evolution_ = holder ? holder->get_evolution() : ownedEvolution_.get();
  }
  void fill_table();
  float compute_raw();
  float compute_interpolated();

  std::unique_ptr<WaveEvolution>  ownedEvolution_;      // for patch_loader path
  WaveEvolution*                  evolution_{nullptr};  // raw ptr: into the holder the slot keeps alive, or ownedEvolution_
  std::vector<float>              values_;
  int                             maxLen_{0};   // values_ is reserved to this at prepare; fill_table never exceeds it
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
