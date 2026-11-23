#pragma once
#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace mforce {

enum class TransformOp {
    None,
    Invert,           // flip step directions
    Reverse,          // retrograde
    Stretch,          // multiply durations by factor (default 2x)
    Compress,         // divide durations by factor (default 2x)
    VaryRhythm,       // split/dot some pulses randomly
    VarySteps,        // randomly perturb some step values
    NewSteps,         // keep rhythm, generate new steps
    NewRhythm,        // keep steps, generate new rhythm
    Replicate,        // repeat N times with step offset between
    TransformGeneral, // do *something* — composer decides the operation
    RhythmTail,       // derive a PulseSequence from a MelodicFigure by
                      // taking the rhythm tail (skip first N pulses).
                      // param = N. Plan B uses this for figures like
                      // K467's A_rhythm_tail.
    Complexify,       // elaborate via split/neighbor/turn to a TARGET NOTE
                      // COUNT (param; 0 = one extra unit). Length
                      // preserved. Matt 2026-09-21: elaboration "reined in
                      // via note count" — the walk-stage rep3_b op.
};

} // namespace mforce

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

// stretch(fig, factor): scale all durations by factor. factor>1 lengthens.
inline MelodicFigure stretch(const MelodicFigure& fig, float factor) {
  MelodicFigure out = fig;
  for (auto& u : out.units) u.duration *= factor;
  return out;
}

