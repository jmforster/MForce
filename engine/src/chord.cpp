#include "mforce/music/basics.h"
#include <stdexcept>

namespace mforce {

// ===== ChordDef static data (compact/default voicings) =====

static std::vector<ChordDef> s_chordDefs = {
  {"",    "",    "Major",        {"1", "M3", "5"}},
  {"m",   "m",   "Minor",        {"1", "m3", "5"}},
  {"dim", "dim", "Diminished",   {"1", "m3", "-5"}},
  {"+",   "+",   "Augmented",    {"1", "M3", "+5"}},
  {"sus2","sus2","Suspended 2nd",{"1", "M2", "5"}},
  {"sus4","sus4","Suspended 4th",{"1", "4",  "5"}},
  {"5",   "5",   "Power",        {"5", "8"}},
  {"7",   "7",   "Dominant 7th", {"1", "M3", "5", "m7"}},
  {"M7",  "M7",  "Major 7th",   {"1", "M3", "5", "M7"}},
  {"m7",  "m7",  "Minor 7th",   {"1", "m3", "5", "m7"}},
  {"mM7", "mM7", "Minor Major 7th", {"1", "m3", "5", "M7"}},
  {"dim7","dim7","Diminished 7th",   {"1", "m3", "-5", "M6"}},
  {"m7b5","m7b5","Half Diminished",  {"1", "m3", "-5", "m7"}},
  {"6",   "6",   "Major 6th",   {"1", "M3", "5", "M6"}},
  {"m6",  "m6",  "Minor 6th",   {"1", "m3", "5", "M6"}},
  {"9",   "9",   "Dominant 9th", {"1", "M3", "5", "m7", "M9"}},
  {"M9",  "M9",  "Major 9th",   {"1", "M3", "5", "M7", "M9"}},
  {"m9",  "m9",  "Minor 9th",   {"1", "m3", "5", "m7", "M9"}},
  {"69",  "69",  "Six/Nine",    {"1", "M3", "5", "M6", "M9"}},
  {"13",  "13",  "Thirteen",    {"1", "M3", "5", "m7", "M13"}},
  {"add9","add9","Add 9",        {"1", "M3", "5", "M9"}},
  {"+7",  "+7",  "Augmented 7th",{"1", "M3", "+5", "m7"}},
  {"+M7", "+M7", "Augmented Maj 7th",{"1", "M3", "+5", "M7"}},
  {"-7",  "dim7","Diminished 7th",{"1", "m3", "-5", "-7"}},
  {"h7",  "hd7", "Half-Diminished 7th",{"1", "m3", "-5", "m7"}},
  {"mu",  "mu",  "Mu Major",     {"1", "M3", "4"}},
  {"7#9", "7#9", "Seven Sharp Nine", {"1", "M3", "5", "m7", "+9"}},
  {"7b13","7b13","Seven Flat Thirteen",{"1", "M3", "5", "m7", "m13"}},
  {"7#5#9","7#5#9","Seven Sharp Five Sharp Nine",{"1", "M3", "+5", "m7", "+9"}},
};

const std::vector<ChordDef>& ChordDef::all() { return s_chordDefs; }

const ChordDef& ChordDef::get(const std::string& name) {
  // "M" is a common alias for Major (whose shortName is "")
  if (name == "M") return get("");
  for (auto& cd : s_chordDefs)
    if (cd.shortName == name || cd.name == name) return cd;
  throw std::runtime_error("Unknown ChordDef: " + name);
}

// ===== ChordDictionary =====

static std::unordered_map<std::string, ChordDictionary> s_dicts;
static bool s_dictsInit = false;

const ChordDef& ChordDictionary::get_chord_def(const std::string& n) const {
  auto it = chords.find(n);
  if (it == chords.end())
    throw std::runtime_error("ChordDictionary '" + name + "': unknown chord '" + n + "'");
  return it->second;
}

void ChordDictionary::init_all() {
  if (s_dictsInit) return;
  s_dictsInit = true;

  // Canonic dictionary — smallest-interval close-voicings for every quality.
  // Serves as the voice-leading reference form and the fallback source of
  // truth for chord qualities missing from instrument-specific dictionaries.
  // Registered under both "Canonic" (canonical name) and "Default" (legacy
  // back-compat alias).
  ChordDictionary canonic;
  canonic.shortName = "Canonic";
  canonic.name = "Canonical Chord Voicings";
  for (auto& cd : s_chordDefs) {
    std::string key = cd.shortName.empty() ? "M" : cd.shortName;
    canonic.chords[key] = cd;
  }
  canonic.chords["M"] = ChordDef::get("");
  s_dicts["Canonic"] = canonic;
  s_dicts["Default"] = std::move(canonic);

  // Guitar 6-string bar chords
  ChordDictionary g6;
  g6.shortName = "Guitar-Bar-6";
  g6.name = "Guitar - 6-note Bar Chords";
  g6.chords["M"]  = {"M",  "M",  "Major",       {"5", "8", "M10", "12", "15"}};
  g6.chords["m"]  = {"m",  "m",  "Minor",       {"5", "8", "m10", "12", "15"}};
  g6.chords["7"]  = {"7",  "7",  "Seven",       {"5", "m7", "M10", "12", "15"}};
  g6.chords["m7"] = {"m7", "m7", "Minor Seven", {"5", "m7", "m10", "12", "15"}};
  g6.chords["M7"]   = {"M7",   "M7",   "Major Seven",  {"5", "M7", "M10", "12", "15"}};
  g6.chords["7#9"]  = {"7#9",  "7#9",  "Seven #9",     {"5", "m7", "M10", "12", "+16"}};
  g6.chords["7b13"] = {"7b13", "7b13", "Seven b13",    {"5", "m7", "M10", "m13", "15"}};
  s_dicts["Guitar-Bar-6"] = std::move(g6);
  s_dicts["g6"] = s_dicts["Guitar-Bar-6"]; // alias

  // Guitar 5-string bar chords
  ChordDictionary g5;
  g5.shortName = "Guitar-Bar-5";
  g5.name = "Guitar - 5-note Bar Chords";
  g5.chords["M"]     = {"M",     "M",     "Major",       {"5", "8", "M10", "12"}};
  g5.chords["m"]     = {"m",     "m",     "Minor",       {"5", "8", "m10", "12"}};
  g5.chords["7"]     = {"7",     "7",     "Seven",       {"5", "m7", "M10", "12"}};
  g5.chords["m7"]    = {"m7",    "m7",    "Minor Seven", {"5", "m7", "m10", "12"}};
  g5.chords["M7"]    = {"M7",    "M7",    "Major Seven", {"5", "M7", "M10", "12"}};
  g5.chords["7b13"]  = {"7b13",  "7b13",  "Seven b13",   {"5", "m7", "M10", "m13"}};
  g5.chords["7#5#9"] = {"7#5#9", "7#5#9", "Seven #5#9",  {"M3", "m7", "+9", "+12"}};
  g5.chords["7#9"]   = {"7#9",   "7#9",   "Seven #9",    {"M3", "m7", "+9", "12"}};
  s_dicts["Guitar-Bar-5"] = std::move(g5);
  s_dicts["g5"] = s_dicts["Guitar-Bar-5"];

  // Guitar 4-string bar chords
  ChordDictionary g4;
  g4.shortName = "Guitar-Bar-4";
  g4.name = "Guitar - 4-note Bar Chords";
  g4.chords["M"]    = {"M",    "M",    "Major",       {"5", "8", "M10"}};
  g4.chords["m"]    = {"m",    "m",    "Minor",       {"5", "8", "m10"}};
  g4.chords["7"]    = {"7",    "7",    "Seven",       {"5", "m7", "M10"}};
  g4.chords["m7"]   = {"m7",   "m7",   "Minor Seven", {"5", "m7", "m10"}};
  g4.chords["M7"]   = {"M7",   "M7",   "Major Seven", {"5", "M7", "M10"}};
  g4.chords["7#9"]  = {"7#9",  "7#9",  "Seven #9",    {"M3", "m7", "+9"}};
  s_dicts["Guitar-Bar-4"] = std::move(g4);
  s_dicts["g4"] = s_dicts["Guitar-Bar-4"];

  // Guitar-Alt (special voicings including mu)
  ChordDictionary gAlt;
  gAlt.shortName = "Guitar-Alt";
  gAlt.name = "Guitar - Alternative Voicings";
  gAlt.chords["mu"] = {"mu", "", "Mu Major", {"4", "8", "M10"}};
  s_dicts["Guitar-Alt"] = std::move(gAlt);

  // Piano voicings (spread with 10ths)
  ChordDictionary piano;
  piano.shortName = "Piano";
  piano.name = "Piano";
  piano.chords["M"]  = {"M",  "",   "Major",       {"5", "8", "M10"}};
  piano.chords["m"]  = {"m",  "m",  "Minor",       {"5", "8", "m10"}};
  piano.chords["7"]  = {"7",  "7",  "Seven",       {"5", "m7", "M10"}};
  piano.chords["m7"] = {"m7", "m7", "Minor Seven", {"5", "m7", "m10"}};
  piano.chords["M7"] = {"M7", "M7", "Major Seven", {"5", "M7", "M10"}};
  s_dicts["Piano"] = std::move(piano);
}

const ChordDictionary& ChordDictionary::get(const std::string& name) {
  init_all();
  auto it = s_dicts.find(name);
  if (it == s_dicts.end())
    throw std::runtime_error("Unknown ChordDictionary: " + name);
  return it->second;
}

const ChordDictionary& ChordDictionary::canonic() {
  init_all();
  return s_dicts.at("Canonic");
}

// ===== Chord =====

void Chord::init_pitches() {
  pitches.clear();
  float rootNN = root.note_number();

  // Implicit root: every chord includes the root unless omitRoot is set.
  // Dictionary authors may specify "1" explicitly in the intervals list
  // (canonic dicts do, instrument dicts like Piano/Guitar typically don't);
  // either way the root appears exactly once.
  if (!def->omitRoot) {
    pitches.push_back(Pitch::from_note_number(rootNN));
  }
  for (auto& intName : def->intervals) {
    float semis = Interval::get(intName).semitones;
    if (semis < 0.01f) continue;  // skip explicit unison — root already added
    pitches.push_back(Pitch::from_note_number(rootNN + semis));
  }

  // Apply inversion: positive = move lowest N up an octave (classical
  // upward inversion). Negative = move highest |N| down an octave
  // (classical downward/drop inversion — puts a higher-degree voice in the
  // bass without transposing the whole chord up).
  if (inversion > 0) {
    for (int i = 0; i < inversion && i < int(pitches.size()); ++i) {
      float nn = pitches[i].note_number() + 12.0f;
      pitches[i] = Pitch::from_note_number(nn);
    }
  } else if (inversion < 0) {
    int dropN = -inversion;
    int total = int(pitches.size());
    for (int i = std::max(0, total - dropN); i < total; ++i) {
      float nn = pitches[i].note_number() - 12.0f;
      pitches[i] = Pitch::from_note_number(nn);
    }
  }

  // Apply spread
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

Chord Chord::create(const std::string& dictName, const std::string& rootName,
                     int octave, const std::string& chordType, float duration,
                     int inversion, int spread) {
  const auto& dict = ChordDictionary::get(dictName);
  Chord c;
  c.root = Pitch::from_name(rootName, octave);
  c.def = &dict.get_chord_def(chordType);
  c.inversion = inversion;
  c.spread = spread;
  c.dur = duration;
  c.init_pitches();
  return c;
}

// ===== ScaleChord =====

Chord ScaleChord::resolve(const Scale& scale, int octave, float duration,
                           int inversion, int spread) const {
  // Sum ascending steps for the degree to get semitone offset from root
  int semitones = 0;
  int scaleLen = scale.length();
  int deg = degree;

  // Handle degrees beyond one octave
  int extraOctaves = 0;
  while (deg >= scaleLen) { deg -= scaleLen; extraOctaves++; }
  while (deg < 0) { deg += scaleLen; extraOctaves--; }

  for (int i = 0; i < deg; ++i)
    semitones += int(scale.ascending_step(i));

  semitones += alteration;

  // Root note number = scale root + semitones + octave adjustment
  int rootNN = scale.offset() + semitones + (octave + extraOctaves) * 12;

  Chord c;
  c.root = Pitch::from_note_number(float(rootNN));
  c.def = quality ? quality : &ChordDef::get("M");
  c.inversion = inversion;
  c.spread = spread;
  c.dur = duration;
  c.init_pitches();
  return c;
}

} // namespace mforce
