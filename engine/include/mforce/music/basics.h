#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <variant>

namespace mforce {

// ===== Enums =====

enum class Articulation {
  Default, Bow, Marcato, Sforzando, Staccato, Pizzicato,
  Pick, Pluck, Strum, Snap, HammerOn, PullOff,
  Harmonic, Mute, MuteHarmonic
};

// ===== Ornaments (variant of value types) =====

struct Mordent {
  int direction{1};   // +1 = above, -1 = below
  int semitones{2};   // interval in semitones (resolved from scale context)
  std::vector<Articulation> articulations;  // per sub-note (e.g. [HammerOn, PullOff])
};

struct Trill {
  int direction{1};   // +1 = above, -1 = below
  int semitones{2};   // interval in semitones
  std::vector<Articulation> articulations;
};

struct Turn {
  int direction{1};        // +1 = above-first, -1 = below-first
  int semitonesAbove{2};   // interval above the main note
  int semitonesBelow{2};   // interval below the main note
  std::vector<Articulation> articulations;
};

using Ornament = std::variant<std::monostate, Mordent, Trill, Turn>;

inline bool has_ornament(const Ornament& o) {
  return !std::holds_alternative<std::monostate>(o);
}

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

  // Semitone distance between two adjacent scale degrees (ascending).
  // degree is 0-based, wraps at scale length.
  float semitones_between(int degree1, int degree2) const {
    float semis = 0;
    int len = length();
    int d = degree1;
    if (degree2 > degree1) {
      for (int i = degree1; i < degree2; ++i)
        semis += scaleDef->ascSteps[i % len];
    } else {
      for (int i = degree2; i < degree1; ++i)
        semis += scaleDef->ascSteps[i % len];
    }
    return semis;
  }

  // Is there room for a chromatic passing tone between two adjacent degrees?
  // True when the interval is a whole step (2 semitones) or larger.
  bool has_passing_tone(int degree) const {
    int d = ((degree % length()) + length()) % length();
    return scaleDef->ascSteps[d] > 1.5f;  // > 1 semitone (use 1.5 for float safety)
  }

  // Get the chromatic passing tone offset (in semitones from degree's pitch).
  // When ascending: returns +1 (sharp the source note, e.g. F→F#→G)
  // When descending: returns the interval minus 1 from the upper note
  //   (i.e., flat the target, e.g. G→Gb→F... or equivalently step-1 from above)
  //
  // For non-adjacent degrees (skip), returns the diatonic fill:
  // the number of scale degrees down from degree2 to find the nearest passing tone.
  // (Caller should use this with PitchReader to get the actual pitch.)
  struct PassingTone {
    bool exists;
    bool chromatic;      // true = chromatic inflection, false = diatonic fill
    int semitonesUp;     // semitones above degree1's pitch (ascending case)
    int fillDegreesFromTarget;  // for diatonic fill: degrees below degree2
  };

  PassingTone get_passing_tone(int degree1, int degree2) const {
    int stepDiff = degree2 - degree1;
    int absStepDiff = stepDiff > 0 ? stepDiff : -stepDiff;

    if (absStepDiff == 0) return {false, false, 0, 0};

    if (absStepDiff > 1) {
      // Non-adjacent: diatonic fill — closest scale tone to target
      return {true, false, 0, stepDiff > 0 ? 1 : -1};
    }

    // Adjacent scale degrees — check semitone distance
    int lo = ((degree1 < degree2 ? degree1 : degree2) % length() + length()) % length();
    float semis = scaleDef->ascSteps[lo];

    if (semis <= 1.5f) {
      // Half step — no room for a passing tone
      return {false, false, 0, 0};
    }

    // Whole step or larger — chromatic passing tone
    if (stepDiff > 0) {
      // Ascending: sharp the source (1 semitone above degree1)
      return {true, true, 1, 0};
    } else {
      // Descending: flat the source (1 semitone below degree1)
      return {true, true, -1, 0};
    }
  }

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
  Ornament ornament;
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
  std::optional<std::string> figureName;  // performance hint for ChordPerformer

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

// ===== ScaleChord — a chord defined relative to a scale =====
// degree 0=i, 1=ii, 2=iii, etc. Alteration: -1=flat, 0=natural, +1=sharp
// e.g. bIII-M7 = degree 2, alteration -1, quality M7

struct ScaleChord {
  int degree{0};
  int alteration{0};
  const ChordDef* quality{nullptr};

  // Resolve to a concrete Chord given a scale and octave
  Chord resolve(const Scale& scale, int octave, float duration = 1.0f,
                int inversion = 0, int spread = 0) const;
};

// ===== ScaleChordSequence — ordered collection of ScaleChords =====

struct ScaleChordSequence {
  std::vector<ScaleChord> chords;

  void add(const ScaleChord& sc) { chords.push_back(sc); }
  void add(int degree, int alteration, const ChordDef* quality) {
    chords.push_back({degree, alteration, quality});
  }
  void add(int degree, const std::string& qualityName) {
    chords.push_back({degree, 0, &ChordDef::get(qualityName)});
  }
  int count() const { return int(chords.size()); }
  const ScaleChord& get(int i) const { return chords[i]; }
};

} // namespace mforce
