#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/source/white_noise_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <vector>
#include <cmath>

namespace mforce {

// Hybrid KS → Additive source.
//
// Phase 1 (attack): standard Karplus-Strong wavetable synthesis.
//   Organic, noisy attack with natural decay character.
//
// Transition: at the end of phase 1, perform a per-harmonic DFT on the
//   KS table to extract the amplitude and phase of each partial.
//
// Phase 2 (sustain): additive synthesis initialized with the extracted
//   amplitudes/phases. Partial amplitudes evolve toward the target spectrum.
//   Phases continue naturally — no discontinuity at the switch.
//
// This is NOT a crossfade — phase 1 output is purely KS, phase 2 output
// is purely additive. The additive partials start where KS left off.
struct HybridKSSource final : WaveSource {
  // After WaveSource's three. The seed waveform is advanced only while a
  // note's table is filled, so inputSource is Manual; this node prepares it.
  Input inputSource{this, "inputSource", Input::Optional, nullptr, false, Pull::Manual};
  Setting<int>   holdCycles   {this, "holdCycles",    5,    1,     50};
  Setting<float> morphDuration{this, "morphDuration", 0.5f, 0.01f, 5.0f};
  Setting<int>   numPartials  {this, "numPartials",   30,   1,     200};

  explicit HybridKSSource(uint32_t seed = 0xBEEF'C0DEu);

  // How many targets the node holds (at least numPartials once prepared); a
  // diagnostic for tests, like tracks_frequency_live().
  int target_count() const { return int(targetAmpl_.size()); }
  void set_target_partials(std::vector<float> t) {
    targetAmpl_ = std::move(t);
    if (prepared()) size_targets();
  }

  const char* type_name() const override { return "HybridKSSource"; }

  void on_prepare(const RenderContext& ctx) override;
  void on_start(int frames) override;
  void on_setting_changed(std::string_view name) override;

protected:
  float wave() override;

private:
  void fill_table();
  void size_targets();
  void ks_evolve(int index);
  void extract_partials();   // DFT at transition point
  float compute_ks();
  float compute_additive();

  Randomizer rng_;

  std::vector<float> targetAmpl_;

  // KS state
  std::vector<float> table_;
  int tablePtr_{-1};
  int sampleInCycle_{0};
  int cycleCount_{0};
  bool inAdditive_{false};

  // Additive state (initialized from KS DFT at transition)
  std::vector<float> partialAmpl_;   // current amplitudes (evolving)
  std::vector<float> partialPhase_;  // current phases (advancing)
  std::vector<float> startAmpl_;     // amplitudes at transition
  int morphSamples_{0};
  int additiveSample_{0};
};

} // namespace mforce
