#pragma once
#include "mforce/music/templates.h"
#include "mforce/music/music_json.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/music/realization_strategy.h"
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_choice.h"
#include "mforce/music/voicing_json.h"
#include "mforce/music/voicing_profile_selector.h"
#include <nlohmann/json.hpp>
#include <any>
#include <functional>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace mforce {

using json = nlohmann::json;

// ===========================================================================
// ChordProgression — shared by SectionTemplate and PassageTemplate
// ===========================================================================

// Two authoring forms are accepted, deliberately:
//
//   flat       [{"degree": 5, "alteration": -1, "quality": "7", "beats": 4}]
//   canonical  {"chords": [{"degree": 5, ...}], "pulses": [4.0]}
//
// The flat form is what a human (or a generator) writes; the canonical form is
// what to_json(ChordProgression) emits, so accepting it makes a template
// round-trip. It did not before: to_json(SectionTemplate) wrote canonical and
// from_json only read flat, so writing a template back out made it unreadable.
//
// `alteration` shifts the chord ROOT chromatically (-1 = flat), which is what
// makes bVI7 — a German sixth, spelled Ab-C-Eb-Gb in C — authorable at all.
inline ChordProgression parse_chord_progression(const json& j) {
    ChordProgression prog;
    if (j.is_object() && j.contains("chords")) {
        from_json(j, prog);              // canonical form (music_json.h)
        return prog;
    }
    for (const auto& entry : j) {
        ScaleChord sc;
        sc.degree = entry.at("degree").get<int>();
        sc.alteration = entry.value("alteration", 0);
        sc.quality = &ChordDef::get(entry.value("quality", std::string("Major")));
        // Optional per-chord voicing pin (see VoicingPin in basics.h):
        //   {"pin": {"inversion": 2, "topTone": 2}}
        // pins the cadential 6/4 — bass = 5th, 5th doubled on top — no matter
        // what a VoicingSelector would prefer.
        if (entry.contains("pin")) {
            const auto& jp = entry["pin"];
            VoicingPin pin;
            pin.inversion = jp.value("inversion", 0);
            pin.spread = jp.value("spread", 0);
            if (jp.contains("octave")) pin.octave = jp["octave"].get<int>();
            pin.topTone = jp.value("topTone", -1);
            sc.pin = pin;
        }
        prog.add(sc, entry.at("beats").get<float>());
    }
    return prog;
}

// Defined here (not next to its from_json further down) because
// to_json(PassageTemplate) needs it and that comes first.
inline void to_json(json& j, const ChordAccompanimentConfig& cc) {
    j = json{{"octave", cc.octave}};
    if (cc.inversion != 0) j["inversion"] = cc.inversion;
    if (cc.spread != 0) j["spread"] = cc.spread;
}

// ===========================================================================
// Constraints (RFB figure constraints) — figure_constraints.h
// ===========================================================================

inline void to_json(json& j, const Constraints& c) {
    j = json::object();
    if (c.count)        j["count"]        = *c.count;
    if (c.length)       j["length"]       = *c.length;
    if (c.net)          j["net"]          = *c.net;
    if (c.ceiling)      j["ceiling"]      = *c.ceiling;
    if (c.floor)        j["floor"]        = *c.floor;
    if (c.defaultPulse) j["defaultPulse"] = *c.defaultPulse;
    if (c.minPulse)     j["minPulse"]     = *c.minPulse;
    if (c.maxPulse)     j["maxPulse"]     = *c.maxPulse;
}

inline void from_json(const json& j, Constraints& c) {
    if (j.contains("count"))        c.count        = j.at("count").get<int>();
    if (j.contains("length"))       c.length       = j.at("length").get<float>();
    if (j.contains("net"))          c.net          = j.at("net").get<int>();
    if (j.contains("ceiling"))      c.ceiling      = j.at("ceiling").get<int>();
    if (j.contains("floor"))        c.floor        = j.at("floor").get<int>();
    if (j.contains("defaultPulse")) c.defaultPulse = j.at("defaultPulse").get<float>();
    if (j.contains("minPulse"))     c.minPulse     = j.at("minPulse").get<float>();
    if (j.contains("maxPulse"))     c.maxPulse     = j.at("maxPulse").get<float>();
}

// ===========================================================================
// Enum serialization
// ===========================================================================

inline void to_json(json& j, FigureSource s) {
    switch (s) {
        case FigureSource::Generate:  j = "generate";  break;
        case FigureSource::Reference: j = "reference"; break;
        case FigureSource::Transform: j = "transform"; break;
        case FigureSource::Locked:    j = "locked";    break;
        case FigureSource::Literal:   j = "literal";   break;
    }
}
inline void from_json(const json& j, FigureSource& s) {
    auto str = j.get<std::string>();
    if (str == "generate")        s = FigureSource::Generate;
    else if (str == "reference")  s = FigureSource::Reference;
    else if (str == "transform")  s = FigureSource::Transform;
    else if (str == "locked")     s = FigureSource::Locked;
    else if (str == "literal")    s = FigureSource::Literal;
    else s = FigureSource::Generate;
}

