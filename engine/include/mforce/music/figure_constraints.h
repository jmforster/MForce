#pragma once
#include <optional>

namespace mforce {

// All constraint axes for figure generation. Fields are optional — only
// set what you want to pin down. RandomFigureBuilder satisfies every set
// constraint or throws.
struct Constraints {
  std::optional<int>   count;          // number of FigureUnits
  std::optional<float> length;         // total beats
  std::optional<int>   net;            // net step movement (sum of steps)
  std::optional<int>   ceiling;        // running step-position ceiling
  std::optional<int>   floor;          // running step-position floor
  std::optional<float> defaultPulse;   // bias center for pulse generator
  std::optional<float> minPulse;       // smallest permitted pulse
  std::optional<float> maxPulse;       // largest permitted pulse
  // future: preferStepwise, preferSkips, maxLeap, maxStep, targetContour
};

} // namespace mforce
