#include "mforce/ui_control/spectrum.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>
#include "mforce/util/fft.h"

namespace mforce::ui {

namespace {

constexpr std::size_t kLargest = 8192, kSmallest = 256;
constexpr float kFloor = -120.0f;
constexpr float kLeast = 1e-6f;          // a magnitude at or below this reads as the floor

}  // namespace

Spectrum spectrum_of(const Take& take) {
  const std::vector<float>& line = take.mono;
  if (line.size() < kSmallest) return {};
  std::size_t n = kLargest;
  while (n > line.size()) n /= 2;
  const std::size_t from = std::min(line.size() - n, line.size() / 2 - std::min(line.size() / 2, n / 2));
  std::vector<std::complex<float>> x(n);
  const double twoPi = 6.283185307179586;
  for (std::size_t i = 0; i < n; ++i) {
    const float w = float(0.5 * (1.0 - std::cos(twoPi * double(i) / double(n - 1))));
    x[i] = std::complex<float>(line[from + i] * w, 0.0f);
  }
  fft_inplace(x);
  Spectrum s{std::vector<float>(n / 2), take.sampleRate, int(n)};
  const float scale = 4.0f / float(n);
  for (std::size_t k = 0; k < n / 2; ++k) {
    const float magnitude = std::abs(x[k]) * scale;
    s.dB[k] = magnitude > kLeast ? std::max(kFloor, 20.0f * std::log10(magnitude)) : kFloor;
  }
  return s;
}

}  // namespace mforce::ui