inline void to_json(json& j, TransformOp t) {
    switch (t) {
        case TransformOp::None:             j = "none"; break;
        case TransformOp::Invert:           j = "invert"; break;
        case TransformOp::Reverse:          j = "reverse"; break;
        case TransformOp::Stretch:          j = "stretch"; break;
        case TransformOp::Compress:         j = "compress"; break;
        case TransformOp::VaryRhythm:       j = "vary_rhythm"; break;
        case TransformOp::VarySteps:        j = "vary_steps"; break;
        case TransformOp::NewSteps:         j = "new_steps"; break;
        case TransformOp::NewRhythm:        j = "new_rhythm"; break;
        case TransformOp::Replicate:        j = "replicate"; break;
        case TransformOp::TransformGeneral: j = "general"; break;
        case TransformOp::RhythmTail:       j = "rhythm_tail"; break;
        case TransformOp::Complexify:       j = "complexify"; break;
    }
}
inline void from_json(const json& j, TransformOp& t) {
    auto str = j.get<std::string>();
    if (str == "none")          t = TransformOp::None;
    else if (str == "invert")        t = TransformOp::Invert;
    else if (str == "reverse")       t = TransformOp::Reverse;
    else if (str == "stretch")       t = TransformOp::Stretch;
    else if (str == "compress")      t = TransformOp::Compress;
    else if (str == "vary_rhythm")   t = TransformOp::VaryRhythm;
    else if (str == "vary_steps")    t = TransformOp::VarySteps;
    else if (str == "new_steps")     t = TransformOp::NewSteps;
    else if (str == "new_rhythm")    t = TransformOp::NewRhythm;
    else if (str == "replicate")     t = TransformOp::Replicate;
    else if (str == "general")       t = TransformOp::TransformGeneral;
    else if (str == "rhythm_tail")   t = TransformOp::RhythmTail;
    else if (str == "complexify")    t = TransformOp::Complexify;
    else t = TransformOp::None;
}

// MotifRole and MotifOrigin enum JSON helpers (2026-04-15 umbrella spec).
inline void to_json(json& j, MotifRole r) {
    switch (r) {
        case MotifRole::Thematic:      j = "Thematic"; break;
        case MotifRole::Cadential:     j = "Cadential"; break;
        case MotifRole::PostCadential: j = "PostCadential"; break;
        case MotifRole::Discursive:    j = "Discursive"; break;
        case MotifRole::Climactic:     j = "Climactic"; break;
        case MotifRole::Connective:    j = "Connective"; break;
        case MotifRole::Ornamental:    j = "Ornamental"; break;
    }
}
inline void from_json(const json& j, MotifRole& r) {
    auto s = j.get<std::string>();
    if (s == "Thematic")           r = MotifRole::Thematic;
    else if (s == "Cadential")     r = MotifRole::Cadential;
    else if (s == "PostCadential") r = MotifRole::PostCadential;
    else if (s == "Discursive")    r = MotifRole::Discursive;
    else if (s == "Climactic")     r = MotifRole::Climactic;
    else if (s == "Connective")    r = MotifRole::Connective;
    else if (s == "Ornamental")    r = MotifRole::Ornamental;
    else throw std::runtime_error("Unknown MotifRole: " + s);
}

inline void to_json(json& j, MotifOrigin o) {
    switch (o) {
        case MotifOrigin::User:      j = "User"; break;
        case MotifOrigin::Generated: j = "Generated"; break;
        case MotifOrigin::Derived:   j = "Derived"; break;
        case MotifOrigin::Extracted: j = "Extracted"; break;
    }
}
inline void from_json(const json& j, MotifOrigin& o) {
    auto s = j.get<std::string>();
    if (s == "User")           o = MotifOrigin::User;
    else if (s == "Generated") o = MotifOrigin::Generated;
    else if (s == "Derived")   o = MotifOrigin::Derived;
    else if (s == "Extracted") o = MotifOrigin::Extracted;
    else throw std::runtime_error("Unknown MotifOrigin: " + s);
}

inline void to_json(json& j, PartRole r) {
    switch (r) {
        case PartRole::Melody:        j = "melody"; break;
        case PartRole::Bass:          j = "bass"; break;
        case PartRole::Harmony:       j = "harmony"; break;
        case PartRole::Drums:         j = "drums"; break;
        case PartRole::Countermelody: j = "countermelody"; break;
        case PartRole::Pad:           j = "pad"; break;
    }
}
inline void from_json(const json& j, PartRole& r) {
    auto str = j.get<std::string>();
    if (str == "melody")             r = PartRole::Melody;
    else if (str == "bass")          r = PartRole::Bass;
    else if (str == "harmony")       r = PartRole::Harmony;
    else if (str == "drums")         r = PartRole::Drums;
    else if (str == "countermelody") r = PartRole::Countermelody;
    else if (str == "pad")           r = PartRole::Pad;
    else r = PartRole::Melody;
}

// ===========================================================================
// MelodicFunction
// ===========================================================================

inline void to_json(json& j, MelodicFunction f) {
    switch (f) {
        case MelodicFunction::Free:        j = "free"; break;
        case MelodicFunction::Statement:   j = "statement"; break;
        case MelodicFunction::Development: j = "development"; break;
        case MelodicFunction::Transition:  j = "transition"; break;
        case MelodicFunction::Cadential:   j = "cadential"; break;
    }
}
inline void from_json(const json& j, MelodicFunction& f) {
    auto s = j.get<std::string>();
    if (s == "statement")        f = MelodicFunction::Statement;
    else if (s == "development") f = MelodicFunction::Development;
    else if (s == "transition")  f = MelodicFunction::Transition;
    else if (s == "cadential")   f = MelodicFunction::Cadential;
    else f = MelodicFunction::Free;
}

// ===========================================================================
// FigureShape
// ===========================================================================

