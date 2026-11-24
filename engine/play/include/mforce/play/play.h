#pragma once
#include "mforce/perform/conductor.h"
#include "mforce/music/structure.h"
#include "mforce/patch/patch.h"
#include "mforce/render/instrument.h"
#include "mforce/render/mixer.h"
#include <string>
#include <unordered_map>
#include <utility>
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
  std::vector<std::vector<float>> strips;   // one per captured node id, in the order asked; timeline-length
};

// A piece on its instruments. Each part with events is performed once over
// the whole piece and played on its instrument. Each distinct instrument
// renders mono; the renders are summed; a sum of more than one instrument
// whose peak passes 0.98 is scaled to 0.98; mono goes to both channels. The
// length is the piece's seconds plus the tail. The instruments are cleared
// after their render: clear forgets what was rendered, not the voices' state
// or the rotation, so a second play on the same instruments continues from
// where the first left them. A part with passages and no events plays
// nothing and adds a warning naming it.
PlayResult play(const Piece& piece, const Ensemble& ensemble,
                Conductor& conductor, float tailSeconds = 2.0f);

// A patch by itself: its embedded score on its own instrument, or its plain
// sound, rendered through its mixer.
PlayResult play(const Patch& patch);

// How loud a render comes out. Guarded: through the mixer, as play(patch)
// renders. Normalised: the instrument alone, its volume 1 and its guard off,
// the whole scaled by 0.95 over its peak when the peak passes 0.95.
enum class Level { Guarded, Normalised };

// The patch's instrument playing `performance`, rendered for `seconds` at the
// patch's rate; each node of `capture` recorded as a strip, by id, in that
// order (a node's raw output, every voice's clone summed at the frame it
// played; an id no voice has is a strip of zeros). Throws on a plain sound:
// it has no instrument to play a note.
PlayResult play(const Patch& patch, const Performance& performance, double seconds,
                Level level, const std::vector<std::string>& capture);
// The same with no strips.
PlayResult play(const Patch& patch, const Performance& performance, double seconds, Level level);

// A drum kit playing `hits` for `seconds` at `sampleRate`, guarded. Each drum
// is its patch's graph as one voice builds it, its note never set, under its
// number; a number no pair names is a gap, and a hit on a gap is ignored.
// Throws when a drum's output is not a signal, or a number is negative or
// named twice.
PlayResult play_drums(const std::vector<std::pair<int, Patch>>& drums, const Performance& hits,
                      double seconds, int sampleRate);

} // namespace mforce
