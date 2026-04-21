#pragma once
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// VoicingProfile — the smallest unit of voicing style. Consulted per chord
// by a VoicingSelector; produced per chord by a VoicingProfileSelector (the
// profile-selector layer above).
//
// Empty allow-lists mean "all values allowed"; populated lists filter the
// selector's candidate enumeration for that chord.
// ---------------------------------------------------------------------------
struct VoicingProfile {
  std::vector<int> allowedInversions;  // empty = any
  std::vector<int> allowedSpreads;     // empty = any
  float priority{0.0f};                 // [0,1] — 0 = pure VL, 1 = pure CT
};

}  // namespace mforce
