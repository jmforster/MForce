#pragma once
#include "mforce/music/figures.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/music/shape_figures.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/core/randomizer.h"
#include <stdexcept>
#include <algorithm>

namespace mforce {

class RandomFigureBuilder {
public:
  explicit RandomFigureBuilder(uint32_t seed)
      : pulseGen(seed), stepGen(seed + 1) {}

  // Authoritative entry point. Satisfy every set constraint or throw.
  MelodicFigure build(const Constraints& c);

  // Convenience methods. Each merges its primary arg into a local
  // Constraints and delegates to build(c). Throws if caller's c already
  // has the field that would be set (presence-level strictness per spec).
  MelodicFigure build_by_count  (int count,               const Constraints& c = {});
  MelodicFigure build_by_length (float length,            const Constraints& c = {});
  MelodicFigure build_by_steps  (const StepSequence& ss,  const Constraints& c = {});
  MelodicFigure build_by_rhythm (const PulseSequence& ps, const Constraints& c = {});
  MelodicFigure build_singleton (const Constraints& c = {});

private:
  PulseGenerator pulseGen;
  StepGenerator  stepGen;

  int  resolve_count_(const Constraints& c);
  bool feasible_(const Constraints& c, int count) const;
  MelodicFigure wander_(int count, const Constraints& c);
  MelodicFigure post_clamp_(MelodicFigure fig, const Constraints& c);
  bool satisfies_(const MelodicFigure& fig, const Constraints& c) const;

