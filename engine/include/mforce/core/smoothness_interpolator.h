#pragma once
#include <cmath>

namespace mforce {

struct SmoothnessInterpolator {
  explicit SmoothnessInterpolator(float s = 0.0f, bool usePower=false)
  : smoothness(s), usePower(usePower) {}

  void setSmoothness(float s) { smoothness = s; }

  static float lerp(float a, float b, float t) { return a + (b - a) * t; }

  static float sineInterp(float a, float b, float t) {
    // standard: 0.5 - 0.5*cos(pi*t)
    float k = 0.5f - 0.5f * std::cos(t * 3.14159265358979323846f);
    return a + (b - a) * k;
  }

  float interpolate(float v1, float v2, float pos) const {
    float sinVal = sineInterp(v1, v2, pos);

    if (smoothness == 0.0f) {
      return v1;
    } else if (smoothness == 0.5f) {
      return lerp(v1, v2, pos);
    } else if (smoothness == 1.0f) {
      return sinVal;
    } else if (smoothness < 0.5f) {
      if (usePower) {
        // v1 + (v2-v1) * pos^(1 + 100*(1-s)^10)
        double p = 1.0 + 100.0 * std::pow(1.0 - smoothness, 10.0);
        return float(v1 + (v2 - v1) * std::pow(pos, p));
      } else {
        // Blend of none and linear:
        // 2*(0.5-s)*v1 + 2*s*linear
        float lin = lerp(v1, v2, pos);
        return 2.0f * (0.5f - smoothness) * v1 + 2.0f * smoothness * lin;
      }
    } else {
      // Blend of linear and sine:
      // 2*(1-s)*linear + 2*(s-0.5)*sine
      float lin = lerp(v1, v2, pos);
      return 2.0f * (1.0f - smoothness) * lin + 2.0f * (smoothness - 0.5f) * sinVal;
    }
  }

  float smoothness{0.0f};
  bool  usePower{false};
};

} // namespace mforce
