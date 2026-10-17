#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// Limiter — lookahead brick-wall. Peak follower with fast-attack /
// slow-release ballistics, instant gain reduction, smoothed gain release.
// 5 ms lookahead so reductions kick in before the loud sample reaches
// the output (transparent transients).
//
// Distinct from engine/include/mforce/render/limiter.h::soft_clip which is
// a fixed safety belt at the render layer; this is a configurable node
// the user places anywhere in the patch graph.
//
// Spec: docs/superpowers/specs/2026-05-06-reverb-limiter-effects-design.md
// ---------------------------------------------------------------------------
struct Limiter final : Processor {
  Param threshold{this, "threshold", 0.95f, 0.0f,   1.0f, "0-1"};
  Param release  {this, "release",   0.05f, 0.001f, 1.0f, "sec"};

  explicit Limiter(int sampleRate)
  : sampleRate_(sampleRate),
    lookaheadSamples_(std::max(1, int(std::lround(sampleRate * kLookaheadSec)))),
    lookahead_(lookaheadSamples_, 0.0f) {}

  const char* type_name() const override { return "Limiter"; }

  void on_prepare(const RenderContext&, int) override {
    std::fill(lookahead_.begin(), lookahead_.end(), 0.0f);
    write_ = 0;
    peak_  = 0.0f;
    gain_  = 1.0f;
  }

  float compute() override {
    float in = input.current();

    float thr     = std::clamp(threshold.current(), 1e-4f, 1.0f);
    float relSec  = std::max(0.001f, release.current());

    // Per-sample release coefficient: peak decays from 1.0 to ~e^-1 in
    // relSec seconds. release_smooth governs the gain-recovery envelope.
    float releaseCoef = std::exp(-1.0f / std::max(1.0f, relSec * float(sampleRate_)));

    float abs_in = std::fabs(in);

    // Peak follower: instant attack, exponential release.
    peak_ = std::max(abs_in, peak_ * releaseCoef);

    // Target gain — bring peak down to threshold, no boost when below.
    float target = (peak_ > thr) ? (thr / peak_) : 1.0f;

    // Gain smoothing: instant down (catch transients), smooth up.
    gain_ = (target < gain_) ? target
                              : gain_ + (target - gain_) * (1.0f - releaseCoef);

    // Lookahead: write current input, output the sample from N samples ago.
    int read = (write_ + 1) % lookaheadSamples_;
    const float out = lookahead_[read] * gain_;
    lookahead_[write_] = in;
    write_ = read;
    return out;
  }

private:
  static constexpr float kLookaheadSec = 0.005f;  // 5 ms

  int   sampleRate_;
  int   lookaheadSamples_;
  std::vector<float> lookahead_;
  int   write_{0};
  float peak_{0.0f};
  float gain_{1.0f};
};

} // namespace mforce
