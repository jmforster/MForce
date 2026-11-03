#pragma once
#include <algorithm>
#include <cmath>

namespace mforce::dsp {

// Smith's memoryless bow friction curve (the Friedlander model as STK's
// BowTable writes it): for a differential velocity dv (bow minus string) the
// fraction of it the bow hands back to the string,
//   rc(dv) = clamp((|(dv + offset) · slope| + 0.75)^-4, minOut, maxOut).
// Near zero velocity the bow sticks (rc at its top); as the string slips away
// the curve falls off as the fourth power. `slope` is the pressure knob (a
// high pressure is a wide pulse), `offset` skews stick against slip. The
// caller multiplies dv by the result in its own order.
//
// The curve's top, STK BowTable's maximum: a string never hands back more than
// this fraction, so a bow at rest against it cannot run away.
inline constexpr float kBowFrictionMax = 0.98f;

inline float bow_friction(float dv, float slope, float offset, float minOut, float maxOut) {
  const float s = std::fabs((dv + offset) * slope) + 0.75f;
  return std::clamp(std::pow(s, -4.0f), minOut, maxOut);
}

} // namespace mforce::dsp
