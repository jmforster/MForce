#pragma once
#include "mforce/music/conductor.h"
#include "mforce/music/structure.h"
#include "mforce/patch/patch.h"
#include "mforce/render/instrument.h"
#include "mforce/render/mixer.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

// Which instrument plays each part, by Part::instrumentType. Not owned.
struct Ensemble {
  std::unordered_map<std::string, Instrument*> byPart;
};

// A performance realized: the interleaved stereo frames and what the host
// prints about them.
struct PlayResult {
  std::vector<float> stereo;       // interleaved
  int   sampleRate{0};
  int   frames{0};
  float peak{0.0f};                // over the stereo frames
  double rms{0.0};                 // over the stereo frames
  float mixPeak{0.0f};             // the mono sum's peak before any scaling
  float scale{1.0f};               // the gain the mix was scaled by; 1 when it was not
  double buildMs{0.0};             // wall-clock of everything before the final mix
  double renderMs{0.0};            // wall-clock of the final mix
  std::vector<FaultReport> faults;
  std::vector<std::string> warnings;
};

// A piece on its instruments. Each part with events is performed once over
// the whole piece and played on its instrument. Each distinct instrument
// renders mono; the renders are summed; a sum of more than one instrument
// whose peak passes 0.98 is scaled to 0.98; mono goes to both channels. The
// length is the piece's seconds plus the tail. The instruments are cleared
// after their render: clear forgets what was rendered, not the voices' state
// or the rotation, so a second play on the same instruments continues from
// where the first left them.
PlayResult play(const Piece& piece, const Ensemble& ensemble,
                Conductor& conductor, float tailSeconds = 2.0f);

// A patch by itself: its embedded score on its own instrument, or its plain
// sound, rendered through its mixer.
PlayResult play(const Patch& patch);

} // namespace mforce
