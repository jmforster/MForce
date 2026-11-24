#include "mforce/ui_control/playing.h"
#include <algorithm>
#include <string>
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

constexpr int kLowestOctave = 0, kHighestOctave = 8;
constexpr float kShortestNote = 0.05f, kLongestNote = 30.0f;

// No player: the keys let go and the adapter handed silence, so no sound but
// the patch's plays.
void silence(Session& s) {
  s.live.all_keys_up();
  s.audio.play(nullptr);
}

}  // namespace

void sound_follows(Session& s) {
  const Patch& p = s.open.patch;
  std::string why;
  if (!p.instrument) why = "a plain sound: it opens, edits and saves, and has no instrument to play yet";
  else if (p.graph.output.empty()) why = "nothing is wired to the Output yet: wire a node's output to it to hear the patch";
  else why = s.live.rebuild(p);
  if (why.empty()) return;
  silence(s);
  s.status = why;
}

void key_down(Session& s, int key, int noteNumber) { s.live.key_down(key, noteNumber, s.keyboard, s.keyboard.velocity); }

void key_down(Session& s, int key, int noteNumber, float velocity) {
  s.live.key_down(key, noteNumber, s.keyboard, velocity);
}

void toggle_computer_keys(Session& s) { s.keyboard.computerKeys = !s.keyboard.computerKeys; }

int sounding_voices(Session& s) {
  LivePlayer* player = s.audio.player();
  return player ? player->sounding() : 0;
}

void key_up(Session& s, int key) { s.live.key_up(key); }

void release_all_keys(Session& s) { s.live.all_keys_up(); }

std::vector<int> held_notes(const Session& s) { return s.live.held_notes(); }

// player() destroys what the output handed back; any control call of the
// player sends what waited for room in its queue, and sounding() is the one
// that changes nothing else.
void keep_up(Session& s) {
  if (LivePlayer* player = s.audio.player()) (void)player->sounding();
}

void change_octave(Session& s, int by) {
  s.keyboard.octave = std::clamp(s.keyboard.octave + by, kLowestOctave, kHighestOctave);
}

void change_note_length(Session& s, float factor) {
  s.keyboard.seconds = std::clamp(s.keyboard.seconds * factor, kShortestNote, kLongestNote);
}

float output_peak(const Session& s) { return s.audio.peak(); }

std::string sound_trouble(const Session& s) { return s.audio.error(); }

}  // namespace mforce::ui
