#pragma once
#include "mforce/contract/performance.h"
#include "mforce/core/dsp/one_pole.h"
#include <algorithm>
#include <cmath>
#include <span>

namespace mforce {

// The one rule for when a voice is over, stepped one sample at a time.
//
// A voice plays its body (the note, or a held line and its release), then a
// fixed tail so ringing state inside it is not cut mid-sample. A voice still
// above the floor (about -60 dBFS) when the tail ends plays on until it falls
// under the floor or reaches the cap, eight seconds past the body; a voice
// still audible at the cap fades to zero over its last 80 ms. The level is a
// follower over the unfaded samples with a 50 ms decay.
//
// The follower counts every sample it is shown, from the voice's start. The
// body length may come late: until it is given the voice goes on and nothing
// fades.
class RingOut {
public:
  static constexpr float kTailSeconds = kVoiceTailSeconds;
  static constexpr float kFloor       = 0.001f;
  static constexpr float kMaxSeconds  = 8.0f;

  RingOut(int sampleRate, int bodySamples) : RingOut(sampleRate) { set_body(bodySamples); }

  // A voice whose body length is not known yet.
  explicit RingOut(int sampleRate)
      : sampleRate_(sampleRate),
        decay_(dsp::pole_for_seconds(0.05f, float(sampleRate))),
        fadeSamples_(int(0.08f * float(sampleRate))) {}

  // The body's length in samples, from the voice's start.
  void set_body(int bodySamples) {
    tailEnd_ = bodySamples + int(kTailSeconds * float(sampleRate_));
    cap_ = bodySamples + int(kMaxSeconds * float(sampleRate_));
    bodyKnown_ = true;
  }

  // What the voice already played, shown to the follower.
  void prime(std::span<const float> played) {
    for (float s : played) level_ = std::max(std::fabs(s), level_ * decay_);
    seen_ += int(played.size());
  }

  // Whether the voice plays another sample.
  bool more() const {
    if (!bodyKnown_) return true;
    return seen_ < cap_ && !(seen_ >= tailEnd_ && level_ < kFloor);
  }

  // The next sample, shown to the follower and faded when the cap is near.
  float shape(float sample) {
    level_ = std::max(std::fabs(sample), level_ * decay_);
    if (bodyKnown_ && seen_ >= cap_ - fadeSamples_ && level_ >= kFloor)
      sample *= float(cap_ - seen_) / float(fadeSamples_);
    ++seen_;
    return sample;
  }

  // The most samples the voice can play; 0 while the body is not known.
  int limit() const { return bodyKnown_ ? cap_ : 0; }

private:
  int   sampleRate_;
  float decay_;
  int   fadeSamples_;
  int   tailEnd_{0};
  int   cap_{0};
  bool  bodyKnown_{false};
  float level_{0.0f};
  int   seen_{0};
};

} // namespace mforce
