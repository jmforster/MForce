#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>

namespace mforce {

// Ported from C# MForce.Sound.Source.RepeatingSource
// Repeats a source with variable duration and gaps between repetitions.
// Good for sound effects (footsteps, dripping, rhythmic textures).
//
// Each repetition: start source for duration seconds, play it, then
// silence for gap seconds. Both duration and gap are randomized by varPct.
struct RepeatingSource final : Processor {
  Setting<float> duration   {this, "duration",    0.5f, 0.001f, 10.0f};   // seconds per repetition
  Setting<float> durVarPct  {this, "durVarPct",   0.0f, 0.0f,   1.0f};    // random variation of duration
  Setting<float> gapDuration{this, "gapDuration", 0.2f, 0.0f,   10.0f};   // seconds of silence between repetitions
  Setting<float> gapVarPct  {this, "gapVarPct",   0.0f, 0.0f,   1.0f};    // random variation of gap

  // The node pulls the inherited `source` only while a repetition plays,
  // never during a gap, and starts it for each repetition's own length at
  // the start of that repetition, so the input is Manual.
  explicit RepeatingSource(uint32_t seed = 0xAEAE'0000u)
  : Processor(Pull::Manual), rng_(seed) {}

  const char* type_name() const override { return "RepeatingSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  void on_prepare(const RenderContext& ctx) override { input.prepare(ctx); }

  void on_start(int /*frames*/) override { start_repetition(); }

  float compute() override {
    if (gapCount_ == 0) {
      // Playing source
      if (srcRemaining_ > 0) {
        srcRemaining_--;
        return input.next();
      }
      // Source finished — enter gap
      gapCount_ = int(std::round(currGap_ * float(sample_rate())));
      return 0.0f;
    }
    // In gap
    gapCount_--;
    if (gapCount_ == 0) {
      start_repetition();
    }
    return 0.0f;
  }

private:
  void start_repetition() {
    float d = duration.value * (1.0f + rng_.valuePN() * durVarPct.value);
    if (d < 0.001f) d = 0.001f;
    currGap_ = gapDuration.value * (1.0f + rng_.valuePN() * gapVarPct.value);
    if (currGap_ < 0.0f) currGap_ = 0.0f;

    int samples = int(std::round(d * float(sample_rate())));
    input.start(samples);
    srcRemaining_ = samples;
    gapCount_ = 0;
  }

  Randomizer rng_;
  float currGap_{0.0f};
  int srcRemaining_{0};
  int gapCount_{0};
};

} // namespace mforce