inline void to_json(json& j, FigureShape s) {
    switch (s) {
        case FigureShape::Free:              j = "free"; break;
        case FigureShape::ScalarRun:         j = "scalar_run"; break;
        case FigureShape::RepeatedNote:      j = "repeated_note"; break;
        case FigureShape::HeldNote:          j = "held_note"; break;
        case FigureShape::CadentialApproach: j = "cadential_approach"; break;
        case FigureShape::TriadicOutline:    j = "triadic_outline"; break;
        case FigureShape::NeighborTone:      j = "neighbor_tone"; break;
        case FigureShape::LeapAndFill:       j = "leap_and_fill"; break;
        case FigureShape::ScalarReturn:      j = "scalar_return"; break;
        case FigureShape::Anacrusis:         j = "anacrusis"; break;
        case FigureShape::Zigzag:            j = "zigzag"; break;
        case FigureShape::Fanfare:           j = "fanfare"; break;
        case FigureShape::Sigh:              j = "sigh"; break;
        case FigureShape::Suspension:        j = "suspension"; break;
        case FigureShape::Cambiata:          j = "cambiata"; break;
        case FigureShape::Skipping:          j = "skipping"; break;
        case FigureShape::Stepping:          j = "stepping"; break;
    }
}
inline void from_json(const json& j, FigureShape& s) {
    auto str = j.get<std::string>();
    if (str == "scalar_run")              s = FigureShape::ScalarRun;
    else if (str == "repeated_note")      s = FigureShape::RepeatedNote;
    else if (str == "held_note")          s = FigureShape::HeldNote;
    else if (str == "cadential_approach") s = FigureShape::CadentialApproach;
    else if (str == "triadic_outline")    s = FigureShape::TriadicOutline;
    else if (str == "neighbor_tone")      s = FigureShape::NeighborTone;
    else if (str == "leap_and_fill")      s = FigureShape::LeapAndFill;
    else if (str == "scalar_return")      s = FigureShape::ScalarReturn;
    else if (str == "anacrusis")          s = FigureShape::Anacrusis;
    else if (str == "zigzag")             s = FigureShape::Zigzag;
    else if (str == "fanfare")            s = FigureShape::Fanfare;
    else if (str == "sigh")               s = FigureShape::Sigh;
    else if (str == "suspension")         s = FigureShape::Suspension;
    else if (str == "cambiata")           s = FigureShape::Cambiata;
    else if (str == "skipping")           s = FigureShape::Skipping;
    else if (str == "stepping")           s = FigureShape::Stepping;
    else s = FigureShape::Free;
}

// ===========================================================================
// FigureDirection
// ===========================================================================

inline void to_json(json& j, FigureDirection d) {
    switch (d) {
        case FigureDirection::Ascending:             j = "ascending"; break;
        case FigureDirection::Descending:            j = "descending"; break;
        case FigureDirection::TurnaroundAscending:   j = "turnaround_ascending"; break;
        case FigureDirection::TurnaroundDescending:  j = "turnaround_descending"; break;
        case FigureDirection::AscendingArc:          j = "ascending_arc"; break;
        case FigureDirection::DescendingArc:         j = "descending_arc"; break;
        case FigureDirection::SineAscending:         j = "sine_ascending"; break;
        case FigureDirection::SineDescending:        j = "sine_descending"; break;
        case FigureDirection::Random:                j = "random"; break;
    }
}
inline void from_json(const json& j, FigureDirection& d) {
    auto str = j.get<std::string>();
    if (str == "ascending")                  d = FigureDirection::Ascending;
    else if (str == "descending")            d = FigureDirection::Descending;
    else if (str == "turnaround_ascending")  d = FigureDirection::TurnaroundAscending;
    else if (str == "turnaround_descending") d = FigureDirection::TurnaroundDescending;
    else if (str == "ascending_arc")         d = FigureDirection::AscendingArc;
    else if (str == "descending_arc")        d = FigureDirection::DescendingArc;
    else if (str == "sine_ascending")        d = FigureDirection::SineAscending;
    else if (str == "sine_descending")       d = FigureDirection::SineDescending;
    else if (str == "random")                d = FigureDirection::Random;
    else d = FigureDirection::Ascending;
}

// ===========================================================================
// FigureTemplate
// ===========================================================================

inline void to_json(json& j, const FigureTemplate& ft) {
    j = json{{"source", ft.source}};

    if (ft.source == FigureSource::Generate) {
        if (ft.minNotes != 2) j["minNotes"] = ft.minNotes;
        if (ft.maxNotes != 8) j["maxNotes"] = ft.maxNotes;
        if (ft.totalBeats != 0.0f) j["totalBeats"] = ft.totalBeats;
        if (ft.defaultPulse != 0.0f) j["defaultPulse"] = ft.defaultPulse;
        if (ft.preferStepwise) j["stepwise"] = true;
        if (ft.preferSkips) j["skips"] = true;
        if (ft.targetNet != 0) j["targetNet"] = ft.targetNet;
    }

    if (ft.source == FigureSource::Reference || ft.source == FigureSource::Transform) {
        j["motifName"] = ft.motifName;
    }
    if (ft.source == FigureSource::Transform) {
        j["transform"] = ft.transform;
        if (ft.transformParam != 0) j["transformParam"] = ft.transformParam;
    }

    if (ft.source == FigureSource::Locked && ft.lockedFigure) {
        j["lockedFigure"] = *ft.lockedFigure;
    }

    if (!ft.literalNotes.empty()) {
        json arr = json::array();
        for (auto& ln : ft.literalNotes) {
            json jn;
            if (ln.rest) {
                jn["rest"] = true;
            } else if (ln.pitch) {
                jn["pitch"] = *ln.pitch;
            }
            jn["duration"] = ln.duration;
            arr.push_back(std::move(jn));
        }
        j["literalNotes"] = std::move(arr);
    }

    if (ft.shape != FigureShape::Free) j["shape"] = ft.shape;
    if (ft.shapeDirection != 1) j["shapeDirection"] = ft.shapeDirection;
    if (ft.shapeParam != 0) j["shapeParam"] = ft.shapeParam;
    if (ft.shapeParam2 != 0) j["shapeParam2"] = ft.shapeParam2;
    if (ft.direction != FigureDirection::Ascending) j["direction"] = ft.direction;

    if (!ft.rhythmMotifName.empty()) j["rhythmMotifName"] = ft.rhythmMotifName;
    if (!ft.contourMotifName.empty()) j["contourMotifName"] = ft.contourMotifName;
    if (!ft.rhythmTransform.empty()) j["rhythmTransform"] = ft.rhythmTransform;
    if (ft.rhythmTransformParam != 0) j["rhythmTransformParam"] = ft.rhythmTransformParam;
    if (!ft.contourTransform.empty()) j["contourTransform"] = ft.contourTransform;
    if (ft.contourTransformParam != 0) j["contourTransformParam"] = ft.contourTransformParam;

    if (ft.maxStep != 0) j["maxStep"] = ft.maxStep;
    if (ft.stepMode != StepMode::Scale) j["stepMode"] = "chordTone";

    if (ft.figureCadenceType != 0) j["figureCadenceType"] = ft.figureCadenceType;
    if (!ft.perfect) j["perfect"] = false;

    if (ft.seed != 0) j["seed"] = ft.seed;
    if (ft.locked) j["locked"] = true;
    if (ft.role) j["role"] = *ft.role;
}

