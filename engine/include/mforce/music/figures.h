#pragma once
#include "mforce/music/basics.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
};

// ---------------------------------------------------------------------------
// StepGenerator — generates random StepSequences.
// ---------------------------------------------------------------------------
struct StepGenerator {
  Randomizer rng;

  explicit StepGenerator(uint32_t seed = 0x57E9'0000u) : rng(seed) {}

  StepSequence random_sequence(int length, float skipProb = 0.3f) {
    StepSequence seq;
    for (int i = 0; i < length; ++i) {
      int step;
      if (rng.decide(skipProb)) {
        step = rng.decide(0.5f) ? 2 : -2;
      } else {
        step = rng.decide(0.5f) ? 1 : -1;
      }
      seq.add(step);
    }
    return seq;
  }

  StepSequence targeted_sequence(int length, int target) {
    StepSequence seq;
    int pos = 0;
    for (int i = 0; i < length; ++i) {
      int remaining = length - i;
      int distance = target - pos;

      if (remaining <= std::abs(distance)) {
        seq.add(distance > 0 ? 1 : -1);
      } else {
        int step = rng.decide(0.6f) ? (distance >= 0 ? 1 : -1) : (distance >= 0 ? -1 : 1);
        seq.add(step);
      }
      pos += seq.steps.back();
    }
    return seq;
  }
};

// ---------------------------------------------------------------------------
// FigureUnit — a single element within a MelodicFigure.
// Replaces parallel arrays of durations, steps, articulations, ornaments.
// ---------------------------------------------------------------------------
struct FigureUnit {
  float duration;  // beats
  int step{0};     // scale-degree movement from previous note (0 for first)
  Articulation articulation{Articulation::Default};
  Ornament ornament{Ornament::None};
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
// FigureConnector — how figures connect to each other.
// ---------------------------------------------------------------------------
enum class ConnectorType { Step, Pitch, EndPitch, Elide };

struct FigureConnector {
  ConnectorType type{ConnectorType::Step};
  int stepValue{0};     // for Step type
  Pitch pitch;          // for Pitch/EndPitch types

  static FigureConnector step(int v) { return {ConnectorType::Step, v}; }
  static FigureConnector elide() { return {ConnectorType::Elide}; }
};

// ---------------------------------------------------------------------------
// MelodicFigure — a melodic pattern built from FigureUnits.
// Constructed from StepSequence + PulseSequence (Composer's building blocks),
// stored as vector<FigureUnit> (Conductor's consumable form).
// ---------------------------------------------------------------------------
struct MelodicFigure {
  std::vector<FigureUnit> units;

  // Construct from Composer's building blocks
  MelodicFigure() = default;

  MelodicFigure(const PulseSequence& pulses, const StepSequence& steps) {
    if (steps.count() != pulses.count() - 1)
      throw std::runtime_error("MelodicFigure: step count must be pulse count - 1");

    for (int i = 0; i < pulses.count(); ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = (i == 0) ? 0 : steps.get(i - 1);
      units.push_back(u);
    }
  }

  int note_count() const { return int(units.size()); }

  void set_articulation(int index, Articulation art) { units[index].articulation = art; }
  void set_ornament(int index, Ornament orn) { units[index].ornament = orn; }

  float total_duration() const {
    float t = 0;
    for (auto& u : units) t += u.duration;
    return t;
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
// ChordFigure — instructions for playing a chord.
// ---------------------------------------------------------------------------
struct ChordFigure {
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
// FigureTemplate — length constraints for figure building.
// ---------------------------------------------------------------------------
struct FigureTemplate {
  int minLength{2};
  int maxLength{8};
};

// ---------------------------------------------------------------------------
// FigureBuilder — builds MelodicFigures from constraints.
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
  int maxNet{2};

  explicit FigureBuilder(uint32_t seed = 0xF1B0'0001u)
  : rng(seed), stepGen(seed + 1) {}

  MelodicFigure build(int noteCount) {
    PulseSequence pulses;
    for (int i = 0; i < noteCount; ++i) {
      float dur = rng.range(minPulse, maxPulse);
      dur = std::round(dur / minPulse) * minPulse; // quantize
      pulses.add(dur);
    }

    // Generate steps (one fewer than notes)
    auto rawSteps = stepGen.random_sequence(noteCount - 1);

    // Constrain steps to range
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

  // Vary a figure's durations slightly
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
};

} // namespace mforce
