#pragma once
#include <optional>

namespace mforce::ui {

// The computer keyboard as a piano: which key is which semitone, the octave,
// and what a key press plays.
//
// Two zones of the same layout, an octave apart, overlapping by a fifth: the
// upper zone (the Q row for white keys, the number row for black) starts at
// the keyboard octave's C, on Q; the lower zone (the Z row for white keys,
// the home row for black) starts an octave lower, on Z, and its comma is the
// same C as Q. Both zones move with the octave.
struct Keyboard {
  int   octave{4};
  float velocity{0.8f};
  float seconds{1.0f};       // a struck note's length
};

// Semitones from the keyboard octave's C; nullopt: not a note key. A letter
// is the same key in either case.
std::optional<int> key_offset(char key);

// octave * 12 + offset; -1 outside 0..127.
int note_number(const Keyboard& how, int offset);

}  // namespace mforce::ui
