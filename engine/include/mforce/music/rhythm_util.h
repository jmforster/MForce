#pragma once
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// generate_musical_rhythm
//
// Returns a sequence of standard binary-subdivision note durations (multiples
// of 0.25 beats) that sum to totalBeats.  All values are drawn from the set
// { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 } so the result is always
// expressed in conventional notation and never produces non-standard durations
// like 1.333 or 0.571.
//
// Note count is variable: the greedy weighted-random selection can produce
// anywhere from 1 note (if totalBeats happens to be 4.0 and the RNG picks 4.0
// on the first draw) to many notes (if 0.25 is picked repeatedly).
//
// defaultPulse biases selection: durations close to defaultPulse receive
// higher weight, while durations far from it receive lower weight.
// ---------------------------------------------------------------------------
inline std::vector<float> generate_musical_rhythm(
    float totalBeats, float defaultPulse, Randomizer& rng) {
  static const float DURATIONS[] = {
    0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f
  };
  static const int NUM_DURATIONS = 8;

  std::vector<float> result;
  float remaining = totalBeats;

  while (remaining > 0.001f) {
    // Collect candidates that fit within the remaining duration
    std::vector<float> candidates;
    for (int i = 0; i < NUM_DURATIONS; ++i) {
      if (DURATIONS[i] <= remaining + 0.001f)
        candidates.push_back(DURATIONS[i]);
    }
    if (candidates.empty()) break;

    // Weight toward defaultPulse: durations closer to defaultPulse
    // receive higher weight via inverse-distance formula.
    std::vector<float> weights;
    float weightSum = 0;
    for (float d : candidates) {
      float w = 1.0f / (1.0f + std::abs(d - defaultPulse) * 2.0f);
      weights.push_back(w);
      weightSum += w;
    }

    // Weighted random selection
    float pick = rng.value() * weightSum;
    float accum = 0;
    int idx = 0;
    for (int i = 0; i < int(weights.size()); ++i) {
      accum += weights[i];
      if (accum >= pick) { idx = i; break; }
    }

    result.push_back(candidates[idx]);
    remaining -= candidates[idx];
  }

  return result;
}

// ---------------------------------------------------------------------------
// direction_sign
//
// Returns +1 or -1 for the preferred step direction at position `pos` out of
// `totalSteps` movements (i.e., the interval between note pos and note pos+1).
// The caller multiplies by the desired step magnitude.
// ---------------------------------------------------------------------------
inline int direction_sign(FigureDirection dir, int pos, int totalSteps, Randomizer& rng) {
  if (totalSteps <= 0) return 1;
  float frac = float(pos) / float(totalSteps);  // 0.0 to 1.0 across the figure

  switch (dir) {
    case FigureDirection::Ascending:            return 1;
    case FigureDirection::Descending:           return -1;
    case FigureDirection::TurnaroundAscending:  return (pos == 0) ? -1 : 1;
    case FigureDirection::TurnaroundDescending: return (pos == 0) ? 1 : -1;
    case FigureDirection::AscendingArc:         return (frac < 0.5f) ? 1 : -1;
    case FigureDirection::DescendingArc:        return (frac < 0.5f) ? -1 : 1;
    case FigureDirection::SineAscending:
      return (frac < 0.33f) ? 1 : (frac < 0.67f) ? -1 : 1;
    case FigureDirection::SineDescending:
      return (frac < 0.33f) ? -1 : (frac < 0.67f) ? 1 : -1;
    case FigureDirection::Random:
    default:
      return rng.decide(0.5f) ? 1 : -1;
  }
}

} // namespace mforce
