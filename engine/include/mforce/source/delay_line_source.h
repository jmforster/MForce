#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace mforce {

// Pitch-tracked delay line — the resonator backbone for feedback loops
// (feedback_loop_design.md §3.4). No internal feedback; `amplitude` is a
// read-side loop-gain control (the KS loss factor — decays without the
// pitch bend cutoff modulation causes). Length = sample rate / frequency *
// ratio samples, fractional read (linear interp), modulatable per sample
// (mild artifacts accepted in v1). Buffer sized in prepare(ctx) for a 20 Hz
// floor — no heap in the render loop; frequencies below floor clamp.
struct DelayLineSource final : Processor {
  Param frequency{this, "frequency", 440.0f, 20.0f, 20000.0f, "hz"};
  Param ratio    {this, "ratio",     1.0f,   0.05f, 20.0f,    "ratio"};
  Param amplitude{this, "amplitude", 1.0f,   0.0f,  2.0f,     "gain"};

  // compensate (off by default, so existing loops are byte-identical): keep
  // the tap-closed loop through this delay ON pitch by shortening the read
  // length by everything else the cycle delays — the tap's positional z^-1
  // plus each loop member's phase_delay_at(f0), re-queried per sample so a
  // moving in-loop cutoff stays pitch-neutral. Requires the closing tap to
  // target THIS node (true of the loop skeletons); parallel feedback paths
  // sum their reported lags, which is only meaningful for a single path.
  Setting<bool> compensate{this, "compensate", false, false, true};

  const char* type_name() const override { return "DelayLine"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  // Sized for the 20 Hz floor at the host's rate; a repeat prepare at the
  // same rate leaves the buffer alone, a new rate re-sizes it. One slot
  // past the ring stays zero (see compute()).
  void on_prepare(const RenderContext&) override {
    const int want = sample_rate() / 20 + 4;
    if (ring_ != want) {
      buf_.assign(size_t(want) + 1, 0.0f);
      ring_ = want;
    }
  }

  // Diagnostic: the delay ring's length in samples.
  int capacity() const { return ring_; }

  void on_start(int) override {
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    writeIdx_ = 0;
    cur_ = 0.0f;
    // Re-discover loop membership each note so live rewiring is picked up.
    // The walk is heap-free (fixed visited/member arrays, graph-depth
    // recursion) — start() may run at note-on in the audio callback.
    for (auto& m : members_) m.reset();
    nMembers_ = 0;
    cycleFound_ = false;
    if (compensate.value && input.source) {
      ValueSource* visited[kMaxWalk];
      int nVisited = 0;
      cycleFound_ = walk_to_tap(input.source, visited, nVisited);
    }
  }

  float compute() override {
    const float in = input.current();
    const float f = std::max(frequency.current(), 20.0f);
    float comp = 0.0f;
    if (cycleFound_) {
      comp = 1.0f;  // the closing tap's positional z^-1
      for (int i = 0; i < nMembers_; ++i) comp += members_[i]->phase_delay_at(f);
    }
    const float len = std::clamp(float(sample_rate()) / f * ratio.current() - comp,
                                 1.0f, float(ring_) - 2.0f);
    buf_[size_t(writeIdx_)] = in;
    float rp = float(writeIdx_) - len;
    if (rp < 0.0f) rp += float(ring_);
    // A read position a hair under the write index wraps to exactly ring_
    // in float: i0 is then the slot past the ring. The correct read is slot
    // 0 (the wrap); the slot stands in for what the old out-of-range read
    // returned in practice, silence, until that render change is taken.
    const int i0 = int(rp);
    const int i1 = (i0 + 1) % ring_;
    const float frac = rp - float(i0);
    // Read-side gain: the loop (and the listener) feel a closing envelope
    // immediately, not one recirculation later as a write-side gain would.
    const float out = (buf_[size_t(i0)] * (1.0f - frac) + buf_[size_t(i1)] * frac)
                      * amplitude.current();
    writeIdx_ = (writeIdx_ + 1) % ring_;
    return out;
  }

private:
  static constexpr int kMaxWalk = 64;
  // 8 proved too small the moment a loop grew a valve chain (backlog 66b:
  // Damp_lpf silently evicted, −85¢). 16 costs 64 bytes per delay.
  static constexpr int kMaxMembers = 16;

  // DFS from a pin toward the tap that closes the cycle back to this node.
  // Returns true when this subtree reaches such a tap; every node on a
  // reaching path records itself in members_ (shared_ptr, so a UI rewire
  // between walks can't dangle the render thread). A tap is not a member: the
  // tap that reads this node closes the loop, and any other tap is walked
  // through to the node it reads.
  //
  // The walk follows INPUT pins only (backlog 66, 2026-09-08). Signal flows
  // through inputs; params (drive, morph, amplitude, cutoff…) are control.
  // A control chain that happens to read the loop — valve2's
  // Junction.drive → valve → sum → tap — delays no signal, but the old walk
  // recorded its nodes as members, compensating phase that isn't in the
  // cycle (r090 +300¢ sharp) and overflowing the member cap until real
  // members were silently evicted (r100 −85¢ flat, Damp_lpf dropped).
  bool walk_to_tap(const std::shared_ptr<ValueSource>& sp,
                   ValueSource** visited, int& nVisited) {
    ValueSource* n = sp.get();
    if (!n) return false;
    if (auto* tap = dynamic_cast<TapSource*>(n)) {
      if (tap->source.get() == this) return true;  // the closing tap
      return walk_to_tap(tap->source, visited, nVisited);
    }
    for (int i = 0; i < nVisited; ++i)
      if (visited[i] == n) return false;
    if (nVisited >= kMaxWalk) return false;
    visited[nVisited++] = n;
    bool found = false;
    for (const auto& d : n->input_descriptors()) {
      auto child = n->get_param(d.name);
      if (child && walk_to_tap(child, visited, nVisited)) found = true;
    }
    if (found && nMembers_ < kMaxMembers) members_[nMembers_++] = sp;
    return found;
  }

  std::vector<float> buf_;   // the ring, then one extra slot that stays zero
  int ring_{0};              // length of the ring in samples; buf_.size() is ring_ + 1
  int writeIdx_{0};
  bool cycleFound_{false};
  std::shared_ptr<ValueSource> members_[kMaxMembers];
  int nMembers_{0};
};

} // namespace mforce
