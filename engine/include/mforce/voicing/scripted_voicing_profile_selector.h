#pragma once
#include "mforce/voicing/voicing_profile_selector.h"
#include <vector>

namespace mforce {

// The settings of ScriptedVoicingProfileSelector: the profiles it cycles
// through, in order.
struct ScriptedProfileSettings {
  std::vector<VoicingProfile> sequence;
};

// ---------------------------------------------------------------------------
// ScriptedVoicingProfileSelector — cycles through an authored profile
// sequence. Deterministic — no randomness. Use when you want
// "bar 1 tight, bar 2 loose, bar 3 open, bar 4 back home" as a repeating
// figure. Returns sequence[chordIdx % sequence.size()] verbatim; an empty
// sequence gives the default profile.
// ---------------------------------------------------------------------------
class ScriptedVoicingProfileSelector : public VoicingProfileSelector {
 public:
  static constexpr const char* kName = "scripted";
  using Settings = ScriptedProfileSettings;

  explicit ScriptedVoicingProfileSelector(const Settings& s) : sequence_(s.sequence) {}

  std::string name() const override { return kName; }

  void reset(uint32_t /*seed*/) override {}  // deterministic; no RNG

  VoicingProfile profile_for_chord(int chordIdx,
                                   float /*beatInBar*/,
                                   float /*beatInPassage*/) override {
    if (sequence_.empty()) return VoicingProfile{};
    int idx = chordIdx % int(sequence_.size());
    if (idx < 0) idx += int(sequence_.size());
    return sequence_[idx];
  }

 private:
  std::vector<VoicingProfile> sequence_;
};

}  // namespace mforce
