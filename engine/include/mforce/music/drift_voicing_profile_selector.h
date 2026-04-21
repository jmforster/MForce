#pragma once
#include "mforce/core/randomizer.h"
#include "mforce/music/voicing_profile_selector.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// DriftVoicingProfileSelector — coherent random walk over profile parameters.
//
// Priority drifts via gaussian steps (mean 0, stddev priorityStepMax),
// clamped to [priorityMin, priorityMax]. At each chord, with probability
// profileTransitionProb, rotate to the next configured inversion/spread
// profile; otherwise retain the current one.
//
// Produces more coherent variation than independent per-chord sampling:
// each chord's profile is closely related to the previous one.
//
// Config (nlohmann::json):
//   priorityMin             (float, default 0.0)
//   priorityMax             (float, default 1.0)
//   priorityStepMax         (float, default 0.15) — gaussian stddev
//   profileTransitionProb   (float, default 0.2)  — chance to rotate profile
//   inversionProfiles       (list-of-lists<int>)
//   spreadProfiles          (list-of-lists<int>)
// ---------------------------------------------------------------------------
class DriftVoicingProfileSelector : public VoicingProfileSelector {
 public:
  std::string name() const override { return "drift"; }

  void configure_from_json(const nlohmann::json& cfg) override {
    priorityMin_ = cfg.value("priorityMin", 0.0f);
    priorityMax_ = cfg.value("priorityMax", 1.0f);
    if (priorityMax_ < priorityMin_) std::swap(priorityMin_, priorityMax_);
    priorityStepMax_ = cfg.value("priorityStepMax", 0.15f);
    profileTransitionProb_ = cfg.value("profileTransitionProb", 0.2f);

    inversionProfiles_.clear();
    if (cfg.contains("inversionProfiles") && cfg["inversionProfiles"].is_array()) {
      for (const auto& pj : cfg["inversionProfiles"]) {
        if (!pj.is_array()) continue;
        std::vector<int> prof;
        for (const auto& v : pj) prof.push_back(v.get<int>());
        inversionProfiles_.push_back(std::move(prof));
      }
    }

    spreadProfiles_.clear();
    if (cfg.contains("spreadProfiles") && cfg["spreadProfiles"].is_array()) {
      for (const auto& pj : cfg["spreadProfiles"]) {
        if (!pj.is_array()) continue;
        std::vector<int> prof;
        for (const auto& v : pj) prof.push_back(v.get<int>());
        spreadProfiles_.push_back(std::move(prof));
      }
    }
  }

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
