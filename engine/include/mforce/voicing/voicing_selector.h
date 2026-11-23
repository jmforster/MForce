#pragma once
#include "mforce/music/basics.h"
#include "mforce/voicing/voicing_profile.h"
#include <optional>
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// VoicingRequest — inputs to a VoicingSelector. Captures the abstract chord
// to voice plus any contextual hints the selector may use to score candidates.
// ---------------------------------------------------------------------------
struct VoicingRequest {
  ScaleChord scaleChord;              // what to voice (degree + alt + quality)
  const Scale* scale{nullptr};        // interpretive context for degree -> pitch
  int rootOctave{3};                  // where to place the root
  float durationBeats{1.0f};          // passes through to Chord.dur
  const Chord* previous{nullptr};     // prior chord on this Part (for voice-leading)
  std::optional<Pitch> melodyPitch;   // optional top-voice hint (future: melody-aware)
  VoicingProfile profile;             // priority + allow-lists for this chord
  std::string dictionaryName;         // ChordDictionary name; empty = Canonic
};

// ---------------------------------------------------------------------------
// VoicingSelector — resolves a ScaleChord into a concrete Chord with pitches.
// Implementations differ in which dictionaries they search and how they score
// candidates (voice-leading distance, melody clash, style preference, etc.).
// ---------------------------------------------------------------------------
class VoicingSelector {
 public:
  virtual ~VoicingSelector() = default;
  virtual std::string name() const = 0;
  virtual Chord select(const VoicingRequest& req) = 0;
};

}  // namespace mforce
