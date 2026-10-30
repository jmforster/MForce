#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>

namespace mforce {

// The phase-accumulating oscillator base (ported from the C# WaveSource).
// The base owns the three pins and the accumulator; a subclass supplies
// wave(), its value at the current position.
struct WaveSource : Generator {
  // Declared in the order the old next() advanced them: amplitude,
  // frequency, phase. Declaration order is the advance order until the
  // per-tick memo lands.
  Param amplitude{this, "amplitude", 1.0f,   0.0f,  10.0f,    "0-1"};
  Param frequency{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  // The lowest frequency a frequency-dependent table (WavetableSource,
  // HybridKSSource) is sized for: the table is reserved once, at prepare,
  // for sample_rate() / kMinTableHz samples, and a note below the floor gets
  // a table of the floor's length. The pin's own minimum (0.01 Hz) is no
  // floor; 20 Hz is the DelayLine's since checkpoint 2.2, and nothing in
  // use asks a table for less (the lowest keyboard note is 27.5 Hz).
  static constexpr float kMinTableHz = 20.0f;
  Param phase    {this, "phase",     0.0f,  -1.0f,  1.0f,     "cycles"};

  Range range() const override { return Range::Bipolar; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  // Mirror legacy: ptr starts at -1, the first compute() increments to 0.
  void on_start(int) override { ptr_ = -1; }

  float compute() override {
    ++ptr_;

    float f = frequency.current();
    if (f <= 0.0f) { f = frequency.min; fault(Fault::FrequencyClamped); }   // never a throw on a render path

    if (ptr_ == 0) {
      currPos_   = phase.current();   // legacy: init pos = phase (cycles)
      lastPhase_ = phase.current();
    }

    currAmpl_  = amplitude.current();
    currFreq_  = f;
    currPhase_ = phase.current();

    currPhaseIncr_ = currFreq_ / float(sample_rate());

    const float out = wave() * currAmpl_;

    // legacy:
    // CurrPos = (CurrPos + CurrPhaseIncr + (CurrPhase - LastPhase)) % 1;
    const float phaseDelta = (currPhase_ - lastPhase_);
    lastPhase_ = currPhase_;

    currPos_ = std::fmod(currPos_ + currPhaseIncr_ + phaseDelta, 1.0f);
    if (currPos_ < 0.0f) currPos_ += 1.0f;

    return out;
  }

protected:
  virtual float wave() = 0;   // the subclass's value at currPos_

  int   ptr_{-1};
  float currAmpl_{1.0f}, currFreq_{440.0f}, currPhase_{0.0f};
  float currPos_{0.0f}, currPhaseIncr_{0.0f};
  float lastPhase_{0.0f};
};

} // namespace mforce
