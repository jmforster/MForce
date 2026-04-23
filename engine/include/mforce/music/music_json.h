#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

// JSON serialization for the music data model.
// All to_json/from_json functions in the mforce namespace for ADL.

namespace mforce {

using json = nlohmann::json;

// ===========================================================================
// Enum helpers
// ===========================================================================

inline void to_json(json& j, const Articulation& a) {
  std::visit([&j](const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr      (std::is_same_v<T, articulations::Default>)      j = json{{"type", "Default"}};
    else if constexpr (std::is_same_v<T, articulations::Bow>)          j = json{{"type", "Bow"}};
    else if constexpr (std::is_same_v<T, articulations::Marcato>)      j = json{{"type", "Marcato"}};
    else if constexpr (std::is_same_v<T, articulations::Sforzando>)    j = json{{"type", "Sforzando"}};
    else if constexpr (std::is_same_v<T, articulations::Staccato>)     j = json{{"type", "Staccato"}};
    else if constexpr (std::is_same_v<T, articulations::Pizzicato>)    j = json{{"type", "Pizzicato"}};
    else if constexpr (std::is_same_v<T, articulations::Pick>)         j = json{{"type", "Pick"}};
    else if constexpr (std::is_same_v<T, articulations::Pluck>)        j = json{{"type", "Pluck"}};
    else if constexpr (std::is_same_v<T, articulations::Strum>)        j = json{{"type", "Strum"}};
    else if constexpr (std::is_same_v<T, articulations::Snap>)         j = json{{"type", "Snap"}};
    else if constexpr (std::is_same_v<T, articulations::HammerOn>)     j = json{{"type", "HammerOn"}};
    else if constexpr (std::is_same_v<T, articulations::PullOff>)      j = json{{"type", "PullOff"}};
    else if constexpr (std::is_same_v<T, articulations::Harmonic>)     j = json{{"type", "Harmonic"}};
    else if constexpr (std::is_same_v<T, articulations::Mute>)         j = json{{"type", "Mute"}};
    else if constexpr (std::is_same_v<T, articulations::MuteHarmonic>) j = json{{"type", "MuteHarmonic"}};
    else if constexpr (std::is_same_v<T, articulations::Bend>) {
      j = json{{"type", "Bend"}, {"direction", v.direction}, {"semitones", v.semitones}};
    }
    else if constexpr (std::is_same_v<T, articulations::Slide>) {
      j = json{{"type", "Slide"}, {"speed", v.speed}};
    }
  }, a);
}

inline void from_json(const json& j, Articulation& a) {
  std::string type = j.at("type").get<std::string>();
  if      (type == "Default")      a = articulations::Default{};
  else if (type == "Bow")          a = articulations::Bow{};
  else if (type == "Marcato")      a = articulations::Marcato{};
  else if (type == "Sforzando")    a = articulations::Sforzando{};
  else if (type == "Staccato")     a = articulations::Staccato{};
  else if (type == "Pizzicato")    a = articulations::Pizzicato{};
  else if (type == "Pick")         a = articulations::Pick{};
  else if (type == "Pluck")        a = articulations::Pluck{};
  else if (type == "Strum")        a = articulations::Strum{};
  else if (type == "Snap")         a = articulations::Snap{};
  else if (type == "HammerOn")     a = articulations::HammerOn{};
  else if (type == "PullOff")      a = articulations::PullOff{};
  else if (type == "Harmonic")     a = articulations::Harmonic{};
  else if (type == "Mute")         a = articulations::Mute{};
  else if (type == "MuteHarmonic") a = articulations::MuteHarmonic{};
  else if (type == "Bend")         a = articulations::Bend{j.value("direction", -1),
                                                          j.value("semitones", 2)};
  else if (type == "Slide")        a = articulations::Slide{j.value("speed", 0.1f)};
  else                              a = articulations::Default{};
}

// Helper: variant-of-empty-types confuses nlohmann's adl_serializer,
// so serialize vector<Articulation> explicitly.
inline json articulations_to_json(const std::vector<Articulation>& arts) {
  json out = json::array();
  for (const auto& a : arts) {
    json aj;
    to_json(aj, a);
    out.push_back(std::move(aj));
  }
  return out;
}

// --- Ornament variant JSON ---

inline void to_json(json& j, const Mordent& m) {
  j = json{{"type", "Mordent"}, {"direction", m.direction}, {"semitones", m.semitones}};
  if (!m.articulations.empty()) j["articulations"] = articulations_to_json(m.articulations);
}

inline void to_json(json& j, const Trill& t) {
  j = json{{"type", "Trill"}, {"direction", t.direction}, {"semitones", t.semitones}};
  if (!t.articulations.empty()) j["articulations"] = articulations_to_json(t.articulations);
}

inline void to_json(json& j, const Turn& t) {
  j = json{{"type", "Turn"}, {"direction", t.direction},
            {"semitonesAbove", t.semitonesAbove}, {"semitonesBelow", t.semitonesBelow}};
  if (!t.articulations.empty()) j["articulations"] = articulations_to_json(t.articulations);
}

inline void to_json(json& j, const BendMordent& bm) {
  j = json{{"type", "BendMordent"}, {"direction", bm.direction}, {"semitones", bm.semitones}};
}

inline void to_json(json& j, const Ornament& o) {
  std::visit([&j](auto&& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, std::monostate>) {
      j = nullptr;
    } else {
      to_json(j, v);
    }
  }, o);
}

