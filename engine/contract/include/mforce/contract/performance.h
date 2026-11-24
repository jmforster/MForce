#pragma once
#include "mforce/contract/pitch_curve.h"
#include <optional>
#include <vector>

namespace mforce {

// One note as a performer decided it: when it starts, how long it lasts, its
// pitch and loudness, how it begins (`onset`, an id from the instrument's
// InstrumentClass) and whether the excitation continues past its end
// (`hold`). The note owns its pitch curve, so it can be copied and kept.
struct PerformedNote {
  float start{0.0f};       // seconds from the start of the performance
  float duration{1.0f};    // seconds
  float noteNumber{60.0f};
  float velocity{0.8f};
  int   onset{0};          // InstrumentClass::onset_id; 0 = none
  bool  hold{false};       // the excitation continues past the note's end
  std::optional<PitchCurve> curve;   // owned
};

// One drum hit: when, how long, which drum, how hard.
struct PerformedHit {
  float start{0.0f};
  float duration{0.1f};    // seconds
  int   drumNumber{0};
  float velocity{1.0f};
};

// How long a voice plays on past the end of its note before the ring-out
// rule decides whether it is quiet: a performance whose last note ends at t
// is heard to t plus this.
inline constexpr float kVoiceTailSeconds = 0.4f;

// What a performer made of one part, in the order it is to be played.
struct Performance {
  std::vector<PerformedNote> notes;
  std::vector<PerformedHit>  hits;
};

} // namespace mforce
