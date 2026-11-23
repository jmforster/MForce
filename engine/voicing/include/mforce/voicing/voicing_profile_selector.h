#pragma once
#include "mforce/voicing/voicing_profile.h"
#include <cstdint>
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// VoicingProfileSelector — produces a VoicingProfile per chord position.
// The per-Passage "selector layer above the VoicingSelector": varies the
// profile (priority, inversion allow-list, spread allow-list) chord-by-
// chord under author-configurable policies. Each is constructed from its
// own typed settings (the static one from the passage's baseline profile).
//
// Implementations ship with their own per-chord logic:
//   StaticVoicingProfileSelector   — returns the same profile every chord
//   RandomVoicingProfileSelector   — samples independently per chord
//   DriftVoicingProfileSelector    — smooth random walk
//   ScriptedVoicingProfileSelector — cycles through an authored sequence
// ---------------------------------------------------------------------------
class VoicingProfileSelector {
 public:
  virtual ~VoicingProfileSelector() = default;
  virtual std::string name() const = 0;
  virtual void reset(uint32_t seed) = 0;

  // Called per chord in chord order. `beatInBar` and `beatInPassage` are
  // float beat positions (from bar start and from passage start).
  virtual VoicingProfile profile_for_chord(int chordIdx,
                                           float beatInBar,
                                           float beatInPassage) = 0;
};

}  // namespace mforce