inline void from_json(const json& j, Ornament& o) {
  // Legacy format: bare string like "MordentAbove"
  if (j.is_string()) {
    static const std::unordered_map<std::string, Ornament> legacy = {
      {"None", Ornament{}},
      {"MordentAbove", Mordent{1, 2, {}}},  {"MordentBelow", Mordent{-1, 2, {}}},
      {"TurnAB", Turn{1, 2, 2, {}}},        {"TurnBA", Turn{-1, 2, 2, {}}},
      {"TrillAbove", Trill{1, 2, {}}},      {"TrillBelow", Trill{-1, 2, {}}},
    };
    o = legacy.at(j.get<std::string>());
    return;
  }

  // New format: object with "type" field
  std::string type = j.at("type").get<std::string>();
  std::vector<Articulation> arts;
  if (j.contains("articulations")) {
    for (auto& a : j.at("articulations")) {
      Articulation art;
      from_json(a, art);
      arts.push_back(std::move(art));
    }
  }
  int dir = j.value("direction", 1);

  if (type == "Mordent")          o = Mordent{dir, j.value("semitones", 2), std::move(arts)};
  else if (type == "Trill")       o = Trill{dir, j.value("semitones", 2), std::move(arts)};
  else if (type == "Turn")        o = Turn{dir, j.value("semitonesAbove", 2),
                                                j.value("semitonesBelow", 2), std::move(arts)};
  else if (type == "BendMordent") o = BendMordent{dir, j.value("semitones", 2)};
  else                            o = Ornament{};
}

inline void to_json(json& j, Dynamic d) {
  static const char* names[] = {"ppp","pp","p","mp","mf","f","ff","fff"};
  j = names[int(d)];
}

inline void from_json(const json& j, Dynamic& d) {
  static const std::unordered_map<std::string, Dynamic> map = {
    {"ppp",Dynamic::ppp},{"pp",Dynamic::pp},{"p",Dynamic::p},
    {"mp",Dynamic::mp},{"mf",Dynamic::mf},{"f",Dynamic::f},
    {"ff",Dynamic::ff},{"fff",Dynamic::fff}
  };
  d = map.at(j.get<std::string>());
}


inline void to_json(json& j, PitchSelectionType ps) {
  static const char* names[] = {
    "Single","Multiple","SingleAlt","MultipleAlt",
    "All","Low","High","LowHigh",
    "AllExLow","AllExHigh","AllExLowHigh",
    "Low2","High2","Even","Odd","LowHalf","HighHalf",
    "AllUnplayed"
  };
  j = names[int(ps)];
}

