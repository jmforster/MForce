#pragma once
#include "mforce/music/basics.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// PulseSequence — sequence of durations in beats.
// ---------------------------------------------------------------------------
struct PulseSequence {
  std::vector<float> pulses;  // durations in beats

  void add(float beats) { pulses.push_back(beats); }
  int count() const { return int(pulses.size()); }
  float get(int i) const { return pulses[i]; }

  float total_length() const {
    float t = 0;
    for (float p : pulses) t += p;
    return t;
  }

  // --- Transforms (all return new PulseSequence, source unchanged) ---

  PulseSequence retrograded() const {
    PulseSequence out;
    out.pulses.assign(pulses.rbegin(), pulses.rend());
    return out;
  }

  PulseSequence stretched(float factor) const {
    PulseSequence out;
    out.pulses.reserve(pulses.size());
    for (float p : pulses) out.pulses.push_back(p * factor);
    return out;
  }

  PulseSequence compressed(float factor) const {
    if (factor <= 0) return *this;
    return stretched(1.0f / factor);
  }
};

// ---------------------------------------------------------------------------
// StepSequence — sequence of melodic scale-degree steps.
// ---------------------------------------------------------------------------
struct StepSequence {
  std::vector<int> steps;

  void add(int step) { steps.push_back(step); }
  int count() const { return int(steps.size()); }
  int get(int i) const { return steps[i]; }

  int peak() const { int p = 0, c = 0; for (int s : steps) { c += s; p = std::max(p, c); } return p; }
  int floor() const { int f = 0, c = 0; for (int s : steps) { c += s; f = std::min(f, c); } return f; }
  int range() const { return peak() - floor(); }
  int net() const { int c = 0; for (int s : steps) c += s; return c; }

  // --- Transforms (all return new StepSequence, source unchanged) ---

  StepSequence inverted() const {
    StepSequence out;
    out.steps.reserve(steps.size());
    for (int s : steps) out.steps.push_back(-s);
    return out;
  }

  StepSequence retrograded() const {
    StepSequence out;
    out.steps.assign(steps.rbegin(), steps.rend());
    return out;
  }

  StepSequence expanded(float factor) const {
    StepSequence out;
    out.steps.reserve(steps.size());
    for (int s : steps) out.steps.push_back(int(std::round(s * factor)));
    return out;
  }

  StepSequence contracted(float factor) const {
    if (factor <= 0) return *this;
    return expanded(1.0f / factor);
  }
};

// ---------------------------------------------------------------------------
// StepGenerator — generates random StepSequences.
// ---------------------------------------------------------------------------
struct StepGenerator {
  Randomizer rng;

