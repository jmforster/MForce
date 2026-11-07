#pragma once
// CoherentWalk: the coherence-mixed random walk under the motion and shimmer layers.
#include "mforce/core/dsp/segment_walk.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace mforce {

// One shared walk every partial follows and one independent walk per
// partial, mixed per sample by a coherence in [0, 1]: at 1 every partial
// moves together, at 0 each moves on its own. Every walk draws from its own
// stream, seeded per note from the owner's note base and the two layer ids,
// so no draw depends on any other walk's. A value type the owner holds:
// prepare sizes it, start seeds it per note, advance runs it once per
// sample, value reads one partial's result.
struct CoherentWalk {
  void prepare(int n) {
    rngs_.assign(size_t(n), Randomizer(0u));
    walks_.assign(size_t(n), dsp::SegmentWalk{});
    vals_.assign(size_t(n), 0.0f);
  }

  // Everything here fills within the capacity prepare gave it.
  void start(int n, uint32_t noteBase, uint32_t sharedLayer, uint32_t indLayer,
             float hz, float evolve, float rate) {
    len_ = std::max(1, int(rate / std::max(0.01f, hz)));
    sharedRng_ = Randomizer(stream_seed(noteBase, 0, sharedLayer));
    shared_.init(len_, evolve, sharedRng_);
    walks_.assign(size_t(n), dsp::SegmentWalk{});
    if (int(rngs_.size()) != n) rngs_.resize(size_t(n), Randomizer(0u));
    for (int i = 0; i < n; ++i) {
      rngs_[size_t(i)] = Randomizer(stream_seed(noteBase, uint32_t(i), indLayer));
      walks_[size_t(i)].init(len_, evolve, rngs_[size_t(i)]);
    }
    vals_.assign(size_t(n), 0.0f);
  }

  void advance(float evolve, float coherence) {
    float shared = shared_.advance(len_, evolve, sharedRng_);
    float coh = coherence;
    for (size_t i = 0; i < walks_.size(); ++i) {
      float ind = walks_[i].advance(len_, evolve, rngs_[i]);
      vals_[i] = coh * shared + (1.0f - coh) * ind;
    }
  }

  float value(int i) const { return vals_[size_t(i)]; }

private:
  dsp::SegmentWalk shared_;
  Randomizer sharedRng_{0u};
  std::vector<dsp::SegmentWalk> walks_;
  std::vector<Randomizer> rngs_;
  std::vector<float> vals_;
  int len_{1};
};

} // namespace mforce