inline void from_json(const json& j, PitchSelectionType& ps) {
  static const std::unordered_map<std::string, PitchSelectionType> map = {
    {"Single",PitchSelectionType::Single},{"Multiple",PitchSelectionType::Multiple},
    {"SingleAlt",PitchSelectionType::SingleAlt},{"MultipleAlt",PitchSelectionType::MultipleAlt},
    {"All",PitchSelectionType::All},{"Low",PitchSelectionType::Low},
    {"High",PitchSelectionType::High},{"LowHigh",PitchSelectionType::LowHigh},
    {"AllExLow",PitchSelectionType::AllExLow},{"AllExHigh",PitchSelectionType::AllExHigh},
    {"AllExLowHigh",PitchSelectionType::AllExLowHigh},
    {"Low2",PitchSelectionType::Low2},{"High2",PitchSelectionType::High2},
    {"Even",PitchSelectionType::Even},{"Odd",PitchSelectionType::Odd},
    {"LowHalf",PitchSelectionType::LowHalf},{"HighHalf",PitchSelectionType::HighHalf},
    {"AllUnplayed",PitchSelectionType::AllUnplayed}
  };
  ps = map.at(j.get<std::string>());
}

// ===========================================================================
// Basics — types serialized by reference (name lookup)
// ===========================================================================

inline void to_json(json& j, const Pitch& p) {
  j = json{{"pitch", p.pitchDef->shortName}, {"octave", p.octave}};
}

inline void from_json(const json& j, Pitch& p) {
  p = Pitch::from_name(j.at("pitch").get<std::string>(), j.at("octave").get<int>());
}

inline void to_json(json& j, const Scale& s) {
  j = json{{"pitch", s.pitchDef->shortName}, {"scale", s.scaleDef->name}};
}

inline void from_json(const json& j, Scale& s) {
  s = Scale::get(j.at("pitch").get<std::string>(), j.at("scale").get<std::string>());
}

inline void to_json(json& j, const Key& k) {
  j = k.to_string();
}

inline void from_json(const json& j, Key& k) {
  k = Key::get(j.get<std::string>());
}

inline void to_json(json& j, const Meter& m) {
  j = json{{"numerator", m.numerator}, {"denominator", m.denominator}};
}

inline void from_json(const json& j, Meter& m) {
  m.numerator = j.at("numerator").get<int>();
  m.denominator = j.at("denominator").get<int>();
}

// ===========================================================================
// Chord
// ===========================================================================

inline void to_json(json& j, const Chord& c) {
  j = json{
    {"root", c.root},
    {"type", c.def->shortName.empty() ? "M" : c.def->shortName},
    {"duration", c.dur}
  };
  if (c.inversion != 0) j["inversion"] = c.inversion;
  if (c.spread != 0) j["spread"] = c.spread;
  if (c.figureName) j["figureName"] = *c.figureName;
}

inline void from_json(const json& j, Chord& c) {
  Pitch root;
  from_json(j.at("root"), root);
  std::string type = j.at("type").get<std::string>();
  float dur = j.value("duration", 1.0f);
  int inv = j.value("inversion", 0);
  int spr = j.value("spread", 0);

  c = Chord::create(root.pitchDef->shortName, root.octave, type, dur, inv, spr);

  if (j.contains("figureName"))
    c.figureName = j.at("figureName").get<std::string>();
}

// ===========================================================================
// ScaleChord / ChordProgression
// ===========================================================================

inline void to_json(json& j, const ScaleChord& sc) {
  j = json{{"degree", sc.degree}};
  if (sc.alteration != 0) j["alteration"] = sc.alteration;
  j["quality"] = sc.quality ? (sc.quality->shortName.empty() ? "M" : sc.quality->shortName) : "M";
}

inline void from_json(const json& j, ScaleChord& sc) {
  sc.degree = j.at("degree").get<int>();
  sc.alteration = j.value("alteration", 0);
  std::string q = j.value("quality", std::string("M"));
  sc.quality = &ChordDef::get(q);
}

