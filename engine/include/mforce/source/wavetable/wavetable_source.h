#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/dsp/tuning_allpass.h"
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/source/noise/white_noise_source.h"
#include "mforce/core/randomizer.h"
#include <vector>

namespace mforce {

// Ported from C# MForce.Sound.Source.WavetableSource
// On first sample: fills a table (Rate/CurrFreq entries) from an input source.
// Each sample: reads from table; if an evolution is wired, it modifies the
// table in-place. With PluckEvolution this implements Karplus-Strong plucked
// string synthesis.
struct WavetableSource final : WaveSource {
  // After WaveSource's three. The wavetable advances speedFactor only in the
  // interpolating read and inputSource only while it fills the table, so
  // both are Manual; this node prepares them. `evolution` is a slot: the
  // evolution it takes is driven through the family's base — prepared with
  // the table's reserve, started per note, adjusted at a note's first
  // sample, evolved once per sample.
  Param speedFactor{this, "speedFactor", 1.0f, 0.01f, 10.0f, "ratio", Pull::Manual};
  Input inputSource{this, "inputSource", Input::Optional, nullptr, false, Pull::Manual};
  Slot<WaveEvolution> evolution{this, "evolution", "an evolution"};
  Setting<bool> interpolate{this, "interpolate", false, false, true};

  explicit WavetableSource(uint32_t inputSeed = 0xC0FFEEu);

  const char* type_name() const override { return "WavetableSource"; }

  void on_prepare(const RenderContext& ctx) override;
  void on_start(int frames) override;

protected:
  float wave() override;

private:
  void fill_table();
  float compute_raw();
  float compute_interpolated();

  std::vector<float>              values_;
  int                             maxLen_{0};   // values_ is reserved to this at prepare; fill_table never exceeds it
  int   tablePtr2_{-1};
  dsp::TuningAllpass tuning_;   // the fractional part of the loop length
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
