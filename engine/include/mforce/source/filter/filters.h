#pragma once
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

  explicit BWLowpassFilter(int sectionCount) : sectionCount_(sectionCount) {}

  const char* type_name() const override { return "BWLowpassFilter"; }

  // Built at prepare, rebuilt only when the host's rate has changed since. A
  // repeat prepare at the same rate leaves the histories alone: they carry
  // across notes.
  void on_prepare(const RenderContext&) override {
    if (!sections_.empty() && builtRate_ == sample_rate()) return;
    dsp::build_butterworth_sections(sections_, dsp::ButterworthSection::Kind::Lowpass,
                                    sectionCount_, float(sample_rate()));
    builtRate_ = sample_rate();
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
  int sectionCount_;
  int builtRate_{0};
  std::vector<dsp::ButterworthSection> sections_;
  float lastCutoff_{-1.0f};
};

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

// ---------------------------------------------------------------------------
// DelayFilter — simple delay with feedback and level control.
// ---------------------------------------------------------------------------
struct DelayFilter final : Processor {
  Param delayTime {this, "delayTime",  0.01f, 0.001f, 1.0f, "sec"};
  Param delayLevel{this, "delayLevel", 0.5f,  0.0f,   1.0f, "0-1"};
  Param feedback  {this, "feedback",   0.3f,  0.0f,   1.0f, "0-1"};

  const char* type_name() const override { return "DelayFilter"; }

  // One second of delay at the host's rate; a repeat prepare at the same
  // rate leaves the buffer alone.
  void on_prepare(const RenderContext&) override {
    if (int(buffer_.size()) != sample_rate()) buffer_.assign(size_t(sample_rate()), 0.0f);
  }

  void on_start(int) override {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    ptr_ = -1;
  }

  // Diagnostic: the delay buffer's length in samples.
  int capacity() const { return int(buffer_.size()); }

  float compute() override {
    ptr_ = (ptr_ + 1) % int(buffer_.size());

    float val = input.current();
    float dt = delayTime.current();
    float dl = delayLevel.current();
    float fb = feedback.current();

    // Read delayed sample with interpolation
    float delaySamples = dt * float(sample_rate());
    float readPos = float(ptr_) - delaySamples;
    if (readPos < 0.0f) readPos += float(buffer_.size());

    int s1 = int(readPos) % int(buffer_.size());
    int s2 = (s1 + 1) % int(buffer_.size());
    float frac = readPos - std::floor(readPos);
    float delayVal = (buffer_[s1] + (buffer_[s2] - buffer_[s1]) * frac) * dl;

    // Write: input + feedback
    float denom = 1.0f + dl * fb;
    buffer_[ptr_] = (val + delayVal * fb) / (denom > 0.0f ? denom : 1.0f);

    return (val + delayVal) / (1.0f + dl);
  }

private:
  std::vector<float> buffer_;
  int ptr_{-1};
};

} // namespace mforce
