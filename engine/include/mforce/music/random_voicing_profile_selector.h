#pragma once
#include "mforce/core/randomizer.h"
#include "mforce/music/voicing_profile_selector.h"
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// RandomVoicingProfileSelector — samples a fresh profile uniformly per chord.
//
// Config (nlohmann::json):
//   priorityMin (float, default 0.0)
//   priorityMax (float, default 1.0)
//   inversionProfiles (optional list-of-lists<int>): each call picks one
//     profile uniformly; empty/absent = leave allowedInversions empty
//     (any inversion allowed).
//   spreadProfiles  (optional list-of-lists<int>): analogous for spread.
//
// Per-chord samples are independent — no temporal continuity. For smoother
// variation, use DriftVoicingProfileSelector instead.
// ---------------------------------------------------------------------------
class RandomVoicingProfileSelector : public VoicingProfileSelector {
 public:
  std::string name() const override { return "random"; }

  void configure_from_json(const nlohmann::json& cfg) override {
    priorityMin_ = cfg.value("priorityMin", 0.0f);
    priorityMax_ = cfg.value("priorityMax", 1.0f);
    if (priorityMax_ < priorityMin_) std::swap(priorityMin_, priorityMax_);

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

  void reset(uint32_t seed) override { rng_ = Randomizer(seed); }

  VoicingProfile profile_for_chord(int /*chordIdx*/,
                                   float /*beatInBar*/,
                                   float /*beatInPassage*/) override {
    VoicingProfile p;
    p.priority = rng_.range(priorityMin_, priorityMax_);
    if (!inversionProfiles_.empty()) {
      int i = rng_.int_range(0, int(inversionProfiles_.size()) - 1);
      p.allowedInversions = inversionProfiles_[i];
    }
    if (!spreadProfiles_.empty()) {
      int i = rng_.int_range(0, int(spreadProfiles_.size()) - 1);
      p.allowedSpreads = spreadProfiles_[i];
    }
    return p;
  }

 private:
  Randomizer rng_{0};
  float priorityMin_{0.0f};
  float priorityMax_{1.0f};
  std::vector<std::vector<int>> inversionProfiles_;
  std::vector<std::vector<int>> spreadProfiles_;
};

}  // namespace mforce
