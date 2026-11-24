#pragma once
#include <vector>
#include "mforce/ui_control/take.h"

namespace mforce::ui {

// The spectrum of a take's line (Take::mono), at the take's own rate.
struct Spectrum {
  std::vector<float> dB;   // size / 2 levels, bin k at k * sampleRate / size Hz; empty for none
  int sampleRate{0};
  int size{0};             // the transform's size N
};

// The middle of the take's line: N samples, N the largest power of two up
// to 8192 the take holds; a Hann window, one transform; each level
// 20 log10(|X| * 4 / N), so a full-scale sine on a bin reads 0 dB, and
// nothing below -120. A take of fewer than 256 samples has none.
Spectrum spectrum_of(const Take& take);

}  // namespace mforce::ui
