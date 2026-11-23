#pragma once
// BandwidthNoise: the per-partial smoothed noise of the bandwidth enhancement.
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace mforce {

// Bandwidth enhancement (Loris / SMS): a partial trades part of its sinusoidal
// energy for a noise band by amplitude-modulating with an independent
// per-partial smoothed random walk, so the bands fill in rather than wobbling
// in unison. Each partial's walk draws from its own stream, seeded per note;
// the segment length is rate / bandwidthHz. A value type the owner holds:
// prepare sizes it, start seeds it per note, factor steps one partial's walk
// and returns the energy-preserving mix dry + wet * noise for it.
struct BandwidthNoise {
  void prepare(int n) {
    cur_.assign(size_t(n), 0.0f);
    target_.assign(size_t(n), 0.0f);
    pos_.assign(size_t(n), 0);
    rngs_.assign(size_t(n), Randomizer(0u));
  }

  // Everything here fills within the capacity prepare gave it.
  void start(int n, uint32_t noteBase, uint32_t layer, float hz, float rate) {
    len_ = std::max(1, int(rate / (hz < 1.0f ? 1.0f : hz)));
    cur_.assign(size_t(n), 0.0f);
    target_.assign(size_t(n), 0.0f);
    pos_.assign(size_t(n), 0);
    if (int(rngs_.size()) != n) rngs_.resize(size_t(n), Randomizer(0u));
    for (int i = 0; i < n; ++i) {
      rngs_[size_t(i)]   = Randomizer(stream_seed(noteBase, uint32_t(i), layer));
      cur_[size_t(i)]    = rngs_[size_t(i)].valuePN();
      target_[size_t(i)] = rngs_[size_t(i)].valuePN();
      pos_[size_t(i)]    = int(rngs_[size_t(i)].range(0.0f, float(len_)));  // stagger phase
    }
  }

  // One partial's step: a new target at the segment's end, the smoothstep
  // between the current and the target, the position advanced.
  float factor(int index, float dry, float wet) {
    const size_t i = size_t(index);
    if (pos_[i] >= len_) {
      cur_[i]    = target_[i];
      target_[i] = rngs_[i].valuePN();  // per-partial stream
      pos_[i]    = 0;
    }
    float u = float(pos_[i]) / float(len_);
    float s = u * u * (3.0f - 2.0f * u);  // smoothstep → low-pass noise
    float noise = cur_[i] + (target_[i] - cur_[i]) * s;
    ++pos_[i];
    return dry + wet * noise;
  }

private:
  std::vector<float> cur_, target_;
  std::vector<int>   pos_;
  std::vector<Randomizer> rngs_;   // per-partial streams
  int len_{1600};                   // segment length in samples, from bandwidthHz at start
};

} // namespace mforce
