#include "mforce/ui_control/keyboard.h"
#include <cctype>

namespace mforce::ui {

namespace {

struct KeyNote { char key; int offset; };

// The owner's layout, as data.
constexpr KeyNote kKeys[] = {
  // The lower zone: Z-row whites, home-row blacks, an octave down.
  {'Z', -12}, {'S', -11}, {'X', -10}, {'D', -9}, {'C', -8}, {'V', -7}, {'G', -6}, {'B', -5},
  {'H', -4},  {'N', -3},  {'J', -2},  {'M', -1}, {',', 0},  {'L', 1},  {'.', 2},  {';', 3}, {'/', 4},
  // The upper zone, at the keyboard octave.
  {'Q', 0},  {'2', 1},  {'W', 2},  {'3', 3},  {'E', 4},  {'R', 5},  {'5', 6},  {'T', 7},  {'6', 8},
  {'Y', 9},  {'7', 10}, {'U', 11}, {'I', 12}, {'9', 13}, {'O', 14}, {'0', 15}, {'P', 16}, {'[', 17},
  {'=', 18}, {']', 19},
};

}  // namespace

std::optional<int> key_offset(char key) {
  const char upper = char(std::toupper(static_cast<unsigned char>(key)));
  for (const KeyNote& k : kKeys)
    if (k.key == upper) return k.offset;
  return std::nullopt;
}

int note_number(const Keyboard& how, int offset) {
  const int n = how.octave * 12 + offset;
  return n >= 0 && n <= 127 ? n : -1;
}

}  // namespace mforce::ui
