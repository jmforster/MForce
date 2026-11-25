#include "mforce/ui_control/playing.h"
#include <algorithm>
#include <array>
#include <string>
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

constexpr int kLowestOctave = 0, kHighestOctave = 8;
constexpr float kShortestNote = 0.05f, kLongestNote = 30.0f;

}  // namespace

void sound_follows(Session& s) {
  const Patch heard = heard_patch(s);
  std::string why;
  if (!heard.instrument) why = "a plain sound: Play runs it until Stop; the keys play nothing on it";
  else if (heard.graph.output.empty()) why = "nothing is wired to the Output yet: wire a node's output to it to hear the patch";
  else why = s.live.rebuild(heard);
  (void)plain_follows(s);
  if (why.empty()) return;
  s.live.silence();
  s.status = why;
}

void key_down(Session& s, int key, int noteNumber) { key_down(s, key, noteNumber, s.keyboard.velocity); }

void key_down(Session& s, int key, int noteNumber, float velocity) {
  if (s.live.key_down(key, noteNumber, s.keyboard, velocity, s.transport.live)) s.lastNote = {noteNumber, velocity};
}

void toggle_computer_keys(Session& s) { s.keyboard.computerKeys = !s.keyboard.computerKeys; }

int sounding_voices(Session& s) { return s.live.sounding(); }

std::vector<int> sounding_notes(Session& s) {
  std::array<int, 64> into{};
  const int n = s.live.sounding_notes(into);
  return std::vector<int>(into.begin(), into.begin() + n);
}

void key_up(Session& s, int key) { s.live.key_up(key); }

void release_all_keys(Session& s) { s.live.keys_up_in(0, kFirstMidiKey); }

std::vector<int> held_notes(const Session& s) { return s.live.held_notes(); }

// playing() destroys what the output handed back; any control call of the
// player sends what waited for room in its queue (faults() is one); a
// playback that has played out is let go.
void keep_up(Session& s) {
  (void)s.audio.playing(AudioAdapter::Line::Keys);
  if (std::string f = s.live.faults(); !f.empty()) s.status = std::move(f);
  if (Sound* playback = s.audio.playing(AudioAdapter::Line::Playback); playback && playback->over()) {
    s.audio.play(AudioAdapter::Line::Playback, nullptr);
    s.runningPlain = nullptr;
  }
}

void change_octave(Session& s, int by) {
  s.keyboard.octave = std::clamp(s.keyboard.octave + by, kLowestOctave, kHighestOctave);
}

void change_note_length(Session& s, float factor) {
  s.keyboard.seconds = std::clamp(s.keyboard.seconds * factor, kShortestNote, kLongestNote);
}

void set_note_length(Session& s, float seconds) { s.keyboard.seconds = std::clamp(seconds, kShortestNote, kLongestNote); }

void set_velocity(Session& s, float velocity) { s.keyboard.velocity = std::clamp(velocity, 0.0f, 1.0f); }

float output_peak(const Session& s) { return s.audio.peak(); }

float output_peak_before_clip(const Session& s) { return s.audio.peak_before_clip(); }

std::string sound_trouble(const Session& s) { return s.audio.error(); }

}  // namespace mforce::ui
