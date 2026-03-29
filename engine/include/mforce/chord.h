#pragma once
#include "music.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// ChordDef — chord structure defined by intervals from root.
// ---------------------------------------------------------------------------
struct ChordDef {
  std::string shortName;
  std::string displayName;
  std::string name;
  std::vector<std::string> intervals; // Interval short names
  bool omitRoot{false};

  static const ChordDef& get(const std::string& name);
  static const std::vector<ChordDef>& all();
};

// ---------------------------------------------------------------------------
// Chord — a specific chord: root pitch + definition + voicing.
// ---------------------------------------------------------------------------
struct Chord {
  Pitch root;
  const ChordDef* def;
  int inversion{0};
  int spread{0};
  float dur{1.0f};

  std::vector<Pitch> pitches;

  // Build pitches from root + intervals + inversion
  void init_pitches();

  float duration() const { return dur; }
  int pitch_count() const { return int(pitches.size()); }

  static Chord create(const std::string& rootName, int octave,
                       const std::string& chordType, float duration = 1.0f,
                       int inversion = 0, int spread = 0);
};

} // namespace mforce
