#pragma once
#include "mforce/music/figures.h"
#include <cmath>

namespace mforce::shape_figures {

// Pure ordinal shape templates — no state, no rng, no walker assumptions.
// All shapes honor the bible rule: units[0].step == 0.

// run(direction, count, pulse): consecutive same-direction steps.
// direction: positive = ascending, negative = descending.
inline MelodicFigure run(int direction, int count, float pulse = 1.0f) {
  if (count < 1) count = 1;
  int dir = (direction >= 0) ? 1 : -1;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting unit
  for (int i = 1; i < count; ++i)
    fig.units.push_back({pulse, dir});
  return fig;
}

// repeats(count, pulse): same pitch N times.
inline MelodicFigure repeats(int count, float pulse = 1.0f) {
  if (count < 1) count = 1;
  MelodicFigure fig;
  for (int i = 0; i < count; ++i)
    fig.units.push_back({pulse, 0});
  return fig;
}

// neighbor(upper, pulse, doublePulseMain): main-neighbor-return (3 units).
// upper=true → 0,+1,-1;  false → 0,-1,+1.
// doublePulseMain: first and last units are 2*pulse.
inline MelodicFigure neighbor(bool upper, float pulse = 1.0f,
                              bool doublePulseMain = false) {
  int dir = upper ? 1 : -1;
  float mainPulse = doublePulseMain ? pulse * 2.0f : pulse;
  MelodicFigure fig;
  fig.units.push_back({mainPulse, 0});                // main
  fig.units.push_back({pulse, dir});                  // neighbor
  fig.units.push_back({mainPulse, -dir});             // return
  return fig;
}

// leap_and_fill(leapSize, leapUp, fillSteps, pulse): big step + stepwise
// recovery. Emits 1 + 1 + fillSteps units. fillSteps=0 defaults to
// leapSize-1 (full fill). leapSize is the magnitude of the leap step.
inline MelodicFigure leap_and_fill(int leapSize, bool leapUp,
                                   int fillSteps = 0, float pulse = 1.0f) {
  if (leapSize < 2) leapSize = 2;
  if (fillSteps <= 0) fillSteps = leapSize - 1;
  int leapDir = leapUp ? 1 : -1;
  int fillDir = -leapDir;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting
  fig.units.push_back({pulse, leapSize * leapDir});   // leap
  for (int i = 0; i < fillSteps; ++i)
    fig.units.push_back({pulse, fillDir});            // stepwise recovery
  return fig;
}

// arc(direction, extent, returnExtent, pulse): arch or inverted arch.
// 1 + extent + returnExtent units. returnExtent=0 → full return
// (returnExtent = extent).
inline MelodicFigure arc(int direction, int extent,
                         int returnExtent = 0, float pulse = 1.0f) {
  if (extent < 1) extent = 1;
  if (returnExtent <= 0) returnExtent = extent;
  int dir = (direction >= 0) ? 1 : -1;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting
  for (int i = 0; i < extent; ++i)
    fig.units.push_back({pulse, dir});                // outward
  for (int i = 0; i < returnExtent; ++i)
    fig.units.push_back({pulse, -dir});               // return
  return fig;
}

// zigzag(direction, cycles, stepSize, skipSize, pulse):
// step stepSize*dir, then -skipSize*dir, per cycle. 1 + 2*cycles units.
inline MelodicFigure zigzag(int direction, int cycles,
                            int stepSize = 2, int skipSize = 1,
                            float pulse = 1.0f) {
  if (cycles < 1) cycles = 1;
  int dir = (direction >= 0) ? 1 : -1;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting
  for (int i = 0; i < cycles; ++i) {
    fig.units.push_back({pulse, stepSize * dir});
    fig.units.push_back({pulse, -skipSize * dir});
  }
  return fig;
}

} // namespace mforce::shape_figures