// compress(fig, factor): scale all durations by 1/factor. factor>1 shortens.
inline MelodicFigure compress(const MelodicFigure& fig, float factor) {
  if (factor <= 0.0f) throw std::invalid_argument("compress: factor must be > 0");
  MelodicFigure out = fig;
  for (auto& u : out.units) u.duration /= factor;
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

// rotate(fig, k): cyclic permutation of the (duration, step) pairs by k
// positions, then re-anchor so units[0].step == 0. The same set of
// note-to-note moves is heard starting from a different point in the cycle,
// so the material is recognizably the same but the contour is not identical
// — which is what makes it usable as a non-literal restatement. Mirrors
// figure_transforms.rotate in the Python prototype library.
inline MelodicFigure rotate(const MelodicFigure& fig, int k = 1) {
  MelodicFigure out;
  const int n = int(fig.units.size());
  if (n == 0) return out;
  k = ((k % n) + n) % n;
  out.units.reserve(n);
  for (int i = 0; i < n; ++i) out.units.push_back(fig.units[(k + i) % n]);
  out.units[0].step = 0;
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

// --- Randomized (Randomizer& passed in) ---

// vary_rhythm(fig, rng): probability-weighted pulse split or dot.
// Legacy behavior from FigureBuilder::vary_rhythm.
inline MelodicFigure vary_rhythm(const MelodicFigure& fig, Randomizer& rng) {
  MelodicFigure out = fig;
  for (int x = 0; x < out.note_count() - 1; ++x) {
    if (rng.decide(0.2f)) {
      float dur = out.units[x].duration;
      if (dur < 0.5f * 2.0f) continue;            // min-pulse gate
      float dur1, dur2;
      if (dur < 1.0f || rng.decide(0.5f)) {
        dur1 = dur * 0.5f; dur2 = dur * 0.5f;     // even split
      } else {
        dur1 = dur * 0.75f; dur2 = dur * 0.25f;   // dotted split
      }
      out.units[x].duration = dur1;
      FigureUnit newUnit{dur2, 0};
      out.units.insert(out.units.begin() + x + 1, newUnit);
      break;
    } else if (x < out.note_count() - 1 && rng.decide(0.3f)) {
      float dur = out.units[x].duration;
      out.units[x].duration = dur * 1.5f;
      out.units[x + 1].duration = dur * 0.5f;
      break;
    }
  }
  return out;
}

// vary_steps(fig, rng, variations): perturb `variations` steps. Any step
// but step[0] (the dummy by convention; the connector carries the entry)
// may move, including the final one (walk3 spec §4: the interior-only
// restriction was unexplained legacy). Each perturbation adds a random int
// in [-2,+2] (guaranteed non-zero). Legacy from FigureBuilder::vary_steps.
inline MelodicFigure vary_steps(const MelodicFigure& fig, Randomizer& rng,
                                int variations = 1) {
  MelodicFigure out = fig;
  for (int i = 0; i < variations && out.note_count() > 1; ++i) {
    int idx = rng.int_range(1, out.note_count() - 1);
    int delta = rng.int_range(-2, 2);
    if (delta == 0) delta = (rng.int_range(0, 1) == 0) ? -1 : 1;
    out.units[idx].step += delta;
  }
  return out;
}

// vary(fig, rng, amount): consolidated jitter. Applies a rhythm
// perturbation and a step perturbation, both scaled by `amount` in
// [0,1]. Round-1 implementation: probability of applying each sub-
// perturbation = amount; if applied, uses vary_rhythm / vary_steps
// atoms internally. Conservative by default.
inline MelodicFigure vary(const MelodicFigure& fig, Randomizer& rng,
                          float amount) {
  MelodicFigure out = fig;
  if (rng.decide(amount)) out = vary_rhythm(out, rng);
  if (rng.decide(amount)) out = vary_steps(out, rng, 1);
  return out;
}

// complexify(fig, rng, amount): length preserved; target unit count ~=
// (1+amount)*original. Round-1 implementation: repeatedly apply one of
// {split, add_neighbor, add_turn} at a random position until target
// reached or max iterations. amount=0 -> no-op; amount=1 -> doubled.
inline MelodicFigure complexify(const MelodicFigure& fig, Randomizer& rng,
                                float amount) {
  MelodicFigure out = fig;
  const int targetCount = int(std::round(float(fig.note_count()) * (1.0f + amount)));
  int safety = 0;
  const int maxIter = targetCount * 3 + 4;
  while (out.note_count() < targetCount && safety++ < maxIter) {
    int addAt = rng.int_range(0, out.note_count() - 1);
    float pick = rng.value();
    try {
      if      (pick < 0.4f) out = split(out, addAt, 2);
      else if (pick < 0.8f) out = add_neighbor(out, addAt, rng.decide(0.5f));
      else                  out = add_turn    (out, addAt, rng.decide(0.5f));
    } catch (const std::invalid_argument&) {
      continue; // addAt was invalid for that transform; try again
    }
  }
  return out;
}

// embellish(fig, rng, count): mark `count` randomly-chosen units with an
// accent-style articulation. Unit count and length unchanged. Round-1
// placeholder — uses Marcato (closest classical accent marking in this
// codebase's articulations set). Later rounds will populate with varied
// Ornament types.
inline MelodicFigure embellish(const MelodicFigure& fig, Randomizer& rng,
                               int count) {
  MelodicFigure out = fig;
  const int n = out.note_count();
  if (n == 0 || count <= 0) return out;
  count = std::min(count, n);
  std::vector<int> idx(n);
  for (int i = 0; i < n; ++i) idx[i] = i;
  for (int i = n - 1; i > 0; --i) {
    int j = rng.int_range(0, i);
    std::swap(idx[i], idx[j]);
  }
  for (int k = 0; k < count; ++k) {
    out.units[idx[k]].articulation = articulations::Marcato{};
  }
  return out;
}

// ---------------------------------------------------------------------------
// split_moving_ — 2-way split where the SECOND piece moves (walk2 spec §2:
// sub-beat repeated pitches are never emitted; "repeated note in a fast run
// = computer did it"). The inserted motion is passing when the following
// unit already moves (toward it), else a neighbor; the following unit's
// step is compensated so every later absolute pitch is preserved. A split
// at the figure's last unit uses a neighbor and changes the figure's net —
// legal under harmonic anchorMode, where the selector sees realized nets.
// ---------------------------------------------------------------------------
inline MelodicFigure split_moving_(const MelodicFigure& fig, int at,
                                   float frac0, Randomizer& rng) {
  MelodicFigure out = fig;
  const auto src = out.units[at];
  FigureUnit p0 = src, p1 = src;
  p0.duration = src.duration * frac0;
  p1.duration = src.duration * (1.0f - frac0);
  int n;
  const bool hasNext = at + 1 < int(out.units.size());
  if (hasNext && out.units[at + 1].step != 0) {
    n = out.units[at + 1].step > 0 ? 1 : -1;      // passing toward next
  } else {
    n = rng.decide(0.5f) ? 1 : -1;                // neighbor
  }
  p1.step = n;
  out.units[at] = p0;
  out.units.insert(out.units.begin() + at + 1, p1);
  if (hasNext) out.units[at + 2].step -= n;       // absolutes preserved
  return out;
}

// ---------------------------------------------------------------------------
// elaborate(fig, rng, targetCount) — walk2's Complexify (spec 2026-09-21
// walk2 §2, from Matt's annotation ladder). Ladder-ordered, late-bar-
// weighted, strictly additive:
//   * while any unit >= 2 beats exists, split IT evenly first (h -> q q,
//     Mary's own move; repeats are fine at quarter length);
//   * further splits pick positions weighted toward the figure's END —
//     "rhythmic speed-up belongs in the second half, leading into the
//     concluding bit"; never lengthen anything (no accelerate-then-brake);
//   * depth: sub-half-of-parent pieces RARELY (~20%), and of those ~80%
//     are the dotted pattern (q -> e. + s); the default split is even;
//   * pieces shorter than a beat always MOVE (split_moving_ / add_turn).
// ---------------------------------------------------------------------------
inline MelodicFigure elaborate(const MelodicFigure& fig, Randomizer& rng,
                               int targetCount) {
  MelodicFigure out = fig;
  int safety = 0;
  while (out.note_count() < targetCount && safety++ < 32) {
    // Rung 1: the long note splits first.
    int longAt = -1;
    for (int i = 0; i < out.note_count(); ++i) {
      if (out.units[i].duration >= 2.0f) { longAt = i; break; }
    }
    if (longAt >= 0) { out = split(out, longAt, 2); continue; }

    // Late-weighted position choice among splittable units (>= an eighth).
    const float total = out.total_duration();
    std::vector<int> idxs;
    std::vector<float> w;
    float start = 0.0f, wsum = 0.0f;
    for (int i = 0; i < out.note_count(); ++i) {
      const float d = out.units[i].duration;
      if (d >= 0.5f) {
        const float wt = 0.2f + (start + 0.5f * d) / total;   // end ramp
        idxs.push_back(i);
        w.push_back(wt);
        wsum += wt;
      }
      start += d;
    }
    if (idxs.empty()) break;
    float draw = rng.value() * wsum;
    int k = 0;
    for (; k + 1 < int(idxs.size()); ++k) {
      draw -= w[k];
      if (draw <= 0.0f) break;
    }
    const int at = idxs[k];
    const float d = out.units[at].duration;

    const bool fine = (d >= 1.0f) && rng.decide(0.2f);
    if (fine && rng.decide(0.8f)) {
      out = split_moving_(out, at, 0.75f, rng);   // dotted: e. + s
    } else if (fine) {
      out = add_turn(out, at, rng.decide(0.5f));  // even fine, net-neutral
    } else if (d * 0.5f < 1.0f) {
      out = split_moving_(out, at, 0.5f, rng);    // fast halves must move
    } else {
      out = split(out, at, 2);                    // q q repeat is idiomatic
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// apply(base, op, param, seed) — TransformOp dispatch.
// Shared by DefaultFigureStrategy::apply_transform and TwoFigurePhraseStrategy.
// Behavior must stay bit-identical to the pre-refactor body — any change here
// changes Composer output for every FigureTemplate with source=Transform.
// ---------------------------------------------------------------------------
inline MelodicFigure apply(const MelodicFigure& base, TransformOp op,
                           int param, uint32_t seed) {
  Randomizer rng(seed);

  switch (op) {
    case TransformOp::Invert:
      return invert(base);

    case TransformOp::Reverse:
      return retrograde_steps(base);

    case TransformOp::Stretch:
      return stretch(base, param > 0 ? float(param) : 2.0f);

    case TransformOp::Compress:
      return compress(base, param > 0 ? float(param) : 2.0f);

    case TransformOp::VaryRhythm:
      return vary_rhythm(base, rng);

    case TransformOp::VarySteps:
      return vary_steps(base, rng, std::max(1, param));

    case TransformOp::NewSteps: {
      StepGenerator sg(seed);
      StepSequence raw = sg.random_sequence(base.note_count() - 1);
      StepSequence newSS; newSS.add(0);
      for (int i = 0; i < raw.count(); ++i) newSS.add(raw.get(i));
      float pulse = base.units.empty() ? 1.0f : base.units[0].duration;
      return MelodicFigure::from_steps(newSS, pulse);
    }

    case TransformOp::NewRhythm: {
      MelodicFigure fig = base;
      for (auto& u : fig.units) {
        u.duration *= (rng.decide(0.5f) ? 0.5f : 1.0f) * (rng.decide(0.3f) ? 1.5f : 1.0f);
      }
      return fig;
    }

    case TransformOp::Replicate: {
      int count = (param > 0) ? param : 2;
      int step = rng.select_int({-2, -1, 1, 2});
      return replicate(base, count, step, false);
    }

    case TransformOp::TransformGeneral: {
      float choice = rng.value();
      if (choice < 0.25f) return invert(base);
      if (choice < 0.50f) return vary_rhythm(base, rng);
      if (choice < 0.75f) return retrograde_steps(base);
      return stretch(base, 2.0f);
    }

    case TransformOp::RhythmTail:
      return base;

    case TransformOp::Complexify: {
      // walk2 (spec §2): ladder-ordered late-bar elaboration replaces the
      // uniform-random complexify — param = target note count.
      const int target = param > 0 ? param : base.note_count() + 1;
      return elaborate(base, rng, target);
    }

    case TransformOp::None:
    default:
      return base;
  }
}

} // namespace mforce::figure_transforms
