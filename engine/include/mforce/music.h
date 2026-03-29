#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace mforce {

// ===== Enums =====

enum class Articulation {
  Default, Bow, Marcato, Sforzando, Staccato, Pizzicato,
  Pick, Pluck, Strum, Snap, HammerOn, PullOff,
  Harmonic, Mute, MuteHarmonic
};

enum class Ornament {
  None, MordentAbove, MordentBelow,
  TurnAB, TurnBA, TrillAbove, TrillBelow
};

enum class KeyType { Major, Minor };

enum class TimeUnit { Ticks, Beats, Bars, Seconds };

// ===== PitchDef =====

struct PitchDef {
  std::string shortName;
  std::string displayName;
  std::string longName;
  int offset; // semitones from C (C=0, C#=1, ..., B=11)

  static const PitchDef& get(const std::string& name);
  static const PitchDef& get(int offset);
  static const std::vector<PitchDef>& all();
};

// ===== Pitch =====

struct Pitch {
  const PitchDef* pitchDef;
  int octave;

  float note_number() const { return float(octave * 12 + pitchDef->offset); }

  std::string to_string() const {
    return pitchDef->displayName + std::to_string(octave);
  }

  static Pitch from_note_number(float nn);
  static Pitch from_name(const std::string& name, int octave);
  static Pitch relative(const Pitch& ref, float offset);
};

// ===== ScaleDef =====

struct ScaleDef {
  std::string name;
  std::vector<float> ascSteps;
  std::vector<float> descSteps;

  int length() const { return int(ascSteps.size()); }

  float span() const {
    float s = 0;
    for (float v : ascSteps) s += v;
    return s;
  }

  static const ScaleDef& get(const std::string& name);
  static const std::vector<ScaleDef>& all();
};

// ===== Scale =====

struct Scale {
  const PitchDef* pitchDef;
  const ScaleDef* scaleDef;

  int length() const { return scaleDef->length(); }
  int offset() const { return pitchDef->offset; }
  float ascending_step(int i) const { return scaleDef->ascSteps[i]; }
  float descending_step(int i) const { return scaleDef->descSteps[i]; }

  static Scale get(const std::string& pitchName, const std::string& scaleType);
};

// ===== Key =====

struct Key {
  const PitchDef* pitchDef;
  KeyType type;
  int sharps, flats;
  Scale scale;

  std::string to_string() const {
    return pitchDef->longName + " " + (type == KeyType::Major ? "Major" : "Minor");
  }

  static const Key& get(const std::string& name);
  static const std::vector<Key>& all();
};

// ===== Interval =====

struct Interval {
  std::string shortName;
  std::string longName;
  float semitones;

  static const Interval& get(const std::string& name);
};

// ===== EqualTemperament =====
// Already have note_to_freq() in equal_temperament.h, but adding the class form
// for compatibility: freq = 13.75 * 2^((noteNumber - 9) / 12)
// (Legacy uses A0 = 13.75 Hz as base, which is equivalent to A4 = 440 Hz)

// ===== Meter =====

struct Meter {
  int numerator;
  int denominator;

  int beats_per_bar() const { return numerator; }
  float beat_length() const { return 1.0f / float(denominator); }

  std::string label() const {
    return std::to_string(numerator) + "/" + std::to_string(denominator);
  }

  static const Meter M_4_4, M_3_4, M_6_8, M_7_8, M_5_4;
};

// ===== TimeValue =====

struct TimeValue {
  float value;
  TimeUnit unit;
};

// ===== MEvent =====

struct MEvent {
  virtual ~MEvent() = default;
  virtual float duration() const = 0;
};

// ===== MNote =====

struct MNote : MEvent {
  float noteNumber;
  float velocity;
  float dur;
  Articulation articulation{Articulation::Default};

  MNote(float nn, float vel, float d, Articulation art = Articulation::Default)
  : noteNumber(nn), velocity(vel), dur(d), articulation(art) {}

  float duration() const override { return dur; }
};

// ===== DrumHit =====

struct DrumHit : MEvent {
  int drumNumber;
  float velocity;
  float dur;

  DrumHit(int dn, float vel, float d) : drumNumber(dn), velocity(vel), dur(d) {}

  float duration() const override { return dur; }
};

// ===== SequencedEvent =====

struct SequencedEvent {
  std::unique_ptr<MEvent> event;
  double startTime;

  SequencedEvent(std::unique_ptr<MEvent> e, double t)
  : event(std::move(e)), startTime(t) {}
};

// ===== EventSequence =====

struct EventSequence {
  std::vector<SequencedEvent> events;
  double totalTime{0.0};

  void add_note(float noteNumber, float velocity, float duration, double startTime,
                Articulation art = Articulation::Default) {
    totalTime = std::max(totalTime, startTime + double(duration));
    events.emplace_back(
        std::make_unique<MNote>(noteNumber, velocity, duration, art),
        startTime);
  }

  void add_note_sequential(float noteNumber, float velocity, float duration,
                           Articulation art = Articulation::Default) {
    add_note(noteNumber, velocity, duration, totalTime, art);
  }

  void add_drum(int drumNumber, float velocity, float duration, double startTime) {
    totalTime = std::max(totalTime, startTime + double(duration));
    events.emplace_back(
        std::make_unique<DrumHit>(drumNumber, velocity, duration),
        startTime);
  }

  int count() const { return int(events.size()); }
};

// ===== SimpleNote (higher-level note with Pitch + TimeValue) =====

struct SimpleNote {
  Pitch pitch;
  TimeValue length;
  Articulation articulation{Articulation::Default};
  Ornament ornament{Ornament::None};
};

// ===== Beat (drum pattern) =====

struct Beat {
  std::string name;
  std::vector<float> kick;
  std::vector<float> snare;
  std::vector<float> hat;
};

} // namespace mforce
