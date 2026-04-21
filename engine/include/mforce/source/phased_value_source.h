#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/smoothness_interpolator.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace mforce {

// Ported from C# MForce.Sound.Source.PhasedValueSource
// Composite source with overlapping stages and crossfade transitions.
// Used for granular synthesis — stages play sequentially with configurable
// overlap where adjacent stages crossfade.
//
// Each stage has: source (ValueSource), percent of total duration, min/max seconds.
// A stage with percent=0 is the "expand" stage (gets remaining duration).
// Overlap is specified in seconds and converted to samples.
struct PhasedValueSource final : ValueSource {

  struct Stage {
    std::shared_ptr<ValueSource> source;
    float percent{0.0f};
    float minSec{0.0f};
    float maxSec{0.0f};
    float gainAdj{1.0f};
  };

  float overlapSeconds{0.05f};
  int sampleRate{48000};

  PhasedValueSource(int sr, float overlap)
  : amplitude_(std::make_shared<ConstantSource>(1.0f)),
    overlapSeconds(overlap), sampleRate(sr) {}

  void set_amplitude(std::shared_ptr<ValueSource> s) { amplitude_ = std::move(s); }
  std::shared_ptr<ValueSource> get_amplitude() const { return amplitude_; }

  const char* type_name() const override { return "PhasedValueSource"; }
  SourceCategory category() const override { return SourceCategory::Envelope; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude", 1.0f, 0.0f, 10.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude") { amplitude_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  void add_stage(Stage s) { stages_.push_back(std::move(s)); }

  void prepare(const RenderContext& ctx, int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;
    overlapSamples_ = int(overlapSeconds * float(sampleRate));

    if (amplitude_) amplitude_->prepare(ctx, frames);

    // Calculate per-stage sample counts
    int expandIdx = -1;
    int totCount = 0;
    stageCounts_.resize(stages_.size(), 0);

    float duration = float(frames) / float(sampleRate);

    for (int i = 0; i < int(stages_.size()); ++i) {
      if (stages_[i].percent == 0.0f) {
        expandIdx = i;
        continue;
      }

      float stgDur = stages_[i].percent * duration;
      if (stages_[i].maxSec > 0)
        stgDur = std::min(stages_[i].maxSec, std::max(stages_[i].minSec, stgDur));
      stageCounts_[i] = int(std::lround(stgDur * sampleRate));

      if (i == int(stages_.size()) - 1 && expandIdx < 0)
        stageCounts_[i] = frames - totCount;

      totCount += stageCounts_[i];
    }

    if (expandIdx >= 0) {
      int halfOl = overlapSamples_ / (expandIdx == 0 || expandIdx == int(stages_.size()) - 1 ? 2 : 1);
      stageCounts_[expandIdx] = std::max(0, frames - totCount + halfOl);
    }

    // Prepare each stage source with its count + overlap
    for (int i = 0; i < int(stages_.size()); ++i) {
      int extra = overlapSamples_ / (i == 0 || i == int(stages_.size()) - 1 ? 2 : 1);
      stages_[i].source->prepare(ctx, stageCounts_[i] + extra);
    }

    // Precompute cumulative boundaries (excluding overlap)
    boundaries_.resize(stages_.size());
    int cum = 0;
    for (int i = 0; i < int(stages_.size()); ++i) {
      int olAdj = overlapSamples_ / (i == 0 || i == int(stages_.size()) - 1 ? 2 : 1);
      cum += stageCounts_[i];
      boundaries_[i] = cum;
    }
  }

  float next() override {
    ++ptr_;
    float amp = amplitude_ ? (amplitude_->next(), amplitude_->current()) : 1.0f;

    // Find current stage
    int stageIdx = 0;
    int prevBound = 0;
    for (int i = 0; i < int(boundaries_.size()); ++i) {
      if (ptr_ < boundaries_[i]) { stageIdx = i; break; }
      prevBound = boundaries_[i];
      stageIdx = i;
    }

    int posInStage = ptr_ - prevBound;
    int stageLen = boundaries_[stageIdx] - prevBound;
    int halfOl = overlapSamples_ / 2;

    // Check if we're in a transition zone
    bool transFromPrev = (stageIdx > 0 && posInStage < halfOl);
    bool transToNext = (stageIdx < int(stages_.size()) - 1 && (stageLen - posInStage) <= halfOl);

    if (transFromPrev) {
      // Crossfade from previous stage to current
      float vPrev = stages_[stageIdx - 1].source->next();
      float vCurr = stages_[stageIdx].source->next();
      float t = float(posInStage) / float(halfOl);
      cur_ = (vPrev * (1.0f - t) + vCurr * stages_[stageIdx].gainAdj * t) * amp;
    } else if (transToNext) {
      // Crossfade from current to next stage
      float vCurr = stages_[stageIdx].source->next();
      float vNext = stages_[stageIdx + 1].source->next();
      float t = float(halfOl - (stageLen - posInStage)) / float(halfOl);
      cur_ = (vCurr * (1.0f - t) + vNext * stages_[stageIdx + 1].gainAdj * t) * amp;
    } else {
      // Not in transition
      cur_ = stages_[stageIdx].source->next() * stages_[stageIdx].gainAdj * amp;
    }

    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> amplitude_;
  std::vector<Stage> stages_;
  std::vector<int> stageCounts_;
  std::vector<int> boundaries_;
  int totalFrames_{0};
  int overlapSamples_{0};
  int ptr_{-1};
  float cur_{0.0f};
};

} // namespace mforce