inline void from_json(const json& j, FigureTemplate& ft) {
    from_json(j.at("source"), ft.source);

    ft.minNotes = j.value("minNotes", 2);
    ft.maxNotes = j.value("maxNotes", 8);
    ft.totalBeats = j.value("totalBeats", 0.0f);
    ft.defaultPulse = j.value("defaultPulse", 0.0f);
    ft.preferStepwise = j.value("stepwise", false);
    ft.preferSkips = j.value("skips", false);
    ft.targetNet = j.value("targetNet", 0);

    ft.motifName = j.value("motifName", std::string(""));
    if (j.contains("transform")) from_json(j.at("transform"), ft.transform);
    ft.transformParam = j.value("transformParam", 0);

    if (j.contains("shape")) from_json(j.at("shape"), ft.shape);
    ft.shapeDirection = j.value("shapeDirection", 1);
    ft.shapeParam = j.value("shapeParam", 0);
    ft.shapeParam2 = j.value("shapeParam2", 0);
    if (j.contains("direction")) from_json(j.at("direction"), ft.direction);

    if (j.contains("lockedFigure")) {
        MelodicFigure mf;
        from_json(j.at("lockedFigure"), mf);
        ft.lockedFigure = std::move(mf);
    }

    if (j.contains("literalNotes")) {
        ft.literalNotes.clear();
        for (auto& jn : j.at("literalNotes")) {
            FigureTemplate::LiteralNote ln;
            ln.rest = jn.value("rest", false);
            if (!ln.rest && jn.contains("pitch")) {
                Pitch p;
                from_json(jn.at("pitch"), p);
                ln.pitch = p;
            }
            ln.duration = jn.value("duration", 1.0f);
            ft.literalNotes.push_back(std::move(ln));
        }
    }

    ft.rhythmMotifName = j.value("rhythmMotifName", std::string(""));
    ft.contourMotifName = j.value("contourMotifName", std::string(""));
    ft.rhythmTransform = j.value("rhythmTransform", std::string(""));
    ft.rhythmTransformParam = j.value("rhythmTransformParam", 0.0f);
    ft.contourTransform = j.value("contourTransform", std::string(""));
    ft.contourTransformParam = j.value("contourTransformParam", 0.0f);

    if (j.contains("stepMode")) {
      auto sm = j["stepMode"].get<std::string>();
      if (sm == "chordTone") ft.stepMode = StepMode::ChordTone;
      else ft.stepMode = StepMode::Scale;
    }

    ft.maxStep = j.value("maxStep", 0);

    ft.figureCadenceType = j.value("figureCadenceType", 0);
    ft.perfect = j.value("perfect", true);

    ft.seed = j.value("seed", 0u);
    ft.locked = j.value("locked", false);

    if (j.contains("role") && !j.at("role").is_null()) {
        ft.role = j.at("role").get<MotifRole>();
    }
}

// ===========================================================================
// Motif
// ===========================================================================

inline void to_json(json& j, const Motif& m) {
    j["name"] = m.name;
    if (m.is_figure()) {
        j["type"] = "figure";
        const auto& fig = m.figure();
        if (!fig.units.empty()) j["figure"] = fig;
    } else if (m.is_rhythm()) {
        j["type"] = "rhythm";
        j["rhythm"] = m.rhythm();
    } else if (m.is_contour()) {
        j["type"] = "contour";
        j["contour"] = m.contour();
    }
    if (m.userProvided) j["userProvided"] = true;
    if (m.generationSeed != 0) j["generationSeed"] = m.generationSeed;
    if (m.constraints) j["constraints"] = *m.constraints;

    // New metadata — emit only when non-default for terse JSON.
    if (!m.roles.empty()) {
        j["roles"] = json::array();
        for (auto r : m.roles) j["roles"].push_back(r);
    }
    if (m.origin != MotifOrigin::User) j["origin"] = m.origin;
    if (m.derivedFrom) j["derivedFrom"] = *m.derivedFrom;
    if (m.transform) j["transform"] = *m.transform;
    if (m.transformParam != 0) j["transformParam"] = m.transformParam;
}
inline void from_json(const json& j, Motif& m) {
    m.name = j.at("name").get<std::string>();
    std::string type = j.value("type", std::string("figure"));
    if (type == "rhythm") {
        PulseSequence ps;
        from_json(j.at("rhythm"), ps);
        m.content = std::move(ps);
    } else if (type == "contour") {
        StepSequence ss;
        from_json(j.at("contour"), ss);
        m.content = std::move(ss);
    } else {
        // Default: figure (backward compatible)
        if (j.contains("figure")) {
            MelodicFigure fig;
            from_json(j.at("figure"), fig);
            m.content = std::move(fig);
        } else {
            m.content = MelodicFigure{};  // empty figure if no content
        }
    }
    m.userProvided = j.value("userProvided", false);
    m.generationSeed = j.value("generationSeed", 0u);
    if (j.contains("constraints")) {
        FigureTemplate ft;
        from_json(j.at("constraints"), ft);
        m.constraints = std::move(ft);
    }

    // New metadata (2026-04-15). Defaults: empty roles, User origin, no derivation.
    if (j.contains("roles") && j.at("roles").is_array()) {
        for (const auto& r : j.at("roles")) {
            m.roles.insert(r.get<MotifRole>());
        }
    }
    m.origin = j.value("origin", MotifOrigin::User);
    if (j.contains("derivedFrom") && !j.at("derivedFrom").is_null()) {
        m.derivedFrom = j.at("derivedFrom").get<std::string>();
    }
    if (j.contains("transform") && !j.at("transform").is_null()) {
        m.transform = j.at("transform").get<TransformOp>();
    }
    m.transformParam = j.value("transformParam", 0);
}

