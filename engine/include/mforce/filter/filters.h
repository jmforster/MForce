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
struct BWLowpassFilter final : ValueSource {
  void set_source(std::shared_ptr<ValueSource> s)     { source_ = std::move(s); }
  void set_cutoffFreq(std::shared_ptr<ValueSource> s) { cutoffFreq_ = std::move(s); }
  std::shared_ptr<ValueSource> get_source() const     { return source_; }
  std::shared_ptr<ValueSource> get_cutoffFreq() const { return cutoffFreq_; }

  const char* type_name() const override { return "BWLowpassFilter"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"cutoffFreq", 1000.0f, 1.0f,   24000.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "cutoffFreq") { cutoffFreq_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "cutoffFreq") return cutoffFreq_;
    return nullptr;
  }

  BWLowpassFilter(int sampleRate, int sectionCount)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionCount; ++i)
      sections_.emplace_back(float(i + 1), float(sectionCount * 2), float(sampleRate));
  }

  void prepare(int frames) override {
    if (source_) source_->prepare(frames);
    if (cutoffFreq_) cutoffFreq_->prepare(frames);
  }

  float next() override {
    float val = source_ ? (source_->next(), source_->current()) : 0.0f;
    float cutoff = cutoffFreq_ ? (cutoffFreq_->next(), cutoffFreq_->current()) : 1000.0f;
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
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> cutoffFreq_;
  int sampleRate_;
  std::vector<BWLPSection> sections_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// BWHighpassFilter — Butterworth highpass with cascaded sections.
// ---------------------------------------------------------------------------
struct BWHighpassFilter final : ValueSource {
  void set_source(std::shared_ptr<ValueSource> s)     { source_ = std::move(s); }
  void set_cutoffFreq(std::shared_ptr<ValueSource> s) { cutoffFreq_ = std::move(s); }
  std::shared_ptr<ValueSource> get_source() const     { return source_; }
  std::shared_ptr<ValueSource> get_cutoffFreq() const { return cutoffFreq_; }

  const char* type_name() const override { return "BWHighpassFilter"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"cutoffFreq", 1000.0f, 1.0f,   24000.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "cutoffFreq") { cutoffFreq_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "cutoffFreq") return cutoffFreq_;
    return nullptr;
  }

  BWHighpassFilter(int sampleRate, int sectionCount)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionCount; ++i)
      sections_.emplace_back(float(i + 1), float(sectionCount * 2), float(sampleRate));
  }

  void prepare(int frames) override {
    if (source_) source_->prepare(frames);
    if (cutoffFreq_) cutoffFreq_->prepare(frames);
  }

  float next() override {
    float val = source_ ? (source_->next(), source_->current()) : 0.0f;
    float cutoff = cutoffFreq_ ? (cutoffFreq_->next(), cutoffFreq_->current()) : 1000.0f;
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
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> cutoffFreq_;
  int sampleRate_;
  std::vector<BWHPSection> sections_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// BWBandpassFilter — highpass then lowpass in series.
// ---------------------------------------------------------------------------
struct BWBandpassFilter final : ValueSource {
  void set_source(std::shared_ptr<ValueSource> s)     { source_ = std::move(s); }
  void set_lowCutoff(std::shared_ptr<ValueSource> s)  { lowCutoff_ = std::move(s); }
  void set_highCutoff(std::shared_ptr<ValueSource> s) { highCutoff_ = std::move(s); }
  std::shared_ptr<ValueSource> get_source() const     { return source_; }
  std::shared_ptr<ValueSource> get_lowCutoff() const  { return lowCutoff_; }
  std::shared_ptr<ValueSource> get_highCutoff() const { return highCutoff_; }

  const char* type_name() const override { return "BWBandpassFilter"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"lowCutoff",  100.0f, 1.0f,    24000.0f},
      {"highCutoff", 5000.0f, 1.0f,   24000.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "lowCutoff")  { lowCutoff_ = std::move(src); return; }
    if (name == "highCutoff") { highCutoff_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "lowCutoff")  return lowCutoff_;
    if (name == "highCutoff") return highCutoff_;
    return nullptr;
  }

  BWBandpassFilter(int sampleRate, int sectionsPerFilter)
  : sampleRate_(sampleRate) {
    for (int i = 0; i < sectionsPerFilter; ++i) {
      hpSections_.emplace_back(float(i + 1), float(sectionsPerFilter * 2), float(sampleRate));
      lpSections_.emplace_back(float(i + 1), float(sectionsPerFilter * 2), float(sampleRate));
    }
  }

  void prepare(int frames) override {
    if (source_) source_->prepare(frames);
    if (lowCutoff_) lowCutoff_->prepare(frames);
    if (highCutoff_) highCutoff_->prepare(frames);
  }

  float next() override {
    float val = source_ ? (source_->next(), source_->current()) : 0.0f;
    float lo = lowCutoff_ ? (lowCutoff_->next(), lowCutoff_->current()) : 100.0f;
    float hi = highCutoff_ ? (highCutoff_->next(), highCutoff_->current()) : 5000.0f;
    lo = std::clamp(lo, 1.0f, float(sampleRate_) * 0.49f);
    hi = std::clamp(hi, 1.0f, float(sampleRate_) * 0.49f);

    for (auto& s : hpSections_) { s.update(lo); val = s.process(val); }
    for (auto& s : lpSections_) { s.update(hi); val = s.process(val); }

    cur_ = val;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> lowCutoff_;
  std::shared_ptr<ValueSource> highCutoff_;
  int sampleRate_;
  std::vector<BWHPSection> hpSections_;
  std::vector<BWLPSection> lpSections_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// DelayFilter — simple delay with feedback and level control.
// ---------------------------------------------------------------------------
struct DelayFilter final : ValueSource {
  void set_source(std::shared_ptr<ValueSource> s)     { source_ = std::move(s); }
  void set_delayTime(std::shared_ptr<ValueSource> s)  { delayTime_ = std::move(s); }
  void set_delayLevel(std::shared_ptr<ValueSource> s) { delayLevel_ = std::move(s); }
  void set_feedback(std::shared_ptr<ValueSource> s)   { feedback_ = std::move(s); }

  std::shared_ptr<ValueSource> get_source() const     { return source_; }
  std::shared_ptr<ValueSource> get_delayTime() const  { return delayTime_; }
  std::shared_ptr<ValueSource> get_delayLevel() const { return delayLevel_; }
  std::shared_ptr<ValueSource> get_feedback() const   { return feedback_; }

  const char* type_name() const override { return "DelayFilter"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"delayTime",  0.01f, 0.001f,    1.0f},
      {"delayLevel", 0.5f,  0.0f,      1.0f},
      {"feedback",   0.3f,  0.0f,      1.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "delayTime")  { delayTime_ = std::move(src); return; }
    if (name == "delayLevel") { delayLevel_ = std::move(src); return; }
    if (name == "feedback")   { feedback_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "delayTime")  return delayTime_;
    if (name == "delayLevel") return delayLevel_;
    if (name == "feedback")   return feedback_;
    return nullptr;
  }

  explicit DelayFilter(int sampleRate)
  : sampleRate_(sampleRate), buffer_(sampleRate, 0.0f) {}  // 1 second max delay

  void prepare(int frames) override {
    if (source_) source_->prepare(frames);
    if (delayTime_) delayTime_->prepare(frames);
    if (delayLevel_) delayLevel_->prepare(frames);
    if (feedback_) feedback_->prepare(frames);
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    ptr_ = -1;
  }

  float next() override {
    ptr_ = (ptr_ + 1) % int(buffer_.size());

    if (delayTime_) delayTime_->next();
    if (delayLevel_) delayLevel_->next();
    if (feedback_) feedback_->next();

    float val = source_ ? (source_->next(), source_->current()) : 0.0f;
    float dt = delayTime_ ? delayTime_->current() : 0.01f;
    float dl = delayLevel_ ? delayLevel_->current() : 0.5f;
    float fb = feedback_ ? feedback_->current() : 0.3f;

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
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> delayTime_;
  std::shared_ptr<ValueSource> delayLevel_;
  std::shared_ptr<ValueSource> feedback_;
  int sampleRate_;
  std::vector<float> buffer_;
  int ptr_{-1};
  float cur_{0.0f};
};

} // namespace mforce
