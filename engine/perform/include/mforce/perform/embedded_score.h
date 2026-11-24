#pragma once
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
#include <optional>
#include <string>
#include <vector>

namespace mforce {

// A patch's embedded score, performed: the short test score a patch file may
// carry under "score", read from the JSON text of that array. Its phrased
// notes are copied across; the rest go through a NotePerformer at 60 bpm, so
// beats are seconds, as the score's "time" and "duration" are.
struct EmbeddedScore {
  Performance performance;
  double endSeconds{0.0};               // the score's end
  std::vector<std::string> warnings;    // each once; the host prints them
};

// `cls` decides what the score may ask of the instrument: "hold" is dropped
// on one that does not sustain, onset names are looked up in its vocabulary.
// Throws on articulation or ornament on a phrased note.
EmbeddedScore perform_embedded_score(const std::string& scoreJson,
                                     const InstrumentClass& cls);

// The first note of an embedded score as it is written, not as performed:
// its "note", "velocity" and "duration", each defaulted when absent (60, 0.8,
// 2 seconds). None when the score is empty text, not an array, an empty
// array, or its first entry is not an object.
struct ScoreNote {
  float note{60.0f};
  float velocity{0.8f};
  float duration{2.0f};
};
std::optional<ScoreNote> first_score_note(const std::string& scoreJson);

// The score a new patch is saved with, as the patch's score text: one note,
// 60 at velocity 0.8, at time 0, two seconds long.
std::string default_embedded_score();

} // namespace mforce
