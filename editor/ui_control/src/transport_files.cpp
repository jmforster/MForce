// The transport's files (the take as a WAV, the passage and chords texts),
// its "Last note", and the seed a patch's embedded score gives it.
#include "mforce/ui_control/generating.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include "mforce/core/equal_temperament.h"
#include "mforce/perform/embedded_score.h"
#include "mforce/render/wav_writer.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

constexpr float kShortestNote = 0.05f, kLongestNote = 30.0f;
constexpr int kLowestOctave = 0, kHighestOctave = 8;

// The tab's text, or null on a tab that has none.
std::string* tab_text(Session& s) {
  if (s.transport.mode == Transport::Mode::Passage) return &s.transport.passage;
  if (s.transport.mode == Transport::Mode::Chords) return &s.transport.chords;
  return nullptr;
}

}  // namespace

void save_wav(Session& s, const std::string& path) {
  if (s.take.stereo.empty()) {
    s.status = "Nothing to save — generate first";
    return;
  }
  if (write_wav_16le_stereo(path, s.take.sampleRate, s.take.stereo))
    s.status = "Saved: " + path + " (" + std::to_string(s.take.frames) + " frames)";
  else
    s.status = "Failed to save: " + path;
}

void save_text(Session& s, const std::string& path) {
  const std::string* text = tab_text(s);
  if (!text) {
    s.status = "only the Passage and Chords tabs have a text to save";
    return;
  }
  std::ofstream f(path, std::ios::binary);
  if (f && (f << *text)) s.status = "Saved: " + path;
  else s.status = "Failed to save: " + path;
}

void load_text(Session& s, const std::string& path) {
  std::string* text = tab_text(s);
  if (!text) {
    s.status = "only the Passage and Chords tabs have a text to load";
    return;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    s.status = "Failed to open: " + path;
    return;
  }
  const std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::string kept;
  std::copy_if(content.begin(), content.end(), std::back_inserter(kept), [](char c) { return c != '\r'; });
  while (!kept.empty() && (kept.back() == '\n' || kept.back() == ' ')) kept.pop_back();
  *text = std::move(kept);
  s.status = "Loaded: " + path;
}

std::string last_note(const Session& s) {
  const int n = s.lastNote.note;
  if (n < 0) return {};
  static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  char text[96];
  std::snprintf(text, sizeof text, "Last note: %d  %s%d  %.1fHz  vel %.2f", n, kNames[n % 12], n / 12,
                double(note_to_freq(float(n))), double(s.lastNote.velocity));
  return text;
}

void seed_from_score(Session& s) {
  const auto first = first_score_note(s.open.patch.score);
  if (!first) return;
  char text[32];
  std::snprintf(text, sizeof text, "%g", double(first->note));
  s.transport.note = text;
  s.keyboard.velocity = first->velocity;
  s.keyboard.seconds = std::clamp(first->duration, kShortestNote, kLongestNote);
  s.keyboard.octave = std::clamp(int(first->note) / 12, kLowestOctave, kHighestOctave);
}

}  // namespace mforce::ui
