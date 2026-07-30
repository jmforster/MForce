#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/filter/filters.h"
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// FM synthesis source — modeled after the pattern used by the legacy FM
// instruments (FMBell1 etc.), NOT the legacy FMSource class which has a
// CombinedSource doubling quirk the instruments bypass.
//
// Signal flow:
//   modFreq     = baseFreq * modRatio
//   modVal      = sin(modPhase)                  (modulator oscillator)
//   carrierFreq = baseFreq * carrierRatio * (1 + modVal * depth)
//   output      = sin(carrierPhase) * amplitude  (carrier oscillator)
//
// Both carrier and modulator are sine oscillators (matching legacy practice).
// depth may be a constant or an envelope for time-varying timbre.
struct FMSource final : WaveSource {
  explicit FMSource(int sampleRate)
  : WaveSource(sampleRate)
  , carrierRatio_(std::make_shared<ConstantSource>(1.0f))
  , modRatio_(std::make_shared<ConstantSource>(1.0f))
  , depth_(std::make_shared<ConstantSource>(1.0f)) {}

  void set_carrier_ratio(std::shared_ptr<ValueSource> r) { carrierRatio_ = std::move(r); }
  void set_mod_ratio(std::shared_ptr<ValueSource> r)     { modRatio_ = std::move(r); }
  void set_depth(std::shared_ptr<ValueSource> d)         { depth_ = std::move(d); }

  const char* type_name() const override { return "FMSource"; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",     440.0f, 0.01f, 20000.0f, "hz"},
      {"amplitude",     1.0f,   0.0f,  10.0f,    "0-1"},
      {"phase",         0.0f,  -1.0f,  1.0f,     "cycles"},
      {"carrierRatio",  1.0f,   0.01f, 32.0f,    "ratio"},
      {"modRatio",      1.0f,   0.01f, 32.0f,    "ratio"},
      {"depth",         1.0f,   0.0f,  100.0f,   "index"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "carrierRatio") { set_carrier_ratio(std::move(src)); return; }
    if (name == "modRatio")     { set_mod_ratio(std::move(src)); return; }
    if (name == "depth")        { set_depth(std::move(src)); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "carrierRatio") return carrierRatio_;
    if (name == "modRatio")     return modRatio_;
    if (name == "depth")        return depth_;
    return WaveSource::get_param(name);
  }

  void prepare(const RenderContext& ctx, int frames) override {
    WaveSource::prepare(ctx, frames);
    carrierRatio_->prepare(ctx, frames);
    modRatio_->prepare(ctx, frames);
    depth_->prepare(ctx, frames);

    carrierPhase_ = 0.0f;
    modPhase_ = 0.0f;

    // Build the decimation lowpass for oversampled rendering. Runs at the
    // oversampled rate (M*SR); fixed cutoff at 0.45*SR (just under the base
    // Nyquist) removes folded content before decimate-by-M. Built here (per
    // note), never in the hot loop. M==1 uses no filter (identity path).
    decimSections_.clear();
    if (oversample_ > 1) {
      const float subRate = float(sampleRate_) * float(oversample_);
      const float cutoff  = 0.45f * float(sampleRate_);
      const int   nSec    = 4;  // 8th-order Butterworth, ~48 dB/oct
      for (int i = 0; i < nSec; ++i)
        decimSections_.emplace_back(float(i + 1), float(nSec * 2), subRate);
      for (auto& s : decimSections_) s.update(cutoff);
    }
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"unbounded_pos", ConfigType::Bool, 0.0f, 0.0f, 1.0f},
      {"oversample",    ConfigType::Int,  1.0f, 1.0f, 16.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "unbounded_pos") { unboundedPos_ = (value != 0.0f); return; }
    if (name == "oversample") {
      int m = int(value + 0.5f);
      oversample_ = m < 1 ? 1 : (m > 16 ? 16 : m);
      return;
    }
  }

  float get_config(std::string_view name) const override {
    if (name == "unbounded_pos") return unboundedPos_ ? 1.0f : 0.0f;
    if (name == "oversample")    return float(oversample_);
    return 0.0f;
  }

protected:
  float compute_wave_value() override {
    carrierRatio_->next();
    modRatio_->next();
    depth_->next();

    const float baseFreq = currFreq_;
    const float cRatio   = carrierRatio_->current();
    const float mRatio   = modRatio_->current();
    const float d        = depth_->current();

    constexpr double TAU_D = 2.0 * 3.14159265358979323846;

    const float modFreq = baseFreq * mRatio;
    const int   M       = oversample_;
    const float subRate = float(sampleRate_) * float(M);

    // Oversample the sin() nonlinearity: run carrier+modulator at M*SR, filter,
    // keep the last of each group of M (decimate). M==1 is the original
    // single-step path with no filter (byte-identical to prior behavior — the
    // spacy-FM family depends on it).
    float val = 0.0f;
    for (int i = 0; i < M; ++i) {
      // Modulator (double-precision sin matches legacy System.Math.Sin;
      // float phase accumulator preserves the legacy precision-wall behavior —
      // the float-mantissa exhaustion at ~8s is part of the spacy character)
      float modVal = float(std::sin(double(modPhase_) * TAU_D));
      modPhase_ += modFreq / subRate;
      if (unboundedPos_) {
        if (modPhase_ > 1.0f) modPhase_ -= 1.0f;  // legacy: single-step, no neg wrap, accumulates
      } else {
        modPhase_ -= std::floor(modPhase_);
      }

      // Carrier with frequency modulation
      float carrierFreq = baseFreq * cRatio * (1.0f + modVal * d);
      float s = float(std::sin(double(carrierPhase_) * TAU_D));
      carrierPhase_ += carrierFreq / subRate;
      if (unboundedPos_) {
        if (carrierPhase_ > 1.0f) carrierPhase_ -= 1.0f;
      } else {
        carrierPhase_ -= std::floor(carrierPhase_);
      }

      if (M > 1)
        for (auto& sec : decimSections_) s = sec.process(s);
      val = s;  // decimate: keep the last filtered sub-sample
    }

    return val;
  }

private:
  std::shared_ptr<ValueSource> carrierRatio_;
  std::shared_ptr<ValueSource> modRatio_;
  std::shared_ptr<ValueSource> depth_;
  float carrierPhase_{0.0f};
  float modPhase_{0.0f};
  bool  unboundedPos_{false};
  int   oversample_{1};
  std::vector<BWLPSection> decimSections_;  // built in prepare() when oversample_>1
};

} // namespace mforce
