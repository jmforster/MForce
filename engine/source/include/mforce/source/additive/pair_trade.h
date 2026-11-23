#pragma once
// PairTrade: the energy-trading walk shared by adjacent partial pairs.
#include "mforce/core/dsp/segment_walk.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace mforce {

// One walk per adjacent pair of partials, all on one stream seeded per note;
// the owner applies it with opposite signs to the two partials of a pair, so
// energy sloshes between neighbours while the pair sum stays about constant.
// A value type the owner holds: prepare sizes it, start seeds it per note,
// advance runs it once per sample, value reads one pair's walk.
struct PairTrade {
  void prepare(int n) {
    walks_.assign(size_t((n + 1) / 2), dsp::SegmentWalk{});
    vals_.assign(size_t((n + 1) / 2), 0.0f);
  }

  // Everything here fills within the capacity prepare gave it.
  void start(int n, uint32_t noteBase, uint32_t layer, float hz, float rate) {
    len_ = std::max(1, int(rate / std::max(0.01f, hz)));
    rng_ = Randomizer(stream_seed(noteBase, 0, layer));
    walks_.assign(size_t((n + 1) / 2), dsp::SegmentWalk{});
    for (auto& w : walks_) w.init(len_, 0.0f, rng_);
    vals_.assign(size_t((n + 1) / 2), 0.0f);
  }

  void advance() {
    for (size_t i = 0; i < walks_.size(); ++i)
      vals_[i] = walks_[i].advance(len_, 0.0f, rng_);
  }

  float value(int pair) const { return vals_[size_t(pair)]; }

private:
  Randomizer rng_{0u};
  std::vector<dsp::SegmentWalk> walks_;
  std::vector<float> vals_;
  int len_{1};
};

} // namespace mforce