inline void to_json(json& j, const ChordProgression& cp) {
  j = json{{"chords", json::array()}, {"pulses", cp.pulses.pulses}};
  for (const auto& sc : cp.chords.chords)
    j["chords"].push_back(sc);
}

inline void from_json(const json& j, ChordProgression& cp) {
  cp.chords.chords.clear();
  cp.pulses.pulses.clear();
  for (const auto& cj : j.at("chords")) {
    ScaleChord sc;
    from_json(cj, sc);
    cp.chords.chords.push_back(sc);
  }
  cp.pulses.pulses = j.at("pulses").get<std::vector<float>>();
}

// ===========================================================================
// Figures
// ===========================================================================

inline void to_json(json& j, const FigureUnit& u) {
  j = json{{"duration", u.duration}, {"step", u.step}};
  if (u.rest) j["rest"] = true;
  if (u.accidental != 0) j["accidental"] = u.accidental;
  if (!is_default(u.articulation)) {
    json aj;
    to_json(aj, u.articulation);
    j["articulation"] = aj;
  }
  if (has_ornament(u.ornament)) j["ornament"] = u.ornament;
}

inline void from_json(const json& j, FigureUnit& u) {
  u.duration = j.at("duration").get<float>();
  u.step = j.at("step").get<int>();
  u.rest = j.value("rest", false);
  u.accidental = j.value("accidental", 0);
  u.articulation = articulations::Default{};
  u.ornament = Ornament{};
  if (j.contains("articulation")) from_json(j.at("articulation"), u.articulation);
  if (j.contains("ornament")) from_json(j.at("ornament"), u.ornament);
}

inline void to_json(json& j, const PulseSequence& ps) {
  j = ps.pulses;
}
inline void from_json(const json& j, PulseSequence& ps) {
  ps.pulses.clear();
  for (const auto& v : j) ps.pulses.push_back(v.get<float>());
}

inline void to_json(json& j, const StepSequence& ss) {
  j = ss.steps;
}
inline void from_json(const json& j, StepSequence& ss) {
  ss.steps.clear();
  for (const auto& v : j) ss.steps.push_back(v.get<int>());
}

inline void to_json(json& j, const MelodicFigure& f) {
  j = json{{"units", f.units}};
}

inline void from_json(const json& j, MelodicFigure& f) {
  for (auto& uj : j.at("units")) {
    FigureUnit u;
    from_json(uj, u);
    f.units.push_back(u);
  }
}

inline void to_json(json& j, const PitchSelection& ps) {
  j = json{{"type", ps.type}};
  if (!ps.indices.empty()) j["indices"] = ps.indices;
  if (!ps.alterations.empty()) j["alterations"] = ps.alterations;
}

inline void from_json(const json& j, PitchSelection& ps) {
  from_json(j.at("type"), ps.type);
  if (j.contains("indices")) ps.indices = j.at("indices").get<std::vector<int>>();
  if (j.contains("alterations")) ps.alterations = j.at("alterations").get<std::vector<float>>();
}

inline void to_json(json& j, const ChordRealization::Element& e) {
  j = json{{"selection", e.selection}, {"duration", e.duration}};
  if (e.direction != 0) j["direction"] = e.direction;
  if (e.delay != 0.0f) j["delay"] = e.delay;
}

inline void from_json(const json& j, ChordRealization::Element& e) {
  from_json(j.at("selection"), e.selection);
  e.duration = j.at("duration").get<float>();
  e.direction = j.value("direction", 0);
  e.delay = j.value("delay", 0.0f);
}

inline void to_json(json& j, const ChordRealization& cf) {
  j = json{{"elements", cf.elements}};
}

inline void from_json(const json& j, ChordRealization& cf) {
  for (auto& ej : j.at("elements")) {
    ChordRealization::Element e;
    from_json(ej, e);
    cf.elements.push_back(e);
  }
}


// ===========================================================================
// Structure — Elements (variant)
// ===========================================================================

