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
struct Limiter final : ValueSource {

  void set_source(std::shared_ptr<ValueSource> s)    { source_    = std::move(s); }
  void set_threshold(std::shared_ptr<ValueSource> s) { threshold_ = std::move(s); }
  void set_release(std::shared_ptr<ValueSource> s)   { release_   = std::move(s); }

  std::shared_ptr<ValueSource> get_source() const    { return source_; }
  std::shared_ptr<ValueSource> get_threshold() const { return threshold_; }
  std::shared_ptr<ValueSource> get_release() const   { return release_; }

  const char* type_name() const override { return "Limiter"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"threshold", 0.95f, 0.0f,   1.0f, "0-1"},
      {"release",   0.05f, 0.001f, 1.0f, "sec"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_    = std::move(src); return; }
    if (name == "threshold") { threshold_ = std::move(src); return; }
    if (name == "release")   { release_   = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "threshold") return threshold_;
    if (name == "release")   return release_;
    return nullptr;
  }

  explicit Limiter(int sampleRate)
  : sampleRate_(sampleRate),
    lookaheadSamples_(std::max(1, int(std::lround(sampleRate * kLookaheadSec)))),
    lookahead_(lookaheadSamples_, 0.0f) {}

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_)    source_->prepare(ctx, frames);
    if (threshold_) threshold_->prepare(ctx, frames);
    if (release_)   release_->prepare(ctx, frames);

    std::fill(lookahead_.begin(), lookahead_.end(), 0.0f);
    write_ = 0;
    peak_  = 0.0f;
    gain_  = 1.0f;
  }

  float next() override {
    float in = 0.0f;
    if (source_)    { source_->next();    in = source_->current(); }
    if (threshold_) { threshold_->next(); }
    if (release_)   { release_->next();   }

    float thr     = threshold_ ? std::clamp(threshold_->current(), 1e-4f, 1.0f) : 0.95f;
    float relSec  = release_   ? std::max(0.001f, release_->current())          : 0.05f;

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
    cur_ = lookahead_[read] * gain_;
    lookahead_[write_] = in;
    write_ = read;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  static constexpr float kLookaheadSec = 0.005f;  // 5 ms

  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> threshold_;
  std::shared_ptr<ValueSource> release_;

  int   sampleRate_;
  int   lookaheadSamples_;
  std::vector<float> lookahead_;
  int   write_{0};
  float peak_{0.0f};
  float gain_{1.0f};
  float cur_{0.0f};
};

} // namespace mforce
