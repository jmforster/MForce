#include "mforce/ui_control/keyboard.h"
#include <algorithm>
#include <cctype>

namespace mforce::ui {

namespace {

struct KeyNote { char key; int offset; };

// The owner's layout, as data. The lower zone: Z-row whites, home-row
// blacks, an octave down.
constexpr KeyNote kLowerZone[] = {
  {'Z', -12}, {'S', -11}, {'X', -10}, {'D', -9}, {'C', -8}, {'V', -7}, {'G', -6}, {'B', -5},
  {'H', -4},  {'N', -3},  {'J', -2},  {'M', -1}, {',', 0},  {'L', 1},  {'.', 2},  {';', 3}, {'/', 4},
};
// The upper zone, at the keyboard octave.
constexpr KeyNote kUpperZone[] = {
  {'Q', 0},  {'2', 1},  {'W', 2},  {'3', 3},  {'E', 4},  {'R', 5},  {'5', 6},  {'T', 7},  {'6', 8},
  {'Y', 9},  {'7', 10}, {'U', 11}, {'I', 12}, {'9', 13}, {'O', 14}, {'0', 15}, {'P', 16}, {'[', 17},
  {'=', 18}, {']', 19},
};

constexpr int kWhitePerOctave = 7;
constexpr int kFewestOctaves = 1, kMostOctaves = 10, kFirstGuess = 4;
constexpr float kNarrowest = 25.0f, kWidest = 45.0f;
constexpr float kSoftest = 0.125f;
constexpr float kGreenUpTo = 0.7f, kYellowUpTo = 0.95f;

}  // namespace

std::optional<int> key_offset(char key) {
  const char upper = char(std::toupper(static_cast<unsigned char>(key)));
  for (const KeyNote& k : kLowerZone)
    if (k.key == upper) return k.offset;
  for (const KeyNote& k : kUpperZone)
    if (k.key == upper) return k.offset;
  return std::nullopt;
}

int note_number(const Keyboard& how, int offset) {
  const int n = how.octave * 12 + offset;
  return n >= 0 && n <= 127 ? n : -1;
}

std::vector<KeyAct> key_acts(const Keyboard& how, const std::vector<KeyChange>& changes, bool notesHeldBack) {
  std::vector<KeyAct> acts;
  for (const KeyChange& c : changes) {
    const auto offset = key_offset(c.key);
    if (!offset) continue;
    if (c.pressed && !notesHeldBack && how.computerKeys) acts.push_back({c.key, true, note_number(how, *offset)});
    if (c.released) acts.push_back({c.key, false, -1});
  }
  return acts;
}

int piano_octaves(float width) {
  const auto key = [&](int octaves) { return width / float(octaves * kWhitePerOctave + 1); };
  int octaves = kFirstGuess;
  while (octaves < kMostOctaves && key(octaves) > kWidest) ++octaves;
  while (octaves > kFewestOctaves && key(octaves) < kNarrowest) --octaves;
  return octaves;
}

std::string note_name(int noteNumber) {
  static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return kNames[((noteNumber % 12) + 12) % 12];
}

std::string key_label(int offset) {
  std::string label;
  for (const KeyNote& k : kUpperZone)
    if (k.offset == offset) label = std::string(1, k.key);
  if (label.empty()) return label;
  for (const KeyNote& k : kLowerZone)
    if (k.offset == offset) label += std::string(" ") + k.key;
  return label;
}

float velocity_at(float fraction) { return kSoftest + std::clamp(fraction, 0.0f, 1.0f) * (1.0f - kSoftest); }

LevelBand level_band(float peak) {
  if (peak > kYellowUpTo) return LevelBand::Red;
  if (peak > kGreenUpTo) return LevelBand::Yellow;
  return LevelBand::Green;
}

}  // namespace mforce::ui
