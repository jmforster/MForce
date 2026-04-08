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
    int dir = (target == 0) ? 1 : (target > 0 ? 1 : -1);

    for (int i = 0; i < length; ++i) {
      int remaining = length - i;
      int distance = target - pos;

      // Reverse if we've overshot
      if ((target < 0 && pos < target) || (target > 0 && pos > target))
        dir = -dir;

      int step;
      if (i == length - 1) {
        // Last step: force exact landing
        step = target - pos;
      } else if (std::abs(distance) > remaining) {
        // Need to skip to get there
        step = (std::abs(distance) > (remaining - 1) * 2) ? 3 * dir : 2 * dir;
      } else {
        // Random walk with bias toward target
        float regProb = (1.0f - float(std::abs(distance)) / float(remaining)) * 0.8f;
        if (rng.decide(regProb)) {
          step = -dir; // regress (creates melodic interest)
        } else {
          step = dir;
        }
      }

      seq.add(step);
      pos += step;
    }
    return seq;
  }

  // Stepwise only (no skips), with direction changes
  StepSequence no_skip_sequence(int length) {
    StepSequence seq;
    int dir = rng.decide(0.5f) ? 1 : -1;
    int dirCount = 0;
    for (int i = 0; i < length; ++i) {
      if (i > 2 && rng.decide(dirCount * 0.15f)) {
        // Prevent 2-note oscillation
        if (i >= 3 && seq.get(i-3) != seq.get(i-1)) {
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

  // Net pitch movement (sum of all steps)
  int net_step() const {
    int n = 0;
    for (auto& u : units) n += u.step;
    return n;
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

    auto rawSteps = stepGen.random_sequence(noteCount - 1);

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
    for (int i = 0; i < ss.count() + 1; ++i)
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

  // Augment: double all durations
  MelodicFigure augment(const MelodicFigure& source) {
    MelodicFigure fig = source;
    for (auto& u : fig.units) u.duration *= 2.0f;
    return fig;
  }

  // Diminute: halve all durations
  MelodicFigure diminute(const MelodicFigure& source) {
    MelodicFigure fig = source;
    for (auto& u : fig.units) u.duration *= 0.5f;
    return fig;
  }

  // Vary steps: randomly perturb some step values
  MelodicFigure vary_steps(const MelodicFigure& source, int variations = 1) {
    MelodicFigure fig = source;
    for (int i = 0; i < variations && fig.note_count() > 1; ++i) {
      int idx = rng.int_range(0, fig.note_count() - 2);
      fig.units[idx].step += rng.int_range(-2, 2);
    }
    return fig;
  }
};

} // namespace mforce