// ===========================================================================
// SettingsCodecs — for each level and name a template can choose, how that
// strategy's settings are read from and written to JSON
// ===========================================================================

// The four places a template names a strategy.
enum class StrategyLevel { Phrase, Passage, Realization, ProfileSelector };

inline const char* level_label(StrategyLevel level) {
    switch (level) {
        case StrategyLevel::Phrase:          return "phrase strategy";
        case StrategyLevel::Passage:         return "passage strategy";
        case StrategyLevel::Realization:     return "realization strategy";
        case StrategyLevel::ProfileSelector: return "voicing profile selector";
    }
    return "strategy";
}

class SettingsCodecs;

// A settings type's JSON functions: to_json / from_json found by ADL, or,
// for a type that holds templates, the forms that take the codecs.
template <class S>
void read_settings(const json& j, S& s, const SettingsCodecs& codecs) {
    if constexpr (requires { from_json(j, s, codecs); }) from_json(j, s, codecs);
    else from_json(j, s);
}

template <class S>
json write_settings(const S& s, const SettingsCodecs& codecs) {
    json j;
    if constexpr (requires { to_json(j, s, codecs); }) to_json(j, s, codecs);
    else to_json(j, s);
    return j;
}

class SettingsCodecs {
public:
    // Registers Strategy::kName at Strategy's level, with the JSON
    // functions of Strategy::Settings (none for NoSettings).
    template <class Strategy> void add() {
        using S = typename Strategy::Settings;
        Entry e;
        if constexpr (!std::is_same_v<S, NoSettings>) {
            e.read = [](const json& j, const SettingsCodecs& c) -> std::any {
                S s{};
                read_settings(j, s, c);
                return s;
            };
            e.write = [](const std::any& a, const SettingsCodecs& c) -> json {
                const S* s = std::any_cast<S>(&a);
                if (!s) {
                    throw std::runtime_error(std::string("strategy '") + Strategy::kName +
                        "' holds settings of type " + a.type().name() +
                        " where it writes " + typeid(S).name());
                }
                return write_settings(*s, c);
            };
        }
        table_[level_of<Strategy>()][Strategy::kName] = std::move(e);
    }

    // A choice in either file form: a bare name, or {name, settings}.
    // Refuses a name not registered at that level, and settings for a
    // strategy that takes none.
    StrategyChoice read(const json& j, StrategyLevel level) const {
        StrategyChoice c;
        if (j.is_string()) {
            c.name = j.get<std::string>();
        } else if (j.is_object()) {
            c.name = j.at("name").get<std::string>();
        } else {
            throw std::runtime_error(std::string("a ") + level_label(level) +
                                     " is a name or {\"name\", \"settings\"}: " + j.dump());
        }
        const bool hasSettings = j.is_object() && j.contains("settings");
        if (c.name.empty()) {
            if (hasSettings)
                throw std::runtime_error(std::string("settings for an unnamed ") +
                                         level_label(level));
            return c;
        }
        const Entry& e = entry(level, c.name);
        if (hasSettings) {
            if (!e.read)
                throw std::runtime_error(std::string(level_label(level)) + " '" + c.name +
                                         "' takes no settings");
            c.settings = e.read(j.at("settings"), *this);
        }
        return c;
    }

    // A bare name when the choice holds no settings; {name, settings}
    // otherwise. The caller omits an empty choice.
    json write(const StrategyChoice& c, StrategyLevel level) const {
        if (!c.settings.has_value()) return c.name;
        const Entry& e = entry(level, c.name);
        if (!e.write)
            throw std::runtime_error(std::string(level_label(level)) + " '" + c.name +
                                     "' takes no settings");
        return json{{"name", c.name}, {"settings", e.write(c.settings, *this)}};
    }

private:
    struct Entry {
        std::function<std::any(const json&, const SettingsCodecs&)> read;
        std::function<json(const std::any&, const SettingsCodecs&)> write;
    };

    template <class Strategy> static constexpr StrategyLevel level_of() {
        if constexpr (std::is_base_of_v<PhraseStrategy, Strategy>) return StrategyLevel::Phrase;
        else if constexpr (std::is_base_of_v<PassageStrategy, Strategy>) return StrategyLevel::Passage;
        else if constexpr (std::is_base_of_v<RealizationStrategy, Strategy>) return StrategyLevel::Realization;
        else {
            static_assert(std::is_base_of_v<VoicingProfileSelector, Strategy>,
                          "a template chooses phrase, passage and realization strategies "
                          "and voicing profile selectors only");
            return StrategyLevel::ProfileSelector;
        }
    }

