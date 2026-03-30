#include "mforce/music/basics.h"
#include <stdexcept>

namespace mforce {

// ===== ChordDef static data =====

static std::vector<ChordDef> s_chordDefs = {
  {"",    "",    "Major",        {"1", "M3", "5"}},
  {"m",   "m",   "Minor",        {"1", "m3", "5"}},
  {"dim", "dim", "Diminished",   {"1", "m3", "-5"}},
  {"aug", "aug", "Augmented",    {"1", "M3", "+5"}},
  {"sus2","sus2","Suspended 2nd",{"1", "M2", "5"}},
  {"sus4","sus4","Suspended 4th",{"1", "4",  "5"}},
  {"7",   "7",   "Dominant 7th", {"1", "M3", "5", "m7"}},
  {"M7",  "M7",  "Major 7th",   {"1", "M3", "5", "M7"}},
  {"m7",  "m7",  "Minor 7th",   {"1", "m3", "5", "m7"}},
  {"mM7", "mM7", "Minor Major 7th", {"1", "m3", "5", "M7"}},
  {"dim7","dim7","Diminished 7th",   {"1", "m3", "-5", "M6"}},
  {"m7b5","m7b5","Half Diminished",  {"1", "m3", "-5", "m7"}},
  {"9",   "9",   "Dominant 9th", {"1", "M3", "5", "m7", "M9"}},
  {"M9",  "M9",  "Major 9th",   {"1", "M3", "5", "M7", "M9"}},
  {"m9",  "m9",  "Minor 9th",   {"1", "m3", "5", "m7", "M9"}},
  {"6",   "6",   "Major 6th",   {"1", "M3", "5", "M6"}},
  {"m6",  "m6",  "Minor 6th",   {"1", "m3", "5", "M6"}},
  {"add9","add9","Add 9",        {"1", "M3", "5", "M9"}},
};

const std::vector<ChordDef>& ChordDef::all() { return s_chordDefs; }

const ChordDef& ChordDef::get(const std::string& name) {
  for (auto& cd : s_chordDefs)
    if (cd.shortName == name || cd.name == name) return cd;
  throw std::runtime_error("Unknown ChordDef: " + name);
}

// ===== Chord =====

void Chord::init_pitches() {
  pitches.clear();
  float rootNN = root.note_number();

  for (auto& intName : def->intervals) {
    float semis = Interval::get(intName).semitones;
    pitches.push_back(Pitch::from_note_number(rootNN + semis));
  }

  // Apply inversion: move lowest N pitches up an octave
  for (int i = 0; i < inversion && i < int(pitches.size()); ++i) {
    float nn = pitches[i].note_number() + 12.0f;
    pitches[i] = Pitch::from_note_number(nn);
  }

  // Apply spread: space notes across octaves
  if (spread > 0) {
    for (int i = 1; i < int(pitches.size()); ++i) {
      float nn = pitches[i].note_number() + float(i / 2) * 12.0f * float(spread);
      pitches[i] = Pitch::from_note_number(nn);
    }
  }

  // Sort by pitch
  std::sort(pitches.begin(), pitches.end(),
            [](const Pitch& a, const Pitch& b) { return a.note_number() < b.note_number(); });
}

Chord Chord::create(const std::string& rootName, int octave,
                     const std::string& chordType, float duration,
                     int inversion, int spread) {
  Chord c;
  c.root = Pitch::from_name(rootName, octave);
  c.def = &ChordDef::get(chordType);
  c.inversion = inversion;
  c.spread = spread;
  c.dur = duration;
  c.init_pitches();
  return c;
}

} // namespace mforce
