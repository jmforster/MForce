#pragma once
#include <optional>
#include <vector>

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

// A computer key's change in one frame.
struct KeyChange { char key; bool pressed; bool released; };
// What a frame's key changes play: a key down for each note key pressed,
// unless notes are held back (Ctrl is down, a text field has the keyboard);
// a key up for each note key released, held back or not, so no note is left
// sounding. A key outside the note table does nothing.
struct KeyAct { char key; bool down; int noteNumber; };
std::vector<KeyAct> key_acts(const Keyboard& how, const std::vector<KeyChange>& changes, bool notesHeldBack);

}  // namespace mforce::ui
