#pragma once
#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace mforce::figure_transforms {

// Deterministic transforms are pure. Randomized transforms take
// Randomizer& explicitly. All functions take const MelodicFigure& and
// return a new MelodicFigure — no mutation.

// --- Deterministic (no rng) ---

// invert(fig): negate all steps. units[0].step=0 is preserved.
inline MelodicFigure invert(const MelodicFigure& fig) {
  MelodicFigure out = fig;
  for (auto& u : out.units) u.step = -u.step;
  return out;
}

// retrograde_steps(fig): reverse pitch sequence in time. Pulses DO NOT
// reverse (hence the _steps suffix). Algorithm: new units[0].step=0;
// for i>=1, new step[i] = -(old step[count-i]). Pulses keep order.
// Example: [0,+1,+1,-2] -> [0,+2,-1,-1].
inline MelodicFigure retrograde_steps(const MelodicFigure& fig) {
  MelodicFigure out;
  const int n = int(fig.units.size());
  if (n == 0) return out;
  out.units.reserve(n);
  out.units.push_back({fig.units[0].duration, 0});
  for (int i = 1; i < n; ++i) {
    out.units.push_back({fig.units[i].duration, -fig.units[n - i].step});
  }
  return out;
}

} // namespace mforce::figure_transforms
