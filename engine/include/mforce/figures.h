#pragma once
#include "music.h"
#include "randomizer.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// PulseSequence — sequence of time values (durations).
// ---------------------------------------------------------------------------
struct PulseSequence {
  std::vector<TimeValue> pulses;

  void add(float value, TimeUnit unit = TimeUnit::Beats) {
    pulses.push_back({value, unit});
  }

  int count() const { return int(pulses.size()); }

  TimeValue get(int i) const { return pulses[i]; }

  float total_length() const {
    float t = 0;
    for (auto& p : pulses) t += p.value;
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
        // Skip: step by 2 or more
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
        // Must head toward target
        seq.add(distance > 0 ? 1 : -1);
      } else {
        // Random step with bias toward target
        int step = rng.decide(0.6f) ? (distance >= 0 ? 1 : -1) : (distance >= 0 ? -1 : 1);
        seq.add(step);
      }
      pos += seq.steps.back();
    }
    return seq;
  }
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
  Pitch pitch;          // for Pitch/EndPitch types (uses default-constructed Pitch)

  static FigureConnector step(int v) { return {ConnectorType::Step, v}; }
  static FigureConnector elide() { return {ConnectorType::Elide}; }
};

// ---------------------------------------------------------------------------
// MelodicFigure — a melodic pattern: pulse sequence + step sequence.
// Pulses define timing, steps define pitch movement.
// pulse count = step count + 1 (one more note than steps between notes).
// ---------------------------------------------------------------------------
struct MelodicFigure {
  PulseSequence pulses;
  StepSequence steps;
  std::vector<Articulation> articulations;
  std::vector<Ornament> ornaments;

  int note_count() const { return pulses.count(); }

  void add_note(float duration, TimeUnit unit = TimeUnit::Beats) {
    pulses.add(duration, unit);
  }

  void add_step(int step) {
    steps.add(step);
  }

  // Convenience: add a note with step (adds pulse + step together)
  void add_element(float duration, int step, TimeUnit unit = TimeUnit::Beats,
                   Articulation art = Articulation::Default, Ornament orn = Ornament::None) {
    pulses.add(duration, unit);
    if (pulses.count() > 1) // first note has no preceding step
      steps.add(step);
    articulations.push_back(art);
    ornaments.push_back(orn);
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
    TimeValue duration;
    int direction{DIR_ASCENDING};
    float delay{0.0f};
  };

  std::vector<Element> elements;

  void add(PitchSelectionType selType, float duration, TimeUnit unit = TimeUnit::Beats,
           int direction = DIR_ASCENDING, float delay = 0.0f) {
    Element e;
    e.selection.type = selType;
    e.duration = {duration, unit};
    e.direction = direction;
    e.delay = delay;
    elements.push_back(e);
  }

  void add(PitchSelectionType selType, std::vector<int> indices,
           float duration, TimeUnit unit = TimeUnit::Beats) {
    Element e;
    e.selection.type = selType;
    e.selection.indices = std::move(indices);
    e.duration = {duration, unit};
    elements.push_back(e);
  }

  int count() const { return int(elements.size()); }

  float total_time() const {
    float t = 0;
    for (auto& e : elements) t += e.duration.value;
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
  TimeUnit pulseUnit{TimeUnit::Beats};

  int maxPeak{4};
  int maxFloor{-4};
  int maxRange{8};
  int maxNet{2};

  explicit FigureBuilder(uint32_t seed = 0xF1B0'0001u)
  : rng(seed), stepGen(seed + 1) {}

  MelodicFigure build(int noteCount) {
    MelodicFigure fig;

    // Generate pulses
    for (int i = 0; i < noteCount; ++i) {
      float dur = rng.range(minPulse, maxPulse);
      dur = std::round(dur / minPulse) * minPulse; // quantize
      fig.pulses.add(dur, pulseUnit);
    }

    // Generate steps (one fewer than notes)
    auto steps = stepGen.random_sequence(noteCount - 1);

    // Constrain steps to range
    int pos = 0;
    for (int i = 0; i < steps.count(); ++i) {
      int s = steps.get(i);
      if (pos + s > maxPeak) s = -std::abs(s);
      if (pos + s < maxFloor) s = std::abs(s);
      pos += s;
      fig.steps.add(s);
    }

    return fig;
  }

  // Vary a figure's pulses slightly
  MelodicFigure vary(const MelodicFigure& source, float amount = 0.3f) {
    MelodicFigure fig = source;
    for (int i = 0; i < fig.pulses.count(); ++i) {
      if (rng.decide(amount)) {
        float v = fig.pulses.pulses[i].value;
        v *= rng.range(0.75f, 1.25f);
        v = std::max(minPulse, std::min(maxPulse, v));
        fig.pulses.pulses[i].value = v;
      }
    }
    return fig;
  }
};

} // namespace mforce
