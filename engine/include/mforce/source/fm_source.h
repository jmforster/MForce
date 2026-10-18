#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/filter/filters.h"
#include <cmath>
#include <string_view>
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
  Param carrierRatio{this, "carrierRatio", 1.0f, 0.01f, 32.0f,  "ratio"};
  Param modRatio    {this, "modRatio",     1.0f, 0.01f, 32.0f,  "ratio"};
  Param depth       {this, "depth",        1.0f, 0.0f,  100.0f, "index"};
  Setting<bool> unboundedPos{this, "unbounded_pos", false, false, true};
  Setting<int>  oversample  {this, "oversample", 1, 1, 16};

  const char* type_name() const override { return "FMSource"; }

  void on_setting_changed(std::string_view name) override {
    if (name != "oversample") return;
    oversample.clamp();
    if (prepared()) build_decimation();
  }

  void on_prepare(const RenderContext&) override {
    if (decimSections_.empty() || builtOversample_ != oversample.value || builtRate_ != sample_rate())
      build_decimation();
  }

  void on_start(int frames) override {
    WaveSource::on_start(frames);

    carrierPhase_ = 0.0f;
    modPhase_ = 0.0f;
    for (auto& s : decimSections_) s.reset();
  }

protected:
  float wave() override {
    const float baseFreq = currFreq_;
    const float cRatio   = carrierRatio.current();
    const float mRatio   = modRatio.current();
    const float d        = depth.current();

    // Carrier phase offset — this is what makes `phase` true PM rather than a
    // dead param. Matches the base-class contract: WaveSource keeps
    // currPos_ = phase_current + sum(incr), i.e. phase is an OFFSET, not an
    // integrated delta. Offset only; never added into carrierPhase_, which
    // would integrate the modulator twice and turn PM back into FM. Constant
    // 0.0f (the default) is exact, so unwired patches stay bit-identical.
    const float phOffset = currPhase_;

    constexpr double TAU_D = 2.0 * 3.14159265358979323846;

    const float modFreq = baseFreq * mRatio;
    const int   M       = oversample.value;
    const float subRate = float(sample_rate()) * float(M);

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
      if (unboundedPos.value) {
        if (modPhase_ > 1.0f) modPhase_ -= 1.0f;  // legacy: single-step, no neg wrap, accumulates
      } else {
        modPhase_ -= std::floor(modPhase_);
      }

      // Carrier with frequency modulation
      float carrierFreq = baseFreq * cRatio * (1.0f + modVal * d);
      float s = float(std::sin((double(carrierPhase_) + double(phOffset)) * TAU_D));
      carrierPhase_ += carrierFreq / subRate;
      if (unboundedPos.value) {
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
  // The decimation lowpass for oversampled rendering. Runs at the
  // oversampled rate (M*SR); fixed cutoff at 0.45*SR (just under the base
  // Nyquist) removes folded content before decimate-by-M. Built at prepare
  // and on an oversample change, never in the hot loop. M==1 uses no filter
  // (identity path).
  void build_decimation() {
    decimSections_.clear();
    builtOversample_ = oversample.value;
    builtRate_ = sample_rate();
    if (oversample.value > 1) {
      const float subRate = float(sample_rate()) * float(oversample.value);
      const float cutoff  = 0.45f * float(sample_rate());
      const int   nSec    = 4;  // 8th-order Butterworth, ~48 dB/oct
      for (int i = 0; i < nSec; ++i)
        decimSections_.emplace_back(float(i + 1), float(nSec * 2), subRate);
      for (auto& s : decimSections_) s.update(cutoff);
    }
  }

  float carrierPhase_{0.0f};
  float modPhase_{0.0f};
  std::vector<BWLPSection> decimSections_;  // built when oversample > 1
  int builtOversample_{0};
  int builtRate_{0};
};

} // namespace mforce
