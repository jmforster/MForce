#pragma once
#include <optional>
#include <string>
#include <stdexcept>

namespace mforce {

// Melodic contour classes for pool-based figure selection. The classifier that
// assigns these lives Python-side at pool-build time; C++ only matches the tag.
enum class Contour { Up, Down, Arch, Valley, Level };

inline const char* to_string(Contour c) {
    switch (c) {
        case Contour::Up:     return "Up";
        case Contour::Down:   return "Down";
        case Contour::Arch:   return "Arch";
        case Contour::Valley: return "Valley";
        case Contour::Level:  return "Level";
    }
    return "Up";
}

inline Contour contour_from_string(const std::string& s) {
    if (s == "Up")     return Contour::Up;
    if (s == "Down")   return Contour::Down;
    if (s == "Arch")   return Contour::Arch;
    if (s == "Valley") return Contour::Valley;
    if (s == "Level")  return Contour::Level;
    throw std::runtime_error("contour_from_string: unknown contour '" + s + "'");
}

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
  std::optional<Contour> contour;      // target melodic contour (pool selection)
  // future: preferStepwise, preferSkips, maxLeap, maxStep
};

} // namespace mforce
