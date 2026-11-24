#pragma once
#include <optional>
#include <string>
#include <vector>

namespace mforce::ui {

// The computer keyboard as a piano: which key is which semitone, the octave,
// and what a key press plays; and the on-screen piano's arithmetic.
//
// Two zones of the same layout, an octave apart, overlapping by a fifth: the
// upper zone (the Q row for white keys, the number row for black) starts at
// the keyboard octave's C, on Q; the lower zone (the Z row for white keys,
// the home row for black) starts an octave lower, on Z, and its comma is the
// same C as Q. Both zones move with the octave.
struct Keyboard {
  int   octave{4};
  float velocity{0.8f};      // a computer key's
  float seconds{1.0f};       // a struck note's length
  bool  computerKeys{true};  // the PC Keyboard button: off, the note keys and the arrows do nothing
};

// Semitones from the keyboard octave's C; nullopt: not a note key. A letter
// is the same key in either case.
std::optional<int> key_offset(char key);

// octave * 12 + offset; -1 outside 0..127.
int note_number(const Keyboard& how, int offset);

// A computer key's change in one frame.
struct KeyChange { char key; bool pressed; bool released; };
// What a frame's key changes play: a key down for each note key pressed,
// unless notes are held back (Ctrl is down, a text field has the keyboard)
// or the PC Keyboard is off; a key up for each note key released, held back
// or not, so no note is left sounding. A key outside the note table does
// nothing.
struct KeyAct { char key; bool down; int noteNumber; };
std::vector<KeyAct> key_acts(const Keyboard& how, const std::vector<KeyChange>& changes, bool notesHeldBack);

// The on-screen piano: as many octaves, from 1 to 10, as keep a white key
// between 25 and 45 points wide in `width`, counting the top C that ends it
// (from 4, one more while a key is too wide, one fewer while too narrow).
int piano_octaves(float width);
// The note's name without its octave: "C", "C#", ... "B".
std::string note_name(int noteNumber);
// The upper zone's computer key for a note `offset` semitones above the
// keyboard octave's C ("Q" for 0, "2" for 1, ...), and where the lower zone
// reaches the same note its key too, upper first ("Q ,"); empty where the
// upper zone has none.
std::string key_label(int offset);
// A click's velocity from where on a key it fell: `fraction` 0 at the key's
// top gives 0.125, 1 at its bottom 1.0; outside, the nearer end.
float velocity_at(float fraction);

// The output level's band, for the meter's colour: green up to 0.7, yellow
// up to 0.95, red above.
enum class LevelBand { Green, Yellow, Red };
LevelBand level_band(float peak);

}  // namespace mforce::ui