inline void to_json(json& j, const Note& n) {
  j = json{{"noteNumber", n.noteNumber}, {"velocity", n.velocity},
            {"duration", n.durationBeats}};
  if (!is_default(n.articulation)) {
    json aj;
    to_json(aj, n.articulation);
    j["articulation"] = aj;
  }
  if (has_ornament(n.ornament)) j["ornament"] = n.ornament;
}

inline void from_json(const json& j, Note& n) {
  n.noteNumber = j.at("noteNumber").get<float>();
  n.velocity = j.contains("velocity") ? j.at("velocity").get<float>() : 1.0f;
  n.durationBeats = j.at("duration").get<float>();
  n.articulation = articulations::Default{};
  n.ornament = Ornament{};
  if (j.contains("articulation")) from_json(j.at("articulation"), n.articulation);
  if (j.contains("ornament")) from_json(j.at("ornament"), n.ornament);
}

inline void to_json(json& j, const Hit& h) {
  j = json{{"drumNumber", h.drumNumber}, {"velocity", h.velocity},
            {"duration", h.durationBeats}};
}

inline void from_json(const json& j, Hit& h) {
  h.drumNumber = j.at("drumNumber").get<int>();
  h.velocity = j.contains("velocity") ? j.at("velocity").get<float>() : 1.0f;
  h.durationBeats = j.contains("duration") ? j.at("duration").get<float>() : 0.1f;
}

inline void to_json(json& j, const Rest& r) {
  j = json{{"duration", r.durationBeats}};
}

inline void from_json(const json& j, Rest& r) {
  r.durationBeats = j.at("duration").get<float>();
}

inline void to_json(json& j, const Element& e) {
  j = json{{"beat", e.startBeats}};
  if (e.is_note()) {
    j["type"] = "note";
    j["data"] = e.note();
  } else if (e.is_chord()) {
    j["type"] = "chord";
    j["data"] = e.chord();
  } else if (e.is_hit()) {
    j["type"] = "hit";
    j["data"] = e.hit();
  } else if (e.is_rest()) {
    j["type"] = "rest";
    j["data"] = e.rest();
  }
}

inline void from_json(const json& j, Element& e) {
  e.startBeats = j.at("beat").get<float>();
  std::string type = j.at("type").get<std::string>();
  if (type == "note") {
    Note n; from_json(j.at("data"), n); e.content = std::move(n);
  } else if (type == "chord") {
    Chord c; from_json(j.at("data"), c); e.content = std::move(c);
  } else if (type == "hit") {
    Hit h; from_json(j.at("data"), h); e.content = std::move(h);
  } else if (type == "rest") {
    Rest r; from_json(j.at("data"), r); e.content = std::move(r);
  }
}

// ===========================================================================
// Structure — Phrase, Passage, Section, Part, Piece
// ===========================================================================

inline void to_json(json& j, const DynamicMarking& dm) {
  j = json{{"beat", dm.beat}, {"level", dm.level}};
  if (dm.rampBeats > 0) j["ramp"] = dm.rampBeats;
}

inline void from_json(const json& j, DynamicMarking& dm) {
  dm.beat = j.at("beat").get<float>();
  from_json(j.at("level"), dm.level);
  dm.rampBeats = j.contains("ramp") ? j.at("ramp").get<float>() : 0.0f;
}

inline void to_json(json& j, const Phrase& ph) {
  j["startingPitch"] = ph.startingPitch;
  j["figures"] = json::array();
  for (const auto& fig : ph.figures) {
    // Serialize via the concrete units (all Figure subclasses share the same wire format)
    json fj;
    fj["units"] = fig->units;
    j["figures"].push_back(fj);
  }
}

inline void from_json(const json& j, Phrase& ph) {
  from_json(j.at("startingPitch"), ph.startingPitch);
  for (auto& fj : j.at("figures")) {
    MelodicFigure f;
    from_json(fj, f);
    ph.add_melodic_figure(std::move(f));
  }
}

inline void to_json(json& j, const Passage& p) {
  j = json{{"phrases", p.phrases}};
  if (!p.dynamicMarkings.empty()) j["dynamics"] = p.dynamicMarkings;
  if (p.scaleOverride) j["scaleOverride"] = *p.scaleOverride;
}

