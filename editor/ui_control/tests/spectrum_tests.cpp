// The spectrum of a take's line: a full-scale sine on a bin reads about 0 dB
// there, at the take's own rate; nothing below -120; a short take has none.
#include "mforce/testing/check.h"
#include "mforce/ui_control/spectrum.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

// A take whose line is a full-scale sine of `hz` for `frames` frames at `rate`.
Take sine_take(double hz, int rate, int frames) {
  Take t;
  t.sampleRate = rate;
  t.frames = frames;
  t.mono.resize(size_t(frames));
  for (int i = 0; i < frames; ++i) t.mono[size_t(i)] = float(std::sin(6.283185307179586 * hz * i / rate));
  return t;
}

}  // namespace

void run_spectrum_tests() {
  std::printf("spectrum: a sine peaks in its bin at about 0 dB at the take's rate; the floor; too short\n");
  for (int rate : {48000, 22050}) {
    const int bin = 100;
    const double hz = double(bin) * rate / 8192.0;
    const Spectrum sp = spectrum_of(sine_take(hz, rate, rate * 2));
    REQUIRE(sp.size == 8192 && sp.dB.size() == 4096 && sp.sampleRate == rate);
    const auto top = std::max_element(sp.dB.begin(), sp.dB.end());
    CHECK(top - sp.dB.begin() == bin);
    CHECK(std::fabs(*top) < 0.1f);
    CHECK(*std::min_element(sp.dB.begin(), sp.dB.end()) >= -120.0f);
  }
  const Spectrum small = spectrum_of(sine_take(1000.0, 48000, 300));
  CHECK(small.size == 256 && small.dB.size() == 128);                      // the largest power of two it holds
  CHECK(spectrum_of(sine_take(1000.0, 48000, 255)).dB.empty());
  Take silent;
  silent.sampleRate = 48000;
  silent.mono.assign(1000, 0.0f);
  const Spectrum quiet = spectrum_of(silent);
  CHECK(!quiet.dB.empty() && std::all_of(quiet.dB.begin(), quiet.dB.end(), [](float d) { return d == -120.0f; }));
}
