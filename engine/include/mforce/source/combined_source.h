#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# CombinedSource — combines two sources via Add, Multiply,
// Fade, or Sum. Add averages (v1+v2)/2 to preserve headroom; Sum is a
// no-halving variant that's correct when callers want raw addition
// (e.g. modulator layering where amplitudes are chosen to stay bounded).
// Fade: linear crossfade from source1 to source2 over the started span.
enum class CombineOp { Add, Multiply, Fade, Sum };

// A combiner has two signal inputs and no `source`, so the inherited input of
// a Processor would be a phantom pin next to them: it derives from ValueSource
// with its Combiner category and becomes a Processor when the multi-input
// shape is settled.
struct CombinedSource final : ValueSource {
  Input source1{this, "source1"};
  Input source2{this, "source2"};
  Setting<int>   operation{this, "operation", 0, 0, 3, kOpLabels};
  Setting<float> gainAdj  {this, "gainAdj", 0.0f, -1.0f, 10.0f};

  CombinedSource(std::shared_ptr<ValueSource> s1, std::shared_ptr<ValueSource> s2,
                 CombineOp op, float ga = 0.0f) {
    source1.set(std::move(s1));
    source2.set(std::move(s2));
    operation.value = static_cast<int>(op);
    gainAdj.value = ga;
  }

  const char* type_name() const override { return "CombinedSource"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  void on_start(int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;
  }

  float compute() override {
    ++ptr_;
    const float v1 = source1.current();
    const float v2 = source2.current();
    const auto op = static_cast<CombineOp>(operation.value);

    if (op == CombineOp::Add) {
      return (v1 + v2 * (1.0f + gainAdj.value)) * 0.5f;
    } else if (op == CombineOp::Multiply) {
      return v1 * v2 * (1.0f + gainAdj.value);
    } else if (op == CombineOp::Sum) {
      return v1 + v2 * (1.0f + gainAdj.value);
    }
    // Fade
    float t = (totalFrames_ > 0) ? float(ptr_) / float(totalFrames_) : 0.0f;
    return v1 * (1.0f - t) + v2 * (1.0f + gainAdj.value) * t;
  }

private:
  // Display labels for the operation dropdown. Index must match CombineOp:
  // 0=Add (averaged mix), 1=Multiply, 2=Fade (crossfade), 3=Sum (raw add).
  // "Add" is shown as "Mix" since it's actually (v1+v2)/2 — averaging, not
  // summation. "Sum" is the raw-addition variant.
  static constexpr const char* kOpLabels[] = {
    "Mix", "Multiply", "Fade", "Sum", nullptr
  };

  int ptr_{-1};
  int totalFrames_{1};
};

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
  SourceCategory category() const override { return SourceCategory::Combiner; }

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

// Ported from C# DistortedSource — waveshaping distortion.
// Shifts, amplifies, and clips the input. Density controls probability of effect.
struct DistortedSource final : Processor {
  // After the inherited `source`, in the order they are advanced.
  Param density  {this, "density",   1.0f, 0.0f,   1.0f,   "0-1"};
  Param gain     {this, "gain",      1.0f, 0.0f,   100.0f, "gain"};
  Param shift    {this, "shift",     0.0f, -1.0f,  1.0f,   "±1"};
  Param amplitude{this, "amplitude", 1.0f, 0.0f,   10.0f,  "0-1"};

  explicit DistortedSource(uint32_t seed = 0xD1570000u) : rng_(seed) {}

  const char* type_name() const override { return "DistortedSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  float compute() override {
    float s = input.current();
    float d = density.current();
    float g = gain.current();
    float sh = shift.current();
    float a = amplitude.current();

    if (rng_.decide(d)) {
      float shifted = s + sh;
      return std::min(std::fabs(shifted) * (1.0f + g), 1.0f)
           * (shifted >= 0.0f ? 1.0f : -1.0f) * a;
    }
    return 0.0f;
  }

private:
  Randomizer rng_;
};

// Ported from C# StaticVarSource — random value drawn once per note, at start().
// value = baseValue * (1 + random * varPct), optionally biased.
struct StaticVarSource final : Generator {
  float baseValue{1.0f};
  float varPct{0.0f};
  float bias{0.0f};

  StaticVarSource(float base, float pct, float b = 0.0f, uint32_t seed = 0x57A70001u)
  : baseValue(base), varPct(pct), bias(b), rng_(seed) {}

  const char* type_name() const override { return "StaticVarSource"; }

  void on_start(int /*frames*/) override {
    float r = rng_.valuePN() * varPct;
    if (bias != 0.0f) r = r * (1.0f - std::fabs(bias)) + bias * varPct;
    value_ = baseValue * (1.0f + r);
    cur_ = value_;
  }

  float compute() override { return value_; }

private:
  Randomizer rng_;
  float value_{0.0f};
};

// Ported from C# StaticRangeSource — random value in [min,max] drawn once per note, at start().
struct StaticRangeSource final : Generator {
  float min{0.0f}, max{1.0f}, bias{0.0f};

  StaticRangeSource(float mn, float mx, float b = 0.0f, uint32_t seed = 0x57A70002u)
  : min(mn), max(mx), bias(b), rng_(seed) {}

  const char* type_name() const override { return "StaticRangeSource"; }

  void on_start(int /*frames*/) override {
    value_ = rng_.range(min, max, bias);
    cur_ = value_;
  }

  float compute() override { return value_; }

private:
  Randomizer rng_;
  float value_{0.0f};
};

} // namespace mforce
