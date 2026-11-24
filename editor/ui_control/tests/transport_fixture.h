#pragma once
// What the transport's tests share: the patches they play, and the session's
// output read by calling session.audio.fill() directly, never a device.
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace transport_fixture {

using namespace mforce;
using namespace mforce::ui;

inline const std::string kPiano = "patches/library/keys/acoustic_piano/piano_default.json";
inline const std::string kOboe = "patches/library/winds/oboe1.json";
inline const std::string kSustaining = "patches/baselines/phrase_smoke_phrased.json";
inline const std::string kPlain = "patches/baselines/fm_bell_test.json";
inline const std::string kSlowRate = "patches/baselines/double_advance_bug.json";

// `buffers` buffers of 512 frames of the session's output; the largest
// sample in size.
inline float run(Session& s, int buffers) {
  std::vector<float> out(2 * 512);
  float most = 0.0f;
  for (int k = 0; k < buffers; ++k) {
    s.audio.fill(out.data(), 512);
    for (float x : out) most = std::max(most, std::fabs(x));
  }
  return most;
}

// Buffers until no live voice sounds, at most `seconds` of them; true when
// none sounds at the end.
inline bool until_quiet(Session& s, float seconds = 12.0f) {
  for (int k = 0; k < int(seconds * 48000.0f / 512.0f) && sounding_voices(s) > 0; ++k) (void)run(s, 1);
  return sounding_voices(s) == 0;
}

// Buffers until the live voices sound `count` notes, at most `seconds`.
inline bool until_sounding(Session& s, size_t count, float seconds = 12.0f) {
  for (int k = 0; k < int(seconds * 48000.0f / 512.0f) && sounding_notes(s).size() != count; ++k) (void)run(s, 1);
  return sounding_notes(s).size() == count;
}

}  // namespace transport_fixture