    const Entry& entry(StrategyLevel level, const std::string& name) const {
        auto lt = table_.find(level);
        if (lt != table_.end()) {
            auto it = lt->second.find(name);
            if (it != lt->second.end()) return it->second;
        }
        throw std::runtime_error(std::string("no ") + level_label(level) + " named '" +
                                 name + "' is registered");
    }

    std::map<StrategyLevel, std::map<std::string, Entry>> table_;
};

// A template key of the form before strategy choices refuses to be read.
inline void refuse_old_keys(const json& j, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        if (j.contains(k)) {
            throw std::runtime_error(std::string("template key '") + k +
                "' is the old form; rewrite the file with: "
                "python tools/convert_template.py <file or folder>");
        }
    }
}

inline void refuse_old_phrase_keys(const json& j) {
    refuse_old_keys(j, {"periodConfig", "sentenceConfig", "twoFigureConfig",
                        "elaboratedConfig"});
}

// Writes the choice under `key` unless it is empty.
inline void write_choice(json& j, const char* key, const StrategyChoice& c,
                         StrategyLevel level, const SettingsCodecs& codecs) {
    if (!c.name.empty() || c.settings.has_value()) j[key] = codecs.write(c, level);
}

// ===========================================================================
// FigureConnector lists
// ===========================================================================

// Connectors: DENSE array, same length as figures — connectors[i] is
// the bridge INTO figures[i], so element 0 is a dummy (write null).
// NOT an N-1 "between figures" list: [1,-1] for three figures shifts
// figure 0 by +1 (bit the 2026-09-21 structural-Mary round).
// Each entry accepts null (dummy/default), an integer (bare leadStep
// shorthand), or a full object with elide/adjust/leadStep fields.
inline std::vector<std::optional<FigureConnector>> read_connectors(const json& arr) {
    std::vector<std::optional<FigureConnector>> connectors;
    for (const auto& cj : arr) {
        if (cj.is_null()) {
            connectors.push_back(std::nullopt);
        } else if (cj.is_number_integer()) {
            FigureConnector fc;
            fc.leadStep = cj.get<int>();
            connectors.push_back(fc);
        } else {
            FigureConnector fc;
            fc.elideCount = cj.value("elide", 0);
            fc.adjustCount = cj.value("adjust", 0.0f);
            fc.leadStep = cj.value("leadStep", 0);
            connectors.push_back(fc);
        }
    }
    return connectors;
}

// ===========================================================================
// PhraseTemplate
// ===========================================================================

