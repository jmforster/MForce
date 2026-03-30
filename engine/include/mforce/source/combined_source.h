#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# CombinedSource — combines two sources via Add, Multiply, or Fade.
// Fade: linear crossfade from source1 to source2 over the prepare'd duration.
enum class CombineOp { Add, Multiply, Fade };

struct CombinedSource final : ValueSource {
  std::shared_ptr<ValueSource> source1;
  std::shared_ptr<ValueSource> source2;
  CombineOp op{CombineOp::Add};
  float gainAdj{0.0f};

  CombinedSource(std::shared_ptr<ValueSource> s1, std::shared_ptr<ValueSource> s2,
                 CombineOp operation, float ga = 0.0f)
  : source1(std::move(s1)), source2(std::move(s2)), op(operation), gainAdj(ga) {}

  void prepare(int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;
    source1->prepare(frames);
    source2->prepare(frames);
  }

  float next() override {
    ++ptr_;
    source1->next();
    source2->next();

    float v1 = source1->current();
    float v2 = source2->current();

    if (op == CombineOp::Add) {
      cur_ = (v1 + v2 * (1.0f + gainAdj)) * 0.5f;
    } else if (op == CombineOp::Multiply) {
      cur_ = v1 * v2 * (1.0f + gainAdj);
    } else { // Fade
      float t = (totalFrames_ > 0) ? float(ptr_) / float(totalFrames_) : 0.0f;
      cur_ = v1 * (1.0f - t) + v2 * (1.0f + gainAdj) * t;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int ptr_{-1};
  int totalFrames_{1};
  float cur_{0.0f};
};

// Ported from C# CrossfadeSource — plays source1, then crossfades into source2.
// ratio: fraction of total time for source1 (0.7 = 70% source1, 30% source2)
// overlap: fraction of the shorter segment that overlaps during transition
struct CrossfadeSource final : ValueSource {
  std::shared_ptr<ValueSource> source1;
  std::shared_ptr<ValueSource> source2;
  std::shared_ptr<ValueSource> amplitude;
  float ratio{0.5f};
  float overlap{0.1f};
  float gainAdj{0.0f};

  void prepare(int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;

    int s1Count = int(std::lround(double(frames) * ratio));
    int s2Count = frames - s1Count;
    overlapCount_ = int(std::lround(overlap * double(std::min(s1Count, s2Count))));

    s1End_ = s1Count + overlapCount_;
    s2Start_ = s1Count - overlapCount_;

    if (amplitude) amplitude->prepare(frames);
    source1->prepare(s1Count + overlapCount_);
    source2->prepare(s2Count + overlapCount_);
  }

  float next() override {
    ++ptr_;
    float amp = amplitude ? (amplitude->next(), amplitude->current()) : 1.0f;

    if (ptr_ < s2Start_) {
      cur_ = source1->next() * amp;
    } else if (ptr_ >= s1End_) {
      cur_ = source2->next() * amp * (1.0f + gainAdj);
    } else {
      float v1 = source1->next();
      float v2 = source2->next();
      float t = float(ptr_ - s2Start_) / float(s1End_ - s2Start_);
      cur_ = (v1 * (1.0f - t) + v2 * (1.0f + gainAdj) * t) * amp;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  int totalFrames_{1};
  int ptr_{-1};
  int overlapCount_{0};
  int s1End_{0}, s2Start_{0};
  float cur_{0.0f};
};

// Ported from C# DistortedSource — waveshaping distortion.
// Shifts, amplifies, and clips the input. Density controls probability of effect.
struct DistortedSource final : ValueSource {
  std::shared_ptr<ValueSource> source;
  std::shared_ptr<ValueSource> amplitude;
  std::shared_ptr<ValueSource> density;
  std::shared_ptr<ValueSource> gain;
  std::shared_ptr<ValueSource> shift;

  explicit DistortedSource(uint32_t seed = 0xD1570000u) : rng_(seed) {}

  void prepare(int frames) override {
    if (source) source->prepare(frames);
    if (amplitude) amplitude->prepare(frames);
    if (density) density->prepare(frames);
    if (gain) gain->prepare(frames);
    if (shift) shift->prepare(frames);
  }

  float next() override {
    if (source) source->next();
    if (density) density->next();
    if (gain) gain->next();
    if (shift) shift->next();
    if (amplitude) amplitude->next();

    float s = source ? source->current() : 0.0f;
    float d = density ? density->current() : 1.0f;
    float g = gain ? gain->current() : 1.0f;
    float sh = shift ? shift->current() : 0.0f;
    float a = amplitude ? amplitude->current() : 1.0f;

    if (rng_.decide(d)) {
      float shifted = s + sh;
      cur_ = std::min(std::fabs(shifted) * (1.0f + g), 1.0f)
           * (shifted >= 0.0f ? 1.0f : -1.0f) * a;
    } else {
      cur_ = 0.0f;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float cur_{0.0f};
};

// Ported from C# StaticVarSource — random value set once on prepare().
// value = baseValue * (1 + random * varPct), optionally biased.
struct StaticVarSource final : ValueSource {
  float baseValue{1.0f};
  float varPct{0.0f};
  float bias{0.0f};

  StaticVarSource(float base, float pct, float b = 0.0f, uint32_t seed = 0x57A70001u)
  : baseValue(base), varPct(pct), bias(b), rng_(seed) {}

  void prepare(int /*frames*/) override {
    float r = rng_.valuePN() * varPct;
    if (bias != 0.0f) r = r * (1.0f - std::fabs(bias)) + bias * varPct;
    cur_ = baseValue * (1.0f + r);
  }

  float next() override { return cur_; }
  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float cur_{0.0f};
};

// Ported from C# StaticRangeSource — random value in [min,max] set once on prepare().
struct StaticRangeSource final : ValueSource {
  float min{0.0f}, max{1.0f}, bias{0.0f};

  StaticRangeSource(float mn, float mx, float b = 0.0f, uint32_t seed = 0x57A70002u)
  : min(mn), max(mx), bias(b), rng_(seed) {}

  void prepare(int /*frames*/) override {
    cur_ = rng_.range(min, max, bias);
  }

  float next() override { return cur_; }
  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float cur_{0.0f};
};

} // namespace mforce
