#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// FIR filter: y(t) = a0*x(t) + a1*x(t-1) + ... + an*x(t-n)
// ---------------------------------------------------------------------------
struct FIRFilter {
  std::vector<float> vals;

  explicit FIRFilter(int order) : vals(order, 0.0f) {}

  float process(float input, const float* a, int n) {
    for (int i = n - 1; i > 0; --i)
      vals[i] = vals[i - 1];
    vals[0] = input;

    float value = 0.0f;
    for (int i = 0; i < n; ++i)
      value += a[i] * vals[i];
    return value;
  }
};

// ---------------------------------------------------------------------------
// IIR filter: y(t) = x(t) + a1*y(t-1) + a2*y(t-2) + ...
// ---------------------------------------------------------------------------
struct IIRFilter {
  std::vector<float> vals;

  explicit IIRFilter(int order) : vals(order, 0.0f) {}

  float process(float input, const float* a, int n) {
    float value = input;
    for (int i = 0; i < n; ++i)
      value += a[i] * vals[i];

    for (int i = n - 1; i > 0; --i)
      vals[i] = vals[i - 1];
    vals[0] = value;

    return value;
  }
};

// ---------------------------------------------------------------------------
// Butterworth lowpass filter section (2nd order)
// ---------------------------------------------------------------------------
struct BWLPSection {
  FIRFilter fir{3};
  IIRFilter iir{2};
  float a[3]{}, b[2]{};
  float gain{0.0f}, zeta{0.0f};
  float rate{48000.0f};

  BWLPSection(float k, float n, float sampleRate)
  : rate(sampleRate) {
    zeta = float(-std::cos(3.14159265358979 * (2.0 * k + n - 1.0) / (2.0 * n)));
  }

  void update(float cutoff) {
    float omegac = float(2.0 * rate * std::tan(3.14159265358979 * cutoff / rate));
    a[0] = omegac * omegac;
    a[1] = 2.0f * omegac * omegac;
    a[2] = omegac * omegac;
    float b0 = (4.0f * rate * rate) + (4.0f * rate * zeta * omegac) + (omegac * omegac);
    b[0] = ((2.0f * omegac * omegac) - (8.0f * rate * rate)) / (-b0);
    b[1] = ((4.0f * rate * rate) - (4.0f * rate * zeta * omegac) + (omegac * omegac)) / (-b0);
    gain = 1.0f / b0;
  }

  float process(float input) {
    return iir.process(fir.process(gain * input, a, 3), b, 2);
  }
};

// ---------------------------------------------------------------------------
// Butterworth highpass filter section (2nd order)
// ---------------------------------------------------------------------------
struct BWHPSection {
  FIRFilter fir{3};
  IIRFilter iir{2};
  float a[3]{}, b[2]{};
  float gain{0.0f}, zeta{0.0f};
  float rate{48000.0f};

  BWHPSection(float k, float n, float sampleRate)
  : rate(sampleRate) {
    zeta = float(-std::cos(3.14159265358979 * (2.0 * k + n - 1.0) / (2.0 * n)));
  }

  void update(float cutoff) {
    float omegac = float(1.0 / (2.0 * rate * std::tan(3.14159265358979 * cutoff / rate)));
    a[0] = 4.0f * rate * rate;
    a[1] = -8.0f * rate * rate;
    a[2] = 4.0f * rate * rate;
    float b0 = (4.0f * rate * rate) + (4.0f * rate * zeta / omegac) + (1.0f / (omegac * omegac));
    b[0] = ((2.0f / (omegac * omegac)) - (8.0f * rate * rate)) / (-b0);
    b[1] = ((4.0f * rate * rate) - (4.0f * rate * zeta / omegac) + (1.0f / (omegac * omegac))) / (-b0);
    gain = 1.0f / b0;
  }

  float process(float input) {
    return iir.process(fir.process(gain * input, a, 3), b, 2);
  }
};

// ---------------------------------------------------------------------------
// BWLowpassFilter — Butterworth lowpass with cascaded sections.
// ---------------------------------------------------------------------------
struct BWLowpassFilter final : Processor {
  Param cutoffFreq{this, "cutoffFreq", 1000.0f, 1.0f, 24000.0f, "hz"};

