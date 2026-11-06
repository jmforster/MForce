#pragma once
// CrossfadeSource: plays one source then crossfades into another.
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# CrossfadeSource — plays source1, then crossfades into source2.
// ratio: fraction of total time for source1 (0.7 = 70% source1, 30% source2)
// overlap: fraction of the shorter segment that overlaps during transition
//
// Two signal inputs and no `source`: a ValueSource for the reason CombinedSource
// is. The sources are Manual: each plays only in its own part of the note and
// is started for that part's length, so the node prepares, starts and pulls them.
struct CrossfadeSource final : ValueSource {
  Param amplitude{this, "amplitude", 1.0f, 0.0f, 10.0f, "0-1"};
  Input source1{this, "source1", Input::RequiredInput, nullptr, false, Pull::Manual};
  Input source2{this, "source2", Input::RequiredInput, nullptr, false, Pull::Manual};
  Setting<float> ratio  {this, "ratio",   0.5f, 0.0f, 1.0f};
  Setting<float> overlap{this, "overlap", 0.1f, 0.0f, 1.0f};
  Setting<float> gainAdj{this, "gainAdj", 0.0f, -1.0f, 10.0f};

  const char* type_name() const override { return "CrossfadeSource"; }

  void on_prepare(const RenderContext& ctx) override {
    source1.prepare(ctx);
    source2.prepare(ctx);
  }

  void on_start(int frames) override {
    ptr_ = -1;

    int s1Count = int(std::lround(double(frames) * ratio.value));
    int s2Count = frames - s1Count;
    overlapCount_ = int(std::lround(overlap.value * double(std::min(s1Count, s2Count))));

    s1End_ = s1Count + overlapCount_;
    s2Start_ = s1Count - overlapCount_;

    source1.start(s1Count + overlapCount_);
    source2.start(s2Count + overlapCount_);
  }

  float compute() override {
    ++ptr_;
    const float amp = amplitude.current();

    if (ptr_ < s2Start_) {
      return source1.next() * amp;
    } else if (ptr_ >= s1End_) {
      return source2.next() * amp * (1.0f + gainAdj.value);
    }
    float v1 = source1.next();
    float v2 = source2.next();
    float t = float(ptr_ - s2Start_) / float(s1End_ - s2Start_);
    return (v1 * (1.0f - t) + v2 * (1.0f + gainAdj.value) * t) * amp;
  }

private:
  int ptr_{-1};
  int overlapCount_{0};
  int s1End_{0}, s2Start_{0};
};

} // namespace mforce
