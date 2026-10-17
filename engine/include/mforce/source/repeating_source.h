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
// Each repetition: prepare source for duration seconds, play it, then
// silence for gap seconds. Both duration and gap are randomized by varPct.
struct RepeatingSource final : Processor {
  Setting<float> duration   {this, "duration",    0.5f, 0.001f, 10.0f};   // seconds per repetition
  Setting<float> durVarPct  {this, "durVarPct",   0.0f, 0.0f,   1.0f};    // random variation of duration
  Setting<float> gapDuration{this, "gapDuration", 0.2f, 0.0f,   10.0f};   // seconds of silence between repetitions
  Setting<float> gapVarPct  {this, "gapVarPct",   0.0f, 0.0f,   1.0f};    // random variation of gap
  int sampleRate{48000};

  // The node pulls the inherited `source` only while a repetition plays,
  // never during a gap, and prepares it for each repetition's own length at
  // the start of that repetition, so the input is Manual.
  RepeatingSource(int sr, uint32_t seed = 0xAEAE'0000u)
  : Processor(Pull::Manual), sampleRate(sr), rng_(seed) {}

  const char* type_name() const override { return "RepeatingSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  void on_prepare(const RenderContext& ctx, int frames) override {
    (void)frames;
    ctx_ = ctx;
    prepare_repetition();
  }

  float compute() override {
    if (gapCount_ == 0) {
      // Playing source
      if (srcRemaining_ > 0) {
        srcRemaining_--;
        return input.advance();
      }
      // Source finished — enter gap
      gapCount_ = int(std::round(currGap_ * float(sampleRate)));
      return 0.0f;
    }
    // In gap
    gapCount_--;
    if (gapCount_ == 0) {
      prepare_repetition();
    }
    return 0.0f;
  }

private:
  void prepare_repetition() {
    float d = duration.value * (1.0f + rng_.valuePN() * durVarPct.value);
    if (d < 0.001f) d = 0.001f;
    currGap_ = gapDuration.value * (1.0f + rng_.valuePN() * gapVarPct.value);
    if (currGap_ < 0.0f) currGap_ = 0.0f;

    int samples = int(std::round(d * float(sampleRate)));
    input.prepare(ctx_, samples);
    srcRemaining_ = samples;
    gapCount_ = 0;
  }

  Randomizer rng_;
  RenderContext ctx_{48000};  // captured at prepare() for use by prepare_repetition() during playback
  float currGap_{0.0f};
  int srcRemaining_{0};
  int gapCount_{0};
};

} // namespace mforce
