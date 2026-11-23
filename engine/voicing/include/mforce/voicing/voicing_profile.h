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

  // --- Three refinements. All default to OFF, so a profile that does not
  // set them scores by distance and common tones alone.

  // NOTE: there is deliberately no register/drift term here: both forms
  // tried measured worse than none, and the symptom one aimed at
  // ("voicings trend into the stratosphere") is not observed — on
  // test_jazz_turnaround_smooth the top voice drifts 0 semitones over 16
  // chords, span 3.

  // Penalty weight on choosing the same voicing as the previous chord.
  // A repeated chord makes voice-leading distance 0 for the identical
  // voicing, which wins by a mile, so a static or drift selector emits the
  // same shape twice. (The random selector dodges this by accident.)
  float repeatPenalty{0.0f};

  // This chord is a cadential arrival: restrict it to root position, so a
  // resolution lands stable instead of merely smooth: the chord Parts'
  // counterpart of the melody's Cadential function.
  bool cadential{false};
};

}  // namespace mforce
