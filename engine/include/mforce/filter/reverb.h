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
struct Reverb final : Processor {
  Param roomSize{this, "roomSize", 0.5f,  0.0f, 1.0f, "0-1"};
  Param damping {this, "damping",  0.5f,  0.0f, 1.0f, "0-1"};
  Param wet     {this, "wet",      0.33f, 0.0f, 1.0f, "0-1"};
  Param dry     {this, "dry",      0.4f,  0.0f, 1.0f, "0-1"};

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

  const char* type_name() const override { return "Reverb"; }

  void on_prepare(const RenderContext&, int) override {
    for (auto& c : combs_)   { std::fill(c.buffer.begin(), c.buffer.end(), 0.0f); c.idx = 0; c.filt = 0.0f; }
    for (auto& a : allpass_) { std::fill(a.buffer.begin(), a.buffer.end(), 0.0f); a.idx = 0; }
  }

  float compute() override {
    float in = input.current();

    float room   = std::clamp(roomSize.current(), 0.0f, 1.0f);
    float damp   = std::clamp(damping.current(),  0.0f, 1.0f);
    float wetAmt = std::clamp(wet.current(),      0.0f, 1.0f);
    float dryAmt = std::clamp(dry.current(),      0.0f, 1.0f);

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

    return ap_out * wetAmt + in * dryAmt;
  }

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

  int sampleRate_;
  std::array<Comb,    kNumCombs>   combs_;
  std::array<Allpass, kNumAllpass> allpass_;
};

} // namespace mforce
