#include "mforce/ui_control/sounds.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace mforce::ui {

namespace {

// `stereo` (interleaved, at `from` Hz) made again at `to` Hz: as many frames
// as last the same seconds, each read between its two neighbours.
std::vector<float> resampled(const std::vector<float>& stereo, int from, int to) {
  const size_t frames = stereo.size() / 2;
  if (from == to || frames == 0 || from <= 0 || to <= 0) return stereo;
  const size_t outFrames = size_t(std::llround(double(frames) * double(to) / double(from)));
  std::vector<float> out(outFrames * 2, 0.0f);
  const double step = double(from) / double(to);
  for (size_t i = 0; i < outFrames; ++i) {
    const double pos = double(i) * step;
    const size_t a = std::min(frames - 1, size_t(pos));
    const size_t b = std::min(frames - 1, a + 1);
    const float t = float(pos - double(a));
    for (size_t c = 0; c < 2; ++c)
      out[2 * i + c] = stereo[2 * a + c] + (stereo[2 * b + c] - stereo[2 * a + c]) * t;
  }
  return out;
}

}  // namespace

KeysSound::KeysSound(std::unique_ptr<LivePlayer> player) : player_(std::move(player)) {}

void KeysSound::fill(float* stereo, int frames) noexcept {
  for (int done = 0; done < frames;) {
    const int n = std::min(int(mono_.size()), frames - done);
    player_->fill(mono_.data(), n);
    for (int i = 0; i < n; ++i) {
      stereo[2 * (done + i)] = mono_[size_t(i)];
      stereo[2 * (done + i) + 1] = mono_[size_t(i)];
    }
    done += n;
  }
}

TakeSound::TakeSound(const Take& take, int outputRate)
    : stereo_(resampled(take.stereo, take.sampleRate, outputRate)) {
  if (stereo_.empty()) over_.store(true, std::memory_order_release);
}

void TakeSound::fill(float* stereo, int frames) noexcept {
  const size_t want = size_t(frames) * 2;
  const size_t have = std::min(want, stereo_.size() - at_);
  std::copy(stereo_.begin() + std::ptrdiff_t(at_), stereo_.begin() + std::ptrdiff_t(at_ + have), stereo);
  std::fill(stereo + have, stereo + want, 0.0f);
  at_ += have;
  if (at_ >= stereo_.size()) over_.store(true, std::memory_order_release);
}

PlainSound::PlainSound(const Patch& patch, int sampleRate) : running_(patch, sampleRate) {}

void PlainSound::fill(float* stereo, int frames) noexcept { running_.fill(stereo, frames); }

}  // namespace mforce::ui
