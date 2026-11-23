#pragma once
#include "mforce/music/no_settings.h"
#include "mforce/music/voicing_profile_selector.h"

namespace mforce {

// ---------------------------------------------------------------------------
// StaticVoicingProfileSelector — returns the passage's baseline profile
// unchanged for every chord. The selector used when a passage names none.
// ---------------------------------------------------------------------------
class StaticVoicingProfileSelector : public VoicingProfileSelector {
 public:
  static constexpr const char* kName = "static";
  using Settings = NoSettings;  // the baseline is the passage's voicingProfile

  explicit StaticVoicingProfileSelector(const VoicingProfile& baseline)
      : baseline_(baseline) {}

  std::string name() const override { return kName; }
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
