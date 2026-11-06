#pragma once
// BWBandpassFilter: a Butterworth bandpass Processor.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp/butterworth_section.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// BWBandpassFilter — highpass then lowpass in series.
// ---------------------------------------------------------------------------
struct BWBandpassFilter final : Processor {
  Param lowCutoff {this, "lowCutoff",  100.0f,  1.0f, 24000.0f, "hz"};
  Param highCutoff{this, "highCutoff", 5000.0f, 1.0f, 24000.0f, "hz"};

  explicit BWBandpassFilter(int sectionsPerFilter) : sectionsPerFilter_(sectionsPerFilter) {}

  const char* type_name() const override { return "BWBandpassFilter"; }

  // Built and rebuilt as BWLowpassFilter's sections are.
  void on_prepare(const RenderContext&) override {
    if (!hpSections_.empty() && builtRate_ == sample_rate()) return;
    dsp::build_butterworth_sections(hpSections_, dsp::ButterworthSection::Kind::Highpass,
                                    sectionsPerFilter_, float(sample_rate()));
    dsp::build_butterworth_sections(lpSections_, dsp::ButterworthSection::Kind::Lowpass,
                                    sectionsPerFilter_, float(sample_rate()));
    builtRate_ = sample_rate();
    lastLo_ = lastHi_ = -1.0f;
  }

  float compute() override {
    float val = input.current();
    float lo = lowCutoff.current();
    float hi = highCutoff.current();
    lo = std::clamp(lo, 1.0f, float(sample_rate()) * 0.49f);
    hi = std::clamp(hi, 1.0f, float(sample_rate()) * 0.49f);

    if (lo != lastLo_ || hi != lastHi_) {
      for (auto& s : hpSections_) s.update(lo);
      for (auto& s : lpSections_) s.update(hi);
      lastLo_ = lo; lastHi_ = hi;
    }
    for (auto& s : hpSections_) { val = s.process(val); }
    for (auto& s : lpSections_) { val = s.process(val); }

    return val;
  }

private:
  int sectionsPerFilter_;
  int builtRate_{0};
  float lastLo_{-1.0f}, lastHi_{-1.0f};
  std::vector<dsp::ButterworthSection> hpSections_;
  std::vector<dsp::ButterworthSection> lpSections_;
};

} // namespace mforce
