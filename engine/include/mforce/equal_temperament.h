#pragma once
#include <cmath>

namespace mforce {

// 12-TET: frequency = 440 * 2^((noteNumber - 69) / 12)
// Note 69 = A4 = 440 Hz. Note 60 = C4 = ~261.63 Hz.
inline float note_to_freq(float noteNumber) {
  return 440.0f * std::pow(2.0f, (noteNumber - 69.0f) / 12.0f);
}

} // namespace mforce