  explicit StepGenerator(uint32_t seed = 0x57E9'0000u) : rng(seed) {}

  // Melody-aware random sequence with:
  //  - Weighted interval distribution (steps dominant, skips rare, leaps rarer)
  //  - Gap-fill: stepwise recovery after skips (85%)
  //  - Range regression: pull toward center as we approach boundaries
  //  - Post-skip reversal scaling with leap size
  //  - Single climax: highest note blocked from reoccurrence
  //  - No repeated notes in mid-stepwise context
  //  - Consecutive same-direction leaps blocked (unless triadic)
  //  - Second-order context: previous step influences current
  StepSequence random_sequence(int length, float /*unused*/ = 0.3f) {
    StepSequence seq;

    // State
    int pos = 0;          // current position relative to start
    int highPos = 0;      // highest position reached
    int lowPos = 0;       // lowest position reached
    int prevStep = 0;     // previous step (for second-order context)
    bool prevWasSkip = false;  // was the last step a skip (|step| >= 2)?
    int prevSkipDir = 0;  // direction of last skip
    int dir = rng.decide(0.5f) ? 1 : -1;  // no neutral start
    int dirCount = 0;
    int stepCount = 0;    // consecutive stepwise moves

    // Range limit: ~10 degrees from start in either direction
    const int rangeLimit = 10;
    // Climax target position: roughly 60-75% through the sequence
    const int climaxZoneStart = length * 6 / 10;
    const int climaxZoneEnd = length * 8 / 10;
    bool climaxReached = false;

    for (int i = 0; i < length; ++i) {
      int step = 0;

      // --- Gap-fill rule (highest priority after a skip) ---
      if (prevWasSkip && rng.decide(0.85f)) {
        // Step back toward the gap, opposite direction of the skip
        step = (prevSkipDir > 0) ? -1 : 1;
        dir = step;
        prevWasSkip = false;
        stepCount = 1;
      } else {
        prevWasSkip = false;

        // --- Range regression: bias toward center near boundaries ---
        float rangePressure = 0.0f;
        if (std::abs(pos) > rangeLimit - 3) {
          rangePressure = float(std::abs(pos) - (rangeLimit - 3)) * 0.25f;
          if (pos > 0) rangePressure = -rangePressure;  // pull down
        }

        // --- Direction change probability ---
        // Base: momentum reversal (longer runs more likely to reverse)
        float reversalProb = dirCount * 0.12f + std::abs(rangePressure);
        if (rng.decide(std::min(reversalProb, 0.9f))) {
          dir = -dir;
          dirCount = 0;
        }

        // If range pressure is strong, force direction
        if (pos > rangeLimit) dir = -1;
        if (pos < -rangeLimit) dir = 1;

        // --- Choose interval size (weighted distribution) ---
        // Temperley: ~55% step, ~20% skip(3rd), ~12% skip(4th), ~8% leap, ~5% unison
        // But suppress unison in stepwise context
        float unisonProb = (stepCount > 0) ? 0.0f : 0.05f;  // no repeated notes mid-stepwise
        float stepProb = 0.58f + unisonProb * (-1.0f);  // rebalance
        float v = rng.value();

        if (v < unisonProb) {
          step = 0;
        } else if (v < unisonProb + stepProb) {
          step = dir;  // step of 1
        } else if (v < unisonProb + stepProb + 0.20f) {
          step = 2 * dir;  // skip of 3rd
        } else if (v < unisonProb + stepProb + 0.20f + 0.10f) {
          step = 3 * dir;  // skip of 4th
        } else {
          step = rng.int_range(4, 6) * dir;  // leap of 5th-7th
        }

        int absStep = std::abs(step);

        // --- Consecutive same-direction skip block ---
        // If previous was a skip in the same direction, force stepwise (unless triadic)
        if (absStep >= 2 && prevStep != 0 && std::abs(prevStep) >= 2) {
          bool sameDir = (step > 0 && prevStep > 0) || (step < 0 && prevStep < 0);
          if (sameDir) {
            // Allow triadic outlines: prev=2,curr=2 (1-3-5) or prev=2,curr=3 (1-3-6)
            bool triadic = (std::abs(prevStep) == 2 && absStep >= 2 && absStep <= 3);
            if (!triadic || !rng.decide(0.3f)) {
              // Force stepwise in opposite direction (gap-fill)
              step = (prevStep > 0) ? -1 : 1;
            }
          }
        }

        // --- Post-skip reversal (scaled with leap size) ---
        absStep = std::abs(step);
        if (absStep >= 2) {
          float revProb = std::min(0.5f + absStep * 0.1f, 0.95f);
          prevWasSkip = true;
          prevSkipDir = (step > 0) ? 1 : -1;

          if (rng.decide(revProb)) {
            dir = -prevSkipDir;
          }
          stepCount = 0;
        } else if (step != 0) {
          stepCount++;
        }

        // --- Climax enforcement ---
        int newPos = pos + step;
        if (climaxReached && newPos >= highPos) {
          // Already used the highest note — don't go there again
          // Pull back down
          step = -1;
          newPos = pos + step;
        }
        // Mark climax if we're in the zone and going higher
        if (!climaxReached && newPos > highPos && i >= climaxZoneStart && i <= climaxZoneEnd) {
          climaxReached = true;
        }
      }

      // --- Commit ---
      seq.add(step);
      prevStep = step;
      pos += step;
      if (pos > highPos) highPos = pos;
      if (pos < lowPos) lowPos = pos;
      dirCount++;
    }
    return seq;
  }

  // Target-directed sequence with gap-fill and melodic shaping.
  // Guarantees landing on exact target at the end.
  StepSequence targeted_sequence(int length, int target) {
    StepSequence seq;
    int pos = 0;
    int dir = (target == 0) ? 1 : (target > 0 ? 1 : -1);
    int prevStep = 0;
    bool prevWasSkip = false;
    int prevSkipDir = 0;

    for (int i = 0; i < length; ++i) {
      int remaining = length - i;
      int distance = target - pos;

      // Reverse if we've overshot
      if ((distance > 0 && dir < 0) || (distance < 0 && dir > 0))
        dir = -dir;

      int step;
      if (i == length - 1) {
        // Last step: force exact landing
        step = distance;
      } else if (std::abs(distance) > remaining) {
        // Must skip to reach target in time
        step = (std::abs(distance) > (remaining - 1) * 2) ? 3 * dir : 2 * dir;
      } else if (prevWasSkip && rng.decide(0.85f)) {
        // Gap-fill: step back toward the gap
        int fillDir = (prevSkipDir > 0) ? -1 : 1;
        // But don't move away from target if we're already behind
        if (std::abs(distance) >= remaining - 1) {
          step = dir;  // can't afford to regress
        } else {
          step = fillDir;
        }
        prevWasSkip = false;
      } else {
        prevWasSkip = false;

        // Regression probability: higher when we have slack
        float slack = 1.0f - float(std::abs(distance)) / float(remaining);
        float regProb = slack * 0.6f;

        if (rng.decide(regProb)) {
          // Regress (creates interest) — usually stepwise
          step = -dir;
        } else {
          // Advance toward target — choose interval size
          float v = rng.value();
          if (v < 0.60f) {
            step = dir;          // step
          } else if (v < 0.82f) {
            step = 2 * dir;      // skip of 3rd
          } else if (v < 0.93f) {
            step = 3 * dir;      // skip of 4th
          } else {
            step = rng.int_range(4, 5) * dir;  // leap
          }
          // Don't overshoot
          if (std::abs(pos + step - target) > std::abs(distance)) {
            step = dir;  // fall back to stepwise
          }
        }
      }

      // Track skip state for gap-fill
      if (std::abs(step) >= 2) {
        prevWasSkip = true;
        prevSkipDir = (step > 0) ? 1 : -1;
      }

      seq.add(step);
      prevStep = step;
      pos += step;
    }
    return seq;
  }

  // Stepwise only (no skips), with direction changes.
  // Anti-oscillation: won't produce -1/+1/-1/+1 patterns.
  StepSequence no_skip_sequence(int length) {
    StepSequence seq;
    int dir = rng.direction(0.5f, 0.5f);
    if (dir == 0) dir = 1;
    int dirCount = 0;
    for (int i = 0; i < length; ++i) {
      if (i > 2 && rng.decide(dirCount * 0.15f)) {
        // Prevent repeated 2-note pattern like -1/1/-1/1
        if (seq.get(i-3) != seq.get(i-1) || -dir != seq.get(i-2)) {
          dir = -dir;
          dirCount = 0;
        }
      }
      seq.add(dir);
      dirCount++;
    }
    return seq;
  }

  // Skip-based sequence for melodic outlines (skips of 2-7 scale degrees)
  StepSequence skip_sequence(int length, int startDegree) {
    StepSequence seq;
    int currDegree = startDegree;
    int dir = rng.decide(0.5f) ? 1 : -1;
    int dirCount = 0;

    for (int i = 0; i < length; ++i) {
      if (rng.decide(dirCount * 0.4f)) {
        dir = -dir;
        dirCount = 0;
      }

      int skip;
      if (dir == 1) {
        if (currDegree == 0) {
          skip = (i == 0)
            ? rng.select_int({2, 4, 7}, {0.5f, 0.35f, 0.15f})
            : rng.select_int({2, 4}, {0.65f, 0.35f});
        } else if (currDegree == 2) {
          skip = rng.select_int({2, 5}, {0.65f, 0.35f});
        } else {
          skip = rng.select_int({3, 5}, {0.65f, 0.35f});
        }
      } else {
        if (currDegree == 0) {
          skip = (i == 0)
            ? rng.select_int({-3, -5, -7}, {0.5f, 0.35f, 0.15f})
            : rng.select_int({-3, -5}, {0.65f, 0.35f});
        } else if (currDegree == 2) {
          skip = rng.select_int({-2, -5}, {0.65f, 0.35f});
        } else {
          skip = rng.select_int({-2, -4}, {0.65f, 0.35f});
        }
      }

      seq.add(skip);
      currDegree += skip;
      if (currDegree < 0) currDegree += 7;
      currDegree %= 7;
      dirCount++;
    }
    return seq;
  }
};

// ---------------------------------------------------------------------------
// PulseGenerator — generates random PulseSequences with standard musical
// durations. Parallel to StepGenerator for the rhythm dimension.
// ---------------------------------------------------------------------------
struct PulseGenerator {
  Randomizer rng;

