// Opening every measured patch and playing middle C on it, the keys on a
// sustaining and on a struck instrument, and the key table.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kStruck = "patches/library/winds/oboe1.json";
const std::string kSustaining = "patches/baselines/phrase_smoke_phrased.json";

// The largest sample, in size, of `seconds` of the session's output.
float loudest(Session& s, float seconds) {
  std::vector<float> buf(2 * 512);
  float most = 0.0f;
  for (int done = 0; done < int(seconds * 48000.0f); done += 512) {
    s.audio.fill(buf.data(), 512);
    for (float x : buf) most = std::max(most, std::fabs(x));
  }
  return most;
}

int sounding(Session& s) { return s.audio.player() ? s.audio.player()->sounding() : 0; }

}  // namespace

// Test 2: every measured patch opens; an instrument gets a player and middle
// C sounds; a plain sound says so and has none.
void run_control_open_census_tests() {
  int patches = 0, instruments = 0, plain = 0, silent = 0, refused = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      const std::string file = entry.path().generic_string();
      Session s;
      open_file(s, file);
      ++patches;
      CHECK(s.open.path == file);
      if (!s.open.patch.instrument) {
        ++plain;
        CHECK(s.audio.player() == nullptr);
        CHECK(s.status.find("plain sound") != std::string::npos);
        continue;
      }
      ++instruments;
      if (!s.audio.player()) {
        ++refused;
        std::printf("  no player for %s: %s\n", file.c_str(), s.status.c_str());
        continue;
      }
      key_down(s, 1, 60);
      if (loudest(s, 0.5f) == 0.0f) {
        ++silent;
        std::printf("  middle C is silent on %s\n", file.c_str());
      }
    }
  std::printf("  open census: %d patches, %d instruments (%d with no player, %d silent at middle C), "
              "%d plain sounds\n", patches, instruments, refused, silent, plain);
  CHECK(patches > 0);
  CHECK(refused == 0);
}

// Test 5: the keys.
void run_control_key_tests() {
  Session s;
  key_down(s, 1, 60);                                    // no player: nothing
  CHECK(sounding(s) == 0);

  open_file(s, kSustaining);
  REQUIRE(s.open.patch.instrument && s.open.patch.instrument->sustaining);
  s.keyboard.seconds = 0.2f;
  key_down(s, 1, 60);
  CHECK(loudest(s, 0.6f) > 0.0f);
  CHECK(sounding(s) == 1);                               // held past the note's length
  key_down(s, 2, 64);                                    // a second key carries the line
  (void)loudest(s, 0.1f);
  CHECK(sounding(s) == 1);
  key_up(s, 1);
  (void)loudest(s, 0.1f);
  CHECK(sounding(s) == 1);                               // the line is the second key's now
  key_up(s, 2);
  (void)loudest(s, 3.0f);
  CHECK(sounding(s) == 0);

  key_down(s, 3, note_number(s.keyboard, 0));
  change_octave(s, 1);                                   // the octave moves while the key is down
  key_up(s, 3);
  (void)loudest(s, 3.0f);
  CHECK(sounding(s) == 0);

  open_file(s, kStruck);
  REQUIRE(s.open.patch.instrument && !s.open.patch.instrument->sustaining);
  s.keyboard.seconds = 1.0f;
  key_down(s, 4, 60);
  key_down(s, 4, 60);                                    // the same key again: played once
  key_up(s, 4);                                          // ignored by a struck note
  (void)loudest(s, 0.1f);
  CHECK(sounding(s) == 1);
  key_down(s, 4, 62);                                    // after its key up the key plays again
  (void)loudest(s, 0.1f);
  CHECK(sounding(s) == 2);
}

void run_control_keyboard_tests() {
  const struct { char key; int offset; } table[] = {
    {'Z', -12}, {'S', -11}, {'X', -10}, {'D', -9}, {'C', -8}, {'V', -7}, {'G', -6}, {'B', -5},
    {'H', -4},  {'N', -3},  {'J', -2},  {'M', -1}, {',', 0},  {'L', 1},  {'.', 2},  {';', 3}, {'/', 4},
    {'Q', 0},  {'2', 1},  {'W', 2},  {'3', 3},  {'E', 4},  {'R', 5},  {'5', 6},  {'T', 7},  {'6', 8},
    {'Y', 9},  {'7', 10}, {'U', 11}, {'I', 12}, {'9', 13}, {'O', 14}, {'0', 15}, {'P', 16}, {'[', 17},
    {'=', 18}, {']', 19},
  };
  for (const auto& k : table) {
    CHECK(key_offset(k.key) == k.offset);
    CHECK(key_offset(char(std::tolower(static_cast<unsigned char>(k.key)))) == k.offset);
  }
  for (char other : {'A', 'F', 'K', '1', '4', '8', '-', ' '}) CHECK(!key_offset(other).has_value());
  Keyboard how;
  CHECK(note_number(how, 0) == 48 && note_number(how, -12) == 36);
  how.octave = 0;
  CHECK(note_number(how, -1) == -1);
  how.octave = 10;
  CHECK(note_number(how, 7) == 127 && note_number(how, 8) == -1);

  Session s;
  change_octave(s, -10);
  CHECK(s.keyboard.octave == 0);
  change_octave(s, 20);
  CHECK(s.keyboard.octave == 8);
  change_note_length(s, 0.001f);
  CHECK(s.keyboard.seconds == 0.05f);
  change_note_length(s, 1e6f);
  CHECK(s.keyboard.seconds == 30.0f);
  CHECK(output_peak(s) == 0.0f);
  CHECK(!sound_trouble(s).empty());                      // no device was opened
}

// The keys down, as note numbers; and every key let go at once, as when the
// window loses the keyboard.
void run_control_held_keys_tests() {
  Session s;
  open_file(s, kSustaining);
  key_down(s, 1, 64);
  key_down(s, 2, 60);
  CHECK((held_notes(s) == std::vector<int>{60, 64}));
  (void)loudest(s, 0.1f);
  release_all_keys(s);
  CHECK(held_notes(s).empty());
  (void)loudest(s, 3.0f);
  CHECK(sounding(s) == 0);
}