  BWLowpassFilter(int sampleRate, int sectionCount)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionCount; ++i)
      sections_.emplace_back(float(i + 1), float(sectionCount * 2), float(sampleRate));
  }

  const char* type_name() const override { return "BWLowpassFilter"; }

  float compute() override {
    float val = input.current();
    float cutoff = cutoffFreq.current();
    cutoff = std::clamp(cutoff, 1.0f, float(sampleRate_) * 0.49f);

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
  int sampleRate_;
  std::vector<BWLPSection> sections_;
  float lastCutoff_{-1.0f};
};

// ---------------------------------------------------------------------------
// BWHighpassFilter — Butterworth highpass with cascaded sections.
// ---------------------------------------------------------------------------
struct BWHighpassFilter final : Processor {
  Param cutoffFreq{this, "cutoffFreq", 1000.0f, 1.0f, 24000.0f, "hz"};

  BWHighpassFilter(int sampleRate, int sectionCount)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionCount; ++i)
      sections_.emplace_back(float(i + 1), float(sectionCount * 2), float(sampleRate));
  }

  const char* type_name() const override { return "BWHighpassFilter"; }

  float compute() override {
    float val = input.current();
    float cutoff = cutoffFreq.current();
    cutoff = std::clamp(cutoff, 1.0f, float(sampleRate_) * 0.49f);

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
  int sampleRate_;
  std::vector<BWHPSection> sections_;
  float lastCutoff_{-1.0f};
};

// ---------------------------------------------------------------------------
// BWBandpassFilter — highpass then lowpass in series.
// ---------------------------------------------------------------------------
struct BWBandpassFilter final : Processor {
  Param lowCutoff {this, "lowCutoff",  100.0f,  1.0f, 24000.0f, "hz"};
  Param highCutoff{this, "highCutoff", 5000.0f, 1.0f, 24000.0f, "hz"};

  BWBandpassFilter(int sampleRate, int sectionsPerFilter)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionsPerFilter; ++i) {
      hpSections_.emplace_back(float(i + 1), float(sectionsPerFilter * 2), float(sampleRate));
      lpSections_.emplace_back(float(i + 1), float(sectionsPerFilter * 2), float(sampleRate));
    }
  }

  const char* type_name() const override { return "BWBandpassFilter"; }

  float compute() override {
    float val = input.current();
    float lo = lowCutoff.current();
    float hi = highCutoff.current();
    lo = std::clamp(lo, 1.0f, float(sampleRate_) * 0.49f);
    hi = std::clamp(hi, 1.0f, float(sampleRate_) * 0.49f);

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
  int sampleRate_;
  float lastLo_{-1.0f}, lastHi_{-1.0f};
  std::vector<BWHPSection> hpSections_;
  std::vector<BWLPSection> lpSections_;
};

// ---------------------------------------------------------------------------
// DelayFilter — simple delay with feedback and level control.
// ---------------------------------------------------------------------------
struct DelayFilter final : Processor {
  Param delayTime {this, "delayTime",  0.01f, 0.001f, 1.0f, "sec"};
  Param delayLevel{this, "delayLevel", 0.5f,  0.0f,   1.0f, "0-1"};
  Param feedback  {this, "feedback",   0.3f,  0.0f,   1.0f, "0-1"};

  explicit DelayFilter(int sampleRate)
  : sampleRate_(sampleRate), buffer_(sampleRate, 0.0f) {}  // 1 second max delay

  const char* type_name() const override { return "DelayFilter"; }

  void on_prepare(const RenderContext&, int) override {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    ptr_ = -1;
  }

  float compute() override {
    ptr_ = (ptr_ + 1) % int(buffer_.size());

    float val = input.current();
    float dt = delayTime.current();
    float dl = delayLevel.current();
    float fb = feedback.current();

    // Read delayed sample with interpolation
    float delaySamples = dt * float(sampleRate_);
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
  int sampleRate_;
  std::vector<float> buffer_;
  int ptr_{-1};
};

} // namespace mforce
