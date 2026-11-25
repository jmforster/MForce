#pragma once
#include <span>
#include <string>
#include <vector>
#include "mforce/patch/patch.h"
#include "mforce/render/live_player.h"
#include "mforce/ui_audio/audio_adapter.h"
#include "mforce/ui_control/keyboard.h"

namespace mforce::ui {

// Keys to notes, and edits to the sounding player. The player sounds inside
// a KeysSound on the adapter's Keys line; this keeps its address for the
// control calls until the next swap or silence, and drops it first.
class LivePlay {
public:
  explicit LivePlay(AudioAdapter& audio);

  // A new player for `patch`, eight notes at once, at the adapter's rate, its
  // guard off (the adapter clips the sum of its lines), handed to the Keys
  // line; the keys that were down are forgotten; the wheel and the pressure
  // given last are given to it. Empty, or why there is no new player (a
  // plain sound has no instrument to play; the engine's refusal of the
  // build): the player before it then keeps sounding.
  std::string rebuild(const Patch& patch);
  // Silence on the Keys line; the keys that were down are forgotten.
  void silence();
  LivePlayer* player() { return player_; }        // null while there is none
  bool playing() const { return player_ != nullptr; }

  // `key` names the key for key_up: a held key is released by the same key
  // whatever the octave is by then. With `live`, a key holds its note until
  // it goes up, on any instrument (the player decides whether it joins the
  // legato line or holds a voice of its own); on an instrument that
  // sustains, letting go of the key whose note sounds while others are
  // still down carries the line back to the one pressed last, at its own
  // velocity, and letting go of another only forgets it. Without `live`, a
  // key strikes a note `how.seconds` long and its going up does nothing. A
  // key already down is not played again. True when a note was sent.
  bool key_down(int key, int noteNumber, const Keyboard& how, float velocity, bool live);
  void key_up(int key);
  // Every key numbered from `first` up to, not including, `limit` up at
  // once: each held note released, the line with no carry back between
  // them; when the line's own key goes, the line carries back to a key
  // outside the range that is still down, as key_up does.
  void keys_up_in(int first, int limit);
  std::vector<int> held_notes() const;            // the note numbers of the keys down, lowest first

  // The instrument's wheel and pressure, 0 to 1: the player's now, and every
  // player built after it.
  void set_wheel(float v);
  void set_pressure(float v);
  float wheel() const { return wheel_; }
  float pressure() const { return pressure_; }

  // Every sounding voice fades out; the keys still down sound no more.
  void stop();
  // The notes of the voices sounding, lowest first; 0 with no player.
  int  sounding_notes(std::span<int> into);
  int  sounding();
  // What the live voices' nodes reported since the last call, as
  // "faults: osc (SineSource): frequency clamped; ..."; empty when nothing.
  std::string faults();

private:
  struct Down {
    int key;
    int noteNumber;
    float velocity;
    float seconds;
    LivePlayer::NoteId id;   // 0 when its note was not sent or no longer sounds
  };
  Down* find(int key);
  bool carry_back(const Down& to);

  AudioAdapter& audio_;
  LivePlayer* player_{nullptr};                  // owned by the KeysSound the adapter holds
  bool sustaining_{false};                       // the current player's instrument sustains
  std::vector<Down> down_;                       // the keys down, in the order pressed
  int lineKey_{-1};                              // the key whose note the legato line plays; -1 for none
  float wheel_{0.0f};                            // the last wheel given; a new player starts with it
  float pressure_{0.0f};                         // and the last pressure
};

}  // namespace mforce::ui
