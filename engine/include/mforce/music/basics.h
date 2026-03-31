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

// ===== SimpleNote (higher-level note with Pitch + duration) =====

struct SimpleNote {
  Pitch pitch;
  float duration;  // beats
  Articulation articulation{Articulation::Default};
  Ornament ornament{Ornament::None};
};

// ===== Tone — render-ready note (output of Conductor) =====

struct Tone {
  float noteNumber;
  float velocity;
  float duration;  // seconds
  Articulation articulation{Articulation::Default};

  Tone(float nn, float vel, float dur, Articulation art = Articulation::Default)
    : noteNumber(nn), velocity(vel), duration(dur), articulation(art) {}
};

// ===== DrumHit — render-ready drum event (output of Conductor) =====

struct DrumHit {
  int drumNumber;
  float velocity;
  float duration;  // seconds

  DrumHit(int dn, float vel, float dur) : drumNumber(dn), velocity(vel), duration(dur) {}
};

// ===== Beat (drum pattern) =====

struct Beat {
  std::string name;
  std::vector<float> kick;
  std::vector<float> snare;
  std::vector<float> hat;
};

// ===== ChordDef — chord structure defined by intervals =====

struct ChordDef {
  std::string shortName;
  std::string displayName;
  std::string name;
  std::vector<std::string> intervals; // Interval short names
  bool omitRoot{false};

  static const ChordDef& get(const std::string& name);
  static const std::vector<ChordDef>& all();
};

// ===== Chord — root pitch + definition + voicing =====

struct Chord {
  Pitch root;
  const ChordDef* def;
  int inversion{0};
  int spread{0};
  float dur{1.0f};

  std::vector<Pitch> pitches;

  void init_pitches();

  float duration() const { return dur; }
  int pitch_count() const { return int(pitches.size()); }

  static Chord create(const std::string& rootName, int octave,
                       const std::string& chordType, float duration = 1.0f,
                       int inversion = 0, int spread = 0);

  // Create using a specific dictionary for voicing
  static Chord create(const std::string& dictName, const std::string& rootName,
                       int octave, const std::string& chordType, float duration = 1.0f,
                       int inversion = 0, int spread = 0);
};

// ===== ChordDictionary — named collection of ChordDefs for voicing =====

struct ChordDictionary {
  std::string shortName;
  std::string name;
  std::unordered_map<std::string, ChordDef> chords;

  const ChordDef& get_chord_def(const std::string& name) const;

  static const ChordDictionary& get(const std::string& name);
  static void init_all();
};

} // namespace mforce
