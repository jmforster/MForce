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

// prune(fig, count, from_start): remove `count` units from the end
// (default) or the start. When from_start=true, the new first unit's
// step value is forced to 0 to honor the step[0]=0 convention (the
// original step value is lost).
inline MelodicFigure prune(const MelodicFigure& fig, int count,
                           bool from_start = false) {
  MelodicFigure out = fig;
  const int n = int(out.units.size());
  if (count < 0 || count >= n)
    throw std::invalid_argument("prune: count out of range");
  if (from_start) {
    out.units.erase(out.units.begin(), out.units.begin() + count);
    if (!out.units.empty()) out.units[0].step = 0;
  } else {
    out.units.resize(n - count);
  }
  return out;
}

// set_last_pulse(fig, duration): set last unit's duration absolutely.
inline MelodicFigure set_last_pulse(const MelodicFigure& fig,
                                    float duration) {
  MelodicFigure out = fig;
  if (!out.units.empty()) out.units.back().duration = duration;
  return out;
}

// adjust_last_pulse(fig, delta): change last unit's duration by delta
// (signed). Clamps at 0 to avoid negative durations.
inline MelodicFigure adjust_last_pulse(const MelodicFigure& fig,
                                       float delta) {
  MelodicFigure out = fig;
  if (!out.units.empty())
    out.units.back().duration = std::max(0.0f,
        out.units.back().duration + delta);
  return out;
}

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

// combine(a, b, fc): canonical join with a FigureConnector.
// Composes: prune(elideCount) -> adjust_last_pulse(adjustCount) ->
// set b.units[0].step = leadStep -> concatenate.
inline MelodicFigure combine(const MelodicFigure& a,
                             const MelodicFigure& b,
                             const FigureConnector& fc = {}) {
  MelodicFigure left = a;
  if (fc.elideCount > 0) left = prune(left, fc.elideCount);
  if (fc.adjustCount != 0.0f) left = adjust_last_pulse(left, fc.adjustCount);

  MelodicFigure right = b;
  if (!right.units.empty()) right.units[0].step = fc.leadStep;

  MelodicFigure out = left;
  for (const auto& u : right.units) out.units.push_back(u);
  return out;
}

// combine(a, b, leadStep, elide): sugar for the common case.
// elide=true means elide 1 unit.
inline MelodicFigure combine(const MelodicFigure& a,
                             const MelodicFigure& b,
                             int leadStep, bool elide = false) {
  FigureConnector fc;
  fc.leadStep = leadStep;
  fc.elideCount = elide ? 1 : 0;
  return combine(a, b, fc);
}

// replicate(fig, repeats, leadStep, elide): iterative combine.
// Returns `repeats` copies of fig joined with (leadStep, elide) each.
inline MelodicFigure replicate(const MelodicFigure& fig, int repeats,
                               int leadStep = 0, bool elide = false) {
  if (repeats < 1) repeats = 1;
  MelodicFigure out = fig;
  for (int i = 1; i < repeats; ++i) out = combine(out, fig, leadStep, elide);
  return out;
}

// replicate(fig, connectorSteps): total copies = 1 + connectorSteps.size().
// connectorSteps[i] is the leadStep joining copy (i+1) to what precedes.
inline MelodicFigure replicate(const MelodicFigure& fig,
                               const std::vector<int>& connectorSteps) {
  MelodicFigure out = fig;
  for (int leadStep : connectorSteps) out = combine(out, fig, leadStep, false);
  return out;
}

} // namespace mforce::figure_transforms
