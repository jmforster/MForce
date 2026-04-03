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

  const char* type_name() const override { return "AdditiveSource2"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",    440.0f, 0.01f, 20000.0f},
      {"amplitude",    1.0f,   0.0f,  10.0f},
      {"phase",        0.0f,  -1.0f,  1.0f},
      {"phaseOffset",  0.0f,   0.0f,  1.0f},
      {"freqVarDepth", 0.0f,   0.0f,  1.0f},
      {"freqVarSpeed", 0.0f,   0.0f,  100.0f},
      {"amplVarDepth", 0.0f,   0.0f,  1.0f},
      {"amplVarSpeed", 0.0f,   0.0f,  100.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"partials"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "phaseOffset")  { set_phase_offset(std::move(src)); return; }
    if (name == "freqVarDepth") { set_freq_var_depth(std::move(src)); return; }
    if (name == "freqVarSpeed") { set_freq_var_speed(std::move(src)); return; }
    if (name == "amplVarDepth") { set_ampl_var_depth(std::move(src)); return; }
    if (name == "amplVarSpeed") { set_ampl_var_speed(std::move(src)); return; }
    if (name == "partials") {
      auto p = std::dynamic_pointer_cast<Partials>(src);
      if (p) { partialsRef_ = p; apply_partials(*p); }
      return;
    }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "phaseOffset")  return phaseOffset_;
    if (name == "freqVarDepth") return freqVarDepth_;
    if (name == "freqVarSpeed") return freqVarSpeed_;
    if (name == "amplVarDepth") return amplVarDepth_;
    if (name == "amplVarSpeed") return amplVarSpeed_;
    if (name == "partials")    return partialsRef_;
    return WaveSource::get_param(name);
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"partialCount", ConfigType::Int, 500.0f, 1.0f, 2000.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "partialCount") { set_default_partials(int(value)); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "partialCount") return float(partialCount_);
    return 0.0f;
  }

  // Apply partials from a Partials base class (used by the "partials" pin)
  void apply_partials(const Partials& p) {
    int n = p.partial_count();
    partialCount_ = n;
    hasStart_ = true;
    startIdx_  = p.get_mult1();
    endIdx_    = p.get_mult2();
    startAmpl_ = p.get_ampl1();
    endAmpl_   = p.get_ampl2();
    absAmpl_.assign(n, true);
    freqEnvRef_.assign(n, -1);
    amplEnvRef_.assign(n, -1);
  }

  void prepare(int frames) override;

protected:
  float compute_wave_value() override;

private:
  bool matches_filter(PartialFilter f, int partialNum) const;

  Randomizer rng_;

  // Partials reference (for get_param round-trip)
  std::shared_ptr<Partials> partialsRef_;

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
