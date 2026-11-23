#pragma once
#include <map>
#include <string>
#include <string_view>
#include "mforce/patch/patch.h"
#include "mforce/render/live_player.h"
#include "mforce/ui_audio/audio_adapter.h"
#include "mforce/ui_control/keyboard.h"

namespace mforce::ui {

// Keys to notes, and edits to the sounding player.
class LivePlay {
public:
  explicit LivePlay(AudioAdapter& audio);

  // A new player for `patch`, eight notes at once, at the adapter's rate,
  // handed to the adapter; the keys that were down are forgotten. Empty,
  // or why there is no new player (a plain sound has no instrument to
  // play; the engine's refusal of the build): the player before it then
  // keeps sounding.
  std::string rebuild(const Patch& patch);
  // A constant changed in `patch`: the player follows it, or is rebuilt
  // when it cannot. Returns as rebuild does.
  std::string value_changed(const Patch& patch, std::string_view node, std::string_view pin, float v);
  bool playing() const;                            // the adapter has a player

  // `key` names the key for key_up: a held key is released by the same key
  // whatever the octave is by then. On an instrument that sustains, a key
  // holds its note until it goes up, and a second key carries the line on;
  // on one that does not, a key strikes a note `how.seconds` long and its
  // going up does nothing. A key already down is not played again; with no
  // player, nothing is played.
  void key_down(int key, int noteNumber, const Keyboard& how);
  void key_up(int key);
  void all_keys_up();

private:
  AudioAdapter& audio_;
  bool sustaining_{false};                         // the current player's instrument sustains
  std::map<int, LivePlayer::NoteId> down_;         // the keys down, and the note each began
};

}  // namespace mforce::ui
