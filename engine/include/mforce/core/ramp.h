#pragma once
#include <cmath>

namespace mforce {

// Ported from C# MForce.Utility.RampType
enum class RampType { Linear, Expo, InverseExpo, Sine };

// ---------------------------------------------------------------------------
// Ported from C# MForce.Utility.Ramp
// Moves from startVal to endVal over normalized position [0,1].
// ---------------------------------------------------------------------------
struct Ramp {
  float startVal{0.0f}, endVal{1.0f};
  RampType type{RampType::Linear};
  float power{0.0f};
  float holdPct{0.0f};

  float value(float pos) const {
    if (pos <= holdPct) return startVal;

    float t = (pos - holdPct) / (1.0f - holdPct);
    float range = endVal - startVal;

    if (type == RampType::Linear) {
      return startVal + range * t;
    }

    if (type == RampType::Expo) {
      return startVal < endVal
        ? startVal + range * std::pow(t, power)
        : endVal - range * std::pow(1.0f - t, power);
    }

    if (type == RampType::InverseExpo) {
      return startVal < endVal
        ? endVal - range * std::pow(1.0f - t, power)  // note: inverted vs Expo
        : startVal + range * std::pow(t, power);
    }

    // Sine
    if (power == 0.0f) {
      constexpr float PI = 3.14159265358979323846f;
      return startVal + range * (std::cos((1.0f + t) * PI) + 1.0f) * 0.5f;
    }

    // Pseudo-sine: two spliced expo curves
    if (startVal < endVal) {
      return t < 0.5f
        ? startVal + range * std::pow(t * 2.0f, power) * 0.5f
        : endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f + 0.5f;
    }
    return t < 0.5f
      ? endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f
      : startVal + range * std::pow(t * 2.0f, power) * 0.5f + 0.5f;
  }
};

} // namespace mforce
