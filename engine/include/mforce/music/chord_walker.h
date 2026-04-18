#pragma once
#include "mforce/music/style_table.h"
#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// WalkConstraint — what the caller (Composer or strategy) provides.
// ---------------------------------------------------------------------------
struct WalkConstraint {
  ScaleChord startChord;
  std::optional<ScaleChord> endChord;
  float totalBeats{16.0f};
  float minChordBeats{2.0f};
  float maxChordBeats{8.0f};
  std::optional<float> cadenceBeat;  // endChord starts here (relative to walk start)
};

// ---------------------------------------------------------------------------
// ChordWalker — generates a ChordProgression by walking a StyleTable.
// ---------------------------------------------------------------------------
struct ChordWalker {

  static ChordProgression walk(const StyleTable& style,
                               const WalkConstraint& constraint,
                               uint32_t seed) {
    Randomizer rng(seed);
    ChordProgression prog;

    // If cadenceBeat is set, split: walk approach chords up to cadenceBeat,
    // then place endChord from cadenceBeat to totalBeats.
    float walkBeats = constraint.totalBeats;
    if (constraint.cadenceBeat && constraint.endChord) {
      walkBeats = *constraint.cadenceBeat;
    }

    float remaining = walkBeats;
    ScaleChord current = constraint.startChord;
    std::string currentLabel = ChordLabel::to_string(current);
    std::vector<std::string> history;

    while (remaining > 0.01f) {
      float chordBeats = pick_duration(style, constraint, remaining, rng);

      // If no cadenceBeat, use old "last chord = endChord" logic
      if (!constraint.cadenceBeat && constraint.endChord) {
        float afterThis = remaining - chordBeats;
        if (afterThis < constraint.minChordBeats) {
          prog.add(constraint.endChord.value(), remaining);
          remaining = 0;
          break;
        }
      }

      prog.add(current, chordBeats);
      remaining -= chordBeats;
      history.push_back(currentLabel);

      if (remaining < 0.01f) break;

      current = pick_next(style, currentLabel, history, constraint, remaining, rng);
      currentLabel = ChordLabel::to_string(current);
    }

    // Place endChord from cadenceBeat to totalBeats
    if (constraint.cadenceBeat && constraint.endChord) {
      float cadenceDur = constraint.totalBeats - *constraint.cadenceBeat;
      if (cadenceDur > 0.01f) {
        prog.add(*constraint.endChord, cadenceDur);
      }
    }

    return prog;
  }

private:
  static float pick_duration(const StyleTable& style,
                             const WalkConstraint& constraint,
                             float remaining,
                             Randomizer& rng) {
    float preferred = style.preferredChordBeats > 0
                      ? style.preferredChordBeats
                      : constraint.minChordBeats;

    float lo = constraint.minChordBeats;
    float hi = std::min(constraint.maxChordBeats, remaining);
    if (lo > hi) lo = hi;

    float base = std::clamp(preferred, lo, hi);
    float variation = rng.decide(0.3f) ? (rng.decide(0.5f) ? 0.5f : 2.0f) : 1.0f;
    float dur = std::clamp(base * variation, lo, hi);

    dur = std::round(dur);
    if (dur < lo) dur = lo;
    if (dur > remaining) dur = remaining;

    return dur;
  }

  static ScaleChord pick_next(const StyleTable& style,
                              const std::string& currentLabel,
                              const std::vector<std::string>& history,
                              const WalkConstraint& constraint,
                              float remaining,
                              Randomizer& rng) {
    const auto* transitions = style.lookup(currentLabel, history);

    if (!transitions || transitions->empty()) {
      return ScaleChord{0, 0, &ChordDef::get("Major")};
    }

    std::vector<float> weights;
    weights.reserve(transitions->size());

    bool approaching = constraint.endChord.has_value()
                       && remaining <= constraint.minChordBeats * 3;

    for (const auto& t : *transitions) {
      float w = t.weight;

      if (approaching && constraint.endChord) {
        if (can_reach_target(style, ChordLabel::to_string(t.target),
                             *constraint.endChord)) {
          w *= 3.0f;
        }
        if (t.target.degree == constraint.endChord->degree
            && t.target.alteration == constraint.endChord->alteration) {
          w *= 5.0f;
        }
      }

      weights.push_back(w);
    }

    float total = 0;
    for (float w : weights) total += w;
    if (total <= 0) return (*transitions)[0].target;

    float roll = rng.value() * total;
    float accum = 0;
    for (int i = 0; i < (int)transitions->size(); ++i) {
      accum += weights[i];
      if (roll <= accum) return (*transitions)[i].target;
    }

    return transitions->back().target;
  }

  static bool can_reach_target(const StyleTable& style,
                               const std::string& fromLabel,
                               const ScaleChord& target) {
    auto it = style.transitions.find(fromLabel);
    if (it == style.transitions.end()) return false;
    for (const auto& t : it->second) {
      if (t.target.degree == target.degree
          && t.target.alteration == target.alteration) {
        return true;
      }
    }
    return false;
  }
};

} // namespace mforce
