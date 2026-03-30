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
  explicit HybridKSSource(int sampleRate, uint32_t seed = 0xBEEF'C0DEu);

  void set_input_source(std::shared_ptr<ValueSource> src) { inputSource_ = std::move(src); }
  void set_hold_cycles(int n) { holdCycles_ = n; }
  void set_morph_duration(float seconds) { morphSeconds_ = seconds; }
  void set_target_partials(std::vector<float> t) { targetAmpl_ = std::move(t); }
  void set_num_partials(int n) { numPartials_ = n; }

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  void fill_table();
  void ks_evolve(int index);
  void extract_partials();   // DFT at transition point
  float compute_ks();
  float compute_additive();

  std::shared_ptr<ValueSource> inputSource_;
  Randomizer rng_;

  int holdCycles_{5};
  float morphSeconds_{0.5f};
  int numPartials_{30};
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
