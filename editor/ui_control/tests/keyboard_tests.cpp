// The keyboard's arithmetic: the PC Keyboard button, the piano's octaves,
// note names and key labels, a click's velocity, the level's band; and a
// clicked note sounding at the velocity it was given.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

void run_computer_keys_tests() {
  Session s;
  CHECK(s.keyboard.computerKeys);                         // on at start
  const std::vector<KeyChange> press{{'Q', true, false}}, lift{{'Q', false, true}};
  CHECK(key_acts(s.keyboard, press, false).size() == 1);
  toggle_computer_keys(s);
  CHECK(!s.keyboard.computerKeys);
  CHECK(key_acts(s.keyboard, press, false).empty());      // off: a key plays nothing
  const auto up = key_acts(s.keyboard, lift, false);
  CHECK(up.size() == 1 && !up[0].down);                   // a key already down still goes up
  toggle_computer_keys(s);
  CHECK(s.keyboard.computerKeys);
}

// Old 7125-7135: from 4 octaves, one more while a white key (with the top C)
// is wider than 45, one fewer while narrower than 25, within 1 to 10.
void run_piano_octaves_tests() {
  CHECK(piano_octaves(29 * 35.0f) == 4);                  // 29 white keys of 35
  CHECK(piano_octaves(1380.0f) == 5);                     // 4 octaves: 47.6 a key; 5: 38.3
  CHECK(piano_octaves(600.0f) == 3);                      // 4 octaves: 20.7 a key; 3: 27.3
  CHECK(piano_octaves(100000.0f) == 10);
  CHECK(piano_octaves(10.0f) == 1);
  for (float w = 200.0f; w < 4000.0f; w += 37.0f) {
    const int n = piano_octaves(w);
    const float key = w / float(n * 7 + 1);
    CHECK(n >= 1 && n <= 10);
    CHECK((key >= 25.0f || n == 1) && (key <= 45.0f || n == 10 || w / float((n + 1) * 7 + 1) < 25.0f));
  }
}

void run_key_label_tests() {
  CHECK(note_name(60) == "C" && note_name(61) == "C#" && note_name(71) == "B" && note_name(0) == "C");
  CHECK(key_label(0) == "Q" && key_label(1) == "2" && key_label(12) == "I" && key_label(19) == "]");
  CHECK(key_label(-1).empty() && key_label(20).empty());
  for (int offset = 0; offset < 20; ++offset) CHECK(key_offset(key_label(offset)[0]) == offset);
}

void run_velocity_and_level_tests() {
  CHECK(velocity_at(0.0f) == 0.125f && velocity_at(1.0f) == 1.0f);
  CHECK(std::fabs(velocity_at(0.5f) - (0.125f + 0.5f * 0.875f)) < 1e-6f);
  CHECK(velocity_at(-3.0f) == 0.125f && velocity_at(7.0f) == 1.0f);
  CHECK(level_band(0.0f) == LevelBand::Green && level_band(0.7f) == LevelBand::Green);
  CHECK(level_band(0.71f) == LevelBand::Yellow && level_band(0.95f) == LevelBand::Yellow);
  CHECK(level_band(0.96f) == LevelBand::Red);

  // A note clicked softly is quieter than the same note clicked hard.
  float loud[2] = {0.0f, 0.0f};
  for (int k = 0; k < 2; ++k) {
    Session s;
    open_file(s, "patches/library/winds/oboe1.json");
    key_down(s, 1, 60, k == 0 ? 0.125f : 1.0f);
    CHECK(sounding_voices(s) >= 1);
    std::vector<float> buf(1024);
    for (int b = 0; b < 40; ++b) {
      s.audio.fill(buf.data(), 512);
      for (float x : buf) loud[k] = std::max(loud[k], std::fabs(x));
    }
  }
  CHECK(loud[0] > 0.0f && loud[0] < loud[1]);
}
