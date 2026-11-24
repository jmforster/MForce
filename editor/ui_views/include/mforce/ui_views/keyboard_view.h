#pragma once
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The keyboard: the PC Keyboard button (green while the computer's note keys
// play), the computer's note keys, the octave and a struck note's length (Up
// and Down, Left and Right, without Shift: with it an arrow pans the canvas),
// the voices sounding and the output's level, coloured by its band; and an
// on-screen piano of as many octaves as its width suits, from the keyboard
// octave to a top C, each white key named, each key the upper zone reaches
// labelled with its computer key, played with the mouse louder the further
// down a key it is pressed. No note key acts while a text field has the
// keyboard; when the window loses the keyboard every held key goes up.
class KeyboardView {
public:
  void draw(Session& s);

private:
  void computer_keys(Session& s);
  void controls(Session& s);
  void piano(Session& s);
  void meter(Session& s);

  int mouseNote_{-1};                              // the note the mouse holds down; -1 for none
  float meter_{0.0f};                              // the peak bar's level, falling slowly
};

}  // namespace mforce::ui
