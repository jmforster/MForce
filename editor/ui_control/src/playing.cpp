#include "mforce/ui_control/playing.h"
#include <algorithm>
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

constexpr int kLowestOctave = 0, kHighestOctave = 8;
constexpr float kShortestNote = 0.05f, kLongestNote = 30.0f;

}  // namespace

void key_down(Session& s, int key, int noteNumber) { s.live.key_down(key, noteNumber, s.keyboard); }

void key_up(Session& s, int key) { s.live.key_up(key); }

void release_all_keys(Session& s) { s.live.all_keys_up(); }

std::vector<int> held_notes(const Session& s) { return s.live.held_notes(); }

void change_octave(Session& s, int by) {
  s.keyboard.octave = std::clamp(s.keyboard.octave + by, kLowestOctave, kHighestOctave);
}

void change_note_length(Session& s, float factor) {
  s.keyboard.seconds = std::clamp(s.keyboard.seconds * factor, kShortestNote, kLongestNote);
}

float output_peak(const Session& s) { return s.audio.peak(); }

std::string sound_trouble(const Session& s) { return s.audio.error(); }

}  // namespace mforce::ui
