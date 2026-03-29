#pragma once
#include "dsp_wave_source.h"
#include "randomizer.h"
#include <memory>
#include <vector>
#include <string>

namespace mforce {

// Ported from C# AdditiveSource2 — per-partial envelope assignment.
//
// Unlike FullAdditiveSource (5 global envelopes shared by all partials),
// this allows different envelopes for different partial subsets:
//   - Odd partials can sustain while even partials decay
//   - Low partials can have slow attack, high partials fast
//
// Per-partial frequency/amplitude variation uses lightweight random walk
// (not per-partial RedNoiseSource as in legacy — avoids heap in hot loop).
//
// Envelope assignment rules applied in order via assign_*_envelope().
// Each rule: envelope ref, PartialFilter, partial range [from, to].
struct AdditiveSource2 final : WaveSource {
  enum class PartialFilter { All, Even, Odd, Mult3, NonMult3 };

  explicit AdditiveSource2(int sampleRate, uint32_t seed = 0xADD3'0000u);

  // --- Partial definition ---
  // Static partials (no start/end evolution):
  void set_partials(std::vector<float> idx, std::vector<float> ampl);
  // Evolving partials (startIdx→endIdx driven by freq envelopes):
  void set_partials(std::vector<float> startIdx, std::vector<float> startAmpl,
                    std::vector<float> endIdx, std::vector<float> endAmpl);
  // Default: integer harmonics 1..N, amplitude = rolloff mode
  void set_default_partials(int count = 500);

  // --- Per-partial envelope assignment ---
  // Assigns envelope to partials matching filter in range [from, to] (1-based).
  void assign_freq_envelope(std::shared_ptr<ValueSource> env,
                            PartialFilter filter, int from, int to);
  void assign_ampl_envelope(std::shared_ptr<ValueSource> env,
                            PartialFilter filter, int from, int to);

  // --- Variation params ---
  void set_phase_offset(std::shared_ptr<ValueSource> v)   { phaseOffset_ = std::move(v); }
  void set_freq_var_depth(std::shared_ptr<ValueSource> v) { freqVarDepth_ = std::move(v); }
  void set_freq_var_speed(std::shared_ptr<ValueSource> v) { freqVarSpeed_ = std::move(v); }
  void set_ampl_var_depth(std::shared_ptr<ValueSource> v) { amplVarDepth_ = std::move(v); }
  void set_ampl_var_speed(std::shared_ptr<ValueSource> v) { amplVarSpeed_ = std::move(v); }

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  bool matches_filter(PartialFilter f, int partialNum) const;

  Randomizer rng_;

  // Partial definition arrays
  std::vector<float> startIdx_, startAmpl_, endIdx_, endAmpl_;
  std::vector<bool>  absAmpl_;
  bool hasStart_{false};

  // Per-partial envelope references (indices into freqEnvs_/amplEnvs_)
  std::vector<std::shared_ptr<ValueSource>> freqEnvs_, amplEnvs_;
  std::vector<int> freqEnvRef_, amplEnvRef_;  // -1 = no envelope

  // Variation params
  std::shared_ptr<ValueSource> phaseOffset_;
  std::shared_ptr<ValueSource> freqVarDepth_;
  std::shared_ptr<ValueSource> freqVarSpeed_;
  std::shared_ptr<ValueSource> amplVarDepth_;
  std::shared_ptr<ValueSource> amplVarSpeed_;

  // Runtime state
  int partialCount_{0};
  std::vector<float> startFreq_, endFreq_;
  std::vector<float> partialPos_;
  // Lightweight per-partial random walk (replaces legacy per-partial RedNoiseSource)
  std::vector<float> freqOffset_, amplOffset_;
  std::vector<int>   freqVarDir_, amplVarDir_;
};

} // namespace mforce
