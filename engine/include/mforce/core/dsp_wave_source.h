#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <cmath>
#include <stdexcept>

namespace mforce {

struct WaveSource : ValueSource {
  explicit WaveSource(int sampleRate)
  : sampleRate_(sampleRate)
  , amplitude_(std::make_shared<ConstantSource>(1.0f))
  , frequency_(std::make_shared<ConstantSource>(440.0f))
  , phase_(std::make_shared<ConstantSource>(0.0f)) {}

  void set_amplitude(std::shared_ptr<ValueSource> s) { amplitude_ = std::move(s); }
  void set_frequency(std::shared_ptr<ValueSource> s) { frequency_ = std::move(s); }
  void set_phase(std::shared_ptr<ValueSource> s) { phase_ = std::move(s); }

  void prepare(int frames) override {
    ValueSource::prepare(frames);
    amplitude_->prepare(frames);
    frequency_->prepare(frames);
    phase_->prepare(frames);

    // Mirror legacy: ptr starts at -1, Next() increments to 0 on first sample.
    ptr_ = -1;
  }

  float next() override {
    ++ptr_;

    amplitude_->next();
    frequency_->next();
    phase_->next();

    const float f = frequency_->current();
    if (f <= 0.0f) throw std::runtime_error("WaveSource: non-positive frequency");

    if (ptr_ == 0) {
      currPos_   = phase_->current();   // legacy: init pos = phase (cycles)
      lastPhase_ = phase_->current();
    }

    currAmpl_ = amplitude_->current();
    currFreq_ = f;
    currPhase_ = phase_->current();

    currPhaseIncr_ = currFreq_ / float(sampleRate_);

    float w = compute_wave_value(); // subclass
    cur_ = w * currAmpl_;

    // legacy:
    // CurrPos = (CurrPos + CurrPhaseIncr + (CurrPhase - LastPhase)) % 1;
    const float phaseDelta = (currPhase_ - lastPhase_);
    lastPhase_ = currPhase_;

    currPos_ = std::fmod(currPos_ + currPhaseIncr_ + phaseDelta, 1.0f);
    if (currPos_ < 0.0f) currPos_ += 1.0f;

    return cur_;
  }

  float current() const override { return cur_; }

  // Accessors for param map resolution
  std::shared_ptr<ValueSource> get_amplitude() const { return amplitude_; }
  std::shared_ptr<ValueSource> get_frequency() const { return frequency_; }
  std::shared_ptr<ValueSource> get_phase()     const { return phase_; }

  // Self-description — base params shared by all WaveSource subclasses
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 440.0f, 0.01f, 20000.0f},
      {"amplitude", 1.0f,   0.0f,  10.0f},
      {"phase",     0.0f,  -1.0f,  1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "frequency") { set_frequency(std::move(src)); return; }
    if (name == "amplitude") { set_amplitude(std::move(src)); return; }
    if (name == "phase")     { set_phase(std::move(src)); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "frequency") return frequency_;
    if (name == "amplitude") return amplitude_;
    if (name == "phase")     return phase_;
    return nullptr;
  }

protected:
  virtual float compute_wave_value() = 0;

  int sampleRate_;
  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> frequency_;
  std::shared_ptr<ValueSource> phase_;

  int   ptr_{-1};
  float currAmpl_{1.0f}, currFreq_{440.0f}, currPhase_{0.0f};
  float currPos_{0.0f}, currPhaseIncr_{0.0f};
  float lastPhase_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
