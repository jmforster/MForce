#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// Reverb — Schroeder/Freeverb-style: 8 parallel feedback comb filters with
// damping → 4 series allpass filters → wet/dry mix. Tunings from Freeverb
// (Jezar at Dreampoint, public domain), scaled to the running sample rate.
//
// Mono output. The ValueSource graph's pan/mix happens at SoundChannel.
// Spec: docs/superpowers/specs/2026-05-06-reverb-limiter-effects-design.md
// ---------------------------------------------------------------------------
struct Reverb final : ValueSource {

  void set_source(std::shared_ptr<ValueSource> s)   { source_   = std::move(s); }
  void set_roomSize(std::shared_ptr<ValueSource> s) { roomSize_ = std::move(s); }
  void set_damping(std::shared_ptr<ValueSource> s)  { damping_  = std::move(s); }
  void set_wet(std::shared_ptr<ValueSource> s)      { wet_      = std::move(s); }
  void set_dry(std::shared_ptr<ValueSource> s)      { dry_      = std::move(s); }

  std::shared_ptr<ValueSource> get_source() const   { return source_; }
  std::shared_ptr<ValueSource> get_roomSize() const { return roomSize_; }
  std::shared_ptr<ValueSource> get_damping() const  { return damping_; }
  std::shared_ptr<ValueSource> get_wet() const      { return wet_; }
  std::shared_ptr<ValueSource> get_dry() const      { return dry_; }

  const char* type_name() const override { return "Reverb"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"roomSize", 0.5f,  0.0f, 1.0f, "0-1"},
      {"damping",  0.5f,  0.0f, 1.0f, "0-1"},
      {"wet",      0.33f, 0.0f, 1.0f, "0-1"},
      {"dry",      0.4f,  0.0f, 1.0f, "0-1"},
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
    if (name == "source")   { source_   = std::move(src); return; }
    if (name == "roomSize") { roomSize_ = std::move(src); return; }
    if (name == "damping")  { damping_  = std::move(src); return; }
    if (name == "wet")      { wet_      = std::move(src); return; }
    if (name == "dry")      { dry_      = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")   return source_;
    if (name == "roomSize") return roomSize_;
    if (name == "damping")  return damping_;
    if (name == "wet")      return wet_;
    if (name == "dry")      return dry_;
    return nullptr;
  }

  explicit Reverb(int sampleRate) : sampleRate_(sampleRate) {
    // Allocate comb + allpass buffers sized to the current sample rate.
    float sr_ratio = float(sampleRate_) / 44100.0f;
    for (int i = 0; i < kNumCombs; ++i) {
      int size = std::max(1, int(std::lround(kCombTunings[i] * sr_ratio)));
      combs_[i].buffer.assign(size, 0.0f);
    }
    for (int i = 0; i < kNumAllpass; ++i) {
      int size = std::max(1, int(std::lround(kAllpassTunings[i] * sr_ratio)));
      allpass_[i].buffer.assign(size, 0.0f);
    }
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_)   source_->prepare(ctx, frames);
    if (roomSize_) roomSize_->prepare(ctx, frames);
    if (damping_)  damping_->prepare(ctx, frames);
    if (wet_)      wet_->prepare(ctx, frames);
    if (dry_)      dry_->prepare(ctx, frames);

    for (auto& c : combs_)   { std::fill(c.buffer.begin(), c.buffer.end(), 0.0f); c.idx = 0; c.filt = 0.0f; }
    for (auto& a : allpass_) { std::fill(a.buffer.begin(), a.buffer.end(), 0.0f); a.idx = 0; }
  }

  float next() override {
    float in = 0.0f;
    if (source_)   { source_->next();   in       = source_->current(); }
    if (roomSize_) { roomSize_->next(); }
    if (damping_)  { damping_->next();  }
    if (wet_)      { wet_->next();      }
    if (dry_)      { dry_->next();      }

    float room = roomSize_ ? std::clamp(roomSize_->current(), 0.0f, 1.0f) : 0.5f;
    float damp = damping_  ? std::clamp(damping_->current(),  0.0f, 1.0f) : 0.5f;
    float wet  = wet_      ? std::clamp(wet_->current(),      0.0f, 1.0f) : 0.33f;
    float dry  = dry_      ? std::clamp(dry_->current(),      0.0f, 1.0f) : 0.4f;

    float feedback = room * kRoomScale + kRoomOffset;  // 0.7 .. 0.98
    float damp1    = damp * kDampScale;
    float damp2    = 1.0f - damp1;

    // Combs: parallel sum
    float gained = in * kFixedGain;
    float combs_sum = 0.0f;
    for (auto& c : combs_) {
      float out = c.buffer[c.idx];
      c.filt = out * damp2 + c.filt * damp1;
      c.buffer[c.idx] = gained + c.filt * feedback;
      c.idx = (c.idx + 1) % int(c.buffer.size());
      combs_sum += out;
    }

    // Allpass: series
    float ap_out = combs_sum;
    for (auto& a : allpass_) {
      float bufout = a.buffer[a.idx];
      float out = -ap_out + bufout;
      a.buffer[a.idx] = ap_out + bufout * kAllpassFeedback;
      a.idx = (a.idx + 1) % int(a.buffer.size());
      ap_out = out;
    }

    cur_ = ap_out * wet + in * dry;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // Freeverb tunings at 44.1 kHz (samples). Scaled to running sample rate
  // in the constructor.
  static constexpr int kNumCombs = 8;
  static constexpr int kNumAllpass = 4;
  static constexpr int kCombTunings[kNumCombs] = {
    1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
  };
  static constexpr int kAllpassTunings[kNumAllpass] = {
    556, 441, 341, 225
  };
  // Internal scaling constants (Freeverb originals).
  static constexpr float kRoomScale       = 0.28f;
  static constexpr float kRoomOffset      = 0.7f;   // feedback = room * 0.28 + 0.7
  static constexpr float kDampScale       = 0.4f;
  static constexpr float kAllpassFeedback = 0.5f;
  static constexpr float kFixedGain       = 0.015f; // input attenuation into combs

  struct Comb {
    std::vector<float> buffer;
    int   idx{0};
    float filt{0.0f};
  };
  struct Allpass {
    std::vector<float> buffer;
    int idx{0};
  };

  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> roomSize_;
  std::shared_ptr<ValueSource> damping_;
  std::shared_ptr<ValueSource> wet_;
  std::shared_ptr<ValueSource> dry_;

  int sampleRate_;
  std::array<Comb,    kNumCombs>   combs_;
  std::array<Allpass, kNumAllpass> allpass_;
  float cur_{0.0f};
};

} // namespace mforce
