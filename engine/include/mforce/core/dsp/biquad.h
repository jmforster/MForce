#pragma once
#include "mforce/core/dsp/constants.h"
#include <algorithm>
#include <cmath>

namespace mforce::dsp {

// The four memories of one second-order section.
struct BiquadState {
  float x1{0.0f}, x2{0.0f}, y1{0.0f}, y2{0.0f};
  void reset() { x1 = x2 = y1 = y2 = 0.0f; }
};

// A second-order section in Direct Form I with a0 normalised to one,
//   y = b0·x + b1·x1 + b2·x2 − a1·y1 − a2·y2,
// kept apart from its state so that one coefficient set can drive many
// sections (the mesh runs one per edge node with coefficients shared per
// sample). The named recipes are the formulas the nodes used to carry.
struct BiquadCoefficients {
  void set_raw(float b0, float b1, float b2, float a1, float a2) {
    b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
  }

  // STK BiQuad::setResonance: two poles at radius r and ±2π·hz/rate, no
  // zeros, b0 the gain.
  void set_resonance(float hz, float radius, float sampleRate, float gain) {
    b0_ = gain;
    b1_ = 0.0f;
    b2_ = 0.0f;
    a1_ = -2.0f * radius * std::cos(2.0f * kPi * hz / sampleRate);
    a2_ = radius * radius;
  }

  // The RBJ resonant lowpass at `omega` radians per sample, with its peak
  // scaled to unity so the section shapes a timbre without adding gain.
  void set_resonant_lowpass(float omega, float q) {
    const float alpha = std::sin(omega) / (2.0f * q);
    const float cosOm = std::cos(omega);
    const float a0 = 1.0f + alpha;
    const float c = 1.0f - cosOm;
    const float scale = 1.0f / (a0 * std::max(1.0f, q));
    b0_ = 0.5f * c * scale;
    b1_ = c * scale;
    b2_ = 0.5f * c * scale;
    a1_ = -2.0f * cosOm / a0;
    a2_ = (1.0f - alpha) / a0;
  }

  // The double-real-pole allpass (a² − 2a z⁻¹ + z⁻²) / (1 − 2a z⁻¹ + a² z⁻²):
  // unity magnitude, a group delay that falls with frequency, so a loop
  // that contains it stretches its upper partials sharp (string dispersion).
  void set_allpass_double_pole(float a) {
    b0_ = a * a;
    b1_ = -2.0f * a;
    b2_ = 1.0f;
    a1_ = -2.0f * a;
    a2_ = a * a;
  }

  // Chafe's second-order allpass (a2 + a1 z⁻¹ + z⁻²) / (1 + a1 z⁻¹ + a2 z⁻²)
  // with a1 = −2R cos(2π·fc/rate), a2 = R²: the mesh's edge termination.
  void set_allpass_chafe(float fc, float R, float sampleRate) {
    a1_ = -2.0f * R * std::cos(2.0f * kPi * fc / sampleRate);
    a2_ = R * R;
    b0_ = a2_;
    b1_ = a1_;
    b2_ = 1.0f;
  }

  float process(BiquadState& s, float x) const {
    const float y = b0_ * x + b1_ * s.x1 + b2_ * s.x2 - a1_ * s.y1 - a2_ * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    return y;
  }

  // Phase delay in samples at `hz`, from the closed-form H(e^{jw}). A net
  // sign inversion (a negative b0 folding a reflection) contributes ±π of
  // phase that is polarity, not delay, so the argument is folded to
  // (−π/2, π/2] modulo π before dividing by w: an inverted section reports
  // the same delay as its positive twin.
  float phase_delay_at(float hz, float sampleRate) const {
    if (hz <= 0.0f) return 0.0f;
    const float w = 2.0f * kPi * hz / sampleRate;
    const float c1 = std::cos(w), s1 = std::sin(w);
    const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
    const float nr = b0_ + b1_ * c1 + b2_ * c2;
    const float ni = -(b1_ * s1 + b2_ * s2);
    const float dr = 1.0f + a1_ * c1 + a2_ * c2;
    const float di = -(a1_ * s1 + a2_ * s2);
    float ph = std::atan2(ni, nr) - std::atan2(di, dr);
    while (ph >  0.5f * kPi) ph -= kPi;
    while (ph <= -0.5f * kPi) ph += kPi;
    return -ph / w;
  }

  float b0() const { return b0_; }
  float b1() const { return b1_; }
  float b2() const { return b2_; }
  float a1() const { return a1_; }
  float a2() const { return a2_; }

private:
  float b0_{1.0f}, b1_{0.0f}, b2_{0.0f}, a1_{0.0f}, a2_{0.0f};
};

// One section with its own state: the common case.
struct Biquad {
  BiquadCoefficients coefficients;
  BiquadState state;
  float process(float x) { return coefficients.process(state, x); }
  void reset() { state.reset(); }
};

} // namespace mforce::dsp
