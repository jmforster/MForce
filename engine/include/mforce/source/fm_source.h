#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <cmath>
#include <memory>

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

  void prepare(int frames) override {
    WaveSource::prepare(frames);
    carrierRatio_->prepare(frames);
    modRatio_->prepare(frames);
    depth_->prepare(frames);

    carrierPhase_ = 0.0f;
    modPhase_ = 0.0f;
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

    constexpr float TAU = 2.0f * 3.14159265358979323846f;

    // Modulator
    float modFreq = baseFreq * mRatio;
    float modVal  = std::sin(modPhase_ * TAU);
    modPhase_ += modFreq / float(sampleRate_);
    modPhase_ -= std::floor(modPhase_);

    // Carrier with frequency modulation
    float carrierFreq = baseFreq * cRatio * (1.0f + modVal * d);
    float val = std::sin(carrierPhase_ * TAU);
    carrierPhase_ += carrierFreq / float(sampleRate_);
    carrierPhase_ -= std::floor(carrierPhase_);

    return val;
  }

private:
  std::shared_ptr<ValueSource> carrierRatio_;
  std::shared_ptr<ValueSource> modRatio_;
  std::shared_ptr<ValueSource> depth_;
  float carrierPhase_{0.0f};
  float modPhase_{0.0f};
};

} // namespace mforce