  MelodicFigure try_arc_      (int count, const Constraints& c, float pulse);
  MelodicFigure try_run_      (int count, const Constraints& c, float pulse);
  MelodicFigure try_zigzag_   (int count, const Constraints& c, float pulse);
  MelodicFigure try_neighbor_ (int count, const Constraints& c, float pulse);
  MelodicFigure try_leap_fill_(int count, const Constraints& c, float pulse);
};

inline int RandomFigureBuilder::resolve_count_(const Constraints& c) {
  if (c.count) return *c.count;
  if (c.length && c.defaultPulse) {
    int n = int(std::round(*c.length / *c.defaultPulse));
    return std::max(1, n);
  }
  static Randomizer countRng(0x0C117u);
  return countRng.int_range(4, 8);
}

inline bool RandomFigureBuilder::feasible_(const Constraints& c, int count) const {
  if (count < 1) return false;
  if (c.minPulse && c.length && (*c.minPulse) * float(count) > *c.length) return false;
  return true;
}

inline bool RandomFigureBuilder::satisfies_(const MelodicFigure& fig, const Constraints& c) const {
  if (c.net && fig.net_step() != *c.net) return false;
  if (c.ceiling || c.floor) {
    int pos = 0, hi = 0, lo = 0;
    for (const auto& u : fig.units) { pos += u.step; hi = std::max(hi, pos); lo = std::min(lo, pos); }
    if (c.ceiling && hi > *c.ceiling) return false;
    if (c.floor   && lo < *c.floor)   return false;
  }
  return true;
}

inline MelodicFigure RandomFigureBuilder::wander_(int count, const Constraints& c) {
  float pulse = c.defaultPulse.value_or(1.0f);
  PulseSequence ps = c.length
      ? pulseGen.generate(*c.length, pulse)
      : pulseGen.generate_count(count, pulse);

  // StepGenerator emits count-1 steps; prepend 0 to honor units[0].step=0.
  StepSequence raw = (c.net
      ? stepGen.targeted_sequence(count - 1, *c.net)
      : stepGen.random_sequence(count - 1));
  StepSequence ss;
  ss.add(0);
  for (int i = 0; i < raw.count(); ++i) ss.add(raw.get(i));

  // Truncate/grow ps to match ss count if generate() returned a different size.
  while (int(ps.pulses.size()) < ss.count()) ps.add(pulse);
  ps.pulses.resize(ss.count());

  return MelodicFigure(ps, ss);
}

inline MelodicFigure RandomFigureBuilder::post_clamp_(MelodicFigure fig, const Constraints& c) {
  // Round-1 no-op — clamping lives in wander_ via StepGenerator targeting.
  // Left as a seam for later rounds.
  (void)c;
  return fig;
}

inline MelodicFigure RandomFigureBuilder::build_by_count(int count, const Constraints& c) {
  if (c.count) throw std::invalid_argument("build_by_count: c.count already set");
  Constraints merged = c; merged.count = count;
  return build(merged);
}

inline MelodicFigure RandomFigureBuilder::build_by_length(float length, const Constraints& c) {
  if (c.length) throw std::invalid_argument("build_by_length: c.length already set");
  Constraints merged = c; merged.length = length;
  return build(merged);
}

inline MelodicFigure RandomFigureBuilder::build_by_steps(const StepSequence& ss, const Constraints& c) {
  if (c.count) throw std::invalid_argument("build_by_steps: c.count already set");
  if (c.net)   throw std::invalid_argument("build_by_steps: c.net already set");
  float pulse = c.defaultPulse.value_or(1.0f);
  PulseSequence ps = c.length
      ? pulseGen.generate(*c.length, pulse)
      : pulseGen.generate_count(ss.count(), pulse);
  while (int(ps.pulses.size()) < ss.count()) ps.add(pulse);
  ps.pulses.resize(ss.count());
  MelodicFigure fig(ps, ss);
  if (!fig.units.empty()) fig.units[0].step = 0; // honor convention
  return fig;
}

inline MelodicFigure RandomFigureBuilder::build_by_rhythm(const PulseSequence& ps, const Constraints& c) {
  if (c.count)  throw std::invalid_argument("build_by_rhythm: c.count already set");
  if (c.length) throw std::invalid_argument("build_by_rhythm: c.length already set");
  StepSequence raw = (c.net
      ? stepGen.targeted_sequence(ps.count() - 1, *c.net)
      : stepGen.random_sequence(ps.count() - 1));
  StepSequence ss; ss.add(0);
  for (int i = 0; i < raw.count(); ++i) ss.add(raw.get(i));
  return MelodicFigure(ps, ss);
}

inline MelodicFigure RandomFigureBuilder::build_singleton(const Constraints& c) {
  if (c.count) throw std::invalid_argument("build_singleton: c.count already set");
  float dur = c.length.value_or(c.defaultPulse.value_or(1.0f));
  MelodicFigure fig;
  fig.units.push_back({dur, 0});
  return fig;
}

// --- Shape helpers: try_* ---

inline MelodicFigure RandomFigureBuilder::try_arc_(int count, const Constraints& c, float pulse) {
  // Shape: 1 + extent + returnExtent = count. Split halves (or per c.net).
  int total = count - 1;
  int extent, returnExtent;
  if (c.net) {
    int n = *c.net;
    int absN = std::abs(n);
    if (absN > total) throw std::runtime_error("try_arc_: net too large for count");
    extent = (total + absN) / 2;
    returnExtent = total - extent;
    if (n < 0) std::swap(extent, returnExtent);
  } else {
    extent = total / 2 + (total % 2);
    returnExtent = total - extent;
  }
  int direction = (c.net.value_or(+1) >= 0) ? +1 : -1;
  return shape_figures::arc(direction, extent, returnExtent, pulse);
}

inline MelodicFigure RandomFigureBuilder::try_run_(int count, const Constraints& c, float pulse) {
  int direction;
  if (c.net) direction = (*c.net >= 0) ? +1 : -1;
  else direction = (stepGen.rng.int_range(0, 1) == 0) ? +1 : -1;
  return shape_figures::run(direction, count, pulse);
}

inline MelodicFigure RandomFigureBuilder::try_zigzag_(int count, const Constraints& c, float pulse) {
  // count = 1 + 2*cycles → cycles = (count-1)/2. Round down; trailing unit may be off-grid.
  int cycles = (count - 1) / 2;
  if (cycles < 1) cycles = 1;
  int direction = (c.net.value_or(+1) >= 0) ? +1 : -1;
  return shape_figures::zigzag(direction, cycles, 2, 1, pulse);
}

inline MelodicFigure RandomFigureBuilder::try_neighbor_(int count, const Constraints& c, float pulse) {
  // neighbor() is always 3 units; if count > 3, combine with a run for the tail.
  MelodicFigure base = shape_figures::neighbor(stepGen.rng.decide(0.5f), pulse);
  if (count == 3) return base;
  int direction = (c.net.value_or(+1) >= 0) ? +1 : -1;
  MelodicFigure tail = shape_figures::run(direction, count - 2, pulse);
  return figure_transforms::combine(base, tail, 0, false);
}

inline MelodicFigure RandomFigureBuilder::try_leap_fill_(int count, const Constraints& c, float pulse) {
  int leapSize = 3; // round-1 default; tuning later
  bool leapUp = (c.net.value_or(+1) >= 0);
  int fillSteps = count - 2;
  if (fillSteps < 0) fillSteps = 0;
  return shape_figures::leap_and_fill(leapSize, leapUp, fillSteps, pulse);
}

// build(c) — feasibility filter + weighted shape-coin-flip with 3-retry.
inline MelodicFigure RandomFigureBuilder::build(const Constraints& c) {
  int count = resolve_count_(c);
  if (!feasible_(c, count))
    throw std::invalid_argument("RandomFigureBuilder::build: infeasible constraints");

  for (int attempt = 0; attempt < 3; ++attempt) {
    MelodicFigure fig;
    float pulse = c.defaultPulse.value_or(1.0f);

    bool canRun       = count >= 2;
    bool canNeighbor  = count >= 3;
    bool canLeapFill  = count >= 2;
    bool canArc       = count >= 3;
    bool canZigzag    = count >= 3;

    float r = pulseGen.rng.value();
    float acc = 0.0f;
    auto eat = [&](bool available, float w) {
      if (!available) return false;
      acc += w; return r < acc;
    };

    if      (eat(canArc,      0.30f)) fig = try_arc_         (count, c, pulse);
    else if (eat(canRun,      0.20f)) fig = try_run_         (count, c, pulse);
    else if (eat(canZigzag,   0.10f)) fig = try_zigzag_      (count, c, pulse);
    else if (eat(canNeighbor, 0.10f)) fig = try_neighbor_    (count, c, pulse);
    else if (eat(canLeapFill, 0.10f)) fig = try_leap_fill_   (count, c, pulse);
    else                              fig = wander_(count, c);

    fig = post_clamp_(fig, c);
    if (satisfies_(fig, c)) return fig;
  }
  throw std::runtime_error("RandomFigureBuilder::build: failed after 3 retries");
}

} // namespace mforce
