#pragma once
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The keyboard: the computer's note keys, an on-screen piano of three
// octaves from the keyboard octave played with the mouse, the octave and a
// struck note's length (Up and Down, Left and Right), and the output's peak.
// No note key acts while a text field has the keyboard; when the window
// loses the keyboard every held key goes up.
class KeyboardView {
public:
  void draw(Session& s);

private:
  void computer_keys(Session& s);
  void controls(Session& s);
  void piano(Session& s);
  void meter(const Session& s);

  int mouseNote_{-1};                              // the note the mouse holds down; -1 for none
  float meter_{0.0f};                              // the peak bar's level, falling slowly
};

}  // namespace mforce::ui
