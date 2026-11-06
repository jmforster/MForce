#pragma once
// BWHighpassFilter: a Butterworth highpass Processor.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp/butterworth_section.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// BWHighpassFilter — Butterworth highpass with cascaded sections.
// ---------------------------------------------------------------------------
struct BWHighpassFilter final : Processor {
  Param cutoffFreq{this, "cutoffFreq", 1000.0f, 1.0f, 24000.0f, "hz"};

  explicit BWHighpassFilter(int sectionCount) : sectionCount_(sectionCount) {}

  const char* type_name() const override { return "BWHighpassFilter"; }

  // Built and rebuilt as BWLowpassFilter's sections are.
  void on_prepare(const RenderContext&) override {
    if (!sections_.empty() && builtRate_ == sample_rate()) return;
    dsp::build_butterworth_sections(sections_, dsp::ButterworthSection::Kind::Highpass,
                                    sectionCount_, float(sample_rate()));
    builtRate_ = sample_rate();
    lastCutoff_ = -1.0f;
  }

  float compute() override {
    float val = input.current();
    float cutoff = cutoffFreq.current();
    cutoff = std::clamp(cutoff, 1.0f, float(sample_rate()) * 0.49f);

    if (cutoff != lastCutoff_) {
      for (auto& s : sections_) s.update(cutoff);
      lastCutoff_ = cutoff;
    }
    for (auto& s : sections_) {
      val = s.process(val);
    }
    return val;
  }

private:
  int sectionCount_;
  int builtRate_{0};
  std::vector<dsp::ButterworthSection> sections_;
  float lastCutoff_{-1.0f};
};

} // namespace mforce
