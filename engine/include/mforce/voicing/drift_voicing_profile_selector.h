#pragma once
#include "mforce/core/randomizer.h"
#include "mforce/voicing/voicing_profile_selector.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace mforce {

// The settings of DriftVoicingProfileSelector.
struct DriftProfileSettings {
  float priorityMin{0.0f};
  float priorityMax{1.0f};
  float priorityStepMax{0.15f};        // gaussian stddev
  float profileTransitionProb{0.2f};   // chance to rotate profile
  std::vector<std::vector<int>> inversionProfiles;
  std::vector<std::vector<int>> spreadProfiles;
};

// ---------------------------------------------------------------------------
// DriftVoicingProfileSelector — coherent random walk over profile parameters.
//
// Priority drifts via gaussian steps (mean 0, stddev priorityStepMax),
// clamped to [priorityMin, priorityMax]; a reversed range is swapped at
// construction. At each chord, with probability profileTransitionProb,
// rotate to the next configured inversion/spread profile; otherwise retain
// the current one.
//
// Produces more coherent variation than independent per-chord sampling:
// each chord's profile is closely related to the previous one.
// ---------------------------------------------------------------------------
class DriftVoicingProfileSelector : public VoicingProfileSelector {
 public:
  static constexpr const char* kName = "drift";
  using Settings = DriftProfileSettings;

  explicit DriftVoicingProfileSelector(const Settings& s)
      : priorityMin_(s.priorityMin), priorityMax_(s.priorityMax),
        priorityStepMax_(s.priorityStepMax),
        profileTransitionProb_(s.profileTransitionProb),
        inversionProfiles_(s.inversionProfiles), spreadProfiles_(s.spreadProfiles) {
    if (priorityMax_ < priorityMin_) std::swap(priorityMin_, priorityMax_);
  }

  std::string name() const override { return kName; }

  void reset(uint32_t seed) override {
    rng_ = Randomizer(seed);
    // Start priority mid-range so drift can go either way initially.
    currentPriority_ = 0.5f * (priorityMin_ + priorityMax_);
    invIdx_ = 0;
    sprIdx_ = 0;
  }

  VoicingProfile profile_for_chord(int /*chordIdx*/,
                                   float /*beatInBar*/,
                                   float /*beatInPassage*/) override {
    // Gaussian step (Box-Muller). Update current priority, clamp to range.
    float u1 = rng_.value(); if (u1 < 1e-6f) u1 = 1e-6f;
    float u2 = rng_.value();
    float z = std::sqrt(-2.0f * std::log(u1))
            * std::cos(2.0f * 3.14159265358979f * u2);
    currentPriority_ += z * priorityStepMax_;
    currentPriority_ = std::clamp(currentPriority_, priorityMin_, priorityMax_);

    // Profile index rotation (rare transition).
    if (!inversionProfiles_.empty() && rng_.decide(profileTransitionProb_)) {
      invIdx_ = (invIdx_ + 1) % int(inversionProfiles_.size());
    }
    if (!spreadProfiles_.empty() && rng_.decide(profileTransitionProb_)) {
      sprIdx_ = (sprIdx_ + 1) % int(spreadProfiles_.size());
    }

    VoicingProfile p;
    p.priority = currentPriority_;
    if (!inversionProfiles_.empty())
      p.allowedInversions = inversionProfiles_[invIdx_];
    if (!spreadProfiles_.empty())
      p.allowedSpreads = spreadProfiles_[sprIdx_];
    return p;
  }

 private:
  Randomizer rng_{0};
  float priorityMin_{0.0f};
  float priorityMax_{1.0f};
  float priorityStepMax_{0.15f};
  float profileTransitionProb_{0.2f};
  std::vector<std::vector<int>> inversionProfiles_;
  std::vector<std::vector<int>> spreadProfiles_;

  float currentPriority_{0.5f};
  int invIdx_{0};
  int sprIdx_{0};
};

}  // namespace mforce
