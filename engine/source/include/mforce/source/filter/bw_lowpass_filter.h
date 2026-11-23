#pragma once
// BWLowpassFilter: a Butterworth lowpass Processor.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp/butterworth_section.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// BWLowpassFilter — Butterworth lowpass with cascaded sections.
// ---------------------------------------------------------------------------
struct BWLowpassFilter final : Processor {
  Param cutoffFreq{this, "cutoffFreq", 1000.0f, 1.0f, 24000.0f, "hz"};
  Setting<int> sections{this, "sections", 2, 1, 8};

  const char* type_name() const override { return "BWLowpassFilter"; }

  // Built at prepare, rebuilt only when the host's rate or the section count
  // has changed since. A repeat prepare at the same rate and count leaves the
  // histories alone: they carry across notes.
  void on_prepare(const RenderContext&) override {
    if (!sections_.empty() && builtRate_ == sample_rate() && builtCount_ == sections.value) return;
    dsp::build_butterworth_sections(sections_, dsp::ButterworthSection::Kind::Lowpass,
                                    sections.value, float(sample_rate()));
    builtRate_ = sample_rate();
    builtCount_ = sections.value;
    lastCutoff_ = -1.0f;
  }

  float compute() override {
    float val = input.current();
    float cutoff = cutoffFreq.current();
    cutoff = std::clamp(cutoff, 1.0f, float(sample_rate()) * 0.49f);

    // Coefficients only when the cutoff moves — update() runs a tan() per
    // section, and doing that per sample made each BW node ~10x costlier
    // than its actual filtering (2026-08-12 live-voice CPU audit).
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
  int builtRate_{0};
  int builtCount_{0};
  std::vector<dsp::ButterworthSection> sections_;
  float lastCutoff_{-1.0f};
};

} // namespace mforce
