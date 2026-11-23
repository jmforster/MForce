#pragma once
#include "mforce/core/randomizer.h"
#include "mforce/music/voicing_profile_selector.h"
#include <utility>
#include <vector>

namespace mforce {

// The settings of RandomVoicingProfileSelector.
struct RandomProfileSettings {
  float priorityMin{0.0f};
  float priorityMax{1.0f};
  // Each call picks one profile uniformly; empty = leave allowedInversions
  // empty (any inversion allowed).
  std::vector<std::vector<int>> inversionProfiles;
  std::vector<std::vector<int>> spreadProfiles;  // analogous for spread
};

// ---------------------------------------------------------------------------
// RandomVoicingProfileSelector — samples a fresh profile uniformly per chord.
// A reversed priority range is swapped at construction.
//
// Per-chord samples are independent — no temporal continuity. For smoother
// variation, use DriftVoicingProfileSelector instead.
// ---------------------------------------------------------------------------
class RandomVoicingProfileSelector : public VoicingProfileSelector {
 public:
  static constexpr const char* kName = "random";
  using Settings = RandomProfileSettings;

  explicit RandomVoicingProfileSelector(const Settings& s)
      : priorityMin_(s.priorityMin), priorityMax_(s.priorityMax),
        inversionProfiles_(s.inversionProfiles), spreadProfiles_(s.spreadProfiles) {
    if (priorityMax_ < priorityMin_) std::swap(priorityMin_, priorityMax_);
  }

  std::string name() const override { return kName; }

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
