#pragma once
#include "mforce/core/dsp_wave_source.h"
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.TriangleSource
// Bias controls the peak position (0..1); 0.5 = symmetric triangle.
//
// `power` warps the two straight legs into curves, modelled on RampSource's
// Expo/Inverse_Expo (MForce.Utility.Ramp). Signed shape control, neutral at 1:
//   |power| <= 1 : linear — the historical triangle, byte-for-byte.
//   power  >  1  : CONCAVE sides — legs pinch inward (spiky), exponent `power`.
//   power  < -1  : CONVEX  sides — legs bow outward (domed), exponent |power|.
// Default 1.0 keeps every existing patch bit-identical (none set `power`).
//
// By default both sides bend the SAME way: the falling leg is the mirror of the
// rising leg about the peak, so the waveform stays symmetric. Setting the
// `asymmetric` flag restores the earlier per-leg warp (each leg warped from its
// own start), which bends the two sides in opposite senses — a "shark fin".
struct TriangleSource final : WaveSource {
  Param bias {this, "bias",  0.5f,  0.0f, 1.0f, "0-1"};
  Param power{this, "power", 1.0f, -8.0f, 8.0f, "shape"};
  // Off: symmetric legs (both sides bend the same way). On: the earlier
  // per-leg warp, which bends the two sides oppositely ("shark fin").
  Setting<bool> asymmetric{this, "asymmetric", false, false, true};

  explicit TriangleSource(int sampleRate) : WaveSource(sampleRate) {}
  const char* type_name() const override { return "TriangleSource"; }

protected:
  // Warp normalized leg position x in [0,1]. Identity in the neutral band.
  static float warp_leg(float x, float p) {
    const float e = std::fabs(p);
    if (p > 0.0f) return std::pow(x, e);              // concave (p > 1)
    return 1.0f - std::pow(1.0f - x, e);              // convex  (p < -1)
  }

  float wave() override {
    const float b = bias.current();
    const float p = power.current();

    // Neutral band: reproduce the legacy arithmetic exactly (byte-identical),
    // identical in both symmetric and asymmetric modes.
    if (std::fabs(p) <= 1.0f) {
      if (currPos_ <= b) {
        return -1.0f + currPos_ * (4.0f / b) / 2.0f;
      } else {
        return 1.0f - (currPos_ - b) * (4.0f / (1.0f - b)) / 2.0f;
      }
    }

    if (currPos_ <= b) {
      // Rising leg: -1 -> +1 as t goes 0 -> 1 (same in both modes).
      float t = std::clamp(currPos_ / b, 0.0f, 1.0f);
      return -1.0f + 2.0f * warp_leg(t, p);
    } else {
      float u = std::clamp((currPos_ - b) / (1.0f - b), 0.0f, 1.0f);
      if (asymmetric.value) {
        // Each leg warped from its own start — bends the sides oppositely.
        return 1.0f - 2.0f * warp_leg(u, p);
      }
      // Symmetric: falling leg mirrors the rising leg about the peak, so both
      // sides bend the same way.
      return -1.0f + 2.0f * warp_leg(1.0f - u, p);
    }
  }
};

} // namespace mforce
