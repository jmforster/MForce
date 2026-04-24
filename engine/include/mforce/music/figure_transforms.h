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

// replicate_and_prune(fig, connectorSteps, pruneAt1, pruneAt2):
// Replicate per connectorSteps (total copies = 1 + size), then prune
// the last unit from the copy at 1-indexed position pruneAt1 and,
// if pruneAt2 != 0, also from pruneAt2. Value 0 = no prune at that slot.
// Use case: acceleration toward a climax.
inline MelodicFigure replicate_and_prune(
    const MelodicFigure& fig,
    const std::vector<int>& connectorSteps,
    int pruneAt1,
    int pruneAt2 = 0) {
  const int copies = 1 + int(connectorSteps.size());
  const int unitsPerCopy = int(fig.units.size());
  auto validate = [&](int idx) {
    if (idx < 0 || idx > copies)
      throw std::invalid_argument("replicate_and_prune: prune index out of range");
    if (idx > 0 && unitsPerCopy < 2)
      throw std::invalid_argument("replicate_and_prune: copy would be emptied by prune");
  };
  validate(pruneAt1);
  validate(pruneAt2);

  MelodicFigure out;
  auto appendCopy = [&](const MelodicFigure& src, int leadStep) {
    MelodicFigure piece = src;
    if (!piece.units.empty()) piece.units[0].step = leadStep;
    for (const auto& u : piece.units) out.units.push_back(u);
  };
  appendCopy(fig, 0);
  if (pruneAt1 == 1 || pruneAt2 == 1) out.units.pop_back();
  for (int i = 0; i < int(connectorSteps.size()); ++i) {
    int copyIdx = i + 2;
    appendCopy(fig, connectorSteps[i]);
    if (pruneAt1 == copyIdx || pruneAt2 == copyIdx) out.units.pop_back();
  }
  return out;
}

// split(fig, splitAt, repeats): replace unit at splitAt with `repeats`
// sub-units each of duration original.duration/repeats. First sub-unit
// inherits step; the rest have step=0. Length preserved; unit count
// grows by repeats-1.
inline MelodicFigure split(const MelodicFigure& fig, int splitAt,
                           int repeats) {
  if (repeats < 2 || splitAt < 0 || splitAt >= int(fig.units.size()))
    throw std::invalid_argument("split: invalid args");
  MelodicFigure out;
  out.units.reserve(fig.units.size() + repeats - 1);
  for (int i = 0; i < int(fig.units.size()); ++i) {
    if (i != splitAt) {
      out.units.push_back(fig.units[i]);
    } else {
      const auto& src = fig.units[i];
      float sub = src.duration / float(repeats);
      out.units.push_back({sub, src.step});      // first inherits step
      for (int k = 1; k < repeats; ++k)
        out.units.push_back({sub, 0});            // rest step=0
    }
  }
  return out;
}

// add_neighbor(fig, addAt, down): replace unit at addAt with a 3-unit
// neighbor motion of equal total duration:
//   sub-unit 0: duration/2, original step   (arrives at same pitch)
//   sub-unit 1: duration/4, step=down?-1:+1 (neighbor)
//   sub-unit 2: duration/4, step=down?+1:-1 (return)
// Length preserved; unit count grows by 2.
inline MelodicFigure add_neighbor(const MelodicFigure& fig, int addAt,
                                  bool down = false) {
  if (addAt < 0 || addAt >= int(fig.units.size()))
    throw std::invalid_argument("add_neighbor: addAt out of range");
  MelodicFigure out;
  out.units.reserve(fig.units.size() + 2);
  for (int i = 0; i < int(fig.units.size()); ++i) {
    if (i != addAt) {
      out.units.push_back(fig.units[i]);
    } else {
      const auto& src = fig.units[i];
      float halfDur = src.duration * 0.5f;
      float quarterDur = src.duration * 0.25f;
      int neighborStep = down ? -1 : +1;
      out.units.push_back({halfDur,    src.step});
      out.units.push_back({quarterDur, neighborStep});
      out.units.push_back({quarterDur, -neighborStep});
    }
  }
  return out;
}

// add_turn(fig, addAt, down): replace unit at addAt with a 4-unit turn
// motion of equal total duration. Round-1: upper/lower neighbor pair
// around the original pitch. down=false → upper-first (+1,-2,+1,0),
// down=true → lower-first (-1,+2,-1,0). Each sub-unit is duration/4.
// Length preserved; unit count grows by 3.
inline MelodicFigure add_turn(const MelodicFigure& fig, int addAt,
                              bool down = false) {
  if (addAt < 0 || addAt >= int(fig.units.size()))
    throw std::invalid_argument("add_turn: addAt out of range");
  MelodicFigure out;
  out.units.reserve(fig.units.size() + 3);
  for (int i = 0; i < int(fig.units.size()); ++i) {
    if (i != addAt) {
      out.units.push_back(fig.units[i]);
    } else {
      const auto& src = fig.units[i];
      float q = src.duration * 0.25f;
      int first = down ? -1 : +1;
      out.units.push_back({q, src.step});       // arrive at main pitch
      out.units.push_back({q, first});          // upper (or lower) neighbor
      out.units.push_back({q, -2 * first});     // cross to other neighbor
      out.units.push_back({q, first});          // return to main
    }
  }
  return out;
}

} // namespace mforce::figure_transforms
