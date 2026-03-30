#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/source/additive/formant.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <vector>

namespace mforce {

// Ported from C# AdditiveSource + Partials + FullPartials/ExplicitPartials.
//
// Sum of sine partials with per-partial evolution via 5 envelope dimensions:
//   multEnv:  interpolates multipliers between mult1[i] and mult2[i]
//   amplEnv:  interpolates amplitudes between ampl1[i] and ampl2[i]
//   poEnv:    interpolates phase offsets between po1[i] and po2[i]
//   roEnv:    interpolates rolloff between ro1 and ro2
//   dtEnv:    interpolates detune between dt1 and dt2
//
// Modes:
//   init_full_partials():     integer harmonics with even/odd weight control
//   init_sequence_partials(): linear multiplier sequences with evolving spacing
//   init_explicit_partials(): user-specified multiplier and amplitude arrays
struct FullAdditiveSource final : WaveSource {
  explicit FullAdditiveSource(int sampleRate, uint32_t seed = 0xADD2'0000u);

  // --- Envelope sources (drive evolution between "1" and "2" values) ---
  void set_mult_env(std::shared_ptr<ValueSource> v) { multEnv_ = std::move(v); }
  void set_ampl_env(std::shared_ptr<ValueSource> v) { amplEnv_ = std::move(v); }
  void set_po_env(std::shared_ptr<ValueSource> v)   { poEnv_ = std::move(v); }
  void set_ro_env(std::shared_ptr<ValueSource> v)   { roEnv_ = std::move(v); }
  void set_dt_env(std::shared_ptr<ValueSource> v)   { dtEnv_ = std::move(v); }

  // --- Global params ---
  void set_ro(float ro1, float ro2) { ro1_ = ro1; ro2_ = ro2; }
  void set_dt(float dt1, float dt2) { dt1_ = dt1; dt2_ = dt2; }

  // --- Formant (optional — any IFormant: FormantSpectrum, FixedSpectrum, etc.) ---
  void set_formant(std::shared_ptr<IFormant> f, std::shared_ptr<ValueSource> weight) {
    formant_ = std::move(f); formantWeight_ = std::move(weight);
  }

  // --- Mode: FullPartials (integer harmonics) ---
  void init_full_partials(int maxPartials, int minMult,
                          float ew1, float ew2, float ow1, float ow2,
                          float unitPO1, float unitPO2);

  // --- Mode: SequencePartials (linear multiplier sequences) ---
  void init_sequence_partials(int maxPartials,
                              float minMult1, float minMult2,
                              float incr1, float incr2,
                              float unitPO1, float unitPO2);

  // --- Mode: ExplicitPartials (user arrays) ---
  void init_explicit_partials(std::vector<float> mult1, std::vector<float> mult2,
                              std::vector<float> ampl1, std::vector<float> ampl2,
                              float unitPO1, float unitPO2);

  // --- Expand rule (optional, applied after init_*) ---
  struct ExpandRule {
    int count{2};          // sub-partials per side
    int recurse{0};        // how many times to re-expand
    float spacing1{0.5f}, spacing2{0.5f};  // semitone spacing (start/end)
    float dt1{0.01f}, dt2{0.01f};          // random detune on sub-partials
    float loPct1{0.1f}, loPct2{0.1f};      // minimum amplitude fraction
    float power1{1.0f}, power2{1.0f};      // amplitude curve power
    float po1{0.0f}, po2{0.0f};            // phase offset for sub-partials
  };
  void set_expand_rule(ExpandRule rule) { expandRule_ = rule; hasExpand_ = true; }

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  void init_detune_values();
  void apply_expand_rule();
  float get_partial_value(float amplitude, float frequency, float phaseDiff,
                          int index, float amplE, float multE, float poE, float roE, float dtE,
                          float fmtWt);

  Randomizer rng_;

  // Envelopes
  std::shared_ptr<ValueSource> multEnv_;
  std::shared_ptr<ValueSource> amplEnv_;
  std::shared_ptr<ValueSource> poEnv_;
  std::shared_ptr<ValueSource> roEnv_;
  std::shared_ptr<ValueSource> dtEnv_;

  // Formant
  std::shared_ptr<IFormant> formant_;
  std::shared_ptr<ValueSource> formantWeight_;

  // Global rolloff/detune ranges
  float ro1_{1.0f}, ro2_{1.0f};
  float dt1_{0.0f}, dt2_{0.0f};

  // Per-partial static arrays (set by init_full/explicit)
  std::vector<float> mult1_, mult2_, ampl1_, ampl2_, po1_, po2_;

  // Per-partial runtime state
  std::vector<float> partialPos_, partialPO_, partialLPO_, dtVals_;

  // Expand rule
  ExpandRule expandRule_;
  bool hasExpand_{false};
  // Pre-expand copies (restore before re-expanding)
  std::vector<float> origMult1_, origMult2_, origAmpl1_, origAmpl2_, origPo1_, origPo2_;

  static constexpr float CUTOFF = 16000.0f;
};

} // namespace mforce
