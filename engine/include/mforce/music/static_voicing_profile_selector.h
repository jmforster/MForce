#pragma once
#include "mforce/music/voicing_profile_selector.h"

namespace mforce {

// ---------------------------------------------------------------------------
// StaticVoicingProfileSelector — returns the configured baseline profile
// unchanged for every chord. The implicit default when no profile selector
// name is set on the PassageTemplate. Equivalent to pre-selector behavior.
// ---------------------------------------------------------------------------
class StaticVoicingProfileSelector : public VoicingProfileSelector {
 public:
  void configure(const VoicingProfile& baseline) { baseline_ = baseline; }

  std::string name() const override { return "static"; }
  void reset(uint32_t /*seed*/) override {}

  VoicingProfile profile_for_chord(int /*chordIdx*/,
                                   float /*beatInBar*/,
                                   float /*beatInPassage*/) override {
    return baseline_;
  }

 private:
  VoicingProfile baseline_;
};

}  // namespace mforce
