#pragma once
#include "dsp_value_source.h"
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
struct BWLowpassFilter final : ValueSource {
  std::shared_ptr<ValueSource> source;
  std::shared_ptr<ValueSource> cutoffFreq;

  BWLowpassFilter(int sampleRate, int sectionCount)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionCount; ++i)
      sections_.emplace_back(float(i + 1), float(sectionCount * 2), float(sampleRate));
  }

  void prepare(int frames) override {
    if (source) source->prepare(frames);
    if (cutoffFreq) cutoffFreq->prepare(frames);
  }

  float next() override {
    float val = source ? (source->next(), source->current()) : 0.0f;
    float cutoff = cutoffFreq ? (cutoffFreq->next(), cutoffFreq->current()) : 1000.0f;
    cutoff = std::clamp(cutoff, 1.0f, float(sampleRate_) * 0.49f);

    for (auto& s : sections_) {
      s.update(cutoff);
      val = s.process(val);
    }
    cur_ = val;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  std::vector<BWLPSection> sections_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// BWHighpassFilter — Butterworth highpass with cascaded sections.
// ---------------------------------------------------------------------------
struct BWHighpassFilter final : ValueSource {
  std::shared_ptr<ValueSource> source;
  std::shared_ptr<ValueSource> cutoffFreq;

  BWHighpassFilter(int sampleRate, int sectionCount)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionCount; ++i)
      sections_.emplace_back(float(i + 1), float(sectionCount * 2), float(sampleRate));
  }

  void prepare(int frames) override {
    if (source) source->prepare(frames);
    if (cutoffFreq) cutoffFreq->prepare(frames);
  }

  float next() override {
    float val = source ? (source->next(), source->current()) : 0.0f;
    float cutoff = cutoffFreq ? (cutoffFreq->next(), cutoffFreq->current()) : 1000.0f;
    cutoff = std::clamp(cutoff, 1.0f, float(sampleRate_) * 0.49f);

    for (auto& s : sections_) {
      s.update(cutoff);
      val = s.process(val);
    }
    cur_ = val;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  std::vector<BWHPSection> sections_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// BWBandpassFilter — highpass then lowpass in series.
// ---------------------------------------------------------------------------
struct BWBandpassFilter final : ValueSource {
  std::shared_ptr<ValueSource> source;
  std::shared_ptr<ValueSource> lowCutoff;   // highpass cutoff (lower bound)
  std::shared_ptr<ValueSource> highCutoff;  // lowpass cutoff (upper bound)

  BWBandpassFilter(int sampleRate, int sectionsPerFilter)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionsPerFilter; ++i) {
      hpSections_.emplace_back(float(i + 1), float(sectionsPerFilter * 2), float(sampleRate));
      lpSections_.emplace_back(float(i + 1), float(sectionsPerFilter * 2), float(sampleRate));
    }
  }

  void prepare(int frames) override {
    if (source) source->prepare(frames);
    if (lowCutoff) lowCutoff->prepare(frames);
    if (highCutoff) highCutoff->prepare(frames);
  }

  float next() override {
    float val = source ? (source->next(), source->current()) : 0.0f;
    float lo = lowCutoff ? (lowCutoff->next(), lowCutoff->current()) : 100.0f;
    float hi = highCutoff ? (highCutoff->next(), highCutoff->current()) : 5000.0f;
    lo = std::clamp(lo, 1.0f, float(sampleRate_) * 0.49f);
    hi = std::clamp(hi, 1.0f, float(sampleRate_) * 0.49f);

    for (auto& s : hpSections_) { s.update(lo); val = s.process(val); }
    for (auto& s : lpSections_) { s.update(hi); val = s.process(val); }

    cur_ = val;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  std::vector<BWHPSection> hpSections_;
  std::vector<BWLPSection> lpSections_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// DelayFilter — simple delay with feedback and level control.
// ---------------------------------------------------------------------------
struct DelayFilter final : ValueSource {
  std::shared_ptr<ValueSource> source;
  std::shared_ptr<ValueSource> delayTime;   // seconds
  std::shared_ptr<ValueSource> delayLevel;  // 0..1
  std::shared_ptr<ValueSource> feedback;    // 0..1

  explicit DelayFilter(int sampleRate)
  : sampleRate_(sampleRate), buffer_(sampleRate, 0.0f) {}  // 1 second max delay

  void prepare(int frames) override {
    if (source) source->prepare(frames);
    if (delayTime) delayTime->prepare(frames);
    if (delayLevel) delayLevel->prepare(frames);
    if (feedback) feedback->prepare(frames);
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    ptr_ = -1;
  }

  float next() override {
    ptr_ = (ptr_ + 1) % int(buffer_.size());

    if (delayTime) delayTime->next();
    if (delayLevel) delayLevel->next();
    if (feedback) feedback->next();

    float val = source ? (source->next(), source->current()) : 0.0f;
    float dt = delayTime ? delayTime->current() : 0.01f;
    float dl = delayLevel ? delayLevel->current() : 0.5f;
    float fb = feedback ? feedback->current() : 0.3f;

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

    cur_ = (val + delayVal) / (1.0f + dl);
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int sampleRate_;
  std::vector<float> buffer_;
  int ptr_{-1};
  float cur_{0.0f};
};

} // namespace mforce
