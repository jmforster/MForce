#include "mforce/music/basics.h"
#include <stdexcept>

namespace mforce {

// ===== PitchDef static data =====

static std::vector<PitchDef> s_pitchDefs = {
  {"C",  "C",  "C",  0}, {"C#", "C#", "C Sharp", 1}, {"D",  "D",  "D",  2},
  {"D#", "D#", "D Sharp", 3}, {"E",  "E",  "E",  4}, {"F",  "F",  "F",  5},
  {"F#", "F#", "F Sharp", 6}, {"G",  "G",  "G",  7}, {"G#", "G#", "G Sharp", 8},
  {"A",  "A",  "A",  9}, {"A#", "A#", "A Sharp",10}, {"B",  "B",  "B", 11},
  // Flats
  {"Db", "Db", "D Flat", 1}, {"Eb", "Eb", "E Flat", 3}, {"Gb", "Gb", "G Flat", 6},
  {"Ab", "Ab", "A Flat", 8}, {"Bb", "Bb", "B Flat",10},
  {"Cb", "Cb", "C Flat",11}, {"Fb", "Fb", "F Flat", 4},
};

const std::vector<PitchDef>& PitchDef::all() { return s_pitchDefs; }

const PitchDef& PitchDef::get(const std::string& name) {
  for (auto& pd : s_pitchDefs)
    if (pd.shortName == name || pd.longName == name) return pd;
  throw std::runtime_error("Unknown PitchDef: " + name);
}

const PitchDef& PitchDef::get(int offset) {
  for (auto& pd : s_pitchDefs)
    if (pd.offset == offset) return pd;
  throw std::runtime_error("Unknown PitchDef offset: " + std::to_string(offset));
}

// ===== Pitch =====

Pitch Pitch::from_note_number(float nn) {
  int n = int(nn);
  return {&PitchDef::get(n % 12), n / 12};
}

Pitch Pitch::from_name(const std::string& name, int octave) {
  return {&PitchDef::get(name), octave};
}

Pitch Pitch::relative(const Pitch& ref, float offset) {
  return from_note_number(ref.note_number() + offset);
}

// ===== ScaleDef static data =====

static std::vector<ScaleDef> s_scaleDefs = {
  {"Major",       {2,2,1,2,2,2,1}, {2,2,1,2,2,2,1}},
  {"Dorian",      {2,1,2,2,2,1,2}, {2,1,2,2,2,1,2}},
  {"Phrygian",    {1,2,2,2,1,2,2}, {1,2,2,2,1,2,2}},
  {"Lydian",      {2,2,2,1,2,2,1}, {2,2,2,1,2,2,1}},
  {"Mixolydian",  {2,2,1,2,2,1,2}, {2,2,1,2,2,1,2}},
  {"Minor",       {2,1,2,2,1,2,2}, {2,1,2,2,1,2,2}},
  {"Locrian",     {1,2,2,1,2,2,2}, {1,2,2,1,2,2,2}},
  {"Minor Pentatonic", {3,2,2,3,2}, {3,2,2,3,2}},
  {"Major Pentatonic", {2,2,3,2,3}, {2,2,3,2,3}},
  {"Minor Blues",      {3,2,1,1,3,2}, {3,2,1,1,3,2}},
  {"Major Blues",      {2,1,1,3,2,3}, {2,1,1,3,2,3}},
  {"Whole Tone",       {2,2,2,2,2}, {2,2,2,2,2}},
  {"Diminished",       {2,1,2,1,2,1,2,1}, {2,1,2,1,2,1,2,1}},
  {"Hungarian",        {2,1,3,1,1,3,1}, {2,1,3,1,1,3,1}},
  {"Japanese",         {1,4,2,1,4}, {1,4,2,1,4}},
  {"Phrygian Dominant",{1,3,1,2,1,2,2}, {1,3,1,2,1,2,2}},
  {"Roma",             {1,3,1,2,1,3,1}, {1,3,1,2,1,3,1}},
  {"Persian",          {1,3,1,1,2,3,1}, {1,3,1,1,2,3,1}},
  {"Harmonic Minor",   {2,1,2,2,1,3,1}, {2,1,2,2,1,2,2}},
  {"Melodic Minor",    {2,1,2,2,2,2,1}, {2,1,2,2,1,2,2}},
  {"Enigmatic",        {1,3,2,2,2,1,1}, {1,3,1,3,1,2,2}},
};

const std::vector<ScaleDef>& ScaleDef::all() { return s_scaleDefs; }

const ScaleDef& ScaleDef::get(const std::string& name) {
  for (auto& sd : s_scaleDefs)
    if (sd.name == name) return sd;
  throw std::runtime_error("Unknown ScaleDef: " + name);
}

// ===== Scale =====

Scale Scale::get(const std::string& pitchName, const std::string& scaleType) {
  return {&PitchDef::get(pitchName), &ScaleDef::get(scaleType)};
}

// ===== Key static data =====

static std::vector<Key> s_keys;
static bool s_keysInit = false;

static void init_keys() {
  if (s_keysInit) return;
  s_keysInit = true;

  auto mk = [](const char* pd, KeyType t, int s, int f) {
    Scale sc = Scale::get(pd, t == KeyType::Major ? "Major" : "Minor");
    s_keys.push_back({&PitchDef::get(pd), t, s, f, sc});
  };

  // Major keys
  mk("C",  KeyType::Major, 0, 0); mk("G",  KeyType::Major, 1, 0);
  mk("D",  KeyType::Major, 2, 0); mk("A",  KeyType::Major, 3, 0);
  mk("E",  KeyType::Major, 4, 0); mk("B",  KeyType::Major, 5, 0);
  mk("F#", KeyType::Major, 6, 0); mk("C#", KeyType::Major, 7, 0);
  mk("F",  KeyType::Major, 0, 1); mk("Bb", KeyType::Major, 0, 2);
  mk("Eb", KeyType::Major, 0, 3); mk("Ab", KeyType::Major, 0, 4);
  mk("Db", KeyType::Major, 0, 5); mk("Gb", KeyType::Major, 0, 6);

  // Minor keys
  mk("A",  KeyType::Minor, 0, 0); mk("E",  KeyType::Minor, 1, 0);
  mk("B",  KeyType::Minor, 2, 0); mk("F#", KeyType::Minor, 3, 0);
  mk("C#", KeyType::Minor, 4, 0); mk("G#", KeyType::Minor, 5, 0);
  mk("D",  KeyType::Minor, 0, 1); mk("G",  KeyType::Minor, 0, 2);
  mk("C",  KeyType::Minor, 0, 3); mk("F",  KeyType::Minor, 0, 4);
  mk("Bb", KeyType::Minor, 0, 5); mk("Eb", KeyType::Minor, 0, 6);
}

const std::vector<Key>& Key::all() { init_keys(); return s_keys; }

const Key& Key::get(const std::string& name) {
  init_keys();
  for (auto& k : s_keys)
    if (k.to_string() == name) return k;
  throw std::runtime_error("Unknown Key: " + name);
}

// ===== Interval static data =====

static std::unordered_map<std::string, Interval> s_intervals = {
  {"1",   {"1",   "Unison",           0}},
  {"m2",  {"m2",  "Minor 2nd",        1}},  {"M2",  {"M2",  "Major 2nd",      2}},
  {"m3",  {"m3",  "Minor 3rd",        3}},  {"M3",  {"M3",  "Major 3rd",      4}},
  {"4",   {"4",   "Perfect 4th",      5}},  {"-5",  {"-5",  "Diminished 5th", 6}},
  {"5",   {"5",   "Perfect 5th",      7}},  {"+5",  {"+5",  "Augmented 5th",  8}},
  {"m6",  {"m6",  "Minor 6th",        8}},  {"M6",  {"M6",  "Major 6th",      9}},
  {"m7",  {"m7",  "Minor 7th",       10}},  {"M7",  {"M7",  "Major 7th",     11}},
  {"8",   {"8",   "Octave",          12}},
  {"m9",  {"m9",  "Minor 9th",       13}},  {"M9",  {"M9",  "Major 9th",     14}},
  {"+9",  {"+9",  "Augmented 9th",   15}},  {"11",  {"11",  "Perfect 11th",  17}},
  {"+11", {"+11", "Augmented 11th",  18}},  {"M13", {"M13", "Major 13th",    21}},
};

const Interval& Interval::get(const std::string& name) {
  auto it = s_intervals.find(name);
  if (it == s_intervals.end())
    throw std::runtime_error("Unknown Interval: " + name);
  return it->second;
}

// ===== Meter static instances =====

const Meter Meter::M_4_4{4, 4};
const Meter Meter::M_3_4{3, 4};
const Meter Meter::M_6_8{6, 8};
const Meter Meter::M_7_8{7, 8};
const Meter Meter::M_5_4{5, 4};

} // namespace mforce