inline void from_json(const json& j, Passage& p) {
  for (auto& phj : j.at("phrases")) {
    Phrase ph;
    from_json(phj, ph);
    p.phrases.push_back(std::move(ph));
  }
  if (j.contains("dynamics")) {
    for (auto& dmj : j.at("dynamics")) {
      DynamicMarking dm(0, Dynamic::mf);
      from_json(dmj, dm);
      p.dynamicMarkings.push_back(dm);
    }
  }
  if (j.contains("scaleOverride")) {
    Scale s;
    from_json(j.at("scaleOverride"), s);
    p.scaleOverride = s;
  }
}

inline void to_json(json& j, const Section& s) {
  j = json{{"name", s.name}, {"beats", s.beats}, {"tempo", s.tempo},
            {"meter", s.meter}, {"scale", s.scale}};
}

inline void from_json(const json& j, Section& s) {
  s.name = j.at("name").get<std::string>();
  s.beats = j.at("beats").get<float>();
  s.tempo = j.contains("tempo") ? j.at("tempo").get<float>() : 120.0f;
  if (j.contains("meter")) from_json(j.at("meter"), s.meter);
  from_json(j.at("scale"), s.scale);
}

inline void to_json(json& j, const Part& p) {
  j = json{{"name", p.name}};
  if (!p.instrumentType.empty()) j["instrumentType"] = p.instrumentType;
  if (!p.elementSequence.empty()) j["events"] = p.elementSequence.elements;
  if (!p.passages.empty()) j["passages"] = p.passages;
  if (p.totalBeats() > 0) j["totalBeats"] = p.totalBeats();
}

inline void from_json(const json& j, Part& p) {
  p.name = j.at("name").get<std::string>();
  p.instrumentType = j.contains("instrumentType") ? j.at("instrumentType").get<std::string>() : "";
  if (j.contains("events")) {
    for (auto& ej : j.at("events")) {
      Element e;
      from_json(ej, e);
      p.elementSequence.add(e);
    }
  }
  if (j.contains("passages")) {
    for (auto& [key, val] : j.at("passages").items()) {
      Passage pass;
      from_json(val, pass);
      p.passages[key] = std::move(pass);
    }
  }
  // totalBeats is now derived from elementSequence; only override if explicitly set
  // and elementSequence didn't compute a larger value.
  if (j.contains("totalBeats")) {
    float tb = j.at("totalBeats").get<float>();
    if (tb > p.elementSequence.totalBeats) p.elementSequence.totalBeats = tb;
  }
}

inline void to_json(json& j, const Piece& p) {
  j = json{{"key", p.key}, {"sections", p.sections}, {"parts", p.parts}};
}

inline void from_json(const json& j, Piece& p) {
  from_json(j.at("key"), p.key);
  for (auto& sj : j.at("sections")) {
    Section s;
    from_json(sj, s);
    p.sections.push_back(std::move(s));
  }
  for (auto& pj : j.at("parts")) {
    Part part;
    from_json(pj, part);
    p.parts.push_back(std::move(part));
  }
}

// DrumFigure
inline void to_json(json& j, const DrumFigure::Hit& h) {
  j = {{"drum", h.drumNumber}, {"time", h.time}, {"velocity", h.velocity}, {"duration", h.duration}};
}
inline void from_json(const json& j, DrumFigure::Hit& h) {
  h.drumNumber = j.at("drum").get<int>();
  h.time = j.at("time").get<float>();
  h.velocity = j.at("velocity").get<float>();
  h.duration = j.value("duration", 0.1f);
}
inline void to_json(json& j, const DrumFigure& df) {
  j = {{"hits", df.hits}, {"totalTime", df.totalTime}};
}
inline void from_json(const json& j, DrumFigure& df) {
  df.hits.clear();
  for (auto& hj : j.at("hits")) {
    DrumFigure::Hit h;
    from_json(hj, h);
    df.hits.push_back(h);
  }
  df.totalTime = j.at("totalTime").get<float>();
}

} // namespace mforce