inline json write_phrase_template(const PhraseTemplate& pt, const SettingsCodecs& codecs) {
    json j = json{{"figures", pt.figures}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (pt.startingPitch) j["startingPitch"] = *pt.startingPitch;
    if (pt.parallel) j["parallel"] = true;
    if (pt.totalBeats != 0.0f) j["totalBeats"] = pt.totalBeats;
    if (pt.cadenceType != 0) j["cadenceType"] = pt.cadenceType;
    if (pt.cadenceTarget != -1) j["cadenceTarget"] = pt.cadenceTarget;
    if (!pt.cadentialArrival.empty()) j["cadentialArrival"] = pt.cadentialArrival;
    if (pt.function != MelodicFunction::Free) j["function"] = pt.function;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
    write_choice(j, "strategy", pt.strategy, StrategyLevel::Phrase, codecs);

    // Connectors: only emit if any are present
    bool anyConnectors = false;
    for (const auto& c : pt.connectors) { if (c) { anyConnectors = true; break; } }
    if (anyConnectors) {
        json connArr = json::array();
        for (const auto& c : pt.connectors) {
            if (!c) { connArr.push_back(nullptr); }
            else {
                json cj = json::object();
                if (c->elideCount != 0) cj["elide"] = c->elideCount;
                if (c->adjustCount != 0) cj["adjust"] = c->adjustCount;
                if (c->leadStep != 0) cj["leadStep"] = c->leadStep;
                connArr.push_back(cj);
            }
        }
        j["connectors"] = connArr;
    }
    return j;
}

inline void read_phrase_template(const json& j, PhraseTemplate& pt,
                                 const SettingsCodecs& codecs) {
    refuse_old_phrase_keys(j);
    pt.figures.clear();
    for (auto& fj : j.at("figures")) {
        FigureTemplate ft;
        from_json(fj, ft);
        pt.figures.push_back(std::move(ft));
    }
    pt.name = j.value("name", std::string(""));
    if (j.contains("startingPitch")) {
        Pitch p; from_json(j.at("startingPitch"), p);
        pt.startingPitch = p;
    }
    pt.parallel = j.value("parallel", false);

    pt.totalBeats = j.value("totalBeats", 0.0f);
    pt.cadenceType = j.value("cadenceType", 0);
    pt.cadenceTarget = j.value("cadenceTarget", -1);
    pt.cadentialArrival = j.value("cadentialArrival", std::string());
    if (j.contains("function")) from_json(j.at("function"), pt.function);
    pt.seed = j.value("seed", 0u);
    pt.locked = j.value("locked", false);
    pt.strategy = j.contains("strategy")
        ? codecs.read(j.at("strategy"), StrategyLevel::Phrase) : StrategyChoice{};

    if (j.contains("connectors")) pt.connectors = read_connectors(j.at("connectors"));
}

// ===========================================================================
// PassageTemplate
// ===========================================================================

inline json write_passage_template(const PassageTemplate& pt, const SettingsCodecs& codecs) {
    json phrases = json::array();
    for (const auto& ph : pt.phrases) phrases.push_back(write_phrase_template(ph, codecs));
    json j = json{{"phrases", std::move(phrases)}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (pt.startingPitch) j["startingPitch"] = *pt.startingPitch;
    if (!pt.character.empty()) j["character"] = pt.character;
    if (!pt.fromKey.empty()) j["fromKey"] = pt.fromKey;
    if (!pt.toKey.empty()) j["toKey"] = pt.toKey;
    if (!pt.scaleOverride.empty()) j["scaleOverride"] = pt.scaleOverride;
    write_choice(j, "strategy", pt.strategy, StrategyLevel::Passage, codecs);
    // Not defaultish (default is 1.0), so it must always be written or the
    // round trip loses an authored 0 / 4.0.
    j["endGrid"] = pt.endGrid;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
    if (pt.chordConfig) {
        json cj;
        to_json(cj, *pt.chordConfig);
        j["chordConfig"] = std::move(cj);
    }
    if (pt.chordProgression) j["chordProgression"] = *pt.chordProgression;
    write_choice(j, "realization", pt.realization, StrategyLevel::Realization, codecs);
    if (!pt.voicingSelector.empty()) j["voicingSelector"] = pt.voicingSelector;
    {
      const auto& vp = pt.voicingProfile;
      bool anySet = vp.priority != 0.0f
                  || !vp.allowedInversions.empty()
                  || !vp.allowedSpreads.empty()
                  || vp.repeatPenalty != 0.0f
                  || vp.cadential;
      if (anySet) j["voicingProfile"] = vp;
    }
    write_choice(j, "voicingProfileSelector", pt.voicingProfileSelector,
                 StrategyLevel::ProfileSelector, codecs);
    if (!pt.voicingDictionary.empty()) j["voicingDictionary"] = pt.voicingDictionary;
    if (!pt.melodyPassageFile.empty()) {
        j["melodyPassageFile"] = pt.melodyPassageFile;
        j["melodyOctave"] = pt.melodyOctave;
    }
    if (!pt.anchorMode.empty()) j["anchorMode"] = pt.anchorMode;
    if (!pt.melodyProfile.empty()) j["melodyProfile"] = pt.melodyProfile;
    return j;
}

inline void from_json(const json& j, ChordAccompanimentConfig& cc) {
    cc.octave = j.value("octave", 3);
    cc.inversion = j.value("inversion", 0);
    cc.spread = j.value("spread", 0);
}

inline void read_passage_template(const json& j, PassageTemplate& pt,
                                  const SettingsCodecs& codecs) {
    refuse_old_keys(j, {"libraryConfig", "pedalBuildupConfig", "sequenceConfig",
                        "connectiveConfig", "wanderingConfig", "periods",
                        "realizationStrategy", "rhythmPattern",
                        "voicingProfileSelectorConfig"});
    if (j.contains("startingPitch")) {
        Pitch p;
        from_json(j.at("startingPitch"), p);
        pt.startingPitch = p;
    }

    // phrases[] may be empty when a strategy's settings drive the passage.
    pt.phrases.clear();
    if (j.contains("phrases") && j.at("phrases").is_array()) {
        for (auto& pj : j.at("phrases")) {
            PhraseTemplate ph; read_phrase_template(pj, ph, codecs);
            pt.phrases.push_back(std::move(ph));
        }
    }
    pt.name = j.value("name", std::string(""));
    pt.character = j.value("character", std::string(""));
    pt.fromKey = j.value("fromKey", std::string(""));
    pt.toKey = j.value("toKey", std::string(""));
    pt.strategy = j.contains("strategy")
        ? codecs.read(j.at("strategy"), StrategyLevel::Passage) : StrategyChoice{};
    pt.scaleOverride = j.value("scaleOverride", std::string(""));
    pt.endGrid = j.value("endGrid", 1.0f);
    pt.seed = j.value("seed", 0u);
    pt.locked = j.value("locked", false);

    // Optional chord accompaniment config.
    if (j.contains("chordConfig")) {
        ChordAccompanimentConfig cc;
        from_json(j["chordConfig"], cc);
        pt.chordConfig = cc;
    }

    // Optional passage-local harmony. Overrides the section timeline for
    // this part in this section.
    if (j.contains("chordProgression")) {
        pt.chordProgression = parse_chord_progression(j["chordProgression"]);
    }

    pt.realization = j.contains("realization")
        ? codecs.read(j.at("realization"), StrategyLevel::Realization) : StrategyChoice{};

    pt.voicingSelector = j.value("voicingSelector", std::string(""));

    // VoicingProfile: prefer nested object; fall back to lifting flat
    // voicingPriority / allowedInversions / allowedSpreads.
    pt.voicingProfile = VoicingProfile{};
    if (j.contains("voicingProfile")) {
        from_json(j.at("voicingProfile"), pt.voicingProfile);
    } else {
        pt.voicingProfile.priority = j.value("voicingPriority", 0.0f);
        if (j.contains("allowedInversions")) {
            pt.voicingProfile.allowedInversions =
                j.at("allowedInversions").get<std::vector<int>>();
        }
        if (j.contains("allowedSpreads")) {
            pt.voicingProfile.allowedSpreads =
                j.at("allowedSpreads").get<std::vector<int>>();
        }
    }

    pt.voicingProfileSelector = j.contains("voicingProfileSelector")
        ? codecs.read(j.at("voicingProfileSelector"), StrategyLevel::ProfileSelector)
        : StrategyChoice{};

    pt.voicingDictionary = j.value("voicingDictionary", std::string(""));

    pt.melodyPassageFile = j.value("melodyPassageFile", std::string(""));
    pt.melodyOctave = j.value("melodyOctave", 5);
    pt.anchorMode = j.value("anchorMode", std::string(""));
    pt.melodyProfile = j.value("melodyProfile", std::string(""));
}

// ===========================================================================
// PartTemplate
// ===========================================================================

inline json write_part_template(const PartTemplate& pt, const SettingsCodecs& codecs) {
    json j = json{{"name", pt.name}, {"role", pt.role}};
    if (!pt.instrumentPatch.empty()) j["instrumentPatch"] = pt.instrumentPatch;
    if (!pt.passages.empty()) {
        json passagesJ;
        for (auto& [secName, passageTmpl] : pt.passages)
            passagesJ[secName] = write_passage_template(passageTmpl, codecs);
        j["passages"] = passagesJ;
    }
    return j;
}

inline void read_part_template(const json& j, PartTemplate& pt, const SettingsCodecs& codecs) {
    pt.name = j.at("name").get<std::string>();
    from_json(j.at("role"), pt.role);
    pt.instrumentPatch = j.value("instrumentPatch", std::string(""));
    if (j.contains("passages")) {
        for (auto& [secName, passageJ] : j.at("passages").items()) {
            PassageTemplate pass; read_passage_template(passageJ, pass, codecs);
            pt.passages[secName] = std::move(pass);
        }
    }
}

// ===========================================================================
// PieceTemplate
// ===========================================================================

inline void to_json(json& j, const PieceTemplate::SectionTemplate& sd) {
    j = json{{"name", sd.name}, {"beats", sd.beats}};
    if (!sd.keyName.empty()) j["keyName"] = sd.keyName;
    if (!sd.scaleOverride.empty()) j["scaleOverride"] = sd.scaleOverride;
    if (!sd.progressionName.empty()) j["progressionName"] = sd.progressionName;
    if (sd.chordProgression) j["chordProgression"] = *sd.chordProgression;
    if (!sd.keyContexts.empty()) {
        json arr = json::array();
        for (const auto& kc : sd.keyContexts) {
            json jkc;
            jkc["beat"] = kc.beat;
            jkc["key"] = kc.key.to_string();
            if (kc.scaleOverride) {
                jkc["scaleOverride"] = *kc.scaleOverride;  // uses to_json(Scale)
            }
            arr.push_back(std::move(jkc));
        }
        j["keyContexts"] = std::move(arr);
    }
}
inline void from_json(const json& j, PieceTemplate::SectionTemplate& sd) {
    sd.name = j.at("name").get<std::string>();
    sd.beats = j.at("beats").get<float>();
    sd.keyName = j.value("keyName", std::string(""));
    sd.scaleOverride = j.value("scaleOverride", std::string(""));

    // progressionName
    if (j.contains("progressionName")) {
        sd.progressionName = j["progressionName"].get<std::string>();
    }

    // Inline chord progression (overrides progressionName)
    if (j.contains("chordProgression")) {
        sd.chordProgression = parse_chord_progression(j["chordProgression"]);
    }

    // Key contexts
    if (j.contains("keyContexts")) {
        for (const auto& kc : j["keyContexts"]) {
            KeyContext ctx;
            ctx.beat = kc["beat"].get<float>();
            std::string keyName = kc["key"].get<std::string>();
            ctx.key = Key::get(keyName);
            if (kc.contains("scaleOverride")) {
                Scale s;
                from_json(kc.at("scaleOverride"), s);
                ctx.scaleOverride = s;
            }
            sd.keyContexts.push_back(ctx);
        }
    }

    if (j.contains("styleName")) {
        sd.styleName = j["styleName"].get<std::string>();
    }
}

// A template to and from its file form. Each strategy choice is read and
// written through the codecs; a template naming a strategy the codecs do not
// know, or holding a key of the form before choices, is refused by name.
inline json write_template(const PieceTemplate& pt, const SettingsCodecs& codecs) {
    json parts = json::array();
    for (const auto& part : pt.parts) parts.push_back(write_part_template(part, codecs));
    json j = json{
        {"keyName", pt.keyName},
        {"scaleName", pt.scaleName},
        {"meter", pt.meter},
        {"bpm", pt.bpm},
        {"sections", pt.sections},
        {"parts", std::move(parts)}
    };
    if (!pt.motifs.empty()) j["motifs"] = pt.motifs;
    if (!pt.form.empty()) j["form"] = pt.form;
    if (pt.harmonySeeds) j["harmony"] = *pt.harmonySeeds;
    if (pt.masterSeed != 0) j["masterSeed"] = pt.masterSeed;
    return j;
}

inline PieceTemplate read_template(const json& j, const SettingsCodecs& codecs) {
    PieceTemplate pt;
    pt.keyName = j.value("keyName", std::string("C"));
    pt.scaleName = j.value("scaleName", std::string("Major"));
    if (j.contains("meter")) from_json(j.at("meter"), pt.meter);
    pt.bpm = j.value("bpm", 120.0f);

    if (j.contains("motifs")) {
        pt.motifs.clear();
        for (auto& mj : j.at("motifs")) {
            Motif m; from_json(mj, m);
            pt.motifs.push_back(std::move(m));
        }
    }

    pt.sections.clear();
    if (j.contains("sections")) {
        for (auto& sj : j.at("sections")) {
            PieceTemplate::SectionTemplate sd; from_json(sj, sd);
            pt.sections.push_back(std::move(sd));
        }
    }

    pt.form = j.value("form", std::string(""));

    pt.parts.clear();
    if (j.contains("parts")) {
        for (auto& pj : j.at("parts")) {
            PartTemplate p; read_part_template(pj, p, codecs);
            pt.parts.push_back(std::move(p));
        }
    }

    if (j.contains("harmony")) {
        ChordProgression cp; from_json(j.at("harmony"), cp);
        pt.harmonySeeds = std::move(cp);
    }

    pt.masterSeed = j.value("masterSeed", 0u);
    return pt;
}

} // namespace mforce
