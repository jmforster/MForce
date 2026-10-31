#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/source/additive/partials.h"
#include "mforce/core/randomizer.h"
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

  // After WaveSource's three, in the order wave() advanced them. `partials`
  // is a slot over the concrete Partials: the node copies the arrays of the
  // part it is wired to (apply_partials) and never drives it.
  Param phaseOffset {this, "phaseOffset",  0.0f, 0.0f, 1.0f,   "cycles"};
  Param freqVarDepth{this, "freqVarDepth", 0.0f, 0.0f, 1.0f,   "0-1"};
  Param freqVarSpeed{this, "freqVarSpeed", 0.0f, 0.0f, 100.0f, "hz"};
  Param amplVarDepth{this, "amplVarDepth", 0.0f, 0.0f, 1.0f,   "0-1"};
  Param amplVarSpeed{this, "amplVarSpeed", 0.0f, 0.0f, 100.0f, "hz"};
  Slot<Partials> partials{this, "partials", "a Partials part"};

  // The number of partials in the current definition; changing it rebuilds
  // the default partials (set_default_partials).
  Setting<int> partialCount{this, "partialCount", 500, 1, 2000};

  explicit AdditiveSource2(uint32_t seed = 0xADD3'0000u);

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

  const char* type_name() const override { return "AdditiveSource2"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  // Wiring `partials` copies the Partials' arrays the moment the slot takes
  // it; the base door does the type-check.
  void on_slot_changed(std::string_view name) override {
    if (name == "partials")
      if (auto* p = partials.get()) apply_partials(*p);
  }

  void on_setting_changed(std::string_view name) override {
    if (name == "partialCount") set_default_partials(partialCount.value);
  }

  // Apply partials from a Partials base class (used by the "partials" pin)
  void apply_partials(const Partials& p) {
    int n = p.partial_count();
    partialCount.value = n;
    hasStart_ = true;
    startIdx_  = p.get_mult1();
    endIdx_    = p.get_mult2();
    startAmpl_ = p.get_ampl1();
    endAmpl_   = p.get_ampl2();
    absAmpl_.assign(n, true);
    freqEnvRef_.assign(n, -1);
    amplEnvRef_.assign(n, -1);
    if (prepared()) reserve_note_arrays();
  }

  void on_prepare(const RenderContext& ctx) override;
  void on_start(int frames) override;

protected:
  float wave() override;

private:
  bool matches_filter(PartialFilter f, int partialNum) const;
  void reserve_note_arrays();   // capacity for the per-note arrays from the definition's size

  Randomizer rng_;

  // Partial definition arrays
  std::vector<float> startIdx_, startAmpl_, endIdx_, endAmpl_;
  std::vector<bool>  absAmpl_;
  bool hasStart_{false};

  // Per-partial envelope references (indices into freqEnvs_/amplEnvs_)
  std::vector<std::shared_ptr<ValueSource>> freqEnvs_, amplEnvs_;
  std::vector<int> freqEnvRef_, amplEnvRef_;  // -1 = no envelope

  // Runtime state
  int activeCount_{0};   // partials rendered: those below ~12 kHz at the note's frequency
  std::vector<float> startFreq_, endFreq_;
  std::vector<float> partialPos_;
  // Lightweight per-partial random walk (replaces legacy per-partial RedNoiseSource)
  std::vector<float> freqOffset_, amplOffset_;
  std::vector<int>   freqVarDir_, amplVarDir_;
};

} // namespace mforce
