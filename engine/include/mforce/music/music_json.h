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

inline void to_json(json& j, Articulation a) {
  static const char* names[] = {
    "Default","Bow","Marcato","Sforzando","Staccato","Pizzicato",
    "Pick","Pluck","Strum","Snap","HammerOn","PullOff",
    "Harmonic","Mute","MuteHarmonic"
  };
  j = names[int(a)];
}

inline void from_json(const json& j, Articulation& a) {
  static const std::unordered_map<std::string, Articulation> map = {
    {"Default",Articulation::Default},{"Bow",Articulation::Bow},
    {"Marcato",Articulation::Marcato},{"Sforzando",Articulation::Sforzando},
    {"Staccato",Articulation::Staccato},{"Pizzicato",Articulation::Pizzicato},
    {"Pick",Articulation::Pick},{"Pluck",Articulation::Pluck},
    {"Strum",Articulation::Strum},{"Snap",Articulation::Snap},
    {"HammerOn",Articulation::HammerOn},{"PullOff",Articulation::PullOff},
    {"Harmonic",Articulation::Harmonic},{"Mute",Articulation::Mute},
    {"MuteHarmonic",Articulation::MuteHarmonic}
  };
  a = map.at(j.get<std::string>());
}

inline void to_json(json& j, Ornament o) {
  static const char* names[] = {
    "None","MordentAbove","MordentBelow",
    "TurnAB","TurnBA","TrillAbove","TrillBelow"
  };
  j = names[int(o)];
}

inline void from_json(const json& j, Ornament& o) {
  static const std::unordered_map<std::string, Ornament> map = {
    {"None",Ornament::None},{"MordentAbove",Ornament::MordentAbove},
    {"MordentBelow",Ornament::MordentBelow},{"TurnAB",Ornament::TurnAB},
    {"TurnBA",Ornament::TurnBA},{"TrillAbove",Ornament::TrillAbove},
    {"TrillBelow",Ornament::TrillBelow}
  };
  o = map.at(j.get<std::string>());
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

inline void to_json(json& j, ConnectorType ct) {
  static const char* names[] = {"Step","Pitch","EndPitch","Elide"};
  j = names[int(ct)];
}

inline void from_json(const json& j, ConnectorType& ct) {
  static const std::unordered_map<std::string, ConnectorType> map = {
    {"Step",ConnectorType::Step},{"Pitch",ConnectorType::Pitch},
    {"EndPitch",ConnectorType::EndPitch},{"Elide",ConnectorType::Elide}
  };
  ct = map.at(j.get<std::string>());
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
  if (u.articulation != Articulation::Default) j["articulation"] = u.articulation;
  if (u.ornament != Ornament::None) j["ornament"] = u.ornament;
}

inline void from_json(const json& j, FigureUnit& u) {
  u.duration = j.at("duration").get<float>();
  u.step = j.at("step").get<int>();
  u.rest = j.value("rest", false);
  u.accidental = j.value("accidental", 0);
  u.articulation = Articulation::Default;
  u.ornament = Ornament::None;
  if (j.contains("articulation")) from_json(j.at("articulation"), u.articulation);
  if (j.contains("ornament")) from_json(j.at("ornament"), u.ornament);
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

inline void to_json(json& j, const ChordArticulation::Element& e) {
  j = json{{"selection", e.selection}, {"duration", e.duration}};
  if (e.direction != 0) j["direction"] = e.direction;
  if (e.delay != 0.0f) j["delay"] = e.delay;
}

inline void from_json(const json& j, ChordArticulation::Element& e) {
  from_json(j.at("selection"), e.selection);
  e.duration = j.at("duration").get<float>();
  e.direction = j.value("direction", 0);
  e.delay = j.value("delay", 0.0f);
}

inline void to_json(json& j, const ChordArticulation& cf) {
  j = json{{"elements", cf.elements}};
}

inline void from_json(const json& j, ChordArticulation& cf) {
  for (auto& ej : j.at("elements")) {
    ChordArticulation::Element e;
    from_json(ej, e);
    cf.elements.push_back(e);
  }
}

inline void to_json(json& j, const FigureConnector& fc) {
  j = json{{"type", fc.type}};
  if (fc.type == ConnectorType::Step) j["step"] = fc.stepValue;
  if (fc.type == ConnectorType::Pitch || fc.type == ConnectorType::EndPitch)
    j["pitch"] = fc.pitch;
}

inline void from_json(const json& j, FigureConnector& fc) {
  from_json(j.at("type"), fc.type);
  if (j.contains("step")) fc.stepValue = j.at("step").get<int>();
  if (j.contains("pitch")) from_json(j.at("pitch"), fc.pitch);
}

// ===========================================================================
// Structure — Elements (variant)
// ===========================================================================

inline void to_json(json& j, const Note& n) {
  j = json{{"noteNumber", n.noteNumber}, {"velocity", n.velocity},
            {"duration", n.durationBeats}};
  if (n.articulation != Articulation::Default) j["articulation"] = n.articulation;
  if (n.ornament != Ornament::None) j["ornament"] = n.ornament;
}

inline void from_json(const json& j, Note& n) {
  n.noteNumber = j.at("noteNumber").get<float>();
  n.velocity = j.contains("velocity") ? j.at("velocity").get<float>() : 1.0f;
  n.durationBeats = j.at("duration").get<float>();
  n.articulation = Articulation::Default;
  n.ornament = Ornament::None;
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
  j = json{{"startingPitch", ph.startingPitch}, {"figures", ph.figures}};
  if (!ph.connectors.empty()) j["connectors"] = ph.connectors;
}

inline void from_json(const json& j, Phrase& ph) {
  from_json(j.at("startingPitch"), ph.startingPitch);
  for (auto& fj : j.at("figures")) {
    MelodicFigure f;
    from_json(fj, f);
    ph.figures.push_back(std::move(f));
  }
  if (j.contains("connectors")) {
    for (auto& cj : j.at("connectors")) {
      FigureConnector fc;
      from_json(cj, fc);
      ph.connectors.push_back(fc);
    }
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
  if (!p.events.empty()) j["events"] = p.events;
  if (!p.passages.empty()) j["passages"] = p.passages;
  if (p.totalBeats > 0) j["totalBeats"] = p.totalBeats;
}

inline void from_json(const json& j, Part& p) {
  p.name = j.at("name").get<std::string>();
  p.instrumentType = j.contains("instrumentType") ? j.at("instrumentType").get<std::string>() : "";
  if (j.contains("events")) {
    for (auto& ej : j.at("events")) {
      Element e;
      from_json(ej, e);
      p.events.push_back(std::move(e));
    }
  }
  if (j.contains("passages")) {
    for (auto& [key, val] : j.at("passages").items()) {
      Passage pass;
      from_json(val, pass);
      p.passages[key] = std::move(pass);
    }
  }
  p.totalBeats = j.contains("totalBeats") ? j.at("totalBeats").get<float>() : 0.0f;
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
