#pragma once
#include <string>
#include "mforce/perform/text_performance.h"

namespace mforce::ui {

// What the transport's fields hold: the tab, and each tab's texts and
// numbers. Not part of the patch, and not saved with it. The Note tab's
// octave, velocity and length are the keyboard's (Session::keyboard).
struct Transport {
  enum class Mode { Note, Passage, Chords, Drums };
  Mode mode{Mode::Note};
  std::string note{"C4"};          // a note name ("C#4") or a number ("60"), as parse_note_input reads it
  bool live{true};                 // a key holds its note until it goes up; off, it strikes one of the keyboard's length
  std::string passage;
  float bpm{120.0f};               // the Passage, Chords and Drums tabs' tempo
  Onsets onsets{Onsets::Auto};
  std::string chords, chordGroup, figure;
  float delayMs{15.0f};            // the spread of a block chord's notes
  std::string drumPattern;
  std::string drumMap{"KK=patches/library/percussion/kick_studio_dry.json;HH=patches/library/percussion/hi_hat1.json"};
};

}  // namespace mforce::ui
