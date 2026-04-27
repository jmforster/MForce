#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Utility.RampType
enum class RampType { Linear, Expo, InverseExpo, Sine };

// ---------------------------------------------------------------------------
// Ported from C# MForce.Utility.Ramp
// Moves from startVal to endVal over normalized position [0,1].
// ---------------------------------------------------------------------------
struct Ramp {
  float startVal{0.0f}, endVal{1.0f};
  RampType type{RampType::Linear};
  float power{0.0f};
  float holdPct{0.0f};

  float value(float pos) const {
    if (pos <= holdPct) return startVal;

    float t = (pos - holdPct) / (1.0f - holdPct);
    float range = endVal - startVal;

    if (type == RampType::Linear) {
      return startVal + range * t;
    }

    if (type == RampType::Expo) {
      return startVal < endVal
        ? startVal + range * std::pow(t, power)
        : endVal - range * std::pow(1.0f - t, power);
    }

    if (type == RampType::InverseExpo) {
      return startVal < endVal
        ? endVal - range * std::pow(1.0f - t, power)  // note: inverted vs Expo
        : startVal + range * std::pow(t, power);
    }

    // Sine
    if (power == 0.0f) {
      constexpr float PI = 3.14159265358979323846f;
      return startVal + range * (std::cos((1.0f + t) * PI) + 1.0f) * 0.5f;
    }

    // Pseudo-sine: two spliced expo curves
    if (startVal < endVal) {
      return t < 0.5f
        ? startVal + range * std::pow(t * 2.0f, power) * 0.5f
        : endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f + 0.5f;
    }
    return t < 0.5f
      ? endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f
      : startVal + range * std::pow(t * 2.0f, power) * 0.5f + 0.5f;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# StagedValueSource + MEnvelope + preset subclasses.
// A multi-stage envelope where each stage is a Ramp with timing controls.
// ---------------------------------------------------------------------------
struct Envelope : ValueSource {

  struct Stage {
    Ramp ramp;
    float percent{0.0f};  // fraction of total duration; 0 = expand to fill
    float minSec{0.0f};
    float maxSec{0.0f};
  };

  explicit Envelope(int sampleRate) : sampleRate_(sampleRate) {}

  const char* type_name() const override { return "Envelope"; }
  SourceCategory category() const override { return SourceCategory::Envelope; }

  void add_stage(Stage s) { stages_.push_back(s); }

  void prepare(const RenderContext& ctx, int frames) override {
    totalFrames_ = frames;
    float duration = float(frames) / float(sampleRate_);

    stageCounts_.resize(stages_.size(), 0);

    int expandIdx = -1;
    int totCount = 0;

    for (int i = 0; i < int(stages_.size()); ++i) {
      if (stages_[i].percent == 0.0f) {
        expandIdx = i;     // last 0% stage becomes expand
        stageCounts_[i] = 0;
        continue;
      }

      float stgDur = duration * stages_[i].percent;
      stgDur = std::clamp(stgDur, stages_[i].minSec, stages_[i].maxSec > 0 ? stages_[i].maxSec : stgDur);
      stageCounts_[i] = int(std::lround(stgDur * sampleRate_));

      // Rounding fix for last non-expand stage
      if (i == int(stages_.size()) - 1 && expandIdx < 0) {
        if (std::abs(totCount + stageCounts_[i] - frames) <= 1)
          stageCounts_[i] = frames - totCount;
      }

      totCount += stageCounts_[i];
    }

    if (expandIdx >= 0) {
      stageCounts_[expandIdx] = std::max(0, frames - totCount);
    }

    ptr_ = -1;
    currStage_ = 0;
    stageStart_ = 0;
    stageEnd_ = stageCounts_.empty() ? 0 : stageCounts_[0];
  }

  float next() override {
    // Pre-prepare safety: stageCounts_ is sized in prepare(); without it,
    // stageCounts_[currStage_] indexes a null buffer. Reachable when UI
    // display paths (e.g. draw_formant_strip) cascade next() through a
    // ValueSource chain that includes an unprepared Envelope.
    if (stageCounts_.empty()) { cur_ = 0.0f; return cur_; }

    ++ptr_;

    // Advance stage if needed
    while (ptr_ >= stageEnd_ && currStage_ < int(stages_.size()) - 1) {
      stageStart_ = stageEnd_;
      ++currStage_;
      if (currStage_ >= int(stageCounts_.size())) {
        cur_ = 0.0f; return cur_;
      }
      stageEnd_ += stageCounts_[currStage_];
    }

    // Past last stage → output 0
    if (ptr_ >= stageEnd_ && currStage_ == int(stages_.size()) - 1) {
      cur_ = 0.0f;
      return cur_;
    }

    int count = stageCounts_[currStage_];
    float pos = (count > 0) ? float(ptr_ - stageStart_) / float(count) : 1.0f;
    cur_ = stages_[currStage_].ramp.value(pos);
    return cur_;
  }

  float current() const override { return cur_; }

  // ----- Preset factories -----

  // Attack → Release (expand)
  static Envelope make_ar(int sampleRate, float attackPct, float attackMin = 0.0f, float attackMax = 1.0f) {
    Envelope env(sampleRate);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attackPct, attackMin, attackMax});
    env.add_stage({{1.0f, 0.0f, RampType::Sine,   0.0f}, 0.0f, 0.0f, 0.0f});  // expand
    return env;
  }

  // Attack → Decay → Sustain (expand) → Release
  static Envelope make_adsr(int sampleRate,
                            float attackPct, float decayPct, float sustainLevel, float releasePct,
                            float attackMin = 0.05f, float attackMax = 1.0f,
                            float decayMin = 0.025f, float decayMax = 0.5f,
                            float releaseMin = 0.0f, float releaseMax = 0.0f) {
    Envelope env(sampleRate);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attackPct, attackMin, attackMax});
    env.add_stage({{1.0f, sustainLevel, RampType::Linear, 0.0f}, decayPct, decayMin, decayMax});
    env.add_stage({{sustainLevel, sustainLevel, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f}); // expand
    env.add_stage({{sustainLevel, 0.0f, RampType::Sine, 0.0f}, releasePct, releaseMin, releaseMax});
    return env;
  }

private:
  int sampleRate_;
  int totalFrames_{0};
  int ptr_{-1};
  int currStage_{0};
  int stageStart_{0};
  int stageEnd_{0};
  float cur_{0.0f};
  std::vector<Stage> stages_;
  std::vector<int>   stageCounts_;
};

} // namespace mforce
