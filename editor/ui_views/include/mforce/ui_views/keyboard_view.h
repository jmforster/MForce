#pragma once
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The keyboard: the PC Keyboard button (green while the computer's note keys
// play), the computer's note keys, the octave and a struck note's length (Up
// and Down, Left and Right, without Shift: with it an arrow pans the canvas),
// the voices sounding and the output's level before and after the clip,
// each coloured by its band; and an on-screen piano of as many octaves as its
// width suits, from the keyboard octave to a top C, every key down and every
// note still sounding drawn pressed, each white key named, each key the
// upper zone reaches labelled with its computer key, played with the mouse louder the further
// down a key it is pressed. No note key acts while a text field has the
// keyboard; when the window loses the keyboard every held key goes up.
class KeyboardView {
public:
  void draw(Session& s);
  Vec2 piano_origin() const { return pianoOrigin_; }   // the piano's top-left corner on screen, from the last frame

private:
  void computer_keys(Session& s);
  void controls(Session& s);
  void piano(Session& s);
  void meter(Session& s);

  Vec2 pianoOrigin_{};                             // the piano's top-left corner on screen
  int mouseNote_{-1};                              // the note the mouse holds down; -1 for none
  float meter_{0.0f};                              // the peak after the clip, falling slowly
  float meterBeforeClip_{0.0f};                    // and before it
};

}  // namespace mforce::ui