  explicit PulseGenerator(uint32_t seed = 0x5057'0000u) : rng(seed) {}

  // Generate a PulseSequence of standard musical durations summing to
  // totalBeats, biased toward defaultPulse but with variety.
  // Includes both binary subdivisions and triplet groups.
  PulseSequence generate(float totalBeats, float defaultPulse = 1.0f) {
    PulseSequence ps;
    float remaining = totalBeats;

    // Binary durations (individual notes)
    static const float BINARY[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    // Triplet groups: each is {perNoteDuration, groupTotal}
    // Triplet sixteenths: 3 x 1/6 = 0.5 beats
    // Triplet eighths:    3 x 1/3 = 1.0 beat
    // Triplet quarters:   3 x 2/3 = 2.0 beats
    struct TripletGroup { float perNote; float total; };
    static const TripletGroup TRIPLETS[] = {
      {1.0f / 6.0f, 0.5f},
      {1.0f / 3.0f, 1.0f},
      {2.0f / 3.0f, 2.0f},
    };

    while (remaining > 0.001f) {
      // Collect binary candidates
      struct Candidate { float duration; int noteCount; bool isTriplet; float perNote; };
      std::vector<Candidate> candidates;

      for (float d : BINARY) {
        if (d <= remaining + 0.001f)
          candidates.push_back({d, 1, false, d});
      }

      // Collect triplet group candidates
      for (const auto& tg : TRIPLETS) {
        if (tg.total <= remaining + 0.001f)
          candidates.push_back({tg.total, 3, true, tg.perNote});
      }

      if (candidates.empty()) break;

      // Weight toward defaultPulse
      std::vector<float> weights;
      float weightSum = 0;
      for (auto& c : candidates) {
        // For triplets, compare the group total (not per-note) against defaultPulse
        float compareVal = c.isTriplet ? c.duration : c.duration;
        float w = 1.0f / (1.0f + std::abs(compareVal - defaultPulse) * 2.0f);
        // Slight penalty for triplets to keep them occasional, not dominant
        if (c.isTriplet) w *= 0.3f;
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

      auto& chosen = candidates[idx];
      if (chosen.isTriplet) {
        // Emit 3 notes
        for (int t = 0; t < 3; ++t) ps.add(chosen.perNote);
      } else {
        ps.add(chosen.duration);
      }
      remaining -= chosen.duration;
    }

    return ps;
  }
};

// ---------------------------------------------------------------------------
// FigureUnit — a single element within a MelodicFigure.
// Replaces parallel arrays of durations, steps, articulations, ornaments.
// ---------------------------------------------------------------------------
struct FigureUnit {
  float duration;  // beats
  int step{0};     // movement in scale degrees from previous note (0 for first)
  bool rest{false}; // true = silence (advance time, don't sound)
  int accidental{0}; // +1=sharp, -1=flat (transient pitch shift, doesn't affect cursor)
  Articulation articulation{Articulation::Default};
  Ornament ornament;
};

// ---------------------------------------------------------------------------
// PitchSelectionType — how to select notes from a chord.
// ---------------------------------------------------------------------------
enum class PitchSelectionType {
  Single, Multiple, SingleAlt, MultipleAlt,
  All, Low, High, LowHigh,
  AllExLow, AllExHigh, AllExLowHigh,
  Low2, High2, Even, Odd, LowHalf, HighHalf,
  AllUnplayed
};

// ---------------------------------------------------------------------------
// PitchSelection — selection criteria for chord pitches.
// ---------------------------------------------------------------------------
struct PitchSelection {
  PitchSelectionType type{PitchSelectionType::All};
  std::vector<int> indices;
  std::vector<float> alterations;
};

// ---------------------------------------------------------------------------
// Figure — base class for melodic/chord patterns built from FigureUnits.
// ---------------------------------------------------------------------------
struct Figure {
  std::vector<FigureUnit> units;
  virtual ~Figure() = default;

  int note_count() const { return int(units.size()); }
  void set_articulation(int index, Articulation art) { units[index].articulation = art; }
  void set_ornament(int index, Ornament orn) { units[index].ornament = orn; }

  float total_duration() const {
    float t = 0;
    for (auto& u : units) t += u.duration;
    return t;
  }

  // Net pitch movement (sum of all steps, including the first unit's step)
  int net_step() const {
    int n = 0;
    for (auto& u : units) n += u.step;
    return n;
  }

  PulseSequence extract_pulses() const {
    PulseSequence ps;
    for (const auto& u : units) ps.add(u.duration);
    return ps;
  }

  StepSequence extract_steps() const {
    StepSequence ss;
    for (const auto& u : units) ss.add(u.step);
    return ss;
  }

  virtual std::unique_ptr<Figure> clone() const = 0;
};

// ---------------------------------------------------------------------------
// MelodicFigure — a melodic pattern built from FigureUnits.
// Constructed from StepSequence + PulseSequence (Composer's building blocks),
// stored as vector<FigureUnit> (Conductor's consumable form).
// ---------------------------------------------------------------------------
struct MelodicFigure : Figure {
  MelodicFigure() = default;

  // N pulses, N steps — one-to-one. step[0] is the bridge from the previous
  // figure's last note (cursor model). Length = min(pulses, steps).
  MelodicFigure(const PulseSequence& pulses, const StepSequence& steps) {
    int n = std::min(pulses.count(), steps.count());
    for (int i = 0; i < n; ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = steps.get(i);
      units.push_back(u);
    }
  }

  std::unique_ptr<Figure> clone() const override {
    auto c = std::make_unique<MelodicFigure>();
    c->units = units;
    return c;
  }
};

// ---------------------------------------------------------------------------
// ChordFigure — a chord-tone movement pattern built from FigureUnits.
// Same structure as MelodicFigure but distinct type for chord parts.
// ---------------------------------------------------------------------------
struct ChordFigure : Figure {
  ChordFigure() = default;

  ChordFigure(const PulseSequence& pulses, const StepSequence& steps) {
    int n = std::min(pulses.count(), steps.count());
    for (int i = 0; i < n; ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = steps.get(i);
      units.push_back(u);
    }
  }

  std::unique_ptr<Figure> clone() const override {
    auto c = std::make_unique<ChordFigure>();
    c->units = units;
    return c;
  }
};

// ---------------------------------------------------------------------------
// RhythmicFigure — a rhythmic pattern: durations + velocities.
// ---------------------------------------------------------------------------
struct RhythmicFigure {
  std::vector<float> durations;
  std::vector<float> velocities;

  void add(float duration, float velocity = 1.0f) {
    durations.push_back(duration);
    velocities.push_back(velocity);
  }

  int count() const { return int(durations.size()); }
  float total_duration() const { float t = 0; for (float d : durations) t += d; return t; }
};

// ---------------------------------------------------------------------------
// FigureConnector — optional per-adjacency join between two figures in a
// phrase. Elides units from the end of the preceding figure and/or adjusts
// the duration of its (new) last unit.
//
// Positive adjustCount: extend the last unit (e.g. soak up a phrase tail).
// Negative adjustCount: shorten the last unit (e.g. make room for a
// following pickup figure). Clamped at 0 to avoid negative durations.
// ---------------------------------------------------------------------------
struct FigureConnector {
  int elideCount{0};     // notes removed from the END of the preceding figure
  float adjustCount{0};  // beats added to (+) or removed from (-) the
                         // preceding figure's last surviving unit
};

// ---------------------------------------------------------------------------
// ChordArticulation — instructions for playing a chord.
// ---------------------------------------------------------------------------
struct ChordArticulation {
  static constexpr int DIR_ASCENDING = 0;
  static constexpr int DIR_DESCENDING = 1;
  static constexpr int DIR_RANDOM = 2;

  struct Element {
    PitchSelection selection;
    float duration;  // beats
    int direction{DIR_ASCENDING};
    float delay{0.0f};
  };

  std::vector<Element> elements;

  void add(PitchSelectionType selType, float duration,
           int direction = DIR_ASCENDING, float delay = 0.0f) {
    Element e;
    e.selection.type = selType;
    e.duration = duration;
    e.direction = direction;
    e.delay = delay;
    elements.push_back(e);
  }

  void add(PitchSelectionType selType, std::vector<int> indices, float duration) {
    Element e;
    e.selection.type = selType;
    e.selection.indices = std::move(indices);
    e.duration = duration;
    elements.push_back(e);
  }

  int count() const { return int(elements.size()); }

  float total_time() const {
    float t = 0;
    for (auto& e : elements) t += e.duration;
    return t;
  }
};

// ---------------------------------------------------------------------------
// ChordProgression — harmonic content: ScaleChordSequence + PulseSequence.
// Parallels MelodicFigure (which pairs StepSequence + PulseSequence).
// Used as seed material for harmonic structure of a piece/section.
// ---------------------------------------------------------------------------
struct ChordProgression {
  PulseSequence pulses;            // duration of each chord in beats
  ScaleChordSequence chords;       // scale-relative chord identifiers

  void add(const ScaleChord& sc, float beats) {
    chords.add(sc);
    pulses.add(beats);
  }

  void add(int degree, const std::string& quality, float beats) {
    chords.add(degree, quality);
    pulses.add(beats);
  }

  int count() const { return chords.count(); }
  float total_duration() const { return pulses.total_length(); }

  // Resolve all chords to concrete Chord objects in a given key
  std::vector<Chord> resolve(const Scale& scale, int octave, int inversion = 0, int spread = 0) const {
    std::vector<Chord> result;
    for (int i = 0; i < chords.count(); ++i) {
      result.push_back(chords.get(i).resolve(scale, octave, pulses.get(i), inversion, spread));
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// DrumFigure — instructions for playing drums.
// ---------------------------------------------------------------------------
struct DrumFigure {
  struct Hit {
    int drumNumber;
    float time;      // beat offset
    float velocity;
    float duration;
  };

  std::vector<Hit> hits;
  float totalTime{0.0f};

  void add(int drumNum, float time, float velocity, float duration = 0.1f) {
    hits.push_back({drumNum, time, velocity, duration});
    totalTime = std::max(totalTime, time + duration);
  }

  int count() const { return int(hits.size()); }
};

// ---------------------------------------------------------------------------
// FigureBuilder — builds MelodicFigures from constraints.
// (FigureTemplate moved to templates.h)
// ---------------------------------------------------------------------------
struct FigureBuilder {
  Randomizer rng;
  StepGenerator stepGen;

  float defaultPulse{1.0f};
  float minPulse{0.5f};
  float maxPulse{2.0f};

  int maxPeak{4};
  int maxFloor{-4};
  int maxRange{8};
  int targetNet{0};  // constrained net pitch movement (0 = unconstrained)

  explicit FigureBuilder(uint32_t seed = 0xF1B0'0001u)
  : rng(seed), stepGen(seed + 1) {}

  // Build from note count with random pulses and steps
  MelodicFigure build(int noteCount) {
    PulseSequence pulses;
    for (int i = 0; i < noteCount; ++i) {
      float dur = rng.range(minPulse, maxPulse);
      dur = std::round(dur / minPulse) * minPulse;
      pulses.add(dur);
    }

    auto rawSteps = stepGen.random_sequence(noteCount);

    StepSequence steps;
    int pos = 0;
    for (int i = 0; i < rawSteps.count(); ++i) {
      int s = rawSteps.get(i);
      if (pos + s > maxPeak) s = -std::abs(s);
      if (pos + s < maxFloor) s = std::abs(s);
      pos += s;
      steps.add(s);
    }

    return MelodicFigure(pulses, steps);
  }

  // Build from step sequence with uniform pulse
  MelodicFigure build(const StepSequence& ss, float pulse) {
    PulseSequence pulses;
    for (int i = 0; i < ss.count(); ++i)
      pulses.add(pulse);
    return MelodicFigure(pulses, ss);
  }

  // Build from duration (total beats) with stepwise steps and optional variation
  MelodicFigure build_from_length(float lengthBeats, bool doVary = false) {
    int count = int(lengthBeats / defaultPulse);
    if (count < 2) count = 2;

    StepSequence steps = stepGen.no_skip_sequence(count - 1);

    PulseSequence pulses;
    for (int i = 0; i < count; ++i)
      pulses.add(defaultPulse);

    MelodicFigure fig(pulses, steps);
    if (doVary) fig = vary_rhythm(fig);
    return fig;
  }

  // Single-note figure (cadential note, held tone)
  MelodicFigure single_note(float durationBeats) {
    PulseSequence ps;
    ps.add(durationBeats);
    StepSequence ss;
    return MelodicFigure(ps, ss);
  }

  // Rhythmic variation: split or dot pulses
  MelodicFigure vary_rhythm(const MelodicFigure& source) {
    MelodicFigure fig = source;

    for (int x = 0; x < fig.note_count() - 1; ++x) {
      if (rng.decide(0.2f)) {
        // Split pulse into 2
        float dur = fig.units[x].duration;
        if (dur < minPulse * 2) continue;

        float dur1, dur2;
        if (dur < 1.0f || rng.decide(0.5f)) {
          dur1 = dur * 0.5f;
          dur2 = dur * 0.5f;
        } else {
          dur1 = dur * 0.75f;
          dur2 = dur * 0.25f;
        }

        fig.units[x].duration = dur1;
        FigureUnit newUnit;
        newUnit.duration = dur2;
        newUnit.step = 0;
        fig.units.insert(fig.units.begin() + x + 1, newUnit);
        break;
      } else if (x < fig.note_count() - 1 && rng.decide(0.3f)) {
        // Dot current, shorten next
        float dur = fig.units[x].duration;
        fig.units[x].duration = dur * 1.5f;
        fig.units[x + 1].duration = dur * 0.5f;
        break;
      }
    }

    return fig;
  }

  // Replicate: repeat a figure N times with a step offset between copies
  MelodicFigure replicate(const MelodicFigure& source, int count, int stepBetween) {
    PulseSequence pulses;
    StepSequence steps;

    for (int rep = 0; rep < count; ++rep) {
      for (int i = 0; i < source.note_count(); ++i) {
        pulses.add(source.units[i].duration);

        if (rep == 0 && i == 0) continue; // first note has no step

        if (i == 0) {
          // Connecting step between repetitions: offset minus net of source
          int net = 0;
          for (auto& u : source.units) net += u.step;
          steps.add(stepBetween - net);
        } else {
          steps.add(source.units[i].step);
        }
      }
    }

    return MelodicFigure(pulses, steps);
  }

  // Vary a figure's durations slightly (simple version)
  MelodicFigure vary(const MelodicFigure& source, float amount = 0.3f) {
    MelodicFigure fig = source;
    for (auto& u : fig.units) {
      if (rng.decide(amount)) {
        u.duration *= rng.range(0.75f, 1.25f);
        u.duration = std::max(minPulse, std::min(maxPulse, u.duration));
      }
    }
    return fig;
  }

  // =========================================================================
  // Shape-based figure builders
  // Each returns a MelodicFigure with a specific melodic contour.
  // =========================================================================

  // ScalarRun: consecutive steps in one direction
  //   direction: +1 = ascending, -1 = descending
  //   count: number of notes (including starting note)
  //   pulse: duration per note (0 = use defaultPulse)
  MelodicFigure scalar_run(int direction, int count, float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    if (count < 2) count = 2;
    int dir = (direction >= 0) ? 1 : -1;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});  // first note
    for (int i = 1; i < count; ++i)
      fig.units.push_back({pulse, dir});
    return fig;
  }

  // RepeatedNote: same pitch repeated N times
  //   count: number of repetitions
  //   pulse: duration per note
  MelodicFigure repeated_note(int count, float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    if (count < 1) count = 1;
    MelodicFigure fig;
    for (int i = 0; i < count; ++i)
      fig.units.push_back({pulse, 0});
    return fig;
  }

  // HeldNote: single note with specified duration
  //   duration: in beats
  MelodicFigure held_note(float duration) {
    MelodicFigure fig;
    fig.units.push_back({duration, 0});
    return fig;
  }

  // CadentialApproach: stepwise approach to a target, ending with a held note
  //   fromAbove: true = descend to target, false = ascend
  //   approachSteps: how many stepwise notes before arrival (1-4)
  //   arrivalDuration: how long the arrival note is held
  //   approachPulse: duration of each approach note
  MelodicFigure cadential_approach(bool fromAbove, int approachSteps,
                                    float arrivalDuration = 0, float approachPulse = 0) {
    if (approachPulse <= 0) approachPulse = defaultPulse;
    if (arrivalDuration <= 0) arrivalDuration = defaultPulse * 2;
    if (approachSteps < 1) approachSteps = 1;
    int dir = fromAbove ? -1 : 1;
    MelodicFigure fig;
    fig.units.push_back({approachPulse, 0});  // first approach note
    for (int i = 1; i < approachSteps; ++i)
      fig.units.push_back({approachPulse, dir});
    fig.units.push_back({arrivalDuration, dir});  // arrival
    return fig;
  }

  // TriadicOutline: outlines a chord (root-3rd-5th or inversions)
  //   direction: +1 = ascending, -1 = descending
  //   includeOctave: if true, continues to the octave (root-3-5-8)
  //   pulse: duration per note
  MelodicFigure triadic_outline(int direction, bool includeOctave = false,
                                 float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    int dir = (direction >= 0) ? 1 : -1;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});         // root
    fig.units.push_back({pulse, 2 * dir});   // 3rd (skip of a third)
    fig.units.push_back({pulse, 2 * dir});   // 5th (another third)
    if (includeOctave)
      fig.units.push_back({pulse, 3 * dir}); // octave (a fourth from 5th)
    return fig;
  }

  // NeighborTone: note, step to neighbor, return
  //   upper: true = upper neighbor (+1 then -1), false = lower (-1 then +1)
  //   pulse: duration per note
  //   doublePulseMain: if true, first and last notes are longer
  MelodicFigure neighbor_tone(bool upper, float pulse = 0, bool doublePulseMain = false) {
    if (pulse <= 0) pulse = defaultPulse;
    int dir = upper ? 1 : -1;
    float mainPulse = doublePulseMain ? pulse * 2.0f : pulse;
    MelodicFigure fig;
    fig.units.push_back({mainPulse, 0});     // main note
    fig.units.push_back({pulse, dir});        // neighbor
    fig.units.push_back({mainPulse, -dir});   // return
    return fig;
  }

  // LeapAndFill: large leap followed by stepwise return
  //   leapSize: interval in scale degrees (3-7)
  //   leapUp: true = leap up then fill down, false = opposite
  //   fillSteps: how many stepwise notes to fill (0 = fill completely)
  //   pulse: duration per note
  MelodicFigure leap_and_fill(int leapSize, bool leapUp, int fillSteps = 0,
                               float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    if (leapSize < 2) leapSize = 2;
    if (fillSteps <= 0) fillSteps = leapSize - 1;
    int leapDir = leapUp ? 1 : -1;
    int fillDir = -leapDir;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});                  // starting note
    fig.units.push_back({pulse, leapSize * leapDir});  // the leap
    for (int i = 0; i < fillSteps; ++i)
      fig.units.push_back({pulse, fillDir});           // stepwise fill
    return fig;
  }

  // ScalarReturn: go out stepwise, return (arch or inverted arch)
  //   direction: +1 = rise then fall, -1 = fall then rise
  //   extent: how many steps out before returning
  //   returnExtent: how many steps back (0 = same as extent, full return)
  //   pulse: duration per note
  MelodicFigure scalar_return(int direction, int extent, int returnExtent = 0,
                               float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    if (extent < 1) extent = 1;
    if (returnExtent <= 0) returnExtent = extent;
    int dir = (direction >= 0) ? 1 : -1;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});  // starting note
    // Outward
    for (int i = 0; i < extent; ++i)
      fig.units.push_back({pulse, dir});
    // Return
    for (int i = 0; i < returnExtent; ++i)
      fig.units.push_back({pulse, -dir});
    return fig;
  }

