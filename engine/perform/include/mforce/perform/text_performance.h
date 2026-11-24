#pragma once
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

// How a phrased passage's notes begin: by the rule (a phrase begins with a
// breath, then a repeated pitch is tongued and a new one slurred), every
// note inside a phrase tongued, every one slurred, or every note a fresh
// breath (the phrase marks ignored).
enum class Onsets { Auto, AllTongue, AllSlur, AllBreath };

// What a text became: the notes or hits; how long a render of them lasts
// (the last note's end plus the voice's tail for a note or a passage; the
// whole of the chords or the pattern plus one second for chords and drums);
// and what is worth telling about it.
struct TextPerformance {
  Performance performance;
  double seconds{0.0};
  std::vector<std::string> notes;
};

// One note at the start: `seconds` long, not held.
TextPerformance perform_note(float noteNumber, float velocity, float seconds);

// A passage in the parse_passage notation, one note after another, a rest a
// gap. Without a '|' every note is plain. With one, on an instrument that
// sustains, every note of a phrase but its last holds and each note's onset
// follows `onsets`; on one that does not, the marks are ignored and that is
// told. An onset name outside the instrument's vocabulary becomes 0. Throws
// the parser's sentence, "No notes parsed from passage string", or "Passage
// contains only rests".
TextPerformance perform_passage(std::string_view text, int octave, float bpm, float velocity,
                                Onsets onsets, const InstrumentClass& cls);

// Chords in the parse_chord_string notation through the chord performer,
// with the Josie figures registered: `group` is the chord dictionary
// ("Default" when empty), `figure` the prefix of a figure's name, `delayMs`
// the spread of a block chord's notes. Throws the parser's sentence, or "No
// chords parsed from string".
TextPerformance perform_chords(std::string_view text, int octave, float bpm, std::string_view group,
                               std::string_view figure, float delayMs);

// A drum pattern in the parse_drum_pattern notation: each hit at its beat,
// as long as its duration (a quarter beat when it has none, at least 0.02 s),
// at its velocity. Throws the parser's sentence, or "No hits parsed from drum
// pattern".
TextPerformance perform_drums(std::string_view pattern, float bpm);

}  // namespace mforce
