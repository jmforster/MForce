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
// Each stage has: a source, a percent of the total duration, min/max seconds
// and a gain. A stage with percent=0 is the "expand" stage (gets the
// remaining duration). Overlap is specified in seconds and converted to
// samples. Only the stage or stages in play are pulled each sample, so the
// node prepares, starts and advances its sources itself.
struct PhasedValueSource final : Generator {
  Param amplitude{this, "amplitude", 1.0f, 0.0f, 10.0f, "0-1"};
  // The stages' signals, in order. The table gives each its span, by
  // position: a feed past the table's end takes the declared defaults.
  InputList sources{this, "sources"};
  Setting<float> overlap{this, "overlap", 0.05f, 0.0f, 10.0f};   // seconds of crossfade between adjacent stages
  Array percent{this, "percent", "stages", 0.0f, 0.0f, 1.0f};
  Array minSec {this, "minSec",  "stages", 0.0f, 0.0f, 1000.0f};
  Array maxSec {this, "maxSec",  "stages", 0.0f, 0.0f, 1000.0f};
  Array gain   {this, "gain",    "stages", 1.0f, -100.0f, 100.0f};

  const char* type_name() const override { return "PhasedValueSource"; }

  void on_input_list_changed(std::string_view) override { rebuild_stages_(); }
  void on_array_changed(std::string_view) override { rebuild_stages_(); }

  void on_prepare(const RenderContext& ctx) override {
    for (auto& s : stages_) s.source->prepare(ctx);
    // The counters and boundaries a note lays out are sized here for the
    // stage list and filled within capacity in on_start.
    stageCounts_.assign(stages_.size(), 0);
    boundaries_.assign(stages_.size(), 0);
  }

  void on_start(int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;
    overlapSamples_ = int(overlap.value * float(sample_rate()));

    // Calculate per-stage sample counts
    int expandIdx = -1;
    int totCount = 0;
    stageCounts_.resize(stages_.size(), 0);

    float duration = float(frames) / float(sample_rate());

    for (int i = 0; i < int(stages_.size()); ++i) {
      if (stages_[i].percent == 0.0f) {
        expandIdx = i;
        continue;
      }

      float stgDur = stages_[i].percent * duration;
      if (stages_[i].maxSec > 0)
        stgDur = std::min(stages_[i].maxSec, std::max(stages_[i].minSec, stgDur));
      stageCounts_[i] = int(std::lround(stgDur * sample_rate()));

      if (i == int(stages_.size()) - 1 && expandIdx < 0)
        stageCounts_[i] = frames - totCount;

      totCount += stageCounts_[i];
    }

    if (expandIdx >= 0) {
      int halfOl = overlapSamples_ / (expandIdx == 0 || expandIdx == int(stages_.size()) - 1 ? 2 : 1);
      stageCounts_[expandIdx] = std::max(0, frames - totCount + halfOl);
    }

    // Start each stage source with its count + overlap
    for (int i = 0; i < int(stages_.size()); ++i) {
      int extra = overlapSamples_ / (i == 0 || i == int(stages_.size()) - 1 ? 2 : 1);
      stages_[i].source->start(stageCounts_[i] + extra);
    }

    // Precompute cumulative boundaries (excluding overlap)
    boundaries_.resize(stages_.size());
    int cum = 0;
    for (int i = 0; i < int(stages_.size()); ++i) {
      cum += stageCounts_[i];
      boundaries_[i] = cum;
    }
  }

  float compute() override {
    if (stages_.empty()) return 0.0f;
    ++ptr_;
    float amp = amplitude.current();

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
      return (vPrev * (1.0f - t) + vCurr * stages_[stageIdx].gainAdj * t) * amp;
    } else if (transToNext) {
      // Crossfade from current to next stage
      float vCurr = stages_[stageIdx].source->next();
      float vNext = stages_[stageIdx + 1].source->next();
      float t = float(halfOl - (stageLen - posInStage)) / float(halfOl);
      return (vCurr * (1.0f - t) + vNext * stages_[stageIdx + 1].gainAdj * t) * amp;
    } else {
      // Not in transition
      return stages_[stageIdx].source->next() * stages_[stageIdx].gainAdj * amp;
    }
  }

private:
  struct Stage {
    std::shared_ptr<ValueSource> source;
    float percent{0.0f};
    float minSec{0.0f};
    float maxSec{0.0f};
    float gainAdj{1.0f};
  };

  // The layout compute() walks: stage i is feed i with row i of the table,
  // or the columns' defaults past its end. Derived from the list and the
  // four columns whenever one changes, and written nowhere else. A walk laid
  // out for more stages than the list now holds is cut to the list, so
  // compute() stays inside it until the next start lays the walk out again.
  void rebuild_stages_() {
    const auto row = [](const Array& column, size_t i) {
      return i < column.values.size() ? column.values[i] : column.def;
    };
    stages_.clear();
    for (size_t i = 0; i < sources.sources.size(); ++i)
      stages_.push_back({sources.sources[i], row(percent, i), row(minSec, i), row(maxSec, i), row(gain, i)});
    if (stageCounts_.size() > stages_.size()) stageCounts_.resize(stages_.size());
    if (boundaries_.size() > stages_.size()) boundaries_.resize(stages_.size());
  }

  std::vector<Stage> stages_;
  std::vector<int> stageCounts_;
  std::vector<int> boundaries_;
  int totalFrames_{0};
  int overlapSamples_{0};
  int ptr_{-1};
};

} // namespace mforce
