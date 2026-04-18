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
// MelodySpan — a melody note sounding over a time span (for chord selection).
// ---------------------------------------------------------------------------
struct MelodySpan {
  float beat;       // relative to walk start
  float duration;
  int scaleDegree;  // 0-indexed (C=0 in C major)
  bool rest{false};
};

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
  std::vector<MelodySpan> melodyProfile;
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
    float currentBeat = 0.0f;
    ScaleChord current = melody_aware_start(constraint);
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
      currentBeat += chordBeats;
      remaining -= chordBeats;
      history.push_back(currentLabel);

      if (remaining < 0.01f) break;

      current = pick_next(style, currentLabel, history, constraint,
                          remaining, currentBeat, rng);
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

  // Harmonize a melody: at each attack beat, pick the best chord from the
  // style table that (a) contains the melody note and (b) transitions well
  // from the previous chord. Cadence constraints override at cadenceBeat.
  static ChordProgression harmonize(const StyleTable& style,
                                     const WalkConstraint& constraint,
                                     const std::vector<float>& attackBeats,
                                     uint32_t seed) {
    Randomizer rng(seed);
    ChordProgression prog;
    if (attackBeats.empty()) return prog;

    ScaleChord current = constraint.startChord;
    std::string currentLabel = ChordLabel::to_string(current);

    // Collect all unique chords in the style table for fallback search
    std::vector<std::pair<std::string, ScaleChord>> allChords;
    for (const auto& [label, transitions] : style.transitions) {
      allChords.push_back({label, ChordLabel::parse(label)});
    }

    int prevMelDeg = -2; // sentinel: no previous

    for (int i = 0; i < (int)attackBeats.size(); ++i) {
      float beat = attackBeats[i];
      float dur = (i + 1 < (int)attackBeats.size())
                  ? attackBeats[i + 1] - beat
                  : constraint.totalBeats - beat;

      // First beat: honor startChord (don't let melody override it)
      if (i == 0) {
        prog.add(current, dur);
        prevMelDeg = melody_degree_at(constraint.melodyProfile, beat);
        continue;
      }

      // Cadence override: force endChord at cadenceBeat
      if (constraint.cadenceBeat && constraint.endChord
          && beat >= *constraint.cadenceBeat - 0.01f) {
        prog.add(*constraint.endChord, dur);
        current = *constraint.endChord;
        currentLabel = ChordLabel::to_string(current);
        prevMelDeg = -2;
        continue;
      }

      int melDeg = melody_degree_at(constraint.melodyProfile, beat);

      // Sustained note rule: if same melody note as previous chord attack,
      // the note is still ringing — repeat the previous chord.
      if (melDeg >= 0 && melDeg == prevMelDeg) {
        prog.add(current, dur);
        continue;
      }
      prevMelDeg = melDeg;

      if (melDeg < 0) {
        // Rest or no melody — stay on current chord
        prog.add(current, dur);
        continue;
      }

      // Step 1: if current chord contains the melody note, staying is always
      // an option (implicit self-transition — no need to author it in the table).
      // Also check explicit transitions for alternatives.
      ScaleChord best = current;
      bool stayWorks = is_chord_tone(melDeg, current);
      float stayWeight = stayWorks ? 4.0f : 0.0f;

      const auto* transitions = style.lookup(currentLabel, {});
      std::vector<const StyleTable::Transition*> compatible;
      if (transitions) {
        for (const auto& t : *transitions) {
          if (is_chord_tone(melDeg, t.target)) {
            compatible.push_back(&t);
          }
        }
      }

      if (stayWorks || !compatible.empty()) {
        float total = stayWeight;
        for (auto* t : compatible) total += t->weight;
        float roll = rng.value() * total;

        if (roll < stayWeight) {
          best = current;  // stay
        } else {
          float accum = stayWeight;
          for (auto* t : compatible) {
            accum += t->weight;
            if (roll <= accum) { best = t->target; break; }
          }
        }
      }
      // Step 2: if neither stay nor any transition works, search all chords
      else {
        std::vector<std::pair<std::string, ScaleChord>> allCompatible;
        for (const auto& [label, sc] : allChords) {
          if (is_chord_tone(melDeg, sc)) {
            allCompatible.push_back({label, sc});
          }
        }
        if (!allCompatible.empty()) {
          int idx = rng.int_range(0, (int)allCompatible.size() - 1);
          best = allCompatible[idx].second;
        }
      }

      prog.add(best, dur);
      current = best;
      currentLabel = ChordLabel::to_string(current);
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

  // If melody profile is available, pick the startChord that contains the
  // melody note at beat 0. Falls back to constraint.startChord if no match.
  static ScaleChord melody_aware_start(const WalkConstraint& constraint) {
    if (constraint.melodyProfile.empty()) return constraint.startChord;
    // The start chord is already set by the caller — don't override it.
    // Melody awareness applies to subsequent chord picks.
    return constraint.startChord;
  }

  // Check if a scale degree is a chord tone of a ScaleChord.
  // Diatonic triads: root, root+2, root+4 (mod 7).
  // 7th chords add root+6 (mod 7).
  static bool is_chord_tone(int scaleDegree, const ScaleChord& chord) {
    int r = chord.degree % 7;
    int d = scaleDegree % 7;
    if (d == r) return true;                    // root
    if (d == (r + 2) % 7) return true;          // 3rd
    if (d == (r + 4) % 7) return true;          // 5th
    // Check for 7th chord qualities
    if (chord.quality && chord.quality->intervals.size() > 3) {
      // Any chord with more than 3 intervals has a 7th (or extension)
      if (d == (r + 6) % 7) return true;
    }
    return false;
  }

  // Find the melody scale degree sounding at a given beat.
  // Returns -1 if no melody note or rest.
  static int melody_degree_at(const std::vector<MelodySpan>& profile, float beat) {
    constexpr float eps = 0.01f;
    // Search backward — if beat falls on a boundary between spans,
    // prefer the later span (the new note, not the ending one).
    for (int i = (int)profile.size() - 1; i >= 0; --i) {
      const auto& span = profile[i];
      if (beat >= span.beat - eps && beat < span.beat + span.duration + eps) {
        return span.rest ? -1 : span.scaleDegree;
      }
    }
    return -1;
  }

  static ScaleChord pick_next(const StyleTable& style,
                              const std::string& currentLabel,
                              const std::vector<std::string>& history,
                              const WalkConstraint& constraint,
                              float remaining,
                              float currentBeat,
                              Randomizer& rng) {
    const auto* transitions = style.lookup(currentLabel, history);

    if (!transitions || transitions->empty()) {
      return ScaleChord{0, 0, &ChordDef::get("Major")};
    }

    // Check melody note at current beat
    int melDeg = melody_degree_at(constraint.melodyProfile, currentBeat);

    std::vector<float> weights;
    weights.reserve(transitions->size());

    bool approaching = constraint.endChord.has_value()
                       && remaining <= constraint.minChordBeats * 3;

    for (const auto& t : *transitions) {
      float w = t.weight;

      // Melody-aware: strongly prefer chords containing the melody note
      if (melDeg >= 0) {
        if (is_chord_tone(melDeg, t.target)) {
          w *= 10.0f;  // strong preference
        } else {
          w *= 0.1f;   // heavy penalty
        }
      }

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
