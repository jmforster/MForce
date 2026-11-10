#pragma once
#include <string>

namespace mforce {

struct PitchedInstrument;

// The short test score a patch file may carry under "score", handed over as
// the JSON text of that array. It is performed at load the way the pre-render
// delivers notes: a NotePerformer at 60 bpm, so beats are seconds, as the
// score's "time" and "duration" are. Returns the score's end in seconds, which
// the caller turns into the frame count. It lives here until the play function
// that performs any score replaces it.
double play_embedded_score(PitchedInstrument& inst, const std::string& scoreText);

} // namespace mforce