  // Anacrusis: short pickup notes (typically ascending) leading to a downbeat
  //   count: number of pickup notes
  //   direction: +1 = ascending pickups, -1 = descending
  //   pickupPulse: duration of each pickup note
  //   downbeatPulse: duration of the downbeat arrival
  MelodicFigure anacrusis(int count, int direction = 1,
                           float pickupPulse = 0, float downbeatPulse = 0) {
    if (pickupPulse <= 0) pickupPulse = defaultPulse * 0.5f;
    if (downbeatPulse <= 0) downbeatPulse = defaultPulse;
    if (count < 1) count = 1;
    int dir = (direction >= 0) ? 1 : -1;
    MelodicFigure fig;
    for (int i = 0; i < count; ++i)
      fig.units.push_back({pickupPulse, (i == 0) ? 0 : dir});
    fig.units.push_back({downbeatPulse, dir});  // downbeat arrival
    return fig;
  }

  // Zigzag: ascending via step-up, skip-down (or reverse)
  //   direction: +1 = net ascending, -1 = net descending
  //   cycles: number of zigzag cycles
  //   stepSize: size of the step (1)
  //   skipSize: size of the skip back (1-3)
  //   pulse: duration per note
  MelodicFigure zigzag(int direction, int cycles, int stepSize = 2,
                        int skipSize = 1, float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    if (cycles < 1) cycles = 1;
    int dir = (direction >= 0) ? 1 : -1;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});
    for (int i = 0; i < cycles; ++i) {
      fig.units.push_back({pulse, stepSize * dir});
      fig.units.push_back({pulse, -skipSize * dir});
    }
    return fig;
  }

  // Fanfare: leaps outlining 4th/5th/octave, optionally with repeated notes
  //   intervals: scale-degree leaps to make (e.g. {4, 3} for root→5th→octave)
  //   repeatsPerNote: how many times to repeat each arrival (1 = no repeat)
  //   pulse: duration per note
  MelodicFigure fanfare(const std::vector<int>& intervals, int repeatsPerNote = 1,
                          float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});  // starting note
    if (repeatsPerNote > 1)
      for (int r = 1; r < repeatsPerNote; ++r)
        fig.units.push_back({pulse, 0});
    for (int leap : intervals) {
      fig.units.push_back({pulse, leap});
      for (int r = 1; r < repeatsPerNote; ++r)
        fig.units.push_back({pulse, 0});
    }
    return fig;
  }

  // Sigh: descending step pair (the "Seufzer")
  //   chromatic: if true, sets accidental for half-step
  //   pulse: duration per note (typically longer, expressive)
  MelodicFigure sigh(float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});
    fig.units.push_back({pulse, -1});
    return fig;
  }

  // Suspension: held note resolving stepwise down
  //   holdDuration: how long the suspension is held
  //   resolutionPulse: duration of the resolution note
  MelodicFigure suspension(float holdDuration = 0, float resolutionPulse = 0) {
    if (holdDuration <= 0) holdDuration = defaultPulse * 2;
    if (resolutionPulse <= 0) resolutionPulse = defaultPulse;
    MelodicFigure fig;
    fig.units.push_back({holdDuration, 0});     // held note
    fig.units.push_back({resolutionPulse, -1}); // resolution down
    return fig;
  }

  // Cambiata: step one way, skip opposite, step — 4-note contrapuntal figure
  //   direction: +1 = step up first, -1 = step down first
  //   pulse: duration per note
  MelodicFigure cambiata(int direction = -1, float pulse = 0) {
    if (pulse <= 0) pulse = defaultPulse;
    int dir = (direction >= 0) ? 1 : -1;
    MelodicFigure fig;
    fig.units.push_back({pulse, 0});        // main note
    fig.units.push_back({pulse, dir});       // step
    fig.units.push_back({pulse, -2 * dir});  // skip opposite
    fig.units.push_back({pulse, dir});       // step to resolution
    return fig;
  }

  // =========================================================================
  // Transforms (existing)
  // =========================================================================

  // Invert: flip all step directions
  MelodicFigure invert(const MelodicFigure& source) {
    MelodicFigure fig = source;
    for (auto& u : fig.units) u.step = -u.step;
    return fig;
  }

  // Reverse: retrograde (reverse order of units)
  MelodicFigure reverse(const MelodicFigure& source) {
    MelodicFigure fig = source;
    std::reverse(fig.units.begin(), fig.units.end());
    // Fix steps: reversed steps need sign flip to maintain musical sense
    // (going up then down → going down then up)
    return fig;
  }

  // Stretch: multiply all durations by factor (default 2x)
  MelodicFigure stretch(const MelodicFigure& source, float factor = 2.0f) {
    MelodicFigure fig = source;
    for (auto& u : fig.units) u.duration *= factor;
    return fig;
  }

  // Compress: divide all durations by factor (default 2x)
  MelodicFigure compress(const MelodicFigure& source, float factor = 2.0f) {
    MelodicFigure fig = source;
    for (auto& u : fig.units) u.duration /= factor;
    return fig;
  }

  // Vary steps: randomly perturb some step values (guaranteed non-zero)
  MelodicFigure vary_steps(const MelodicFigure& source, int variations = 1) {
    MelodicFigure fig = source;
    for (int i = 0; i < variations && fig.note_count() > 1; ++i) {
      int idx = rng.int_range(1, fig.note_count() - 2);
      int delta = rng.int_range(-2, 2);
      if (delta == 0) delta = (rng.int_range(0, 1) == 0) ? -1 : 1;
      fig.units[idx].step += delta;
    }
    return fig;
  }
};

} // namespace mforce
