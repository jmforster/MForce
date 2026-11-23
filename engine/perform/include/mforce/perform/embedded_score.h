#pragma once
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
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

} // namespace mforce
